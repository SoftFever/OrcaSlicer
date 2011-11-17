package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
# if you're asking why there's a Slic3r::Polygon as well
# as a Slic3r::Polyline::Closed you're right. I plan to
# ditch the latter and port everything to this class.

use Slic3r::Geometry qw(polygon_lines polygon_remove_parallel_continuous_edges
    polygon_segment_having_point point_in_polygon move_points rotate_points);

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
    bless $_, 'Slic3r::Point' for @$self;
    $self;
}

# legacy method, to be removed when we ditch Slic3r::Polyline::Closed
sub closed_polyline {
    my $self = shift;
    return Slic3r::Polyline::Closed->cast($self);
}

sub lines {
    my $self = shift;
    return map Slic3r::Line->new($_), polygon_lines($self);
}

sub cleanup {
    my $self = shift;
    polygon_remove_parallel_continuous_edges($self);
}

sub point_on_segment {
    my $self = shift;
    my ($point) = @_;
    return polygon_segment_having_point($self, $point);
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    return point_in_polygon($point, $self);
}

sub translate {
    my $self = shift;
    my ($x, $y) = @_;
    @$self = move_points([$x, $y], @$self);
}

sub rotate {
    my $self = shift;
    my ($angle, $center) = @_;
    @$self = rotate_points($angle, $center, @$self);
}

sub area {
    my $self = shift;
    return Slic3r::Geometry::Clipper::area($self);
}

sub safety_offset {
    my $self = shift;
    return (ref $self)->new(Slic3r::Geometry::Clipper::safety_offset([$self])->[0]);
}

1;