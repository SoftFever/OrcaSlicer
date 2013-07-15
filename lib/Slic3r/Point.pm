package Slic3r::Point;
use strict;
use warnings;

sub distance_to {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::distance_between_points($self, $point);
}

1;
