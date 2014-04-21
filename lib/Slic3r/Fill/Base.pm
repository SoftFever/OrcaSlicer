package Slic3r::Fill::Base;
use Moo;

use Slic3r::Geometry qw(PI rad2deg);

has 'layer_id'            => (is => 'rw');
has 'angle'               => (is => 'rw'); # in radians, ccw, 0 = East
has 'bounding_box'        => (is => 'ro', required => 0);  # Slic3r::Geometry::BoundingBox object

sub angles () { [0, PI/2] }

sub infill_direction {
    my $self = shift;
    my ($surface) = @_;
    
    if (!defined $self->angle) {
        warn "Using undefined infill angle";
        $self->angle(0);
    }
    
    # set infill angle
    my (@rotate);
    $rotate[0] = $self->angle;
    $rotate[1] = $self->bounding_box
        ? $self->bounding_box->center
        : $surface->expolygon->bounding_box->center;
    my $shift = $rotate[1]->clone;
    
    if (defined $self->layer_id) {
        # alternate fill direction
        my $layer_num = $self->layer_id / $surface->thickness_layers;
        my $angle = $self->angles->[$layer_num % @{$self->angles}];
        $rotate[0] = $self->angle + $angle if $angle;
    }
        
    # use bridge angle
    if ($surface->bridge_angle >= 0) {
        Slic3r::debugf "Filling bridge with angle %d\n", rad2deg($surface->bridge_angle);
        $rotate[0] = $surface->bridge_angle;
    }
    
    $rotate[0] += PI/2;
    $shift->rotate(@rotate);
    return [\@rotate, $shift];
}

# this method accepts any object that implements rotate() and translate()
sub rotate_points {
    my $self = shift;
    my ($expolygon, $rotate_vector) = @_;
    
    # rotate points
    my ($rotate, $shift) = @$rotate_vector;
    $rotate = [ -$rotate->[0], $rotate->[1] ];
    $expolygon->rotate(@$rotate);
    $expolygon->translate(@$shift);
}

sub rotate_points_back {
    my $self = shift;
    my ($paths, $rotate_vector) = @_;
    
    my ($rotate, $shift) = @$rotate_vector;
    $shift = [ map -$_, @$shift ];
    
    $_->translate(@$shift) for @$paths;
    $_->rotate(@$rotate) for @$paths;
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
