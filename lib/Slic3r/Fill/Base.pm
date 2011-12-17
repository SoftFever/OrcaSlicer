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
    $rotate[1] = [ $self->max_print_dimension * sqrt(2) / 2, $self->max_print_dimension * sqrt(2) / 2 ];
    @shift = @{$rotate[1]};
    
    # alternate fill direction
    if (($self->layer->id / $surface->depth_layers) % 2) {
        $rotate[0] = Slic3r::Geometry::deg2rad($Slic3r::fill_angle) + PI/2;
    }
    
    # use bridge angle
    if ($surface->surface_type eq 'bottom' && $self->layer->id > 0 && defined $surface->bridge_angle) {
        Slic3r::debugf "Filling bridge with angle %d\n", $surface->bridge_angle;
        $rotate[0] = Slic3r::Geometry::deg2rad($surface->bridge_angle);
    }
    
    @shift = @{ +(Slic3r::Geometry::rotate_points(@rotate, \@shift))[0] };
    return [\@rotate, \@shift];
}

sub rotate_points {
    my $self = shift;
    my ($expolygon, $rotate_vector) = @_;
    my @rotate = @{$rotate_vector->[0]};
    my @shift  = @{$rotate_vector->[1]};
    
    # rotate points as needed
    if ($rotate[0]) {
        $expolygon->rotate(@rotate);
        $expolygon->translate(@shift);
    }
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

sub adjust_solid_spacing {
    my $self = shift;
    my %params = @_;
    
    my $number_of_lines = int($params{width} / $params{distance}) + 1;
    return $params{distance} if $number_of_lines <= 1;
    
    my $extra_space = $params{width} % $params{distance};
    return $params{distance} + $extra_space / ($number_of_lines - 1);
}

1;
