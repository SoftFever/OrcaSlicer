package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(polygon_remove_parallel_continuous_edges
    polygon_remove_acute_vertices polygon_segment_having_point
    PI X1 X2 Y1 Y2 epsilon);

sub wkt {
    my $self = shift;
    return sprintf "POLYGON((%s))", join ',', map "$_->[0] $_->[1]", @$self;
}

sub merge_continuous_lines {
    my $self = shift;
    
    my $p = $self->pp;
    polygon_remove_parallel_continuous_edges($p);
    return __PACKAGE__->new(@$p);
}

sub remove_acute_vertices {
    my $self = shift;
    polygon_remove_acute_vertices($self);
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    return Boost::Geometry::Utils::point_covered_by_polygon($point->pp, [$self->pp]);
}

sub grow {
    my $self = shift;
    return $self->split_at_first_point->grow(@_);
}

# NOTE that this will turn the polygon to ccw regardless of its 
# original orientation
sub simplify {
    my $self = shift;
    return @{Slic3r::Geometry::Clipper::simplify_polygons([ $self->SUPER::simplify(@_) ])};
}

# this method subdivides the polygon segments to that no one of them
# is longer than the length provided
sub subdivide {
    my $self = shift;
    my ($max_length) = @_;
    
    my @points = @$self;
    push @points, $points[0];  # append first point as this is a polygon
    my @new_points = shift @points;
    while (@points) {
        while ($new_points[-1]->distance_to($points[0]) > $max_length) {
            push @new_points, map Slic3r::Point->new(@$_),
                Slic3r::Geometry::point_along_segment($new_points[-1], $points[0], $max_length);
        }
        push @new_points, shift @points;
    }
    pop @new_points;  # remove last point as it coincides with first one
    return Slic3r::Polygon->new(@new_points);
}

# for cw polygons this will return convex points!
sub concave_points {
    my $self = shift;
    
    my @points = @$self;
    my @points_pp = @{$self->pp};
    return map $points[$_],
        grep Slic3r::Geometry::angle3points(@points_pp[$_, $_-1, $_+1]) < PI - epsilon,
        -1 .. ($#points-1);
}

1;