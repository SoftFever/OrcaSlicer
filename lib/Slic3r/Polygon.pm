package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(PI);

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

1;