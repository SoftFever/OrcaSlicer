package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(
    polygon_segment_having_point
    PI X1 X2 Y1 Y2 epsilon);
use Slic3r::Geometry::Clipper qw(intersection_pl);

sub wkt {
    my $self = shift;
    return sprintf "POLYGON((%s))", join ',', map "$_->[0] $_->[1]", @$self;
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

# for cw polygons this will return convex points!
sub concave_points {
    my $self = shift;
    
    my @points = @$self;
    my @points_pp = @{$self->pp};
    return map $points[$_],
        grep Slic3r::Geometry::angle3points(@points_pp[$_, $_-1, $_+1]) < PI - epsilon,
        -1 .. ($#points-1);
}

sub clip_as_polyline {
    my ($self, $polygons) = @_;
    
    my $self_pl = $self->split_at_first_point;
    
    # Clipper will remove a polyline segment if first point coincides with last one.
    # Until that bug is not fixed upstream, we move one of those points slightly.
    $self_pl->[0]->translate(1, 0);
    
    my @polylines = @{intersection_pl([$self_pl], $polygons)};
    if (@polylines == 1) {
        if ($polylines[0][0]->coincides_with($self_pl->[0])) {
            # compensate the above workaround for Clipper bug
            $polylines[0][0]->translate(-1, 0);
        }
    } elsif (@polylines == 2) {
        # If the split_at_first_point() call above happens to split the polygon inside the clipping area
        # we would get two consecutive polylines instead of a single one, so we use this ugly hack to 
        # recombine them back into a single one in order to trigger the @edges == 2 logic below.
        # This needs to be replaced with something way better.
        if ($polylines[0][-1]->coincides_with($self_pl->[-1]) && $polylines[-1][0]->coincides_with($self_pl->[0])) {
            my $p = $polylines[0]->clone;
            $p->pop_back;
            $p->append(@{$polylines[-1]});
            return [$p];
        }
        if ($polylines[0][0]->coincides_with($self_pl->[0]) && $polylines[-1][-1]->coincides_with($self_pl->[-1])) {
            my $p = $polylines[-1]->clone;
            $p->pop_back;
            $p->append(@{$polylines[0]});
            return [$p];
        }
    }
    
    return [ @polylines ];
}

1;