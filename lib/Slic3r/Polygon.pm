package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
# if you're asking why there's a Slic3r::Polygon as well
# as a Slic3r::Polyline::Closed you're right. I plan to
# ditch the latter and port everything to this class.

use Slic3r::Geometry qw(polygon_remove_parallel_continuous_edges);

# the constructor accepts an array(ref) of points
sub new {
    my $class = shift;
    my $self;
    if (@_ == 1) {
        $self = [ @{$_[0]} ];
    } else {
        $self = [ @_ ];
    }
    bless $self, $class;
    $self;
}

sub cleanup {
    my $self = shift;
    polygon_remove_parallel_continuous_edges($self);
}

1;