package Slic3r::Polygon;
use strict;
use warnings;

# a polygon is a closed polyline.
use parent 'Slic3r::Polyline';

use Slic3r::Geometry qw(polygon_lines polygon_remove_parallel_continuous_edges
    scale polygon_remove_acute_vertices polygon_segment_having_point point_in_polygon);
use Slic3r::Geometry::Clipper qw(JT_MITER);

sub lines {
    my $self = shift;
    return polygon_lines($self);
}

sub boost_linestring {
    my $self = shift;
    return Boost::Geometry::Utils::linestring([@$self, $self->[0]]);
}

sub is_counter_clockwise {
    my $self = shift;
    return Math::Clipper::is_counter_clockwise($self);
}

sub make_counter_clockwise {
    my $self = shift;
    $self->reverse if !$self->is_counter_clockwise;
}

sub make_clockwise {
    my $self = shift;
    $self->reverse if $self->is_counter_clockwise;
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
    return map Slic3r::Polygon->new($_), Slic3r::Geometry::Clipper::offset([$self], @_);
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
        
        # $num_points is the number of points to add between $i-1 and $i
        next if $num_points == -1;
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
    my ($flow_width) = @_;
    
    # try to get an inwards offset
    # for a distance equal to half of the extrusion width;
    # if no offset is possible, then polyline is not printable.
    # we use flow_width here because this has to be consistent 
    # with the thin wall detection in Layer->make_surfaces, 
    # otherwise we could lose surfaces as that logic wouldn't
    # detect them and we would be discarding them.
    my $p = $self->clone;
    $p->make_counter_clockwise;
    return $p->offset(scale($flow_width || $Slic3r::flow->width) / 2) ? 1 : 0;
}

sub is_valid {
    my $self = shift;
    return @$self >= 3;
}

1;