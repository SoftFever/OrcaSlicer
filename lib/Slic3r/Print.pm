package Slic3r::Print;
use Moose;

use constant PI => 4 * atan2(1, 1);
use constant X => 0;
use constant Y => 1;

has 'x_length' => (
    is          => 'ro',
    isa         => 'Slic3r::Line::Length',
    required    => 1,
    coerce      => 1,
);

has 'y_length' => (
    is          => 'ro',
    isa         => 'Slic3r::Line::Length',
    required    => 1,
    coerce      => 1,
);

has 'layers' => (
    traits  => ['Array'],
    is      => 'rw',
    isa     => 'ArrayRef[Slic3r::Layer]',
    default => sub { [] },
    handles => {
        layer_count => 'count',
        add_layer   => 'push',
    },
);

sub layer {
    my $self = shift;
    my ($layer_id) = @_;
    
    # extend our print by creating all necessary layers
    if ($self->layer_count < $layer_id + 1) {
        for (my $i = $self->layer_count; $i <= $layer_id; $i++) {
            $self->add_layer(Slic3r::Layer->new(id => $i));
        }
    }
    
    return $self->layers->[$layer_id];
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
            join '  ', map $_->id, @{ $layer->fills } if $Slic3r::debug;
    }
}

sub export_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # calculate speed for gcode commands
    my $travel_feed_rate      = $Slic3r::travel_feed_rate * 60;  # mm/min
    my $print_feed_rate       = $Slic3r::print_feed_rate  * 60;  # mm/min
    my $retract_speed         = $Slic3r::retract_speed    * 60;  # mm/min
    
    # calculate number of decimals
    my $dec = length((1 / $Slic3r::resolution) - 1) + 1;
    
    # calculate X,Y shift to center print around specified origin
    my @shift = (
        $Slic3r::print_center->[X] - ($self->x_length * $Slic3r::resolution / 2),
        $Slic3r::print_center->[Y] - ($self->y_length * $Slic3r::resolution / 2),
    );
    
    # open output gcode file
    open my $fh, ">", $file
        or die "Failed to open $file for writing\n";
    
    # write start commands to file
    # TODO: this must be customizable by user
    print  $fh "G28 ; home all axes\n";
    printf $fh "M109 S%d ; wait for temperature to be reached\n", $Slic3r::temperature;
    print  $fh "G90 ; use absolute coordinates\n";
    print  $fh "G21 ; set units to millimeters\n";
    if ($Slic3r::use_relative_e_distances) {
        print $fh "M83 ; use relative distances for extrusion\n";
    } else {
        print $fh "M82 ; use absolute distances for extrusion\n";
    }
    
    # make up a subroutine to generate G1 commands
    my $extrusion_distance = 0;
    my $G1 = sub {
        my ($point, $z, $e, $comment) = @_;
        printf $fh "G1";
        
        if ($point) {
            printf $fh " X%.${dec}f Y%.${dec}f", 
                ($point->x * $Slic3r::resolution) + $shift[X], 
                ($point->y * $Slic3r::resolution) + $shift[Y]; #**
        }
        if ($z) {
            printf $fh " Z%.${dec}f", $z;
        }
        
        # apply the speed reduction for print moves on bottom layer
        my $speed_multiplier = defined $z && $z == 0 && $point 
            ? $Slic3r::bottom_layer_speed_ratio 
            : 1;

        if ($e) {
            $extrusion_distance = 0 if $Slic3r::use_relative_e_distances;
            $extrusion_distance += $e;
            printf $fh " F%.${dec}f E%.5f", 
                $e < 0 
                    ? $retract_speed
                    : ($print_feed_rate * $speed_multiplier), 
                $extrusion_distance;
        } else {
            printf $fh " F%.${dec}f", ($travel_feed_rate * $speed_multiplier);
        }
        printf $fh " ; %s", $comment if $comment;
        print  $fh "\n";
    };
    
    my $z;
    my $retracted = 0;
    my $Extrude = sub {
        my ($path, $description) = @_;
        
        # reset extrusion distance counter
        if (!$Slic3r::use_relative_e_distances) {
            $extrusion_distance = 0;
            print $fh "G92 E0 ; reset extrusion distance\n";
        }
        
        # go to first point while compensating retraction
        $G1->($path->lines->[0]->a, $z, 0, "move to first $description point");
        
        # compensate retraction
        if ($retracted) {
            $G1->(undef, undef, ($Slic3r::retract_length + $Slic3r::retract_restart_extra), 
                "compensate retraction");
        }
        
        # extrude while going to next points
        foreach my $line (@{ $path->lines }) {
            # calculate how much filament to drive into the extruder
            # to get the desired amount of extruded plastic
            my $e = $line->a->distance_to($line->b) * $Slic3r::resolution
                * $Slic3r::flow_width 
                * $Slic3r::layer_height
                / (($Slic3r::filament_diameter ** 2) * PI)
                / $Slic3r::filament_packing_density;
            
            $G1->($line->b, $z, $e, $description);
        }
        
        # retract
        if ($Slic3r::retract_length > 0) {
            $G1->(undef, undef, -$Slic3r::retract_length, "retract");
            $retracted = 1;
        }
    };
    
    # write gcode commands layer by layer
    foreach my $layer (@{ $self->layers }) {
        $z = ($layer->z * $Slic3r::resolution);
        
        # go to layer
        # TODO: retraction
        printf $fh "G1 Z%.${dec}f F%.${dec}f ; move to next layer\n", 
            $z, $travel_feed_rate;
        
        # extrude skirts
        $Extrude->($_, 'skirt') for @{ $layer->skirts };
        
        # extrude perimeters
        $Extrude->($_, 'perimeter') for @{ $layer->perimeters };
        
        # extrude fills
        $Extrude->($_, 'fill') for @{ $layer->fills };
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
