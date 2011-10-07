package Slic3r::Fill::Base;
use Moo;

use XXX;

has 'layer'               => (is => 'rw');
has 'max_print_dimension' => (is => 'rw');

use constant PI => 4 * atan2(1, 1);

sub infill_direction {
    my $self = shift;
    my ($surface) = @_;
    
    # set infill angle
    my (@rotate, @shift);
    $rotate[0] = Slic3r::Geometry::deg2rad($Slic3r::fill_angle);
    $rotate[1] = [ $self->max_print_dimension / 2, $self->max_print_dimension / 2 ];
    @shift = @{$rotate[1]};
    
    # alternate fill direction
    if ($self->layer->id % 2) {
        $rotate[0] = Slic3r::Geometry::deg2rad($Slic3r::fill_angle) + PI/2;
    }
    
    # use bridge angle
    if ($surface->isa('Slic3r::Surface::Bridge')) {
        Slic3r::debugf "Filling bridge with angle %d\n", $surface->bridge_angle;
        $rotate[0] = Slic3r::Geometry::deg2rad($surface->bridge_angle);
    }
    
    @shift = @{ +(Slic3r::Geometry::rotate_points(@rotate, \@shift))[0] };
    return [\@rotate, \@shift];
}

sub rotate_points {
    my $self = shift;
    my ($polygons, $rotate_vector) = @_;
    my @rotate = @{$rotate_vector->[0]};
    my @shift  = @{$rotate_vector->[1]};
    
    # rotate surface as needed
    @$polygons = map [ Slic3r::Geometry::move_points(\@shift, @$_) ],
        map [ Slic3r::Geometry::rotate_points(@rotate, @$_) ], @$polygons if $rotate[0];
    
}

sub rotate_points_back {
    my $self = shift;
    my ($paths, $rotate_vector) = @_;
    my @rotate = @{$rotate_vector->[0]};
    my @shift  = @{$rotate_vector->[1]};
    
    if ($rotate[0]) {
        @$paths = map [ Slic3r::Geometry::rotate_points(-$rotate[0], $rotate[1], @$_) ], 
            map [ Slic3r::Geometry::move_points([map -$_, @shift], @$_) ], @$paths;
    }
}

1;
