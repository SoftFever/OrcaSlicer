package Slic3r::Print;
use Moo;

use Math::Clipper ':all';
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

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    my $clipper = Math::Clipper->new;
    
    for (my $i = 0; $i < $self->layer_count; $i++) {
        my $layer = $self->layers->[$i];
        foreach my $type (qw(top bottom)) {
            # find surfaces of current type for current layer
            my @surfaces = grep $_->surface_type eq $type, @{$layer->surfaces} or next;
            Slic3r::debugf "Layer %d has %d surfaces of type '%s'\n",
                $i, scalar(@surfaces), $type;
            
            for (my $n = $type eq 'top' ? $i-1 : $i+1; 
                    abs($n - $i) <= $Slic3r::solid_layers-1; 
                    $type eq 'top' ? $n-- : $n++) {
                
                next if $n < 0 || $n >= $self->layer_count;
                Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                
                my $neighbor_polygons = [ map $_->p, grep $_->surface_type eq 'internal', @{$self->layers->[$n]->surfaces} ];
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
                $self->layers->[$n]->surfaces([]);
                foreach my $p (@$internal_polygons) {
                    push @{$self->layers->[$n]->surfaces}, Slic3r::Surface->new(
                        surface_type => 'internal',
                        contour => Slic3r::Polyline::Closed->cast($p->{outer}),
                        holes   => [
                            map Slic3r::Polyline::Closed->cast($_), @{$p->{holes}}
                        ],
                    );
                }
                
                # assign new internal-solid surfaces to layer
                foreach my $p (@$intersections) {
                    push @{$self->layers->[$n]->surfaces}, Slic3r::Surface->new(
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

# remove perimeters and fill surfaces which are too small to be extruded
sub remove_small_features {
    my $self = shift;
    
    $_->remove_small_features for @{$self->layers};
}

sub extrude_perimeters {
    my $self = shift;
    
    my $perimeter_extruder = Slic3r::Perimeter->new;
    
    foreach my $layer (@{ $self->layers }) {
        $perimeter_extruder->make_perimeter($layer);
        Slic3r::debugf "  generated paths: %s\n",
            join '  ', map $_->id, @{ $layer->perimeters } if $Slic3r::debug;
    }
}

sub extrude_fills {
    my $self = shift;
    
    my $fill_extruder = Slic3r::Fill::Rectilinear->new;
    
    foreach my $layer (@{ $self->layers }) {
        $fill_extruder->make_fill($self, $layer);
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
    # TODO: this must be customizable by user
    print  $fh "G28 ; home all axes\n";
    printf $fh "M104 S%d ; wait for temperature to be reached\n", $Slic3r::temperature;
    print  $fh "G90 ; use absolute coordinates\n";
    print  $fh "G21 ; set units to millimeters\n";
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
    # TODO: this must be customizable by user
    print $fh "M104 S0 ; turn off temperature\n";
    print $fh "G28 X0 ; home X axis\n";
    print $fh "M84 ; disable motors\n";
    
    # close our gcode file
    close $fh;
}

1;
