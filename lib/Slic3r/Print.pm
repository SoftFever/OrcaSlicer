package Slic3r::Print;
use Moo;

use Math::Clipper ':all';
use Slic3r::Geometry::Clipper qw(diff_ex union_ex);
use XXX;

use constant X => 0;
use constant Y => 1;

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
    
    my $clipper = Math::Clipper->new;
    
    # prepare a reusable subroutine to make surface differences
    my $surface_difference = sub {
        my ($subject_surfaces, $clip_surfaces, $result_type) = @_;
        my $expolygons = diff_ex(
            [ map { ref $_ eq 'ARRAY' ? $_ : $_->p } @$subject_surfaces ],
            [ map { ref $_ eq 'ARRAY' ? $_ : $_->p } @$clip_surfaces ],
        );
        return grep $_->contour->is_printable,
            map Slic3r::Surface->cast_from_expolygon($_, surface_type => $result_type), 
            @$expolygons;
    };
    
    for (my $i = 0; $i < $self->layer_count; $i++) {
        my $layer = $self->layers->[$i];
        Slic3r::debugf "Detecting solid surfaces for layer %d\n", $layer->id;
        my $upper_layer = $self->layers->[$i+1];
        my $lower_layer = $i > 0 ? $self->layers->[$i-1] : undef;
        
        my (@bottom, @top, @internal) = ();
        
        # find top surfaces (difference between current surfaces
        # of current layer and upper one)
        if ($upper_layer) {
            # offset upper layer surfaces by extrusion_width * perimeters
            @top = $surface_difference->($layer->surfaces, $upper_layer->surfaces, 'top');
            
            # now check whether each resulting top surfaces is large enough to have its
            # own perimeters or whether it may be sufficient to use the lower layer's 
            # perimeters
            
            # offset upper layer's surfaces
            my $upper_surfaces_offsetted;
            {
                my $distance = $Slic3r::flow_width * ($Slic3r::perimeter_offsets + 1) / $Slic3r::resolution;
                $upper_surfaces_offsetted = offset([ map $_->p, @{$upper_layer->surfaces} ], $distance, 100, JT_MITER, 2);
            }
            
            @top = grep {
                my $surface = $_;
                my $diff = diff_ex([ map $_->p, $surface ], $upper_surfaces_offsetted);
                @$diff;
            } @top;
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
                my $offset = offset([$surface->contour->p], 100, 100, JT_MITER, 2);
                @{$surface->contour->points} = map Slic3r::Point->new($_), @{ $offset->[0] };
            }
            
            #Slic3r::SVG::output(undef, "layer_" . $layer->id . "_diff.svg",
            #    green_polygons  => [ map $_->p, @{$layer->surfaces} ],
            #    red_polygons    => [ map $_->p, @{$lower_layer->surfaces} ],
            #);
            
            # offset lower layer's surfaces
            my $lower_surfaces_offsetted;
            {
                my $distance = $Slic3r::flow_width * ($Slic3r::perimeter_offsets + 1) / $Slic3r::resolution;
                $lower_surfaces_offsetted = offset([ map $_->p, @{$lower_layer->surfaces} ], $distance, 100, JT_MITER, 2);
            }
            
            @bottom = grep {
                my $surface = $_;
                my $diff = diff_ex([ map $_->p, $surface ], $lower_surfaces_offsetted);
                @$diff;
            } @bottom;
            
        } else {
            # if no lower layer, all surfaces of this one are solid
            @bottom = @{$layer->surfaces};
            $_->surface_type('bottom') for @bottom;
        }
        
        # find internal surfaces (difference between top/bottom surfaces and others)
        @internal = $surface_difference->($layer->surfaces, [@top, @bottom], 'internal');
        
        # save surfaces to layer
        $layer->surfaces([ @bottom, @top, @internal ]);
        
        #use Slic3r::SVG;
        #Slic3r::SVG::output(undef, "layer_" . $layer->id . ".svg",
        #    white_polygons  => [ map $_->p, @internal ],
        #    green_polygons  => [ map $_->p, @bottom ],
        #    red_polygons    => [ map $_->p, @top ],
        #);
        
        Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
            $layer->id, scalar(@bottom), scalar(@top), scalar(@internal);
    }
}

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    my $clipper = Math::Clipper->new;
    
    for (my $i = 0; $i < $self->layer_count; $i++) {
        my $layer = $self->layers->[$i];
        foreach my $type (qw(top bottom)) {
            # find surfaces of current type for current layer
            my @surfaces = grep $_->surface_type eq $type, map @$_, @{$layer->fill_surfaces} or next;
            Slic3r::debugf "Layer %d has %d surfaces of type '%s'\n",
                $i, scalar(@surfaces), $type;
            
            for (my $n = $type eq 'top' ? $i-1 : $i+1; 
                    abs($n - $i) <= $Slic3r::solid_layers-1; 
                    $type eq 'top' ? $n-- : $n++) {
                
                next if $n < 0 || $n >= $self->layer_count;
                Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                
                foreach my $surfaces (@{$self->layers->[$n]->fill_surfaces}) {
                    my $neighbor_polygons = [ map $_->p, grep $_->surface_type =~ /internal/, @$surfaces ];
                    
                    # find intersection between @surfaces and current layer's surfaces
                    $clipper->add_subject_polygons([ map $_->p, @surfaces ]);
                    $clipper->add_clip_polygons($neighbor_polygons);
                    
                    # intersections have contours and holes
                    my $intersections = $clipper->ex_execute(CT_INTERSECTION, PFT_NONZERO, PFT_NONZERO);
                    $clipper->clear;
                    
                    next if @$intersections == 0;
                    Slic3r::debugf "    %d intersections found\n", scalar @$intersections;
                    
                    # subtract intersections from layer surfaces to get resulting inner surfaces
                    $clipper->add_subject_polygons($neighbor_polygons);
                    $clipper->add_clip_polygons([ map { $_->{outer}, @{$_->{holes}} } @$intersections ]);
                    my $internal_polygons = $clipper->ex_execute(CT_DIFFERENCE, PFT_NONZERO, PFT_NONZERO);
                    $clipper->clear;
                    
                    # Note: due to floating point math we're going to get some very small
                    # polygons as $internal_polygons; they will be removed by removed_small_features()
                    
                    # assign resulting inner surfaces to layer
                    @$surfaces = ();
                    foreach my $p (@$internal_polygons) {
                        push @$surfaces, Slic3r::Surface->new(
                            surface_type => 'internal',
                            contour => Slic3r::Polyline::Closed->cast($p->{outer}),
                            holes   => [
                                map Slic3r::Polyline::Closed->cast($_), @{$p->{holes}}
                            ],
                        );
                    }
                    
                    # assign new internal-solid surfaces to layer
                    foreach my $p (@$intersections) {
                        push @$surfaces, Slic3r::Surface->new(
                            surface_type => 'internal-solid',
                            contour => Slic3r::Polyline::Closed->cast($p->{outer}),
                            holes   => [
                                map Slic3r::Polyline::Closed->cast($_), @{$p->{holes}}
                            ],
                        );
                    }
                }
            }
        }
    }
}

# remove surfaces which are too small to be extruded
sub remove_small_surfaces {
    my $self = shift;
    $_->remove_small_surfaces for @{$self->layers};
}

# remove perimeters which are too small to be extruded
sub remove_small_perimeters {
    my $self = shift;
    $_->remove_small_perimeters for @{$self->layers};
}

# make bridges printable
sub process_bridges {
    my $self = shift;
    $_->process_bridges for @{ $self->layers };
}

sub extrude_perimeters {
    my $self = shift;
    
    my $perimeter_extruder = Slic3r::Perimeter->new;
    
    foreach my $layer (@{ $self->layers }) {
        $layer->detect_perimeter_surfaces;
        $perimeter_extruder->make_perimeter($layer);
        Slic3r::debugf "  generated paths: %s\n",
            join '  ', map $_->id, @{ $layer->perimeters } if $Slic3r::debug;
    }
}

# splits fill_surfaces in internal and bridge surfaces
sub split_bridges_fills {
    my $self = shift;
    $_->split_bridges_fills for @{$self->layers};
}

sub extrude_fills {
    my $self = shift;
    
    my $fill_extruder = Slic3r::Fill->new('print' => $self);
    
    foreach my $layer (@{ $self->layers }) {
        $fill_extruder->make_fill($layer);
        Slic3r::debugf "  generated %d paths: %s\n",
            scalar @{ $layer->fills },
            join '  ', map $_->id, map @{$_->paths}, @{ $layer->fills } if $Slic3r::debug;
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
    printf $fh "M104 S%d ; wait for temperature to be reached\n", $Slic3r::temperature;
    print  $fh "$Slic3r::start_gcode\n";
    print  $fh "G90 ; use absolute coordinates\n";
    print  $fh "G21 ; set units to millimeters\n";
    print  $fh "G92 E0 ; reset extrusion distance\n";
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
        
        # with the --high-res-perimeters options enabled we extrude perimeters for
        # each layer twice at half height
        if ($Slic3r::high_res_perimeters && $layer->id > 0) {
            # go to half-layer
            printf $fh $extruder->move_z($Slic3r::z_offset + $layer->z * $Slic3r::resolution - $Slic3r::layer_height/2);
            
            # extrude perimeters
            $extruder->flow_ratio(0.5);
            printf $fh $extruder->extrude_loop($_, 'perimeter') for @{ $layer->perimeters };
            $extruder->flow_ratio(1);
        }
        
        # go to layer
        printf $fh $extruder->move_z($Slic3r::z_offset + $layer->z * $Slic3r::resolution);
        
        # extrude skirts
        printf $fh $extruder->extrude_loop($_, 'skirt') for @{ $layer->skirts };
        
        # extrude perimeters
        printf $fh $extruder->extrude_loop($_, 'perimeter') for @{ $layer->perimeters };
        
        # extrude fills
        for my $fill (@{ $layer->fills }) {
            printf $fh $extruder->extrude($_, 'fill') 
                for $fill->shortest_path($extruder->last_pos);
        }
    }
    
    # write end commands to file
    print $fh "$Slic3r::end_gcode\n";
    
    # close our gcode file
    close $fh;
}

1;
