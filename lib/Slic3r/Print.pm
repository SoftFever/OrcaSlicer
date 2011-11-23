package Slic3r::Print;
use Moo;

use Math::ConvexHull 1.0.4 qw(convex_hull);
use Slic3r::Geometry qw(X Y PI);
use Slic3r::Geometry::Clipper qw(explode_expolygons safety_offset diff_ex intersection_ex
    union_ex offset JT_ROUND JT_MITER);
use XXX;

has 'x_length' => (
    is          => 'ro',
    required    => 1,
    coerce      => sub { sprintf '%.0f', $_[0] },
);

has 'y_length' => (
    is          => 'ro',
    required    => 1,
    coerce      => sub { sprintf '%.0f', $_[0] },
);

has 'layers' => (
    traits  => ['Array'],
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::Layer]',
    default => sub { [] },
);

sub new_from_stl {
    my $self = shift;
    my ($stl_file) = @_;
    
    my $print = Slic3r::STL->new->parse_file($stl_file);
    
    print "\n==> PROCESSING SLICES:\n";
    foreach my $layer (@{ $print->layers }) {
        printf "Making surfaces for layer %d:\n", $layer->id;
        
        # layer currently has many lines representing intersections of
        # model facets with the layer plane. there may also be lines
        # that we need to ignore (for example, when two non-horizontal
        # facets share a common edge on our plane, we get a single line;
        # however that line has no meaning for our layer as it's enclosed
        # inside a closed polyline)
        
        # build surfaces from sparse lines
        $layer->make_surfaces;
    }
    
    return $print;
}

sub layer_count {
    my $self = shift;
    return scalar @{ $self->layers };
}

sub max_length {
    my $self = shift;
    return ($self->x_length > $self->y_length) ? $self->x_length : $self->y_length;
}

sub layer {
    my $self = shift;
    my ($layer_id) = @_;
    
    # extend our print by creating all necessary layers
    
    if ($self->layer_count < $layer_id + 1) {
        for (my $i = $self->layer_count; $i <= $layer_id; $i++) {
            push @{ $self->layers }, Slic3r::Layer->new(id => $i);
        }
    }
    
    return $self->layers->[$layer_id];
}

sub detect_surfaces_type {
    my $self = shift;
    
    # prepare a reusable subroutine to make surface differences
    my $surface_difference = sub {
        my ($subject_surfaces, $clip_surfaces, $result_type) = @_;
        my $expolygons = diff_ex(
            [ map { ref $_ eq 'ARRAY' ? $_ : ref $_ eq 'Slic3r::ExPolygon' ? @$_ : $_->p } @$subject_surfaces ],
            [ map { ref $_ eq 'ARRAY' ? $_ : ref $_ eq 'Slic3r::ExPolygon' ? @$_ : $_->p } @$clip_surfaces ],
        );
        return grep $_->contour->is_printable,
            map Slic3r::Surface->cast_from_expolygon($_, surface_type => $result_type), 
            @$expolygons;
    };
    
    # clip surfaces to the fill boundaries
    foreach my $layer (@{$self->layers}) {
        my $intersection = intersection_ex(
            [ map $_->p, @{$layer->surfaces} ],
            [ map $_->p, @{$layer->fill_boundaries} ],
        );
        @{$layer->surfaces} = map Slic3r::Surface->cast_from_expolygon
            ($_, surface_type => 'internal'),
            @$intersection;
    }
    
    for (my $i = 0; $i < $self->layer_count; $i++) {
        my $layer = $self->layers->[$i];
        Slic3r::debugf "Detecting solid surfaces for layer %d\n", $layer->id;
        my $upper_layer = $self->layers->[$i+1];
        my $lower_layer = $i > 0 ? $self->layers->[$i-1] : undef;
        
        my (@bottom, @top, @internal) = ();
        
        # find top surfaces (difference between current surfaces
        # of current layer and upper one)
        if ($upper_layer) {
            # only consider those upper surfaces that are not small
            # (if they're too small, the interface with them can be treated
            # like a continuous solid surface instead of cutting a little
            # internal surface in it)
            my $min_area = ((7 * $Slic3r::flow_width / $Slic3r::resolution)**2) * PI;
            my $upper_surfaces = [ grep { $_->expolygon->contour->area > $min_area } @{$upper_layer->surfaces} ];
            
            @top = $surface_difference->($layer->surfaces, $upper_surfaces, 'top');
            
        } else {
            # if no upper layer, all surfaces of this one are solid
            @top = @{$layer->surfaces};
            $_->surface_type('top') for @top;
        }
        
        # find bottom surfaces (difference between current surfaces
        # of current layer and lower one)
        if ($lower_layer) {
            @bottom = $surface_difference->($layer->surfaces, $lower_layer->surfaces, 'bottom');
            
            $_->contour->merge_continuous_lines for @bottom;
            
            # merge_continuous_lines could return polylines with less than 3 points (thus invalid)
            # actually, this shouldn't happen so it deserves further investigation
            @bottom = grep $_->contour->is_valid, @bottom;
            
            foreach my $surface (@bottom) {
                $surface->contour->remove_acute_vertices;
        
                # okay, this is an Ugly Hack(tm) to avoid floating point math problems
                # with diagonal bridges. will find a nicer solution, promised.
                my $offset = safety_offset([$surface->contour->p]);
                @{$surface->contour->points} = map Slic3r::Point->new($_), @{ $offset->[0] };
            }
            
        } else {
            # if no lower layer, all surfaces of this one are solid
            @bottom = @{$layer->surfaces};
            $_->surface_type('bottom') for @bottom;
        }
        
        # now, if the object contained a thin membrane, we could have overlapping bottom
        # and top surfaces; let's do an intersection to discover them and consider them
        # as bottom surfaces (to allow for bridge detection)
        if (@top && @bottom) {
            my $overlapping = intersection_ex([ map $_->p, @top ], [ map $_->p, @bottom ]);
            Slic3r::debugf "  layer %d contains %d membrane(s)\n", $layer->id, scalar(@$overlapping);
            @top = $surface_difference->([@top], $overlapping, 'top');
        }
        
        # find internal surfaces (difference between top/bottom surfaces and others)
        @internal = $surface_difference->($layer->surfaces, [@top, @bottom], 'internal');
        
        # save surfaces to layer
        $layer->surfaces([ @bottom, @top, @internal ]);
        
        Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
            $layer->id, scalar(@bottom), scalar(@top), scalar(@internal);
    }
}

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    for (my $i = 0; $i < $self->layer_count; $i++) {
        my $layer = $self->layers->[$i];
        foreach my $type (qw(top bottom)) {
            # find surfaces of current type for current layer
            my @surfaces = grep $_->surface_type eq $type, @{$layer->surfaces} or next;
            my $surfaces_p = [ map $_->p, @surfaces ];
            Slic3r::debugf "Layer %d has %d surfaces of type '%s'\n",
                $i, scalar(@surfaces), $type;
            
            for (my $n = $type eq 'top' ? $i-1 : $i+1; 
                    abs($n - $i) <= $Slic3r::solid_layers-1; 
                    $type eq 'top' ? $n-- : $n++) {
                
                next if $n < 0 || $n >= $self->layer_count;
                Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                
                my $surfaces = $self->layers->[$n]->surfaces;
                my @neighbor = @$surfaces;
                
                # find intersection between @surfaces and current layer's surfaces
                # intersections have contours and holes
                my $new_internal_solid = intersection_ex(
                    $surfaces_p,
                    [ map $_->p, grep $_->surface_type =~ /internal/, @neighbor ],
                );
                next if !@$new_internal_solid;
                
                # internal-solid are the union of the existing internal-solid surfaces
                # and new ones
                my $internal_solid = union_ex([
                    ( map $_->p, grep $_->surface_type eq 'internal-solid', @neighbor ),
                    ( map @$_, @$new_internal_solid ),
                ]);
                
                # subtract intersections from layer surfaces to get resulting inner surfaces
                my $internal = diff_ex(
                    [ map $_->p, grep $_->surface_type eq 'internal', @neighbor ],
                    [ map @$_, @$internal_solid ],
                );
                Slic3r::debugf "    %d internal-solid and %d internal surfaces found\n",
                    scalar(@$internal_solid), scalar(@$internal);
                
                # Note: due to floating point math we're going to get some very small
                # polygons as $internal; they will be removed by removed_small_features()
                
                # assign resulting inner surfaces to layer
                @$surfaces = ();
                push @$surfaces, Slic3r::Surface->cast_from_expolygon
                    ($_, surface_type => 'internal')
                    for @$internal;
                
                # assign new internal-solid surfaces to layer
                push @$surfaces, Slic3r::Surface->cast_from_expolygon
                    ($_, surface_type => 'internal-solid')
                    for @$internal_solid;
                
                # assign top and bottom surfaces to layer
                foreach my $s (Slic3r::Surface->group(grep $_->surface_type =~ /top|bottom/, @neighbor)) {
                    my $solid_surfaces = diff_ex(
                        [ map $_->p, @$s ],
                        [ map @$_, @$internal_solid, @$internal ],
                    );
                    push @$surfaces, Slic3r::Surface->cast_from_expolygon
                        ($_, surface_type => $s->[0]->surface_type, bridge_angle => $s->[0]->bridge_angle)
                        for @$solid_surfaces;
                }
            }
        }
    }
}

sub extrude_skirt {
    my $self = shift;
    return unless $Slic3r::skirts > 0;
    
    # collect points from all layers contained in skirt height
    my @points = ();
    my @layers = map $self->layer($_), 0..($Slic3r::skirt_height-1);
    push @points, map @$_, map $_->p, map @{ $_->surfaces }, @layers;
    
    # find out convex hull
    my $convex_hull = convex_hull(\@points);
    
    # draw outlines from outside to inside
    my @skirts = ();
    for (my $i = $Slic3r::skirts - 1; $i >= 0; $i--) {
        my $distance = ($Slic3r::skirt_distance + ($Slic3r::flow_width * $i)) / $Slic3r::resolution;
        my $outline = offset([$convex_hull], $distance, $Slic3r::resolution * 100, JT_ROUND);
        push @skirts, Slic3r::ExtrusionLoop->cast([ @{$outline->[0]} ]);
    }
    
    # apply skirts to all layers
    push @{$_->skirts}, @skirts for @layers;
}

# combine fill surfaces across layers
sub infill_every_layers {
    my $self = shift;
    return unless $Slic3r::infill_every_layers > 1;
    
    printf "==> COMBINING INFILL\n";
    
    # start from bottom, skip first layer
    for (my $i = 1; $i < $self->layer_count; $i++) {
        my $layer = $self->layer($i);
        
        # skip layer if no internal fill surfaces
        next if !grep $_->surface_type eq 'internal', map @$_, @{$layer->surfaces};
        
        # for each possible depth, look for intersections with the lower layer
        # we do this from the greater depth to the smaller
        for (my $d = $Slic3r::infill_every_layers - 1; $d >= 1; $d--) {
            next if ($i - $d) < 0;
            my $lower_layer = $self->layer($i - 1);
            
            # select surfaces of the lower layer having the depth we're looking for
            my @lower_surfaces = grep $_->depth_layers == $d && $_->surface_type eq 'internal',
                map @$_, @{$lower_layer->surfaces};
            next if !@lower_surfaces;
            # process each group of surfaces separately
            foreach my $surfaces (@{$layer->surfaces}) {
                # calculate intersection between our surfaces and theirs
                my $intersection = intersection_ex(
                    [ map $_->p, grep $_->depth_layers <= $d, @lower_surfaces ],
                    [ map $_->p, grep $_->surface_type eq 'internal', @$surfaces ],
                );
                next if !@$intersection;
                my $intersection_offsetted = safety_offset([ map @$_, @$intersection ]);
                
                # new fill surfaces of the current layer are:
                # - any non-internal surface
                # - intersections found (with a $d + 1 depth)
                # - any internal surface not belonging to the intersection (with its original depth)
                {
                    my @new_surfaces = ();
                    push @new_surfaces, grep $_->surface_type ne 'internal', @$surfaces;
                    push @new_surfaces, map Slic3r::Surface->cast_from_expolygon
                        ($_, surface_type => 'internal', depth_layers => $d + 1), @$intersection;
                    
                    foreach my $depth (reverse $d..$Slic3r::infill_every_layers) {
                        push @new_surfaces, map Slic3r::Surface->cast_from_expolygon
                            ($_, surface_type => 'internal', depth_layers => $depth),
                            
                            # difference between our internal layers with depth == $depth
                            # and the intersection found
                            @{diff_ex(
                                [
                                    map $_->p, grep $_->surface_type eq 'internal' && $_->depth_layers == $depth, 
                                        @$surfaces,
                                ],
                                $intersection_offsetted,
                            )};
                    }
                    @$surfaces = @new_surfaces;
                }
                
                # now we remove the intersections from lower layer
                foreach my $lower_surfaces (@{$lower_layer->surfaces}) {
                    my @new_surfaces = ();
                    push @new_surfaces, grep $_->surface_type ne 'internal', @$lower_surfaces;
                    foreach my $depth (1..$Slic3r::infill_every_layers) {
                        push @new_surfaces, map Slic3r::Surface->cast_from_expolygon
                            ($_, surface_type => 'internal', depth_layers => $depth),
                            
                            # difference between internal layers with depth == $depth
                            # and the intersection found
                            @{diff_ex(
                                [
                                    map $_->p, grep $_->surface_type eq 'internal' && $_->depth_layers == $depth, 
                                        @$lower_surfaces,
                                ],
                                $intersection_offsetted,
                            )};
                    }
                    @$lower_surfaces = @new_surfaces;
                }
            }
        }
    }
}

sub export_gcode {
    my $self = shift;
    my ($file) = @_;
    
    printf "Exporting GCODE file...\n";
    
    # open output gcode file
    open my $fh, ">", $file
        or die "Failed to open $file for writing\n";
    
    # write start commands to file
    printf $fh "M104 S%d ; set temperature\n", $Slic3r::temperature unless $Slic3r::temperature;
    print  $fh "$Slic3r::start_gcode\n";
    printf $fh "M109 S%d ; wait for temperature to be reached\n", $Slic3r::temperature unless $Slic3r::temperature;
    print  $fh "G90 ; use absolute coordinates\n";
    print  $fh "G21 ; set units to millimeters\n";
    print  $fh "G92 E0 ; reset extrusion distance\n" if !$Slic3r::no_extrusion;
    if ($Slic3r::use_relative_e_distances) {
        print $fh "M83 ; use relative distances for extrusion\n";
    } else {
        print $fh "M82 ; use absolute distances for extrusion\n";
    }
    
    # set up our extruder object
    my $extruder = Slic3r::Extruder->new(
        # calculate X,Y shift to center print around specified origin
        shift_x => $Slic3r::print_center->[X] - ($self->x_length * $Slic3r::resolution / 2),
        shift_y => $Slic3r::print_center->[Y] - ($self->y_length * $Slic3r::resolution / 2),
    );
    
    # write gcode commands layer by layer
    foreach my $layer (@{ $self->layers }) {
        # go to layer
        printf $fh $extruder->change_layer($layer);
        
        # extrude skirts
        printf $fh $extruder->extrude_loop($_, 'skirt') for @{ $layer->skirts };
        
        # extrude perimeters
        printf $fh $extruder->extrude_loop($_, 'perimeter') for @{ $layer->perimeters };
        
        # extrude fills
        for my $fill (@{ $layer->fills }) {
            for ($fill->shortest_path($extruder->last_pos)) {
                printf $fh $extruder->extrude($_, 'fill');
                $extruder->enable_retraction(0);
            }
            $extruder->enable_retraction(1);
        }
    }
    
    # write end commands to file
    print $fh "$Slic3r::end_gcode\n";
    
    # close our gcode file
    close $fh;
}

1;
