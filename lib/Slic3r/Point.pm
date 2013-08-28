package Slic3r::Point;
use strict;
use warnings;

sub new_scale {
    my $class = shift;
    return $class->new(map Slic3r::Geometry::scale($_), @_);
}

1;
