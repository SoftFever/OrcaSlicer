package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(
    polygon_segment_having_point
    PI X1 X2 Y1 Y2 epsilon scaled_epsilon);
use Slic3r::Geometry::Clipper qw(intersection_pl);

sub wkt {
    my $self = shift;
    return sprintf "POLYGON((%s))", join ',', map "$_->[0] $_->[1]", @$self;
}

sub dump_perl {
    my $self = shift;
    return sprintf "[%s]", join ',', map "[$_->[0],$_->[1]]", @$self;
}

sub grow {
    my $self = shift;
    return $self->split_at_first_point->grow(@_);
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

sub concave_points {
    my ($self, $angle) = @_;
    
    $angle //= PI;
    
    # input angle threshold is checked on the internal side of the polygon
    # but angle3points measures CCW angle, so we calculate the complementary angle
    my $ccw_angle = 2*PI-$angle;
    
    my @concave = ();
    my @points = @$self;
    my @points_pp = @{$self->pp};

    for my $i (-1 .. ($#points-1)) {
        # angle is measured in ccw orientation
        my $vertex_angle = Slic3r::Geometry::angle3points(@points_pp[$i, $i-1, $i+1]);
        if ($vertex_angle <= $ccw_angle) {
            push @concave, $points[$i];
        }
    }
    
    return [@concave];
}

sub convex_points {
    my ($self, $angle) = @_;
    
    $angle //= PI;
    
    # input angle threshold is checked on the internal side of the polygon
    # but angle3points measures CCW angle, so we calculate the complementary angle
    my $ccw_angle = 2*PI-$angle;
    
    my @convex = ();
    my @points = @$self;
    my @points_pp = @{$self->pp};
    
    for my $i (-1 .. ($#points-1)) {
        # angle is measured in ccw orientation
        my $vertex_angle = Slic3r::Geometry::angle3points(@points_pp[$i, $i-1, $i+1]);
        if ($vertex_angle >= $ccw_angle) {
            push @convex, $points[$i];
        }
    }
    return [@convex];
}

1;