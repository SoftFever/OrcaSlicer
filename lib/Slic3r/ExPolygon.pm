package Slic3r::ExPolygon;
use strict;
use warnings;

# an ExPolygon is a polygon with holes

use Boost::Geometry::Utils;
use List::Util qw(first);
use Math::Geometry::Voronoi;
use Slic3r::Geometry qw(X Y A B point_in_polygon same_line epsilon);
use Slic3r::Geometry::Clipper qw(union_ex);

sub wkt {
    my $self = shift;
    return sprintf "POLYGON(%s)", 
        join ',', map "($_)", map { join ',', map "$_->[0] $_->[1]", @$_ } @$self;
}

sub dump_perl {
    my $self = shift;
    return sprintf "[%s]", 
        join ',', map "[$_]", map { join ',', map "[$_->[0],$_->[1]]", @$_ } @$self;
}

sub offset {
    my $self = shift;
    return Slic3r::Geometry::Clipper::offset(\@$self, @_);
}

sub offset_ex {
    my $self = shift;
    return Slic3r::Geometry::Clipper::offset_ex(\@$self, @_);
}

sub noncollapsing_offset_ex {
    my $self = shift;
    my ($distance, @params) = @_;
    
    return $self->offset_ex($distance + 1, @params);
}

sub encloses_point {
    my $self = shift;
    my ($point) = @_;
    return Boost::Geometry::Utils::point_covered_by_polygon($point->pp, $self->pp);
}

# A version of encloses_point for use when hole borders do not matter.
# Useful because point_on_segment is probably slower (this was true
# before the switch to Boost.Geometry, not sure about now)
sub encloses_point_quick {
    my $self = shift;
    my ($point) = @_;
    return Boost::Geometry::Utils::point_within_polygon($point->pp, $self->pp);
}

sub encloses_line {
    my $self = shift;
    my ($line, $tolerance) = @_;
    my $clip = $self->clip_line($line);
    if (!defined $tolerance) {
        # optimization
        return @$clip == 1 && same_line($clip->[0]->pp, $line->pp);
    } else {
        return @$clip == 1 && abs(Boost::Geometry::Utils::linestring_length($clip->[0]->pp) - $line->length) < $tolerance;
    }
}

sub bounding_box {
    my $self = shift;
    return $self->contour->bounding_box;
}

sub clip_line {
    my $self = shift;
    my ($line) = @_;  # line must be a Slic3r::Line object
    
    return [
        map Slic3r::Line->new(@$_),
            @{Boost::Geometry::Utils::polygon_multi_linestring_intersection($self->pp, [$line->pp])}
    ];
}

sub simplify_as_polygons {
    my $self = shift;
    my ($tolerance) = @_;
    
    # it would be nice to have a multilinestring_simplify method in B::G::U
    return @{Slic3r::Geometry::Clipper::simplify_polygons(
        [ map Boost::Geometry::Utils::linestring_simplify($_, $tolerance), @{$self->pp} ],
    )};
}

sub simplify {
    my $self = shift;
    my ($tolerance) = @_;
    
    return @{ Slic3r::Geometry::Clipper::union_ex([ $self->simplify_as_polygons($tolerance) ]) };
}

# this method only works for expolygons having only a contour or
# a contour and a hole, and not being thicker than the supplied 
# width. it returns a polyline or a polygon
sub medial_axis {
    my ($self, $width) = @_;
    return $self->_medial_axis_clip($width);
}

sub _medial_axis_clip {
    my ($self, $width) = @_;
    
    my $grow = sub {
        my ($line, $distance) = @_;
        my ($a, $b) = @$line;
        my $dx = $a->x - $b->x;
        my $dy = $a->y - $b->y; #-
        my $dist = sqrt($dx*$dx + $dy*$dy);
        $dx /= $dist;
        $dy /= $dist;
        return Slic3r::Polygon->new(
            Slic3r::Point->new($a->x + $distance*$dy, $a->y - $distance*$dx), #--
            Slic3r::Point->new($b->x + $distance*$dy, $b->y - $distance*$dx), #--
            Slic3r::Point->new($b->x - $distance*$dy, $b->y + $distance*$dx), #++
            Slic3r::Point->new($a->x - $distance*$dy, $a->y + $distance*$dx), #++
        );
    };
    
    my @result = ();
    my $covered = [];
    foreach my $polygon (@$self) {
        my @polylines = ();
        foreach my $line (@{$polygon->lines}) {
            my $clipped = Boost::Geometry::Utils::multi_linestring_multi_polygon_difference([$line->pp], [ map $_->pp, @{union_ex($covered)} ]);
            @$clipped = grep $_->length > $width/10, map Slic3r::Polyline->new(@$_), @$clipped;
            push @$covered, map $grow->($_, $width*1.1), @$clipped;
            if (@polylines && @$clipped && $clipped->[0]->first_point->distance_to($polylines[-1]->last_point) <= $width/10) {
                $polylines[-1]->append_polyline(shift @$clipped);
            }
            push @polylines, @$clipped;
        }
        
        foreach my $polyline (@polylines) {
            next if $polyline->length <= $width/10;
            if ($polyline->first_point->coincides_with($polyline->last_point)) {
                next if @$polyline == 2;
                $polyline->pop_back;
                push @result, Slic3r::Polygon->new(@$polyline);
            } else {
                push @result, $polyline;
            }
        }
    }
    
    return @result;
}

sub _medial_axis_voronoi {
    my ($self, $width) = @_;
    
    my $voronoi;
    {
        my @points = ();
        foreach my $polygon (@$self) {
            {
                my $p = $polygon->pp;
                Slic3r::Geometry::polyline_remove_short_segments($p, $width / 2);
                $polygon = Slic3r::Polygon->new(@$p);
            }
            
            # subdivide polygon segments so that we don't have anyone of them
            # being longer than $width / 2
            $polygon = $polygon->subdivide($width/2);
            
            push @points, map $_->pp, @$polygon;
        }
        $voronoi = Math::Geometry::Voronoi->new(points => \@points);
    }
    
    $voronoi->compute;
    my $vertices = $voronoi->vertices;
    
    my @skeleton_lines = ();
    foreach my $edge (@{ $voronoi->edges }) {
        # ignore lines going to infinite
        next if $edge->[1] == -1 || $edge->[2] == -1;
        
        my ($a, $b);
        $a = Slic3r::Point->new(@{$vertices->[$edge->[1]]});
        $b = Slic3r::Point->new(@{$vertices->[$edge->[2]]});
        next if !$self->encloses_point_quick($a) || !$self->encloses_point_quick($b);
        
        push @skeleton_lines, [$edge->[1], $edge->[2]];
    }
    return () if !@skeleton_lines;
    
    # now walk along the medial axis and build continuos polylines or polygons
    my @polylines = ();
    {
        my @lines = @skeleton_lines;
        push @polylines, [ map @$_, shift @lines ];
        CYCLE: while (@lines) {
            for my $i (0..$#lines) {
                if ($lines[$i][0] == $polylines[-1][-1]) {
                    push @{$polylines[-1]}, $lines[$i][1];
                } elsif ($lines[$i][1] == $polylines[-1][-1]) {
                    push @{$polylines[-1]}, $lines[$i][0];
                } elsif ($lines[$i][1] == $polylines[-1][0]) {
                    unshift @{$polylines[-1]}, $lines[$i][0];
                } elsif ($lines[$i][0] == $polylines[-1][0]) {
                    unshift @{$polylines[-1]}, $lines[$i][1];
                } else {
                    next;
                }
                splice @lines, $i, 1;
                next CYCLE;
            }
            push @polylines, [ map @$_, shift @lines ];
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
            push @result, Slic3r::Polyline->new(@$polyline);
        }
    }
    
    return @result;
}

package Slic3r::ExPolygon::Collection;
use Slic3r::Geometry qw(X1 Y1);

sub align_to_origin {
    my $self = shift;
    
    my @bb = Slic3r::Geometry::bounding_box([ map @$_, map @$_, @$self ]);
    $self->translate(-$bb[X1], -$bb[Y1]);
    $self;
}

sub size {
    my $self = shift;
    return [ Slic3r::Geometry::size_2D([ map @$_, map @$_, @$self ]) ];
}

1;
