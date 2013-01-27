package Slic3r::Polyline;
use strict;
use warnings;

use Scalar::Util qw(reftype);
use Slic3r::Geometry qw(A B X Y X1 X2 Y1 Y2 polyline_remove_parallel_continuous_edges polyline_remove_acute_vertices
    polyline_lines move_points same_point);

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

sub clone {
    my $self = shift;
    return (ref $self)->new(map $_->clone, @$self);
}

sub serialize {
    my $self = shift;
    return pack 'l*', map @$_, @$self;
}

sub deserialize {
    my $class = shift;
    my ($s) = @_;
    
    my @v = unpack '(l2)*', $s;
    return $class->new(map [ $v[2*$_], $v[2*$_+1] ], 0 .. int($#v/2));
}

sub lines {
    my $self = shift;
    return polyline_lines($self);
}

sub boost_linestring {
    my $self = shift;
    return Boost::Geometry::Utils::linestring($self);
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
    
    @$self = @{ Boost::Geometry::Utils::linestring_simplify($self, $tolerance) };
    bless $_, 'Slic3r::Point' for @$self;
}

sub reverse {
    my $self = shift;
    @$self = CORE::reverse @$self;
}

sub length {
    my $self = shift;
    my $length = 0;
    $length += $_->length for $self->lines;
    return $length;
}

# this only applies to polylines
sub grow {
    my $self = shift;
    return Slic3r::Polygon->new(@$self, CORE::reverse @$self[1..($#$self-1)])->offset(@_);
}

sub nearest_point_to {
    my $self = shift;
    my ($point) = @_;
    
    $point = Slic3r::Geometry::nearest_point($point, $self);
    return Slic3r::Point->new($point);
}

sub nearest_point_index_to {
    my $self = shift;
    my ($point) = @_;
    return Slic3r::Geometry::nearest_point_index($point, $self);
}

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    my $result = Boost::Geometry::Utils::polygon_multi_linestring_intersection($expolygon, [$self]);
    bless $_, 'Slic3r::Polyline' for @$result;
    bless $_, 'Slic3r::Point' for map @$_, @$result;
    return @$result;
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::bounding_box($self);
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D($self) ];
}

sub align_to_origin {
    my $self = shift;
    my @bb = $self->bounding_box;
    return $self->translate(-$bb[X1], -$bb[Y1]);
}

sub rotate {
    my $self = shift;
    my ($angle, $center) = @_;
    @$self = Slic3r::Geometry::rotate_points($angle, $center, @$self);
    bless $_, 'Slic3r::Point' for @$self;
    return $self;
}

sub translate {
    my $self = shift;
    my ($x, $y) = @_;
    @$self = Slic3r::Geometry::move_points([$x, $y], @$self);
    bless $_, 'Slic3r::Point' for @$self;
    return $self;
}

sub scale {
    my $self = shift;
    my ($factor) = @_;
    return if $factor == 1;
    
    # transform point coordinates
    foreach my $point (@$self) {
        $point->[$_] *= $factor for X,Y;
    }
    return $self;
}

sub clip_end {
    my $self = shift;
    my ($distance) = @_;
    
    while ($distance > 0) {
        my $last_point = pop @$self;
        last if !@$self;
        
        my $last_segment_length = $last_point->distance_to($self->[-1]);
        if ($last_segment_length <= $distance) {
            $distance -= $last_segment_length;
            next;
        }
        
        my $new_point = Slic3r::Geometry::point_along_segment($last_point, $self->[-1], $distance);
        push @$self, Slic3r::Point->new($new_point);
        $distance = 0;
    }
}

package Slic3r::Polyline::Collection;
use Moo;

has 'polylines' => (is => 'ro', default => sub { [] });

# If the second argument is provided, this method will return its items sorted
# instead of returning the actual sorted polylines. 
# Note that our polylines will be reversed in place when necessary.
sub shortest_path {
    my $self = shift;
    my ($start_near, $items) = @_;
    
    $items ||= $self->polylines;
    my %items_map = map { $self->polylines->[$_] => $items->[$_] } 0 .. $#{$self->polylines};
    my @my_paths = @{$self->polylines};
    
    my @paths = ();
    my $start_at;
    my $endpoints = [ map { $_->[0], $_->[-1] } @my_paths ];
    while (@my_paths) {
        # find nearest point
        my $start_index = $start_near
            ? Slic3r::Geometry::nearest_point_index($start_near, $endpoints)
            : 0;

        my $path_index = int($start_index/2);
        if ($start_index%2) { # index is end so reverse to make it the start
            $my_paths[$path_index]->reverse;
        }
        push @paths, splice @my_paths, $path_index, 1;
        splice @$endpoints, $path_index*2, 2;
        $start_near = $paths[-1][-1];
    }
    return map $items_map{"$_"}, @paths;
}

1;
