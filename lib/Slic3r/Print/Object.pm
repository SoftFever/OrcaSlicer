package Slic3r::Print::Object;
use Moo;

use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(Z PI scale unscale deg2rad rad2deg scaled_epsilon);
use Slic3r::Geometry::Clipper qw(diff_ex intersection_ex union_ex);
use Slic3r::Surface ':types';

has 'print'             => (is => 'ro', weak_ref => 1, required => 1);
has 'input_file'        => (is => 'rw', required => 0);
has 'meshes'            => (is => 'rw', default => sub { [] });  # by region_id
has 'size'              => (is => 'rw', required => 1);
has 'copies'            => (is => 'rw', default => sub {[ [0,0] ]});
has 'layers'            => (is => 'rw', default => sub { [] });

sub BUILD {
    my $self = shift;
 	 
    # make layers
    while (!@{$self->layers} || $self->layers->[-1]->slice_z < $self->size->[Z]) {
        push @{$self->layers}, Slic3r::Layer->new(
            object  => $self,
            id      => $#{$self->layers} + 1,
        );
    }
}

sub layer_count {
    my $self = shift;
    return scalar @{ $self->layers };
}

sub get_layer_range {
    my $self = shift;
    my ($min_z, $max_z) = @_;
    
    my ($min_layer, $max_layer) = (0, undef);
 	for my $layer (@{$self->layers}) {
        $min_layer = $layer->id if $layer->slice_z <= $min_z;
        if ($layer->slice_z >= $max_z) {
            $max_layer = $layer->id;
            last;
        }
    }
    return ($min_layer, $max_layer);
}

sub slice {
    my $self = shift;
    my %params = @_;
    
    # process facets
    for my $region_id (0 .. $#{$self->meshes}) {
        my $mesh = $self->meshes->[$region_id];  # ignore undef meshes
        
        my $apply_lines = sub {
            my $lines = shift;
            foreach my $layer_id (keys %$lines) {
                my $layerm = $self->layers->[$layer_id]->region($region_id);
                push @{$layerm->lines}, @{$lines->{$layer_id}};
            }
        };
        Slic3r::parallelize(
            disable => ($#{$mesh->facets} < 500),  # don't parallelize when too few facets
            items => [ 0..$#{$mesh->facets} ],
            thread_cb => sub {
                my $q = shift;
                my $result_lines = {};
                while (defined (my $facet_id = $q->dequeue)) {
                    my $lines = $mesh->slice_facet($self, $facet_id);
                    foreach my $layer_id (keys %$lines) {
                        $result_lines->{$layer_id} ||= [];
                        push @{ $result_lines->{$layer_id} }, @{ $lines->{$layer_id} };
                    }
                }
                return $result_lines;
            },
            collect_cb => sub {
                $apply_lines->($_[0]);
            },
            no_threads_cb => sub {
                for (0..$#{$mesh->facets}) {
                    my $lines = $mesh->slice_facet($self, $_);
                    $apply_lines->($lines);
                }
            },
        );
    }
    die "Invalid input file\n" if !@{$self->layers};
    
    # free memory
    $self->meshes(undef) unless $params{keep_meshes};
    
    # remove last layer if empty
    # (we might have created it because of the $max_layer = ... + 1 code in TriangleMesh)
    pop @{$self->layers} if !map @{$_->lines}, @{$self->layers->[-1]->regions};
    
    foreach my $layer (@{ $self->layers }) {
        # make sure all layers contain layer region objects for all regions
        $layer->region($_) for 0 .. ($self->print->regions_count-1);
        
        Slic3r::debugf "Making surfaces for layer %d (slice z = %f):\n",
            $layer->id, unscale $layer->slice_z if $Slic3r::debug;
        
        # layer currently has many lines representing intersections of
        # model facets with the layer plane. there may also be lines
        # that we need to ignore (for example, when two non-horizontal
        # facets share a common edge on our plane, we get a single line;
        # however that line has no meaning for our layer as it's enclosed
        # inside a closed polyline)
        
        # build surfaces from sparse lines
        foreach my $layerm (@{$layer->regions}) {
            my ($slicing_errors, $loops) = Slic3r::TriangleMesh::make_loops($layerm->lines);
            $layer->slicing_errors(1) if $slicing_errors;
            $layerm->make_surfaces($loops);
            
            # free memory
            $layerm->lines(undef);
        }
        
        # merge all regions' slices to get islands
        $layer->make_slices;
    }
    
    # detect slicing errors
    my $warning_thrown = 0;
    for my $i (0 .. $#{$self->layers}) {
        my $layer = $self->layers->[$i];
        next unless $layer->slicing_errors;
        if (!$warning_thrown) {
            warn "The model has overlapping or self-intersecting facets. I tried to repair it, "
                . "however you might want to check the results or repair the input file and retry.\n";
            $warning_thrown = 1;
        }
        
        # try to repair the layer surfaces by merging all contours and all holes from
        # neighbor layers
        Slic3r::debugf "Attempting to repair layer %d\n", $i;
        
        foreach my $region_id (0 .. $#{$layer->regions}) {
            my $layerm = $layer->region($region_id);
            
            my (@upper_surfaces, @lower_surfaces);
            for (my $j = $i+1; $j <= $#{$self->layers}; $j++) {
                if (!$self->layers->[$j]->slicing_errors) {
                    @upper_surfaces = @{$self->layers->[$j]->region($region_id)->slices};
                    last;
                }
            }
            for (my $j = $i-1; $j >= 0; $j--) {
                if (!$self->layers->[$j]->slicing_errors) {
                    @lower_surfaces = @{$self->layers->[$j]->region($region_id)->slices};
                    last;
                }
            }
            
            my $union = union_ex([
                map $_->expolygon->contour, @upper_surfaces, @lower_surfaces,
            ]);
            my $diff = diff_ex(
                [ map @$_, @$union ],
                [ map $_->expolygon->holes, @upper_surfaces, @lower_surfaces, ],
            );
            
            @{$layerm->slices} = map Slic3r::Surface->new
                (expolygon => $_, surface_type => S_TYPE_INTERNAL),
                @$diff;
        }
            
        # update layer slices after repairing the single regions
        $layer->make_slices;
    }
    
    # remove empty layers from bottom
    while (@{$self->layers} && !@{$self->layers->[0]->slices} && !map @{$_->thin_walls}, @{$self->layers->[0]->regions}) {
        shift @{$self->layers};
        for (my $i = 0; $i <= $#{$self->layers}; $i++) {
            $self->layers->[$i]->id($i);
        }
    }
    
    warn "No layers were detected. You might want to repair your STL file and retry.\n"
        if !@{$self->layers};
}

sub make_perimeters {
    my $self = shift;
    
    # compare each layer to the one below, and mark those slices needing
    # one additional inner perimeter, like the top of domed objects-
    
    # this algorithm makes sure that almost one perimeter is overlapping
    if ($Slic3r::Config->extra_perimeters && $Slic3r::Config->perimeters > 0) {
        for my $region_id (0 .. ($self->print->regions_count-1)) {
            for my $layer_id (0 .. $self->layer_count-2) {
                my $layerm          = $self->layers->[$layer_id]->regions->[$region_id];
                my $upper_layerm    = $self->layers->[$layer_id+1]->regions->[$region_id];
                my $perimeter_flow  = $layerm->perimeter_flow;
                
                my $overlap = $perimeter_flow->spacing;  # one perimeter
                
                # compute polygons representing the thickness of the first external perimeter of
                # the upper layer slices
                my $upper = diff_ex(
                    [ map @$_, map $_->expolygon->offset_ex(+ 0.5 * $perimeter_flow->scaled_spacing), @{$upper_layerm->slices} ],
                    [ map @$_, map $_->expolygon->offset_ex(- scale($overlap) + (0.5 * $perimeter_flow->scaled_spacing)), @{$upper_layerm->slices} ],
                );
                next if !@$upper;
                
                # we need to limit our detection to the areas which would actually benefit from 
                # more perimeters. so, let's compute the area we want to ignore
                my $ignore = [];
                {
                    my $diff = diff_ex(
                        [ map @$_, map $_->expolygon->offset_ex(- ($Slic3r::Config->perimeters-0.5) * $perimeter_flow->scaled_spacing), @{$layerm->slices} ],
                        [ map @{$_->expolygon}, @{$upper_layerm->slices} ],
                    );
                    $ignore = [ map @$_, map $_->offset_ex($perimeter_flow->scaled_spacing), @$diff ];
                }
                
                foreach my $slice (@{$layerm->slices}) {
                    my $hypothetical_perimeter_num = $Slic3r::Config->perimeters + 1;
                    CYCLE: while (1) {
                        # compute polygons representing the thickness of the hypotetical new internal perimeter
                        # of our slice
                        my $hypothetical_perimeter;
                        {
                            my $outer = [ map @$_, $slice->expolygon->offset_ex(- ($hypothetical_perimeter_num-1.5) * $perimeter_flow->scaled_spacing - scaled_epsilon) ];
                            last CYCLE if !@$outer;
                            my $inner = [ map @$_, $slice->expolygon->offset_ex(- ($hypothetical_perimeter_num-0.5) * $perimeter_flow->scaled_spacing) ];
                            last CYCLE if !@$inner;
                            $hypothetical_perimeter = diff_ex($outer, $inner);
                        }
                        last CYCLE if !@$hypothetical_perimeter;
                        
                        
                        my $intersection = intersection_ex([ map @$_, @$upper ], [ map @$_, @$hypothetical_perimeter ]);
                        $intersection = diff_ex([ map @$_, @$intersection ], $ignore) if @$ignore;
                        last CYCLE if !@{ $intersection };
                        Slic3r::debugf "  adding one more perimeter at layer %d\n", $layer_id;
                        $slice->additional_inner_perimeters(($slice->additional_inner_perimeters || 0) + 1);
                        $hypothetical_perimeter_num++;
                    }
                }
            }
        }
    }
    
    $_->make_perimeters for @{$self->layers};
}

sub detect_surfaces_type {
    my $self = shift;
    Slic3r::debugf "Detecting solid surfaces...\n";
    
    # prepare a reusable subroutine to make surface differences
    my $surface_difference = sub {
        my ($subject_surfaces, $clip_surfaces, $result_type, $layerm) = @_;
        my $expolygons = diff_ex(
            [ map { ref $_ eq 'ARRAY' ? $_ : ref $_ eq 'Slic3r::ExPolygon' ? @$_ : $_->p } @$subject_surfaces ],
            [ map { ref $_ eq 'ARRAY' ? $_ : ref $_ eq 'Slic3r::ExPolygon' ? @$_ : $_->p } @$clip_surfaces ],
            1,
        );
        return grep $_->contour->is_printable($layerm->flow->width),
            map Slic3r::Surface->new(expolygon => $_, surface_type => $result_type), 
            @$expolygons;
    };
    
    for my $region_id (0 .. ($self->print->regions_count-1)) {
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layerm = $self->layers->[$i]->regions->[$region_id];
            
            # comparison happens against the *full* slices (considering all regions)
            my $upper_layer = $self->layers->[$i+1];
            my $lower_layer = $i > 0 ? $self->layers->[$i-1] : undef;
            
            my (@bottom, @top, @internal) = ();
            
            # find top surfaces (difference between current surfaces
            # of current layer and upper one)
            if ($upper_layer) {
                @top = $surface_difference->($layerm->slices, $upper_layer->slices, S_TYPE_TOP, $layerm);
            } else {
                # if no upper layer, all surfaces of this one are solid
                @top = @{$layerm->slices};
                $_->surface_type(S_TYPE_TOP) for @top;
            }
            
            # find bottom surfaces (difference between current surfaces
            # of current layer and lower one)
            if ($lower_layer) {
                @bottom = $surface_difference->($layerm->slices, $lower_layer->slices, S_TYPE_BOTTOM, $layerm);
            } else {
                # if no lower layer, all surfaces of this one are solid
                @bottom = @{$layerm->slices};
                $_->surface_type(S_TYPE_BOTTOM) for @bottom;
            }
            
            # now, if the object contained a thin membrane, we could have overlapping bottom
            # and top surfaces; let's do an intersection to discover them and consider them
            # as bottom surfaces (to allow for bridge detection)
            if (@top && @bottom) {
                my $overlapping = intersection_ex([ map $_->p, @top ], [ map $_->p, @bottom ]);
                Slic3r::debugf "  layer %d contains %d membrane(s)\n", $layerm->id, scalar(@$overlapping);
                @top = $surface_difference->([@top], $overlapping, S_TYPE_TOP, $layerm);
            }
            
            # find internal surfaces (difference between top/bottom surfaces and others)
            @internal = $surface_difference->($layerm->slices, [@top, @bottom], S_TYPE_INTERNAL, $layerm);
            
            # save surfaces to layer
            @{$layerm->slices} = (@bottom, @top, @internal);
            
            Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
                $layerm->id, scalar(@bottom), scalar(@top), scalar(@internal);
        }
        
        # clip surfaces to the fill boundaries
        foreach my $layer (@{$self->layers}) {
            my $layerm = $layer->regions->[$region_id];
            my $fill_boundaries = [ map @$_, @{$layerm->fill_surfaces} ];
            @{$layerm->fill_surfaces} = ();
            foreach my $surface (@{$layerm->slices}) {
                my $intersection = intersection_ex(
                    [ $surface->p ],
                    $fill_boundaries,
                );
                push @{$layerm->fill_surfaces}, map Slic3r::Surface->new
                    (expolygon => $_, surface_type => $surface->surface_type),
                    @$intersection;
            }
        }
    }
}

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    my $area_threshold = $Slic3r::flow->scaled_spacing ** 2;
    
    for my $region_id (0 .. ($self->print->regions_count-1)) {
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layerm = $self->layers->[$i]->regions->[$region_id];
            
            if ($Slic3r::Config->solid_infill_every_layers && ($i % $Slic3r::Config->solid_infill_every_layers) == 0) {
                $_->surface_type(S_TYPE_INTERNALSOLID)
                    for grep $_->surface_type == S_TYPE_INTERNAL, @{$layerm->fill_surfaces};
            }
            
            foreach my $type (S_TYPE_TOP, S_TYPE_BOTTOM) {
                # find slices of current type for current layer
                my @surfaces = grep $_->surface_type == $type, @{$layerm->slices} or next;
                my $surfaces_p = [ map $_->p, @surfaces ];
                Slic3r::debugf "Layer %d has %d surfaces of type '%s'\n",
                    $i, scalar(@surfaces), ($type == S_TYPE_TOP ? 'top' : 'bottom');
                
                my $solid_layers = ($type == S_TYPE_TOP)
                    ? $Slic3r::Config->top_solid_layers
                    : $Slic3r::Config->bottom_solid_layers;
                for (my $n = $type == S_TYPE_TOP ? $i-1 : $i+1; 
                        abs($n - $i) <= $solid_layers-1; 
                        $type == S_TYPE_TOP ? $n-- : $n++) {
                    
                    next if $n < 0 || $n >= $self->layer_count;
                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                    
                    my @neighbor_fill_surfaces  = @{$self->layers->[$n]->regions->[$region_id]->fill_surfaces};
                    
                    # find intersection between neighbor and current layer's surfaces
                    # intersections have contours and holes
                    my $new_internal_solid = intersection_ex(
                        $surfaces_p,
                        [ map $_->p, grep { $_->surface_type == S_TYPE_INTERNAL || $_->surface_type == S_TYPE_INTERNALSOLID } @neighbor_fill_surfaces ],
                        undef, 1,
                    );
                    next if !@$new_internal_solid;
                    
                    # internal-solid are the union of the existing internal-solid surfaces
                    # and new ones
                    my $internal_solid = union_ex([
                        ( map $_->p, grep $_->surface_type == S_TYPE_INTERNALSOLID, @neighbor_fill_surfaces ),
                        ( map @$_, @$new_internal_solid ),
                    ]);
                    
                    # subtract intersections from layer surfaces to get resulting inner surfaces
                    my $internal = diff_ex(
                        [ map $_->p, grep $_->surface_type == S_TYPE_INTERNAL, @neighbor_fill_surfaces ],
                        [ map @$_, @$internal_solid ],
                        1,
                    );
                    Slic3r::debugf "    %d internal-solid and %d internal surfaces found\n",
                        scalar(@$internal_solid), scalar(@$internal);
                    
                    # Note: due to floating point math we're going to get some very small
                    # polygons as $internal; they will be removed by removed_small_features()
                    
                    # assign resulting inner surfaces to layer
                    my $neighbor_fill_surfaces = $self->layers->[$n]->regions->[$region_id]->fill_surfaces;
                    @$neighbor_fill_surfaces = ();
                    push @$neighbor_fill_surfaces, Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNAL)
                        for @$internal;
                    
                    # assign new internal-solid surfaces to layer
                    push @$neighbor_fill_surfaces, Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNALSOLID)
                        for @$internal_solid;
                    
                    # assign top and bottom surfaces to layer
                    foreach my $s (Slic3r::Surface->group(grep { $_->surface_type == S_TYPE_TOP || $_->surface_type == S_TYPE_BOTTOM } @neighbor_fill_surfaces)) {
                        my $solid_surfaces = diff_ex(
                            [ map $_->p, @$s ],
                            [ map @$_, @$internal_solid, @$internal ],
                            1,
                        );
                        push @$neighbor_fill_surfaces, Slic3r::Surface->new
                            (expolygon => $_, surface_type => $s->[0]->surface_type, bridge_angle => $s->[0]->bridge_angle)
                            for @$solid_surfaces;
                    }
                }
            }
            
            @{$layerm->fill_surfaces} = grep $_->expolygon->area > $area_threshold, @{$layerm->fill_surfaces};
        }
        
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layerm = $self->layers->[$i]->regions->[$region_id];
            
            # if hollow object is requested, remove internal surfaces
            if ($Slic3r::Config->fill_density == 0) {
                @{$layerm->fill_surfaces} = grep $_->surface_type != S_TYPE_INTERNAL, @{$layerm->fill_surfaces};
            }
        }
    }
}

# combine fill surfaces across layers
sub combine_infill {
    my $self = shift;
    return unless $Slic3r::Config->infill_every_layers > 1 && $Slic3r::Config->fill_density > 0;
    
    my $area_threshold = $Slic3r::flow->scaled_spacing ** 2;
    
    for my $region_id (0 .. ($self->print->regions_count-1)) {
        # start from top, skip lowest layer
        for (my $i = $self->layer_count - 1; $i > 0; $i--) {
            my $layerm = $self->layers->[$i]->regions->[$region_id];
            
            # skip layer if no internal fill surfaces
            next if !grep $_->surface_type == S_TYPE_INTERNAL, @{$layerm->fill_surfaces};
            
            # for each possible depth, look for intersections with the lower layer
            # we do this from the greater depth to the smaller
            for (my $d = $Slic3r::Config->infill_every_layers - 1; $d >= 1; $d--) {
                next if ($i - $d) <= 0; # do not combine infill for bottom layer
                my $lower_layerm = $self->layers->[$i - 1]->regions->[$region_id];
                
                # select surfaces of the lower layer having the depth we're looking for
                my @lower_surfaces = grep $_->depth_layers == $d && $_->surface_type == S_TYPE_INTERNAL,
                    @{$lower_layerm->fill_surfaces};
                next if !@lower_surfaces;
                
                # calculate intersection between our surfaces and theirs
                my $intersection = intersection_ex(
                    [ map $_->p, grep $_->depth_layers <= $d, @lower_surfaces ],
                    [ map $_->p, grep $_->surface_type == S_TYPE_INTERNAL, @{$layerm->fill_surfaces} ],
                    undef, 1,
                );
                
                # purge intersections, skip tiny regions
                @$intersection = grep $_->area > $area_threshold, @$intersection;
                next if !@$intersection;
                
                # new fill surfaces of the current layer are:
                # - any non-internal surface
                # - intersections found (with a $d + 1 depth)
                # - any internal surface not belonging to the intersection (with its original depth)
                {
                    my @new_surfaces = ();
                    push @new_surfaces, grep $_->surface_type != S_TYPE_INTERNAL, @{$layerm->fill_surfaces};
                    push @new_surfaces, map Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNAL, depth_layers => $d + 1), @$intersection;
                    
                    foreach my $depth (reverse $d..$Slic3r::Config->infill_every_layers) {
                        push @new_surfaces, map Slic3r::Surface->new
                            (expolygon => $_, surface_type => S_TYPE_INTERNAL, depth_layers => $depth),
                            
                            # difference between our internal layers with depth == $depth
                            # and the intersection found
                            @{diff_ex(
                                [
                                    map $_->p, grep $_->surface_type == S_TYPE_INTERNAL && $_->depth_layers == $depth, 
                                        @{$layerm->fill_surfaces},
                                ],
                                [ map @$_, @$intersection ],
                                1,
                            )};
                    }
                    @{$layerm->fill_surfaces} = @new_surfaces;
                }
                
                # now we remove the intersections from lower layer
                {
                    my @new_surfaces = ();
                    push @new_surfaces, grep $_->surface_type != S_TYPE_INTERNAL, @{$lower_layerm->fill_surfaces};

                    # offset for the two different flow spacings
                    $intersection = [ map $_->offset_ex(
                                          $lower_layerm->infill_flow->scaled_spacing / 2
                                        + $layerm->infill_flow->scaled_spacing / 2
                                        ), @$intersection];

                    foreach my $depth (1..$Slic3r::Config->infill_every_layers) {
                        push @new_surfaces, map Slic3r::Surface->new
                            (expolygon => $_, surface_type => S_TYPE_INTERNAL, depth_layers => $depth),
                            
                            # difference between internal layers with depth == $depth
                            # and the intersection found
                            @{diff_ex(
                                [
                                    map $_->p, grep $_->surface_type == S_TYPE_INTERNAL && $_->depth_layers == $depth, 
                                        @{$lower_layerm->fill_surfaces},
                                ],
                                [ map @$_, @$intersection ],
                                1,
                            )};
                    }
                    @{$lower_layerm->fill_surfaces} = @new_surfaces;
                }
            }
        }
    }
}

sub generate_support_material {
    my $self = shift;
    
    my $threshold_rad = $Slic3r::Config->support_material_threshold
                        ? deg2rad($Slic3r::Config->support_material_threshold + 1)  # +1 makes the threshold inclusive
                        : PI/2 - atan2($self->layers->[1]->regions->[0]->perimeter_flow->width/$Slic3r::Config->layer_height/2, 1);
    Slic3r::debugf "Threshold angle = %d°\n", rad2deg($threshold_rad);
    
    my $flow                    = $self->print->support_material_flow;
    my $overhang_width          = $threshold_rad == 0 ? undef : scale $Slic3r::Config->layer_height * ((cos $threshold_rad) / (sin $threshold_rad));
    my $distance_from_object    = 1.5 * $flow->scaled_width;
    my $pattern_spacing = ($Slic3r::Config->support_material_spacing > $flow->spacing)
        ? $Slic3r::Config->support_material_spacing
        : $flow->spacing;
    
    # determine support regions in each layer (for upper layers)
    Slic3r::debugf "Detecting regions\n";
    my %layers = ();            # this represents the areas of each layer having to support upper layers (excluding interfaces)
    my %layers_interfaces = (); # this represents the areas of each layer having an overhang in the immediately upper layer
    {
        my @current_support_regions = ();   # expolygons we've started to support (i.e. below the empty interface layers)
        my @queue = ();                     # the number of items of this array determines the number of empty interface layers
        for my $i (reverse 0 .. $#{$self->layers}) {
            my $layer = $self->layers->[$i];
            my $lower_layer = $i > 0 ? $self->layers->[$i-1] : undef;
            
            my @current_layer_offsetted_slices = map $_->offset_ex($distance_from_object), @{$layer->slices};
            
            # $queue[-1] contains the overhangs of the upper layer, regardless of any empty interface layers
            # $queue[0] contains the overhangs of the first upper layer above the empty interface layers
            $layers_interfaces{$i} = diff_ex(
                [ map @$_, @{ $queue[-1] || [] } ],
                [ map @$_, @current_layer_offsetted_slices ],
            );
            
            # step 1: generate support material in current layer (for upper layers)
            push @current_support_regions, @{ shift @queue } if @queue && $i < $#{$self->layers};
            
            @current_support_regions = @{diff_ex(
                [ map @$_, @current_support_regions ],
                [ map @$_, @{$layer->slices} ],
            )};
            
            $layers{$i} = diff_ex(
                [ map @$_, @current_support_regions ],
                [
                    (map @$_, @current_layer_offsetted_slices),
                    (map @$_, @{ $layers_interfaces{$i} }),
                ],
            );
            $_->simplify($flow->scaled_spacing) for @{$layers{$i}};
            
            # step 2: get layer overhangs and put them into queue for adding support inside lower layers
            # we need an angle threshold for this
            my @overhangs = ();
            if ($lower_layer) {
                @overhangs = map $_->offset_ex(2 * $overhang_width), @{diff_ex(
                    [ map @$_, map $_->offset_ex(-$overhang_width), @{$layer->slices} ],
                    [ map @$_, @{$lower_layer->slices} ],
                    1,
                )};
            }
            push @queue, [@overhangs];
        }
    }
    return if !map @$_, values %layers;
    
    # generate paths for the pattern that we're going to use
    Slic3r::debugf "Generating patterns\n";
    my $support_patterns = [];  # in case we want cross-hatching
    {
        # 0.5 makes sure the paths don't get clipped externally when applying them to layers
        my @support_material_areas = map $_->offset_ex(- 0.5 * $flow->scaled_width),
            @{union_ex([ map $_->contour, map @$_, values %layers ])};
        
        my $filler = Slic3r::Fill->filler($Slic3r::Config->support_material_pattern);
        $filler->angle($Slic3r::Config->support_material_angle);
        {
            my @patterns = ();
            foreach my $expolygon (@support_material_areas) {
                my @paths = $filler->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon),
                    density         => $flow->spacing / $pattern_spacing,
                    flow_spacing    => $flow->spacing,
                );
                my $params = shift @paths;
                
                push @patterns,
                    map Slic3r::ExtrusionPath->new(
                        polyline        => Slic3r::Polyline->new(@$_),
                        role            => EXTR_ROLE_SUPPORTMATERIAL,
                        height          => undef,
                        flow_spacing    => $params->{flow_spacing},
                    ), @paths;
            }
            push @$support_patterns, [@patterns];
        }
    
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("support_$_.svg",
                polylines        => [ map $_->polyline, map @$_, $support_patterns->[$_] ],
                polygons         => [ map @$_, @support_material_areas ],
            ) for 0 .. $#$support_patterns;
        }
    }
    
    # apply the pattern to layers
    Slic3r::debugf "Applying patterns\n";
    {
        my $clip_pattern = sub {
            my ($layer_id, $expolygons, $height) = @_;
            my @paths = ();
            foreach my $expolygon (@$expolygons) {
                push @paths,
                    map $_->pack,
                    map {
                        $_->height($height);
                        $_->flow_spacing($self->print->first_layer_support_material_flow->spacing)
                            if $layer_id == 0;
                        $_;
                    }
                    map $_->clip_with_expolygon($expolygon),
                    map $_->clip_with_polygon($expolygon->bounding_box_polygon),
                    @{$support_patterns->[ $layer_id % @$support_patterns ]};
            };
            return @paths;
        };
        my %layer_paths             = ();
        my %layer_interface_paths   = ();
        my %layer_islands           = ();
        my $process_layer = sub {
            my ($layer_id) = @_;
            
            my $layer = $self->layers->[$layer_id];
            my $paths           = [ $clip_pattern->($layer_id, $layers{$layer_id}, $layer->height) ];
            my $interface_paths = [ $clip_pattern->($layer_id, $layers_interfaces{$layer_id}, $layer->support_material_interface_height) ];
            my $islands         = union_ex([ map @$_, map @$_, $layers{$layer_id}, $layers_interfaces{$layer_id} ]);
            return ($paths, $interface_paths, $islands);
        };
        Slic3r::parallelize(
            items => [ keys %layers ],
            thread_cb => sub {
                my $q = shift;
                $Slic3r::Geometry::Clipper::clipper = Math::Clipper->new;
                my $result = {};
                while (defined (my $layer_id = $q->dequeue)) {
                    $result->{$layer_id} = [ $process_layer->($layer_id) ];
                }
                return $result;
            },
            collect_cb => sub {
                my $result = shift;
                ($layer_paths{$_}, $layer_interface_paths{$_}, $layer_islands{$_}) = @{$result->{$_}} for keys %$result;
            },
            no_threads_cb => sub {
                ($layer_paths{$_}, $layer_interface_paths{$_}, $layer_islands{$_}) = $process_layer->($_) for keys %layers;
            },
        );
        
        foreach my $layer_id (keys %layer_paths) {
            my $layer = $self->layers->[$layer_id];
            $layer->support_islands($layer_islands{$layer_id});
            $layer->support_fills(Slic3r::ExtrusionPath::Collection->new);
            $layer->support_interface_fills(Slic3r::ExtrusionPath::Collection->new);
            push @{$layer->support_fills->paths}, @{$layer_paths{$layer_id}};
            push @{$layer->support_interface_fills->paths}, @{$layer_interface_paths{$layer_id}};
        }
    }
}

1;
