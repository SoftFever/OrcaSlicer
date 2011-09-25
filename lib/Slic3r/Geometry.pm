package Slic3r::Geometry;
use strict;
use warnings;

use XXX;

use constant A => 0;
use constant B => 1;
use constant X => 0;
use constant Y => 1;
use constant epsilon => 1E-8;
use constant epsilon2 => epsilon**2;

sub slope {
    my ($line) = @_;
    return undef if abs($line->[B][X] - $line->[A][X]) < epsilon;  # line is vertical
    return ($line->[B][Y] - $line->[A][Y]) / ($line->[B][X] - $line->[A][X]);
}

sub lines_parallel {
    my ($line1, $line2) = @_;
    
    my @slopes = map slope($_), $line1, $line2;
    return 1 if !defined $slopes[0] && !defined $slopes[1];
    return 0 if grep !defined, @slopes;
    return 1 if abs($slopes[0] - $slopes[1]) < epsilon;
    return 0;
}

# this subroutine checks whether a given point may belong to a given
# segment given the hypothesis that it belongs to the line containing
# the segment
sub line_point_belongs_to_segment {
    my ($point, $segment) = @_;
    
    #printf "   checking whether %f,%f may belong to segment %f,%f - %f,%f\n",
    #    @$point, map @$_, @$segment;
    
    my @segment_extents = (
        [ sort { $a <=> $b } map $_->[X], @$segment ],
        [ sort { $a <=> $b } map $_->[Y], @$segment ],
    );
    
    return 0 if $point->[X] < ($segment_extents[X][0] - epsilon) || $point->[X] > ($segment_extents[X][1] + epsilon);
    return 0 if $point->[Y] < ($segment_extents[Y][0] - epsilon) || $point->[Y] > ($segment_extents[Y][1] + epsilon);
    return 1;
}

sub points_coincide {
    my ($p1, $p2) = @_;
    return 1 if abs($p2->[X] - $p1->[X]) < epsilon && abs($p2->[Y] - $p1->[Y]) < epsilon;
    return 0;
}

sub distance_between_points {
    my ($p1, $p2) = @_;
    return sqrt(($p1->[X] - $p2->[X])**2 + ($p1->[Y] - $p2->[Y])**2);
}

sub point_in_polygon {
    my ($point, $polygon) = @_;
    
    my ($x, $y) = @$point;
    my @xy = map @$_, @$polygon;
    
    # Derived from the comp.graphics.algorithms FAQ,
    # courtesy of Wm. Randolph Franklin
    my $n = @xy / 2;                        # Number of points in polygon
    my @i = map { 2*$_ } 0..(@xy/2);        # The even indices of @xy
    my @x = map { $xy[$_]     } @i;         # Even indices: x-coordinates
    my @y = map { $xy[$_ + 1] } @i;         # Odd indices:  y-coordinates
    
    my ($i, $j);
    my $side = 0;                           # 0 = outside; 1 = inside
    for ($i = 0, $j = $n - 1; $i < $n; $j = $i++) {
        if (
            # If the y is between the (y-) borders...
            ($y[$i] <= $y && $y < $y[$j]) || ($y[$j] <= $y && $y < $y[$i])
            and
            # ...the (x,y) to infinity line crosses the edge
            # from the ith point to the jth point...
            ($x < ($x[$j] - $x[$i]) * ($y - $y[$i]) / ($y[$j] - $y[$i]) + $x[$i])
        ) {
            $side = not $side;  # Jump the fence
        }
    }
    
    # if point is not in polygon, let's check whether it belongs to the contour
    if (!$side) {
        foreach my $line (polygon_lines($polygon)) {
            # calculate the Y in line at X of the point
            if ($line->[A][X] == $line->[B][X]) {
                return 1 if abs($x - $line->[A][X]) < epsilon;
                next;
            }
            my $y3 = $line->[A][Y] + ($line->[B][Y] - $line->[A][Y])
                * ($x - $line->[A][X]) / ($line->[B][X] - $line->[A][X]);
            return 1 if abs($y3 - $y) < epsilon2;
        }
    }
    
    return $side;
}

sub polygon_lines {
    my ($polygon) = @_;
    
    my @lines = ();
    my $last_point = $polygon->[-1];
    foreach my $point (@$polygon) {
        push @lines, [ $last_point, $point ];
        $last_point = $point;
    }
    
    return @lines;
}

sub nearest_point {
    my ($point, $points) = @_;
    
    my ($nearest_point, $distance);
    foreach my $p (@$points) {
        my $d = distance_between_points($point, $p);
        if (!defined $distance || $d < $distance) {
            $nearest_point = $p;
            $distance = $d;
        }
    }
    return $nearest_point;
}

sub point_along_segment {
    my ($p1, $p2, $distance) = @_;
    
    my $point = [ @$p1 ];
    
    my $line_length = sqrt( (($p2->[X] - $p1->[X])**2) + (($p2->[Y] - $p1->[Y])**2) );
    for (X, Y) {
        if ($p1->[$_] != $p2->[$_]) {
            $point->[$_] = $p1->[$_] + ($p2->[$_] - $p1->[$_]) * $distance / $line_length;
        }
    }
    
    return $point;
}

1;
