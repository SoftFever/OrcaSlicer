package Slic3r::Print;
use Moose;

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
        printf "  generated paths: %s\n",
            join '  ', map $_->id, @{ $layer->perimeters };
    }
}

sub extrude_fills {
    my $self = shift;
    
    my $fill_extruder = Slic3r::Fill::Rectilinear->new;
    
    foreach my $layer (@{ $self->layers }) {
        $fill_extruder->make_fill($self, $layer);
        printf "  generated %d paths: %s\n",
            scalar @{ $layer->fills },
            join '  ', map $_->id, @{ $layer->fills };
    }
}

sub export_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # calculate speed for gcode commands
    my $travel_feed_rate      = $Slic3r::travel_feed_rate * 60;  # mm/min
    my $print_feed_rate       = $Slic3r::print_feed_rate  * 60;  # mm/min
    my $extrusion_speed_ratio = ($Slic3r::flow_rate / $Slic3r::print_feed_rate);
    
    # calculate number of decimals
    my $dec = length((1 / $Slic3r::resolution) - 1);
    
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
    my $G1 = sub {
        my ($point, $z, $extrusion_distance, $comment) = @_;
        printf $fh "G1 X%.${dec}f Y%.${dec}f Z%.${dec}f", 
            ($point->x * $Slic3r::resolution) + $shift[X], 
            ($point->y * $Slic3r::resolution) + $shift[Y], #**
            $z;
        if ($extrusion_distance) {
            printf $fh " F%.${dec}f E%.${dec}f", 
                $print_feed_rate, 
                ($extrusion_distance * $extrusion_speed_ratio * $Slic3r::resolution);
        } else {
            printf $fh " F%.${dec}f", $travel_feed_rate;
        }
        printf $fh " ; %s", $comment if $comment;
        print  $fh "\n";
    };
    
    my $z;
    my $Extrude = sub {
        my ($path, $description) = @_;
        
        # reset extrusion distance counter
        my $extrusion_distance = 0;
        if (!$Slic3r::use_relative_e_distances) {
            print $fh "G92 E0 ; reset extrusion distance\n";
        }
        
        # go to first point (without extruding)
        $G1->($path->lines->[0]->a, $z, 0, "move to first $description point");
        
        # extrude while going to next points
        foreach my $line (@{ $path->lines }) {
            $extrusion_distance = 0 if $Slic3r::use_relative_e_distances;
            $extrusion_distance += $line->a->distance_to($line->b);
            $G1->($line->b, $z, $extrusion_distance, $description);
        }
        
        # TODO: retraction
    };
    
    # write gcode commands layer by layer
    foreach my $layer (@{ $self->layers }) {
        $z = ($layer->z * $Slic3r::resolution);
        
        # go to layer
        # TODO: retraction
        printf $fh "G1 Z%.${dec}f F%.${dec}f ; move to next layer\n", 
            $z, $travel_feed_rate;
        
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
