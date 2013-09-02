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
    my $self = shift;
    my ($width) = @_;
    
    my @self_lines = map $_->lines, @$self;
    my $expolygon = $self->clone;
    my @points = ();
    foreach my $polygon (@$expolygon) {
        {
            my $p = $polygon->pp;
            Slic3r::Geometry::polyline_remove_short_segments($p, $width / 2);
            $polygon = Slic3r::Polygon->new(@$p);
        }
        
        # subdivide polygon segments so that we don't have anyone of them
        # being longer than $width / 2
        $polygon->subdivide($width/2);
        
        push @points, @$polygon;
    }
    
    my $voronoi = Math::Geometry::Voronoi->new(points => [ map $_->pp, @points ]);
    $voronoi->compute;
    
    my @skeleton_lines = ();
    
    my $vertices = $voronoi->vertices;
    my $edges = $voronoi->edges;
    foreach my $edge (@$edges) {
        # ignore lines going to infinite
        next if $edge->[1] == -1 || $edge->[2] == -1;
        
        my ($a, $b);
        $a = Slic3r::Point->new(@{$vertices->[$edge->[1]]});
        $b = Slic3r::Point->new(@{$vertices->[$edge->[2]]});
        
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
        $polyline = Slic3r::Polyline->new(@$polyline);
        
        if (Slic3r::Geometry::same_point($polyline->first_point, $polyline->last_point)) {
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
