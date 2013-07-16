package Slic3r::ExtrusionPath::Arc;
use Moo;

has 'polyline'      => (is => 'rw', required => 1);
has 'role'          => (is => 'rw', required => 1);
has 'height'        => (is => 'rw');
has 'flow_spacing'  => (is => 'rw');
has 'center' => (is => 'ro', required => 1);
has 'radius' => (is => 'ro', required => 1);
has 'orientation' => (is => 'ro', required => 1);  # cw/ccw

use Slic3r::Geometry qw(PI angle3points);

sub points {
    my $self = shift;
    return $self->polyline;
}

sub angle {
    my $self = shift;
    return angle3points($self->center, @{$self->points});
}

sub length {
    my $self = shift;
    
    if($self->orientation eq 'ccw') {
        return $self->radius * $self->angle;
    } else {
        return $self->radius * (2*PI() - $self->angle);
    }
}

1;
