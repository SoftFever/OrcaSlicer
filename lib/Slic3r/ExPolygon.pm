package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use Slic3r::Geometry qw(point_in_polygon X Y A B);
use Slic3r::Geometry::Clipper qw(union_ex JT_MITER);

# the constructor accepts an array of polygons 
# or a Math::Clipper ExPolygon (hashref)
sub new {
    my $class = shift;
    my $self;
    if (@_ == 1 && ref $_[0] eq 'HASH') {
        $self = [
            Slic3r::Polygon->new($_[0]{outer}),
            map Slic3r::Polygon->new($_), @{$_[0]{holes}},
        ];
    } else {
        $self = [ map Slic3r::Polygon->new($_), @_ ];
    }
    bless $self, $class;
    $self;
}

sub contour {
    my $self = shift;
    return $self->[0];
}

sub holes {
    my $self = shift;
    return @$self[1..$#$self];
}

sub clipper_expolygon {
    my $self = shift;
    return {
        outer => $self->contour,
        holes => [ $self->holes ],
    };
}

sub offset {
    my $self = shift;
    my ($distance, $scale, $joinType, $miterLimit) = @_;
    $scale      ||= $Slic3r::resolution * 1000000;
    $joinType   = JT_MITER if !defined $joinType;
    $miterLimit ||= 2;
    
    my $offsets = Math::Clipper::offset($self, $distance, $scale, $joinType, $miterLimit);
    return @$offsets;
}

sub safety_offset {
    my $self = shift;
    
    # we're offsetting contour and holes separately
    # because Clipper doesn't return polygons in the same order as 
    # we feed them to it
    
    return (ref $self)->new(
        $self->contour->safety_offset,
        @{ Slic3r::Geometry::Clipper::safety_offset([$self->holes]) },
    );
}

sub offset_ex {
    my $self = shift;
    my @offsets = $self->offset(@_);
    
    # apply holes to the right contours
    return @{ union_ex(\@offsets) };
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    return $self->contour->encloses_point($point)
        && (!grep($_->encloses_point($point), $self->holes)
            || grep($_->point_on_segment($point), $self->holes));
}

sub point_on_segment {
    my $self = shift;
    my ($point) = @_;
    for (@$self) {
        my $line = $_->point_on_segment($point);
        return $line if $line;
    }
    return undef;
}

sub bounding_box {
    my $self = shift;
    return Slic3r::Geometry::bounding_box($self->contour);
}

sub clip_line {
    my $self = shift;
    my ($line) = @_;
    $line = Slic3r::Line->cast($line);
    
    my @intersections = grep $_, map $_->intersection($line, 1), map $_->lines, @$self;
    my @dir = (
        $line->[B][X] <=> $line->[A][X],
        $line->[B][Y] <=> $line->[A][Y],
    );
    
    @intersections = sort {
        (($a->[X] <=> $b->[X]) == $dir[X]) && (($a->[Y] <=> $b->[Y]) == $dir[Y]) ? 1 : -1
    } @intersections, @$line;
    
    shift @intersections if $intersections[0]->coincides_with($intersections[1]);
    pop @intersections if $intersections[-1]->coincides_with($intersections[-2]);
    
    shift @intersections
        if !$self->encloses_point($intersections[0])
        && !$self->point_on_segment($intersections[0]);
    
    my @lines = ();
    while (@intersections) {
        # skip tangent points
        my @points = splice @intersections, 0, 2;
        next if !$points[1];
        next if $points[0]->coincides_with($points[1]);
        push @lines, [ @points ];
    }
    return [@lines];
}

sub translate {
    my $self = shift;
    $_->translate(@_) for @$self;
}

sub rotate {
    my $self = shift;
    $_->rotate(@_) for @$self;
}

sub area {
    my $self = shift;
    my $area = $self->contour->area;
    $area -= $_->area for $self->holes;
    return $area;
}

1;
