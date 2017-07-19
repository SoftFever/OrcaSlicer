package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

sub grow {
    my $self = shift;
    return $self->split_at_first_point->grow(@_);
}

1;