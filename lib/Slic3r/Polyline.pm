package Slic3r::Polyline;
use strict;
use warnings;

use Slic3r::Geometry qw(A B X Y X1 X2 Y1 Y2 polyline_remove_parallel_continuous_edges polyline_remove_acute_vertices);
use Slic3r::Geometry::Clipper qw(JT_SQUARE);
use Storable qw();

sub new_scale {
    my $class = shift;
    my @points = map { ref($_) eq 'Slic3r::Point' ? $_->pp : $_ } @_;
    return $class->new(map [ Slic3r::Geometry::scale($_->[X]), Slic3r::Geometry::scale($_->[Y]) ], @points);
}

sub wkt {
    my $self = shift;
    return sprintf "LINESTRING((%s))", join ',', map "$_->[0] $_->[1]", @$self;
}

sub merge_continuous_lines {
    my $self = shift;
    polyline_remove_parallel_continuous_edges($self);
}

sub remove_acute_vertices {
    my $self = shift;
    polyline_remove_acute_vertices($self);
}

sub simplify {
    my $self = shift;
    my $tolerance = shift || 10;
    
    my $simplified = Boost::Geometry::Utils::linestring_simplify($self->pp, $tolerance);
    return __PACKAGE__->new(@$simplified);
}

sub grow {
    my $self = shift;
    my ($distance, $scale, $joinType, $miterLimit) = @_;
    $joinType   //= JT_SQUARE;  # we override this one
    $scale      //= 100000;     # we init these because we can't pass undef
    $miterLimit //= 3;
    
    my @points = @$self;
    return @{Slic3r::Geometry::Clipper::offset(
        [ Slic3r::Polygon->new(@points, CORE::reverse @points[1..($#points-1)]) ],
        $distance, $scale, $joinType, $miterLimit,
    )};
}

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    my $result = Boost::Geometry::Utils::polygon_multi_linestring_intersection($expolygon->pp, [$self->pp]);
    return map { __PACKAGE__->new(@$_) } @$result;
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::BoundingBox->new_from_points($self);
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D($self) ];
}

sub align_to_origin {
    my $self = shift;
    my $bb = $self->bounding_box;
    return $self->translate(-$bb->x_min, -$bb->y_min);
}

# this method returns a collection of points picked on the polygon contour
# so that they are evenly spaced according to the input distance
# (find a better name!)
sub regular_points {
    my $self = shift;
    my ($distance) = @_;
    
    my @my_points = @$self;
    my @points = ($my_points[0]->clone);
    my $len = 0;
    
    for (my $i = 1; $i <= $#my_points; $i++) {
        my $point = $my_points[$i];
        my $segment_length = $point->distance_to($my_points[$i-1]);
        $len += $segment_length;
        next if $len < $distance;
        
        if ($len == $distance) {
            push @points, $point;
            $len = 0;
            next;
        }
        
        my $take = $segment_length - ($len - $distance);  # how much we take of this segment
        my $new_point = Slic3r::Geometry::point_along_segment($my_points[$i-1], $point, $take);
        push @points, Slic3r::Point->new(@$new_point);
        $i--;
        $len = -$take;
    }
    
    return @points;
}

1;
