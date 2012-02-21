package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(polygon_lines polygon_remove_parallel_continuous_edges
    scale polygon_remove_acute_vertices
    polygon_segment_having_point point_in_polygon move_points rotate_points);
use Slic3r::Geometry::Clipper qw(JT_MITER);

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

sub reverse {
    my $self = shift;
    @$self = CORE::reverse @$self;
}

sub lines {
    my $self = shift;
    return map Slic3r::Line->new($_), polygon_lines($self);
}

sub cleanup {
    my $self = shift;
    $self->merge_continuous_lines;
    return @$self >= 3;
}

sub merge_continuous_lines {
    my $self = shift;
    
    polygon_remove_parallel_continuous_edges($self);
    bless $_, 'Slic3r::Point' for @$self;
}

sub remove_acute_vertices {
    my $self = shift;
    polygon_remove_acute_vertices($self);
    bless $_, 'Slic3r::Point' for @$self;
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

sub offset {
    my $self = shift;
    my ($distance, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= $Slic3r::resolution * 1000000;
    $joinType   = JT_MITER if !defined $joinType;
    $miterLimit ||= 2;
    
    my $offsets = Math::Clipper::offset([$self], $distance, $scale, $joinType, $miterLimit);
    return @$offsets;
}

# this method subdivides the polygon segments to that no one of them
# is longer than the length provided
sub subdivide {
    my $self = shift;
    my ($max_length) = @_;
    
    for (my $i = 0; $i <= $#$self; $i++) {
        my $len = Slic3r::Geometry::line_length([ $self->[$i-1], $self->[$i] ]);
        my $num_points = int($len / $max_length) - 1;
        $num_points++ if $len % $max_length;
        next unless $num_points;
        
        # $num_points is the number of points to add between $i-1 and $i
        my $spacing = $len / ($num_points + 1);
        my @new_points = map Slic3r::Point->new($_),
            map Slic3r::Geometry::point_along_segment($self->[$i-1], $self->[$i], $spacing * $_),
            1..$num_points;
        
        splice @$self, $i, 0, @new_points;
        $i += @new_points;
    }
}

# returns false if the polyline is too tight to be printed
sub is_printable {
    my $self = shift;
    
    # try to get an inwards offset
    # for a distance equal to half of the extrusion width;
    # if no offset is possible, then polyline is not printable
    my $p = $self->clone;
    @$p = CORE::reverse @$p if !Math::Clipper::is_counter_clockwise($p);
    my $offsets = Math::Clipper::offset([$p], -(scale $Slic3r::flow_spacing / 2), $Slic3r::resolution * 100000, JT_MITER, 2);
    return @$offsets ? 1 : 0;
}

sub is_valid {
    my $self = shift;
    return @$self >= 3;
}

1;