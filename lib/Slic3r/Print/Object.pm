package Slic3r::Print::Object;
use Moo;

use List::Util qw(min max sum first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(X Y Z PI scale unscale deg2rad rad2deg scaled_epsilon chained_path_points);
use Slic3r::Geometry::Clipper qw(diff diff_ex intersection intersection_ex union union_ex 
    offset offset_ex offset2 CLIPPER_OFFSET_SCALE JT_MITER);
use Slic3r::Surface ':types';

has 'print'             => (is => 'ro', weak_ref => 1, required => 1);
has 'input_file'        => (is => 'rw', required => 0);
has 'meshes'            => (is => 'rw', default => sub { [] });  # by region_id
has 'size'              => (is => 'rw', required => 1); # XYZ in scaled coordinates
has 'copies'            => (is => 'rw', trigger => 1);  # in scaled coordinates
has 'layers'            => (is => 'rw', default => sub { [] });
has 'support_layers'    => (is => 'rw', default => sub { [] });
has 'config_overrides'  => (is => 'rw', default => sub { Slic3r::Config->new });
has 'config'            => (is => 'rw');
has 'layer_height_ranges' => (is => 'rw', default => sub { [] }); # [ z_min, z_max, layer_height ]
has 'fill_maker'        => (is => 'lazy');
has '_slice_z_table'    => (is => 'lazy');

sub BUILD {
    my $self = shift;
 	
 	$self->init_config;
 	
    # make layers taking custom heights into account
    my $print_z = my $slice_z = my $height = 0;
    
    # add raft layers
    for my $id (0 .. $self->config->raft_layers-1) {
        $height = ($id == 0)
            ? $Slic3r::Config->get_value('first_layer_height')
            : $Slic3r::Config->layer_height;
        
        $print_z += $height;
        
        push @{$self->layers}, Slic3r::Layer->new(
            object  => $self,
            id      => $id,
            height  => $height,
            print_z => $print_z,
            slice_z => -1,
        );
    }
    
    # loop until we have at least one layer and the max slice_z reaches the object height
    my $max_z = unscale $self->size->[Z];
    while (!@{$self->layers} || ($slice_z - $height) <= $max_z) {
        my $id = $#{$self->layers} + 1;
        
        # assign the default height to the layer according to the general settings
        $height = ($id == 0)
            ? $Slic3r::Config->get_value('first_layer_height')
            : $Slic3r::Config->layer_height;
        
        # look for an applicable custom range
        if (my $range = first { $_->[0] <= $slice_z && $_->[1] > $slice_z } @{$self->layer_height_ranges}) {
            $height = $range->[2];
        
            # if user set custom height to zero we should just skip the range and resume slicing over it
            if ($height == 0) {
                $slice_z += $range->[1] - $range->[0];
                next;
            }
        }
        
        $print_z += $height;
        $slice_z += $height/2;
        
        ### Slic3r::debugf "Layer %d: height = %s; slice_z = %s; print_z = %s\n", $id, $height, $slice_z, $print_z;
        
        push @{$self->layers}, Slic3r::Layer->new(
            object  => $self,
            id      => $id,
            height  => $height,
            print_z => $print_z,
            slice_z => scale $slice_z,
        );
        
        $slice_z += $height/2;   # add the other half layer
    }
}

sub _build_fill_maker {
    my $self = shift;
    return Slic3r::Fill->new(object => $self);
}

sub _build__slice_z_table {
    my $self = shift;
    return Slic3r::Object::XS::ZTable->new([ map $_->slice_z, @{$self->layers} ]);
}

# This should be probably moved in Print.pm at the point where we sort Layer objects
sub _trigger_copies {
    my $self = shift;
    return unless @{$self->copies} > 1;
    
    # order copies with a nearest neighbor search
    @{$self->copies} = @{chained_path_points($self->copies)}
}

sub init_config {
    my $self = shift;
    $self->config(Slic3r::Config->merge($self->print->config, $self->config_overrides));
}

sub layer_count {
    my $self = shift;
    return scalar @{ $self->layers };
}

sub get_layer_range {
    my $self = shift;
    my ($min_z, $max_z) = @_;
    
    my $min_layer = $self->_slice_z_table->lower_bound($min_z); # first layer whose slice_z is >= $min_z
    return (
        $min_layer,
        $self->_slice_z_table->upper_bound($max_z, $min_layer)-1, # last layer whose slice_z is <= $max_z
    );
}

sub bounding_box {
    my $self = shift;
    
    # since the object is aligned to origin, bounding box coincides with size
    return Slic3r::Geometry::BoundingBox->new_from_points([ map Slic3r::Point->new(@$_[X,Y]), [0,0], $self->size ]);
}

sub slice {
    my $self = shift;
    my %params = @_;
    
    # make sure all layers contain layer region objects for all regions
    my $regions_count = $self->print->regions_count;
    foreach my $layer (@{ $self->layers }) {
        $layer->region($_) for 0 .. ($regions_count-1);
    }
    
    # process facets
    for my $region_id (0 .. $#{$self->meshes}) {
        my $mesh = $self->meshes->[$region_id] // next;  # ignore undef meshes
        $mesh->repair;
        
        {
            my $loops = $mesh->slice([ map $_->slice_z, @{$self->layers} ]);
            for my $layer_id (0..$#$loops) {
                my $layerm = $self->layers->[$layer_id]->regions->[$region_id];
                $layerm->make_surfaces($loops->[$layer_id]);
            }
            # TODO: read slicing_errors
        }
        
        # free memory
        undef $mesh;
        undef $self->meshes->[$region_id];
    }
    
    # free memory
    $self->meshes(undef);
    
    # remove last layer(s) if empty
    pop @{$self->layers} while @{$self->layers} && (!map @{$_->slices}, @{$self->layers->[-1]->regions});
    
    foreach my $layer (@{ $self->layers }) {
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
                [ map @{$_->expolygon->holes}, @upper_surfaces, @lower_surfaces, ],
            );
            
            $layerm->slices->clear;
            $layerm->slices->append(
                map Slic3r::Surface->new
                    (expolygon => $_, surface_type => S_TYPE_INTERNAL),
                    @$diff
            );
        }
            
        # update layer slices after repairing the single regions
        $layer->make_slices;
    }
    
    # remove empty layers from bottom
    my $first_object_layer_id = $self->config->raft_layers;
    while (@{$self->layers} && !@{$self->layers->[$first_object_layer_id]->slices} && !map @{$_->thin_walls}, @{$self->layers->[$first_object_layer_id]->regions}) {
        splice @{$self->layers}, $first_object_layer_id, 1;
        for (my $i = $first_object_layer_id; $i <= $#{$self->layers}; $i++) {
            $self->layers->[$i]->id($i);
        }
    }
}

sub make_perimeters {
    my $self = shift;
    
    # compare each layer to the one below, and mark those slices needing
    # one additional inner perimeter, like the top of domed objects-
    
    # this algorithm makes sure that at least one perimeter is overlapping
    # but we don't generate any extra perimeter if fill density is zero, as they would be floating
    # inside the object - infill_only_where_needed should be the method of choice for printing
    # hollow objects
    if ($self->config->extra_perimeters && $self->config->perimeters > 0 && $self->config->fill_density > 0) {
        for my $region_id (0 .. ($self->print->regions_count-1)) {
            for my $layer_id (0 .. $self->layer_count-2) {
                my $layerm          = $self->layers->[$layer_id]->regions->[$region_id];
                my $upper_layerm    = $self->layers->[$layer_id+1]->regions->[$region_id];
                my $perimeter_spacing       = $layerm->perimeter_flow->scaled_spacing;
                
                my $overlap = $perimeter_spacing;  # one perimeter
                
                my $diff = diff(
                    offset([ map @{$_->expolygon}, @{$layerm->slices} ], -($self->config->perimeters * $perimeter_spacing)),
                    offset([ map @{$_->expolygon}, @{$upper_layerm->slices} ], -$overlap),
                );
                next if !@$diff;
                # if we need more perimeters, $diff should contain a narrow region that we can collapse
                
                $diff = diff(
                    $diff,
                    offset2($diff, -$perimeter_spacing, +$perimeter_spacing),
                    1,
                );
                next if !@$diff;
                # diff contains the collapsed area
                
                foreach my $slice (@{$layerm->slices}) {
                    my $extra_perimeters = 0;
                    CYCLE: while (1) {
                        # compute polygons representing the thickness of the hypotetical new internal perimeter
                        # of our slice
                        $extra_perimeters++;
                        my $hypothetical_perimeter = diff(
                            offset($slice->expolygon->arrayref, -($perimeter_spacing * ($self->config->perimeters + $extra_perimeters-1))),
                            offset($slice->expolygon->arrayref, -($perimeter_spacing * ($self->config->perimeters + $extra_perimeters))),
                        );
                        last CYCLE if !@$hypothetical_perimeter;  # no extra perimeter is possible
                        
                        # only add the perimeter if there's an intersection with the collapsed area
                        last CYCLE if !@{ intersection($diff, $hypothetical_perimeter) };
                        Slic3r::debugf "  adding one more perimeter at layer %d\n", $layer_id;
                        $slice->extra_perimeters($extra_perimeters);
                    }
                }
            }
        }
    }
    
    Slic3r::parallelize(
        items => sub { 0 .. ($self->layer_count-1) },
        thread_cb => sub {
            my $q = shift;
            while (defined (my $layer_id = $q->dequeue)) {
                $self->layers->[$layer_id]->make_perimeters;
            }
        },
        collect_cb => sub {},
        no_threads_cb => sub {
            $_->make_perimeters for @{$self->layers};
        },
    );
}

sub detect_surfaces_type {
    my $self = shift;
    Slic3r::debugf "Detecting solid surfaces...\n";
    
    # prepare a reusable subroutine to make surface differences
    my $difference = sub {
        my ($subject, $clip, $result_type) = @_;
        my $expolygons = diff_ex(
            [ map @$_, @$subject ],
            [ map @$_, @$clip ],
            1,
        );
        return map Slic3r::Surface->new(expolygon => $_, surface_type => $result_type),
            @$expolygons;
    };
    
    for my $region_id (0 .. ($self->print->regions_count-1)) {
        for my $i (0 .. ($self->layer_count-1)) {
            my $layerm = $self->layers->[$i]->regions->[$region_id];
            
            # comparison happens against the *full* slices (considering all regions)
            my $upper_layer = $self->layers->[$i+1];
            my $lower_layer = $i > 0 ? $self->layers->[$i-1] : undef;
            
            my (@bottom, @top, @internal) = ();
            
            # find top surfaces (difference between current surfaces
            # of current layer and upper one)
            if ($upper_layer) {
                @top = $difference->(
                    [ map $_->expolygon, @{$layerm->slices} ],
                    $upper_layer->slices,
                    S_TYPE_TOP,
                );
            } else {
                # if no upper layer, all surfaces of this one are solid
                # we clone surfaces because we're going to clear the slices collection
                @top = map $_->clone, @{$layerm->slices};
                $_->surface_type(S_TYPE_TOP) for @top;
            }
            
            # find bottom surfaces (difference between current surfaces
            # of current layer and lower one)
            if ($lower_layer) {
                # lower layer's slices are already Surface objects
                @bottom = $difference->(
                    [ map $_->expolygon, @{$layerm->slices} ],
                    $lower_layer->slices,
                    S_TYPE_BOTTOM,
                );
            } else {
                # if no lower layer, all surfaces of this one are solid
                # we clone surfaces because we're going to clear the slices collection
                @bottom = map $_->clone, @{$layerm->slices};
                $_->surface_type(S_TYPE_BOTTOM) for @bottom;
            }
            
            # now, if the object contained a thin membrane, we could have overlapping bottom
            # and top surfaces; let's do an intersection to discover them and consider them
            # as bottom surfaces (to allow for bridge detection)
            if (@top && @bottom) {
                my $overlapping = intersection_ex([ map $_->p, @top ], [ map $_->p, @bottom ]);
                Slic3r::debugf "  layer %d contains %d membrane(s)\n", $layerm->id, scalar(@$overlapping);
                @top = $difference->([map $_->expolygon, @top], $overlapping, S_TYPE_TOP);
            }
            
            # find internal surfaces (difference between top/bottom surfaces and others)
            @internal = $difference->(
                [ map $_->expolygon, @{$layerm->slices} ],
                [ map $_->expolygon, @top, @bottom ],
                S_TYPE_INTERNAL,
            );
            
            # save surfaces to layer
            $layerm->slices->clear;
            $layerm->slices->append(@bottom, @top, @internal);
            
            Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
                $layerm->id, scalar(@bottom), scalar(@top), scalar(@internal);
        }
        
        # clip surfaces to the fill boundaries
        foreach my $layer (@{$self->layers}) {
            my $layerm = $layer->regions->[$region_id];
            my $fill_boundaries = [ map $_->clone->p, @{$layerm->fill_surfaces} ];
            $layerm->fill_surfaces->clear;
            foreach my $surface (@{$layerm->slices}) {
                my $intersection = intersection_ex(
                    [ $surface->p ],
                    $fill_boundaries,
                );
                $layerm->fill_surfaces->append(map Slic3r::Surface->new
                    (expolygon => $_, surface_type => $surface->surface_type),
                    @$intersection);
            }
        }
    }
}

sub clip_fill_surfaces {
    my $self = shift;
    return unless $self->config->infill_only_where_needed;
    
    # We only want infill under ceilings; this is almost like an
    # internal support material.
    
    my $additional_margin = scale 3;
    
    my $overhangs = [];  # arrayref of polygons
    for my $layer_id (reverse 0..$#{$self->layers}) {
        my $layer = $self->layers->[$layer_id];
        my @layer_internal = ();
        my @new_internal = ();
        
        # clip this layer's internal surfaces to @overhangs
        foreach my $layerm (@{$layer->regions}) {
            # we assume that this step is run before bridge_over_infill() and combine_infill()
            # so these are the only internal types we might have
            my (@internal, @other) = ();
            foreach my $surface (map $_->clone, @{$layerm->fill_surfaces}) {
                $surface->surface_type == S_TYPE_INTERNAL
                    ? push @internal, $surface
                    : push @other, $surface;
            }
            
            # keep all the original internal surfaces to detect overhangs in this layer
            push @layer_internal, @internal;
            
            push @new_internal, my @new = map Slic3r::Surface->new(
                expolygon       => $_,
                surface_type    => S_TYPE_INTERNAL,
            ),
            @{intersection_ex(
                $overhangs,
                [ map $_->p, @internal ],
            )};
            
            $layerm->fill_surfaces->clear;
            $layerm->fill_surfaces->append(@new, @other);
        }
        
        # get this layer's overhangs defined as the full slice minus the internal infill
        # (thus we also consider perimeters)
        if ($layer_id > 0) {
            my $solid = diff(
                [ map @$_, @{$layer->slices} ],
                \@layer_internal,
            );
            $overhangs = offset($solid, +$additional_margin);
            push @$overhangs, @new_internal;  # propagate upper overhangs
        }
    }
}

sub bridge_over_infill {
    my $self = shift;
    return if $self->config->fill_density == 1;
    
    for my $layer_id (1..$#{$self->layers}) {
        my $layer       = $self->layers->[$layer_id];
        my $lower_layer = $self->layers->[$layer_id-1];
        
        foreach my $layerm (@{$layer->regions}) {
            # compute the areas needing bridge math 
            my @internal_solid = @{$layerm->fill_surfaces->filter_by_type(S_TYPE_INTERNALSOLID)};
            my @lower_internal = map @{$_->fill_surfaces->filter_by_type(S_TYPE_INTERNAL)}, @{$lower_layer->regions};
            my $to_bridge = intersection_ex(
                [ map $_->p, @internal_solid ],
                [ map $_->p, @lower_internal ],
            );
            next unless @$to_bridge;
            Slic3r::debugf "Bridging %d internal areas at layer %d\n", scalar(@$to_bridge), $layer_id;
            
            # build the new collection of fill_surfaces
            {
                my @new_surfaces = map $_->clone, grep $_->surface_type != S_TYPE_INTERNALSOLID, @{$layerm->fill_surfaces};
                push @new_surfaces, map Slic3r::Surface->new(
                        expolygon       => $_,
                        surface_type    => S_TYPE_INTERNALBRIDGE,
                    ), @$to_bridge;
                push @new_surfaces, map Slic3r::Surface->new(
                        expolygon       => $_,
                        surface_type    => S_TYPE_INTERNALSOLID,
                    ), @{diff_ex(
                        [ map $_->p, @internal_solid ],
                        [ map @$_, @$to_bridge ],
                        1,
                    )};
                $layerm->fill_surfaces->clear;
                $layerm->fill_surfaces->append(@new_surfaces);
            }
            
            # exclude infill from the layers below if needed
            # see discussion at https://github.com/alexrj/Slic3r/issues/240
            # Update: do not exclude any infill. Sparse infill is able to absorb the excess material.
            if (0) {
                my $excess = $layerm->extruders->{infill}->bridge_flow->width - $layerm->height;
                for (my $i = $layer_id-1; $excess >= $self->layers->[$i]->height; $i--) {
                    Slic3r::debugf "  skipping infill below those areas at layer %d\n", $i;
                    foreach my $lower_layerm (@{$self->layers->[$i]->regions}) {
                        my @new_surfaces = ();
                        # subtract the area from all types of surfaces
                        foreach my $group (Slic3r::Surface->group(@{$lower_layerm->fill_surfaces})) {
                            push @new_surfaces, map $group->[0]->clone(expolygon => $_),
                                @{diff_ex(
                                    [ map $_->p, @$group ],
                                    [ map @$_, @$to_bridge ],
                                )};
                            push @new_surfaces, map Slic3r::Surface->new(
                                expolygon       => $_,
                                surface_type    => S_TYPE_INTERNALVOID,
                            ), @{intersection_ex(
                                [ map $_->p, @$group ],
                                [ map @$_, @$to_bridge ],
                            )};
                        }
                        $lower_layerm->fill_surfaces->clear;
                        $lower_layerm->fill_surfaces->append(@new_surfaces);
                    }
                    
                    $excess -= $self->layers->[$i]->height;
                }
            }
        }
    }
}

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    my $margin = scale &Slic3r::EXTERNAL_INFILL_MARGIN;
    
    for my $region_id (0 .. ($self->print->regions_count-1)) {
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layerm = $self->layers->[$i]->regions->[$region_id];
            
            if ($self->config->solid_infill_every_layers && $self->config->fill_density > 0
                && ($i % $self->config->solid_infill_every_layers) == 0) {
                $_->surface_type(S_TYPE_INTERNALSOLID) for @{$layerm->fill_surfaces->filter_by_type(S_TYPE_INTERNAL)};
            }
            
            EXTERNAL: foreach my $type (S_TYPE_TOP, S_TYPE_BOTTOM) {
                # find slices of current type for current layer
                # use slices instead of fill_surfaces because they also include the perimeter area
                # which needs to be propagated in shells; we need to grow slices like we did for
                # fill_surfaces though.  Using both ungrown slices and grown fill_surfaces will
                # not work in some situations, as there won't be any grown region in the perimeter 
                # area (this was seen in a model where the top layer had one extra perimeter, thus
                # its fill_surfaces was thinner than the lower layer's infill)
                my $solid = offset([ map $_->p, @{$layerm->slices->filter_by_type($type)} ], $margin);
                next if !@$solid;
                Slic3r::debugf "Layer %d has %s surfaces\n", $i, ($type == S_TYPE_TOP) ? 'top' : 'bottom';
                
                my $solid_layers = ($type == S_TYPE_TOP)
                    ? $self->config->top_solid_layers
                    : $self->config->bottom_solid_layers;
                NEIGHBOR: for (my $n = ($type == S_TYPE_TOP) ? $i-1 : $i+1; 
                        abs($n - $i) <= $solid_layers-1; 
                        ($type == S_TYPE_TOP) ? $n-- : $n++) {
                    
                    next if $n < 0 || $n >= $self->layer_count;
                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                    
                    my $neighbor_fill_surfaces = $self->layers->[$n]->regions->[$region_id]->fill_surfaces;
                    my @neighbor_fill_surfaces = map $_->clone, @$neighbor_fill_surfaces;  # clone because we will use these surfaces even after clearing the collection
                    
                    # find intersection between neighbor and current layer's surfaces
                    # intersections have contours and holes
                    # we update $solid so that we limit the next neighbor layer to the areas that were
                    # found on this one - in other words, solid shells on one layer (for a given external surface)
                    # are always a subset of the shells found on the previous shell layer
                    # this approach allows for DWIM in hollow sloping vases, where we want bottom
                    # shells to be generated in the base but not in the walls (where there are many
                    # narrow bottom surfaces): reassigning $solid will consider the 'shadow' of the 
                    # upper perimeter as an obstacle and shell will not be propagated to more upper layers
                    my $new_internal_solid = $solid = intersection(
                        $solid,
                        [ map $_->p, grep { ($_->surface_type == S_TYPE_INTERNAL) || ($_->surface_type == S_TYPE_INTERNALSOLID) } @neighbor_fill_surfaces ],
                        1,
                    );
                    next EXTERNAL if !@$new_internal_solid;
                    
                    # make sure the new internal solid is wide enough, as it might get collapsed when
                    # spacing is added in Fill.pm
                    {
                        # we use a higher miterLimit here to handle areas with acute angles
                        # in those cases, the default miterLimit would cut the corner and we'd
                        # get a triangle in $too_narrow; if we grow it below then the shell
                        # would have a different shape from the external surface and we'd still
                        # have the same angle, so the next shell would be grown even more and so on.
                        my $margin = 3 * $layerm->solid_infill_flow->scaled_width; # require at least this size
                        my $too_narrow = diff(
                            $new_internal_solid,
                            offset2($new_internal_solid, -$margin, +$margin, CLIPPER_OFFSET_SCALE, JT_MITER, 5),
                            1,
                        );
                        
                        # if some parts are going to collapse, use a different strategy according to fill density
                        if (@$too_narrow) {
                            if ($self->config->fill_density > 0) {
                                # if we have internal infill, grow the collapsing parts and add the extra area to 
                                # the neighbor layer as well as to our original surfaces so that we support this 
                                # additional area in the next shell too

                                # make sure our grown surfaces don't exceed the fill area
                                my @grown = @{intersection(
                                    offset($too_narrow, +$margin),
                                    [ map $_->p, @neighbor_fill_surfaces ],
                                )};
                                $new_internal_solid = $solid = [ @grown, @$new_internal_solid ];
                            } else {
                                # if we're printing a hollow object, we discard such small parts
                                $new_internal_solid = $solid = diff(
                                    $new_internal_solid,
                                    $too_narrow,
                                );
                            }
                        }
                    }
                    
                    # internal-solid are the union of the existing internal-solid surfaces
                    # and new ones
                    my $internal_solid = union_ex([
                        ( map $_->p, grep $_->surface_type == S_TYPE_INTERNALSOLID, @neighbor_fill_surfaces ),
                        @$new_internal_solid,
                    ]);
                    
                    # subtract intersections from layer surfaces to get resulting internal surfaces
                    my $internal = diff_ex(
                        [ map $_->p, grep $_->surface_type == S_TYPE_INTERNAL, @neighbor_fill_surfaces ],
                        [ map @$_, @$internal_solid ],
                        1,
                    );
                    Slic3r::debugf "    %d internal-solid and %d internal surfaces found\n",
                        scalar(@$internal_solid), scalar(@$internal);
                    
                    # assign resulting internal surfaces to layer
                    $neighbor_fill_surfaces->clear;
                    $neighbor_fill_surfaces->append(map Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNAL), @$internal);
                    
                    # assign new internal-solid surfaces to layer
                    $neighbor_fill_surfaces->append(map Slic3r::Surface->new
                        (expolygon => $_, surface_type => S_TYPE_INTERNALSOLID), @$internal_solid);
                    
                    # assign top and bottom surfaces to layer
                    foreach my $s (Slic3r::Surface->group(grep { ($_->surface_type == S_TYPE_TOP) || ($_->surface_type == S_TYPE_BOTTOM) } @neighbor_fill_surfaces)) {
                        my $solid_surfaces = diff_ex(
                            [ map $_->p, @$s ],
                            [ map @$_, @$internal_solid, @$internal ],
                            1,
                        );
                        $neighbor_fill_surfaces->append(map $s->[0]->clone(expolygon => $_), @$solid_surfaces);
                    }
                }
            }
        }
    }
}

# combine fill surfaces across layers
sub combine_infill {
    my $self = shift;
    return unless $self->config->infill_every_layers > 1 && $self->config->fill_density > 0;
    my $every = $self->config->infill_every_layers;
    
    my $layer_count = $self->layer_count;
    my @layer_heights = map $self->layers->[$_]->height, 0 .. $layer_count-1;
    
    for my $region_id (0 .. ($self->print->regions_count-1)) {
        # limit the number of combined layers to the maximum height allowed by this regions' nozzle
        my $nozzle_diameter = $self->print->regions->[$region_id]->extruders->{infill}->nozzle_diameter;
        
        # define the combinations
        my @combine = ();   # layer_id => thickness in layers
        {
            my $current_height = my $layers = 0;
            for my $layer_id (1 .. $#layer_heights) {
                my $height = $self->layers->[$layer_id]->height;
                
                if ($current_height + $height >= $nozzle_diameter || $layers >= $every) {
                    $combine[$layer_id-1] = $layers;
                    $current_height = $layers = 0;
                }
                
                $current_height += $height;
                $layers++;
            }
        }
        
        # skip bottom layer
        for my $layer_id (1 .. $#combine) {
            next unless ($combine[$layer_id] // 1) > 1;
            my @layerms = map $self->layers->[$_]->regions->[$region_id],
                ($layer_id - ($combine[$layer_id]-1) .. $layer_id);
            
            # only combine internal infill
            for my $type (S_TYPE_INTERNAL) {
                # we need to perform a multi-layer intersection, so let's split it in pairs
                
                # initialize the intersection with the candidates of the lowest layer
                my $intersection = [ map $_->expolygon, @{$layerms[0]->fill_surfaces->filter_by_type($type)} ];
                
                # start looping from the second layer and intersect the current intersection with it
                for my $layerm (@layerms[1 .. $#layerms]) {
                    $intersection = intersection_ex(
                        [ map @$_, @$intersection ],
                        [ map @{$_->expolygon}, @{$layerm->fill_surfaces->filter_by_type($type)} ],
                    );
                }
                
                my $area_threshold = $layerms[0]->infill_area_threshold;
                @$intersection = grep $_->area > $area_threshold, @$intersection;
                next if !@$intersection;
                Slic3r::debugf "  combining %d %s regions from layers %d-%d\n",
                    scalar(@$intersection),
                    ($type == S_TYPE_INTERNAL ? 'internal' : 'internal-solid'),
                    $layer_id-($every-1), $layer_id;
                
                # $intersection now contains the regions that can be combined across the full amount of layers
                # so let's remove those areas from all layers
                
                 my @intersection_with_clearance = map @{$_->offset(
                       $layerms[-1]->solid_infill_flow->scaled_width    / 2
                     + $layerms[-1]->perimeter_flow->scaled_width / 2
                     # Because fill areas for rectilinear and honeycomb are grown 
                     # later to overlap perimeters, we need to counteract that too.
                     + (($type == S_TYPE_INTERNALSOLID || $self->config->fill_pattern =~ /(rectilinear|honeycomb)/)
                       ? $layerms[-1]->solid_infill_flow->scaled_width * &Slic3r::INFILL_OVERLAP_OVER_SPACING
                       : 0)
                     )}, @$intersection;

                
                foreach my $layerm (@layerms) {
                    my @this_type   = @{$layerm->fill_surfaces->filter_by_type($type)};
                    my @other_types = map $_->clone, grep $_->surface_type != $type, @{$layerm->fill_surfaces};
                    
                    my @new_this_type = map Slic3r::Surface->new(expolygon => $_, surface_type => $type),
                        @{diff_ex(
                            [ map $_->p, @this_type ],
                            [ @intersection_with_clearance ],
                        )};
                    
                    # apply surfaces back with adjusted depth to the uppermost layer
                    if ($layerm->id == $layer_id) {
                        push @new_this_type,
                            map Slic3r::Surface->new(
                                expolygon        => $_,
                                surface_type     => $type,
                                thickness        => sum(map $_->height, @layerms),
                                thickness_layers => scalar(@layerms),
                            ),
                            @$intersection;
                    } else {
                        # save void surfaces
                        push @this_type,
                            map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNALVOID),
                            @{intersection_ex(
                                [ map @{$_->expolygon}, @this_type ],
                                [ @intersection_with_clearance ],
                            )};
                    }
                    
                    $layerm->fill_surfaces->clear;
                    $layerm->fill_surfaces->append(@new_this_type, @other_types);
                }
            }
        }
    }
}

sub generate_support_material {
    my $self = shift;
    return unless $self->config->support_material && $self->layer_count >= 2;
    
    my $flow = $self->print->support_material_flow;
    
    # how much we extend support around the actual contact area
    #my $margin      = $flow->scaled_width / 2;
    my $margin      = scale 3;
    
    # increment used to reach $margin in steps to avoid trespassing thin objects
    my $margin_step = $margin/3;
    
    # if user specified a custom angle threshold, convert it to radians
    my $threshold_rad;
    if ($self->config->support_material_threshold) {
        $threshold_rad = deg2rad($self->config->support_material_threshold + 1);  # +1 makes the threshold inclusive
        Slic3r::debugf "Threshold angle = %d°\n", rad2deg($threshold_rad);
    }
    
    # shape of contact area
    my $contact_loops   = 1;
    my $circle_radius   = 1.5 * $flow->scaled_width;
    my $circle_distance = 3 * $circle_radius;
    my $circle          = Slic3r::Polygon->new(map [ $circle_radius * cos $_, $circle_radius * sin $_ ], (5*PI/3, 4*PI/3, PI, 2*PI/3, PI/3, 0));
    
    # determine contact areas
    my %contact  = ();  # contact_z => [ polygons ]
    my %overhang = ();  # contact_z => [ expolygons ] - this stores the actual overhang supported by each contact layer
    for my $layer_id (1 .. $#{$self->layers}) {
        my $layer = $self->layers->[$layer_id];
        my $lower_layer = $self->layers->[$layer_id-1];
        
        # detect overhangs and contact areas needed to support them
        my (@overhang, @contact) = ();
        foreach my $layerm (@{$layer->regions}) {
            my $fw = $layerm->perimeter_flow->scaled_width;
            my $diff;
            
            # If a threshold angle was specified, use a different logic for detecting overhangs.
            if (defined $threshold_rad || $layer_id <= $self->config->support_material_enforce_layers) {
                my $d = defined $threshold_rad
                    ? scale $lower_layer->height * ((cos $threshold_rad) / (sin $threshold_rad))
                    : 0;
                
                $diff = diff(
                    offset([ map $_->p, @{$layerm->slices} ], -$d),
                    [ map @$_, @{$lower_layer->slices} ],
                );
                
                # only enforce spacing from the object ($fw/2) if the threshold angle
                # is not too high: in that case, $d will be very small (as we need to catch
                # very short overhangs), and such contact area would be eaten by the
                # enforced spacing, resulting in high threshold angles to be almost ignored
                $diff = diff(
                    offset($diff, $d - $fw/2),
                    [ map @$_, @{$lower_layer->slices} ],
                ) if $d > $fw/2;
            } else {
                $diff = diff(
                    offset([ map $_->p, @{$layerm->slices} ], -$fw/2),
                    [ map @$_, @{$lower_layer->slices} ],
                );
                # $diff now contains the ring or stripe comprised between the boundary of 
                # lower slices and the centerline of the last perimeter in this overhanging layer.
                # Void $diff means that there's no upper perimeter whose centerline is
                # outside the lower slice boundary, thus no overhang
            }
            
            next if !@$diff;
            push @overhang, @{union_ex($diff)};  # NOTE: this is not the full overhang as it misses the outermost half of the perimeter width!
            
            # Let's define the required contact area by using a max gap of half the upper 
            # extrusion width and extending the area according to the configured margin.
            # We increment the area in steps because we don't want our support to overflow
            # on the other side of the object (if it's very thin).
            {
                my @slices_margin = @{offset([ map @$_, @{$lower_layer->slices} ], $fw/2)};
                for ($fw/2, map {$margin_step} 1..($margin / $margin_step)) {
                    $diff = diff(
                        offset($diff, $_),
                        \@slices_margin,
                    );
                }
            }
            push @contact, @$diff;
        }
        next if !@contact;
        
        # now apply the contact areas to the layer were they need to be made
        {
            # get the average nozzle diameter used on this layer
            my @nozzle_diameters = map $_->nozzle_diameter,
                map { $_->perimeter_flow, $_->solid_infill_flow }
                @{$layer->regions};
            my $nozzle_diameter = sum(@nozzle_diameters)/@nozzle_diameters;
            
            my $contact_z = $layer->print_z - $nozzle_diameter * 1.5;
            ###$contact_z = $layer->print_z - $layer->height;
            
            # ignore this contact area if it's too low
            next if $contact_z < $Slic3r::Config->get_value('first_layer_height');
            
            $contact{$contact_z}  = [ @contact ];
            $overhang{$contact_z} = [ @overhang ];
            
            if (0) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("contact_" . $contact_z . ".svg",
                    expolygons      => union_ex(\@contact),
                    red_expolygons  => \@overhang,
                );
            }
        }
    }
    my @contact_z = sort keys %contact;
    
    # find object top surfaces
    # we'll use them to clip our support and detect where does it stick
    my %top = ();  # print_z => [ expolygons ]
    {
        my $projection = [];
        foreach my $layer (reverse @{$self->layers}) {
            if (my @top = map @{$_->slices->filter_by_type(S_TYPE_TOP)}, @{$layer->regions}) {
                # compute projection of the contact areas above this top layer
                # first add all the 'new' contact areas to the current projection
                # ('new' means all the areas that are lower than the last top layer
                # we considered)
                my $min_top = min(keys %top) // max(keys %contact);
                # use <= instead of just < because otherwise we'd ignore any contact regions
                # having the same Z of top layers
                push @$projection, map @{$contact{$_}}, grep { $_ > $layer->print_z && $_ <= $min_top } keys %contact;
                
                # now find whether any projection falls onto this top surface
                my $touching = intersection($projection, [ map $_->p, @top ]);
                if (@$touching) {
                    # grow top surfaces so that interface and support generation are generated
                    # with some spacing from object - it looks we don't need the actual
                    # top shapes so this can be done here
                    $top{ $layer->print_z } = offset($touching, $flow->scaled_spacing);
                }
                
                # remove the areas that touched from the projection that will continue on 
                # next, lower, top surfaces
                $projection = diff($projection, $touching);
            }
        }
    }
    my @top_z = sort keys %top;
    
    # we now know the upper and lower boundaries for our support material object
    # (@contact_z and @top_z), so we can generate intermediate layers
    my @support_layers = _compute_support_layers(\@contact_z, \@top_z, $self->config, $flow);
    
    # if we wanted to apply some special logic to the first support layers lying on
    # object's top surfaces this is the place to detect them
    
    # let's now generate interface layers below contact areas
    my %interface = ();  # layer_id => [ polygons ]
    my $interface_layers = $self->config->support_material_interface_layers;
    for my $layer_id (0 .. $#support_layers) {
        my $z = $support_layers[$layer_id];
        my $this = $contact{$z} // next;
        
        # count contact layer as interface layer
        for (my $i = $layer_id-1; $i >= 0 && $i > $layer_id-$interface_layers; $i--) {
            $z = $support_layers[$i];
            # Compute interface area on this layer as diff of upper contact area
            # (or upper interface area) and layer slices.
            # This diff is responsible of the contact between support material and
            # the top surfaces of the object. We should probably offset the top 
            # surfaces before performing the diff, but this needs investigation.
            $this = $interface{$i} = diff(
                [
                    @$this,                         # clipped projection of the current contact regions
                    @{ $interface{$i} || [] },      # interface regions already applied to this layer
                ],
                [
                    @{ $top{$z} || [] },            # top slices on this layer
                    @{ $contact{$z} || [] },        # contact regions on this layer
                ],
                1,
            );
        }
    }
    
    # let's now generate support layers under interface layers
    my %support   = ();  # layer_id => [ polygons ]
    {
        for my $i (reverse 0 .. $#support_layers-1) {
            my $z = $support_layers[$i];
            $support{$i} = diff(
                [
                    @{ $support{$i+1} || [] },      # support regions on upper layer
                    @{ $interface{$i+1} || [] },    # interface regions on upper layer
                ],
                [
                    @{ $top{$z} || [] },            # top slices on this layer
                    @{ $interface{$i} || [] },      # interface regions on this layer
                    @{ $contact{$z} || [] },        # contact regions on this layer
                ],
                1,
            );
        }
    }
    
    push @{$self->support_layers}, map Slic3r::Layer::Support->new(
        object  => $self,
        id      => $_,
        height  => ($_ == 0) ? $support_layers[$_] : ($support_layers[$_] - $support_layers[$_-1]),
        print_z => $support_layers[$_],
        slice_z => -1,
        slices  => [],
    ), 0 .. $#support_layers;

    Slic3r::debugf "Generating patterns\n";
    
    # prepare fillers
    my $pattern = $self->config->support_material_pattern;
    my @angles = ($self->config->support_material_angle);
    if ($pattern eq 'rectilinear-grid') {
        $pattern = 'rectilinear';
        push @angles, $angles[0] + 90;
    }
    
    my %fillers = (
        interface   => $self->fill_maker->filler('rectilinear'),
        support     => $self->fill_maker->filler($pattern),
    );
    
    my $interface_angle = $self->config->support_material_angle + 90;
    my $interface_spacing = $self->config->support_material_interface_spacing + $flow->spacing;
    my $interface_density = $interface_spacing == 0 ? 1 : $flow->spacing / $interface_spacing;
    my $support_spacing = $self->config->support_material_spacing + $flow->spacing;
    my $support_density = $support_spacing == 0 ? 1 : $flow->spacing / $support_spacing;
    
    my $process_layer = sub {
        my ($layer_id) = @_;
        
        $contact{$support_layers[$layer_id]}    ||= [];
        $interface{$layer_id}                   ||= [];
        $support{$layer_id}                     ||= [];
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("layer_" . $support_layers[$layer_id] . ".svg",
                red_expolygons      => union_ex($contact{$support_layers[$layer_id]}),
                green_expolygons    => union_ex($interface{$layer_id}),
            );
        }
        
        # islands
        my $result = { contact => [], interface => [], support => [] };
        $result->{islands} = union_ex([
            map @$_,
                $interface{$layer_id},
                $support{$layer_id},
                $contact{$support_layers[$layer_id]},
        ]);
        
        # contact
        my $contact_infill = [];
        if ((my $contact = $contact{$support_layers[$layer_id]}) && $contact_loops > 0) {
            my $overhang = $overhang{$support_layers[$layer_id]};
            $contact = [ grep $_->is_counter_clockwise, @$contact ];
            
            # generate the outermost loop
            my @loops0;
            {
                # find centerline of the external loop of the contours
                my @external_loops = @{offset($contact, -$flow->scaled_width/2)};
                
                # apply a pattern to the loop
                my @positions = map Slic3r::Polygon->new(@$_)->split_at_first_point->regular_points($circle_distance), @external_loops;
                @loops0 = @{diff(
                    [ @external_loops ],
                    [ map $circle->clone->translate(@$_), @positions ],
                )};
            }
            
            # make more loops
            my @loops = @loops0;
            for my $i (2..$contact_loops) {
                my $d = ($i-1) * $flow->scaled_spacing;
                push @loops, @{offset2(\@loops0, -$d -0.5*$flow->scaled_spacing, +0.5*$flow->scaled_spacing)};
            }
            
            # clip such loops to the side oriented towards the object
            @loops = map Slic3r::Polyline->new(@$_),
                @{ Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection(
                    [ map $_->pp, @{offset_ex([ map @$_, @$overhang ], +scale 3)} ],
                    [ map Slic3r::Polygon->new(@$_)->split_at_first_point->pp, @loops ],
                ) };
            
            # add the contact infill area to the interface area
            $contact_infill = offset2(\@loops0, -($contact_loops + 0.5) * $flow->scaled_spacing, +0.5*$flow->scaled_spacing);
            
            # transform loops into ExtrusionPath objects
            @loops = map Slic3r::ExtrusionPath->new(
                polyline        => $_,
                role            => EXTR_ROLE_SUPPORTMATERIAL,
                flow_spacing    => $flow->spacing,
            ), @loops;
            
            $result->{contact} = [ @loops ];
        }
        
        # interface and contact infill
        if (@{$interface{$layer_id}} || @$contact_infill) {
            $fillers{interface}->angle($interface_angle);
            
            # steal some space from support
            $interface{$layer_id} = intersection(
                offset([ map @$_, $interface{$layer_id}, $contact_infill ], scale 3),
                [ map @$_, $interface{$layer_id}, $support{$layer_id}, $contact_infill ],
                1,
            );
            $support{$layer_id} = diff(
                $support{$layer_id},
                $interface{$layer_id},
            );
            
            my @paths = ();
            foreach my $expolygon (@{union_ex($interface{$layer_id})}) {
                my @p = $fillers{interface}->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNAL),
                    density         => $interface_density,
                    flow_spacing    => $flow->spacing,
                    complete        => 1,
                );
                my $params = shift @p;
                
                push @paths, map Slic3r::ExtrusionPath->new(
                    polyline        => Slic3r::Polyline->new(@$_),
                    role            => EXTR_ROLE_SUPPORTMATERIAL,
                    height          => undef,
                    flow_spacing    => $params->{flow_spacing},
                ), @p;
            }            
            $result->{interface} = [ @paths ];
        }
        
        # support or flange
        if (@{$support{$layer_id}}) {
            my $filler = $fillers{support};
            $filler->angle($angles[ ($layer_id) % @angles ]);
            my $density         = $support_density;
            my $flow_spacing    = $flow->spacing;
            
            # TODO: use offset2_ex()
            my $to_infill = union_ex($support{$layer_id}, 1);
            my @paths = ();
            
            # base flange
            if ($layer_id == 0) {
                $filler = $fillers{interface};
                $filler->angle($self->config->support_material_angle + 90);
                $density        = 0.5;
                $flow_spacing   = $self->print->first_layer_support_material_flow->spacing;
            } else {
                # draw a perimeter all around support infill
                # TODO: use brim ordering algorithm
                push @paths, map Slic3r::ExtrusionPath->new(
                    polyline        => $_->split_at_first_point,
                    role            => EXTR_ROLE_SUPPORTMATERIAL,
                    height          => undef,
                    flow_spacing    => $flow->spacing,
                ), map @$_, @$to_infill;
                
                # TODO: use offset2_ex()
                $to_infill = offset_ex([ map @$_, @$to_infill ], -$flow->scaled_spacing);
            }
            
            foreach my $expolygon (@$to_infill) {
                my @p = $filler->fill_surface(
                    Slic3r::Surface->new(expolygon => $expolygon, surface_type => S_TYPE_INTERNAL),
                    density         => $density,
                    flow_spacing    => $flow_spacing,
                    complete        => 1,
                );
                my $params = shift @p;
                
                push @paths, map Slic3r::ExtrusionPath->new(
                    polyline        => Slic3r::Polyline->new(@$_),
                    role            => EXTR_ROLE_SUPPORTMATERIAL,
                    height          => undef,
                    flow_spacing    => $params->{flow_spacing},
                ), @p;
            }
            
            push @{$result->{support}}, @paths;
        }
        
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("islands_" . $support_layers[$layer_id] . ".svg",
                red_expolygons      => union_ex($contact{$support_layers[$layer_id]} || []),
                green_expolygons    => union_ex($interface{$layer_id} || []),
                red_polylines       => [ map $_->unpack->polyline, @{$result->{contact}} ],
                green_polylines     => [ map $_->unpack->polyline, @{$result->{interface}} ],
                polylines           => [ map $_->unpack->polyline, @{$result->{support}} ],
            );
        }
        
        return $result;
    };
    
    my $apply = sub {
        my ($layer_id, $result) = @_;
        my $layer = $self->support_layers->[$layer_id];
        
        my $interface_collection = Slic3r::ExtrusionPath::Collection->new(@{$result->{contact}}, @{$result->{interface}});
        $layer->support_interface_fills($interface_collection) if @$interface_collection > 0;
        
        my $support_collection = Slic3r::ExtrusionPath::Collection->new(@{$result->{support}});
        $layer->support_fills($support_collection) if @$support_collection > 0;
        
        # TODO: use a Slic3r::ExPolygon::Collection
        $layer->support_islands($result->{islands});
    };
    Slic3r::parallelize(
        items => [ 0 .. $#{$self->support_layers} ],
        thread_cb => sub {
            my $q = shift;
            my $result = {};
            while (defined (my $layer_id = $q->dequeue)) {
                $result->{$layer_id} = $process_layer->($layer_id);
            }
            return $result;
        },
        collect_cb => sub {
            my $result = shift;
            $apply->($_, $result->{$_}) for keys %$result;
        },
        no_threads_cb => sub {
            $apply->($_, $process_layer->($_)) for 0 .. $#{$self->support_layers};
        },
    );
}

sub _compute_support_layers {
    my ($contact_z, $top_z, $config, $flow) = @_;
    
    # quick table to check whether a given Z is a top surface
    my %top = map { $_ => 1 } @$top_z;
    
    # determine layer height for any non-contact layer
    # we use max() to prevent many ultra-thin layers to be inserted in case
    # layer_height > nozzle_diameter * 0.75
    my $support_material_height = max($config->layer_height, $flow->nozzle_diameter * 0.75);
    
    my @support_layers = sort { $a <=> $b } @$contact_z, @$top_z,
        (map { $_ + $flow->nozzle_diameter } @$top_z);
    
    # enforce first layer height
    my $first_layer_height = $config->get_value('first_layer_height');
    shift @support_layers while @support_layers && $support_layers[0] <= $first_layer_height;
    unshift @support_layers, $first_layer_height;
    
    for (my $i = $#support_layers; $i >= 0; $i--) {
        my $target_height = $support_material_height;
        if ($i > 0 && $top{ $support_layers[$i-1] }) {
            $target_height = $flow->nozzle_diameter;
        }
        
        # enforce first layer height
        if (($i == 0 && $support_layers[$i] > $target_height + $first_layer_height)
            || ($support_layers[$i] - $support_layers[$i-1] > $target_height + Slic3r::Geometry::epsilon)) {
            splice @support_layers, $i, 0, ($support_layers[$i] - $target_height);
            $i++;
        }
    }
    
    # remove duplicates and make sure all 0.x values have the leading 0
    {
        my %sl = map { 1 * $_ => 1 } @support_layers;
        @support_layers = sort { $a <=> $b } keys %sl;
    }
    
    return @support_layers;
}

1;
