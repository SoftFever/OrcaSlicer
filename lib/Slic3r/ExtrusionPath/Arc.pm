package Slic3r::ExtrusionPath::Arc;
use Moo;

extends 'Slic3r::ExtrusionPath';

has 'center' => (is => 'ro', required => 1);
has 'radius' => (is => 'ro', required => 1);
has 'orientation' => (is => 'ro', required => 1);  # cw/ccw

use Slic3r::Geometry qw(PI angle3points);

sub angle {
    my $self = shift;
    return angle3points($self->center, @{$self->points});
}

sub length {
    my $self = shift;
    
    return $self->radius * $self->angle;
}

1;
