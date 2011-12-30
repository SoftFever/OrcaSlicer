package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use Math::Geometry::Voronoi;
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

sub clone {
    my $self = shift;
    return (ref $self)->new(map $_->clone, @$self);
}

sub contour {
    my $self = shift;
    return $self->[0];
}

sub holes {
    my $self = shift;
    return @$self[1..$#$self];
}

sub lines {
    my $self = shift;
    return map $_->lines, @$self;
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
    $line = Slic3r::Line->new($line) if ref $line eq 'ARRAY';
    
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

# this method only works for expolygons having only a contour or
# a contour and a hole, and not being thicker than the supplied 
# width. it returns a polyline or a polygon
sub medial_axis {
    my $self = shift;
    my ($width) = @_;
    
    my @self_lines = map $_->lines, @$self;
    my $expolygon = $self->clone;
    my @points = ();
    foreach my $polygon (@$expolygon) {
        Slic3r::Geometry::polyline_remove_short_segments($polygon, $width / 2);
        
        # subdivide polygon segments so that we don't have anyone of them
        # being longer than $width / 2
        $polygon->subdivide($width/2);
        
        push @points, @$polygon;
    }
    
    my $voronoi = Math::Geometry::Voronoi->new(points => \@points);
    $voronoi->compute;
    
    my @skeleton_lines = ();
    
    my $vertices = $voronoi->vertices;
    my $edges = $voronoi->edges;
    foreach my $edge (@$edges) {
        # ignore lines going to infinite
        next if $edge->[1] == -1 || $edge->[2] == -1;
        
        my ($a, $b);
        $a = $vertices->[$edge->[1]];
        $b = $vertices->[$edge->[2]];
        
        next if !$self->encloses_point($a) || !$self->encloses_point($b);
        
        push @skeleton_lines, [$edge->[1], $edge->[2]];
    }
    
    # remove leafs (lines not connected to other lines at one of their endpoints)
    {
        my %pointmap = ();
        $pointmap{$_}++ for map @$_, @skeleton_lines;
        @skeleton_lines = grep {
            $pointmap{$_->[A]} >= 2 && $pointmap{$_->[B]} >= 2
        } @skeleton_lines;
    }
    return undef if !@skeleton_lines;
    
    return undef if !@skeleton_lines;
    
    # now build a single polyline
    my $polyline = [];
    {
        my %pointmap = ();
        foreach my $line (@skeleton_lines) {
            foreach my $point_id (@$line) {
                $pointmap{$point_id} ||= [];
                push @{$pointmap{$point_id}}, $line;
            }
        }
        
        # start from a point having only one line
        foreach my $point_id (keys %pointmap) {
            if (@{$pointmap{$point_id}} == 1) {
                push @$polyline, grep $_ ne $point_id, map @$_, shift @{$pointmap{$point_id}};
                last;
            }
        }
        
        # if no such point is found, pick a random one
        push @$polyline, shift @{ +(values %pointmap)[0][0] } if !@$polyline;
        
        my %visited_lines = ();
        while (1) {
            my $last_point_id = $polyline->[-1];
            
            shift @{ $pointmap{$last_point_id} }
                while @{ $pointmap{$last_point_id} } && $visited_lines{$pointmap{$last_point_id}[0]};
            my $next_line = shift @{ $pointmap{$last_point_id} } or last;
            $visited_lines{$next_line} = 1;
            push @$polyline, grep $_ ne $last_point_id, @$next_line;
        }
    }
    
    # now replace point indexes with coordinates
    @$polyline = map $vertices->[$_], @$polyline;
    
    # cleanup
    Slic3r::Geometry::polyline_remove_short_segments($polyline, $width / 2);
    @$polyline = Slic3r::Geometry::Douglas_Peucker($polyline, $width / 100);
    Slic3r::Geometry::polyline_remove_parallel_continuous_edges($polyline);
    
    if (Slic3r::Geometry::same_point($polyline->[0], $polyline->[-1])) {
        return Slic3r::Polygon->new(@$polyline[0..$#$polyline-1]);
    } else {
        return Slic3r::Polyline->new($polyline);
    }
}

1;
