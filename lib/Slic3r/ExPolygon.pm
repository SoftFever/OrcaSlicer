package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use Boost::Geometry::Utils;
use Math::Geometry::Voronoi;
use Slic3r::Geometry qw(X Y A B point_in_polygon same_line line_length epsilon);
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

sub boost_polygon {
    my $self = shift;
    return Boost::Geometry::Utils::polygon(@$self);
}

sub wkt {
    my $self = shift;
    return sprintf "POLYGON(%s)", 
        join ',', map "($_)", map { join ',', map "$_->[0] $_->[1]", @$_ } @$self;
}

sub offset {
    my $self = shift;
    return Slic3r::Geometry::Clipper::offset($self, @_);
}

sub offset_ex {
    my $self = shift;
    return Slic3r::Geometry::Clipper::offset_ex($self, @_);
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

sub noncollapsing_offset_ex {
    my $self = shift;
    my ($distance, @params) = @_;
    
    return $self->offset_ex($distance + 1, @params);
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    return $self->contour->encloses_point($point)
        && (!grep($_->encloses_point($point), $self->holes)
            || grep($_->point_on_segment($point), $self->holes));
}

# A version of encloses_point for use when hole borders do not matter.
# Useful because point_on_segment is slow
sub encloses_point_quick {
    my $self = shift;
    my ($point) = @_;
    return $self->contour->encloses_point($point)
        && !grep($_->encloses_point($point), $self->holes);
}

sub encloses_line {
    my $self = shift;
    my ($line, $tolerance) = @_;
    my $clip = $self->clip_line($line);
    if (!defined $tolerance) {
        # optimization
        return @$clip == 1 && same_line($clip->[0], $line);
    } else {
        return @$clip == 1 && abs(line_length($clip->[0]) - $line->length) < $tolerance;
    }
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

sub bounding_box_polygon {
    my $self = shift;
    my @bb = $self->bounding_box;
    return Slic3r::Polygon->new([
        [ $bb[0], $bb[1] ],
        [ $bb[2], $bb[1] ],
        [ $bb[2], $bb[3] ],
        [ $bb[0], $bb[3] ],
    ]);
}

sub bounding_box_center {
    my $self = shift;
    return Slic3r::Geometry::bounding_box_center($self->contour);
}

sub clip_line {
    my $self = shift;
    my ($line) = @_;  # line must be a Slic3r::Line object
    
    return Boost::Geometry::Utils::polygon_multi_linestring_intersection($self, [$line]);
}

sub simplify {
    my $self = shift;
    $_->simplify(@_) for @$self;
    $self;
}

sub scale {
    my $self = shift;
    $_->scale(@_) for @$self;
}

sub translate {
    my $self = shift;
    $_->translate(@_) for @$self;
    $self;
}

sub rotate {
    my $self = shift;
    $_->rotate(@_) for @$self;
    $self;
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
        
        next if !$self->encloses_point_quick($a) || !$self->encloses_point_quick($b);
        
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
    return () if !@skeleton_lines;
    
    # now walk along the medial axis and build continuos polylines or polygons
    my @polylines = ();
    {
        # build a map of line endpoints
        my %pointmap = ();  # point_idx => [line_idx, line_idx ...]
        for my $line_idx (0 .. $#skeleton_lines) {
            for my $point_idx (@{$skeleton_lines[$line_idx]}) {
                $pointmap{$point_idx} ||= [];
                push @{$pointmap{$point_idx}}, $line_idx;
            }
        }
        
        # build the list of available lines
        my %spare_lines = map {$_ => 1} (0 .. $#skeleton_lines);
        
        CYCLE: while (%spare_lines) {
            push @polylines, [];
            my $polyline = $polylines[-1];
            
            # start from a random line
            my $first_line_idx = +(keys %spare_lines)[0];
            delete $spare_lines{$first_line_idx};
            push @$polyline, @{ $skeleton_lines[$first_line_idx] };
            
            while (1) {
                my $last_point_id = $polyline->[-1];
                my $lines_starting_here = $pointmap{$last_point_id};
                
                # remove all the visited lines from the array
                shift @$lines_starting_here
                    while @$lines_starting_here && !$spare_lines{$lines_starting_here->[0]};
                
                # do we have a line starting here?
                my $next_line_idx = shift @$lines_starting_here;
                if (!defined $next_line_idx) {
                    delete $pointmap{$last_point_id};
                    next CYCLE;
                }
                
                # line is not available anymore
                delete $spare_lines{$next_line_idx};
                
                # add the other point to our polyline and continue walking
                push @$polyline, grep $_ ne $last_point_id, @{$skeleton_lines[$next_line_idx]};
            }
        }
    }
    
    my @result = ();
    foreach my $polyline (@polylines) {
        next unless @$polyline >= 2;
        
        # now replace point indexes with coordinates
        @$polyline = map $vertices->[$_], @$polyline;
        
        # cleanup
        $polyline = Slic3r::Geometry::douglas_peucker($polyline, $width / 7);
        
        if (Slic3r::Geometry::same_point($polyline->[0], $polyline->[-1])) {
            next if @$polyline == 2;
            push @result, Slic3r::Polygon->new(@$polyline[0..$#$polyline-1]);
        } else {
            push @result, Slic3r::Polyline->new($polyline);
        }
    }
    
    return @result;
}

package Slic3r::ExPolygon::Collection;
use Moo;
use Slic3r::Geometry qw(X1 Y1);

has 'expolygons' => (is => 'ro', default => sub { [] });

sub clone {
    my $self = shift;
    return (ref $self)->new(
        expolygons => [ map $_->clone, @{$self->expolygons} ],
    );
}

sub align_to_origin {
    my $self = shift;
    
    my @bb = Slic3r::Geometry::bounding_box([ map @$_, map @$_, @{$self->expolygons} ]);
    $_->translate(-$bb[X1], -$bb[Y1]) for @{$self->expolygons};
}

sub rotate {
    my $self = shift;
    $_->rotate(@_) for @{$self->expolygons};
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D([ map @$_, map @$_, @{$self->expolygons} ]) ];
}

1;
