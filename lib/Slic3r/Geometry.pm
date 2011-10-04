package Slic3r::Geometry;
use strict;
use warnings;

use Slic3r::Geometry::DouglasPeucker;
use XXX;

use constant PI => 4 * atan2(1, 1);
use constant A => 0;
use constant B => 1;
use constant X => 0;
use constant Y => 1;
use constant epsilon => 1E-6;
our $parallel_degrees_limit = abs(deg2rad(3));

sub slope {
    my ($line) = @_;
    return undef if abs($line->[B][X] - $line->[A][X]) < epsilon;  # line is vertical
    return ($line->[B][Y] - $line->[A][Y]) / ($line->[B][X] - $line->[A][X]);
}

sub line_atan {
    my ($line) = @_;
    return atan2($line->[B][Y] - $line->[A][Y], $line->[B][X] - $line->[A][X]);
}

sub lines_parallel {
    my ($line1, $line2) = @_;
    
    return abs(line_atan($line1) - line_atan($line2)) < $parallel_degrees_limit;
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
    if (!$side && 0) {
        foreach my $line (polygon_lines($polygon)) {
            return 1 if point_in_segment($point, $line);
        }
    }
    
    return $side;
}

sub point_in_segment {
    my ($point, $line) = @_;
    
    my ($x, $y) = @$point;
    my @line_x = sort { $a <=> $b } $line->[A][X], $line->[B][X];
    my @line_y = sort { $a <=> $b } $line->[A][Y], $line->[B][Y];
    
    # check whether the point is in the segment bounding box
    return 0 unless $x >= ($line_x[0] - epsilon) && $x <= ($line_x[1] + epsilon)
        && $y >= ($line_y[0] - epsilon) && $y <= ($line_y[1] + epsilon);
    
    # if line is vertical, check whether point's X is the same as the line
    if ($line->[A][X] == $line->[B][X]) {
        return 1 if abs($x - $line->[A][X]) < epsilon;
    }
    
    # calculate the Y in line at X of the point
    my $y3 = $line->[A][Y] + ($line->[B][Y] - $line->[A][Y])
        * ($x - $line->[A][X]) / ($line->[B][X] - $line->[A][X]);
    return abs($y3 - $y) < epsilon ? 1 : 0;
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
            return $p if $distance < epsilon;
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

sub deg2rad {
    my ($degrees) = @_;
    return PI() * $degrees / 180;
}

sub rotate_points {
    my ($radians, $center, @points) = @_;
    $center ||= [0,0];
    return map {
        [
            $center->[X] + cos($radians) * ($_->[X] - $center->[X]) - sin($radians) * ($_->[Y] - $center->[Y]),
            $center->[Y] + cos($radians) * ($_->[Y] - $center->[Y]) + sin($radians) * ($_->[X] - $center->[X]),
        ]
    } @points;
}

sub move_points {
    my ($shift, @points) = @_;
    return map [ $shift->[X] + $_->[X], $shift->[Y] + $_->[Y] ], @points;
}

# preserves order
sub remove_coinciding_points {
    my ($points) = @_;
    
    my %p = map { sprintf('%f,%f', @$_) => "$_" } @$points;
    %p = reverse %p;
    @$points = grep $p{"$_"}, @$points;
}

# implementation of Liang-Barsky algorithm
# polygon must be convex and ccw
sub clip_segment_polygon {
    my ($line, $polygon) = @_;
    
    if (@$line == 1) {
        # the segment is a point, check for inclusion
        return point_in_polygon($line, $polygon);
    }
    
    my @V = (@$polygon, $polygon->[0]);
    my $tE = 0; # the maximum entering segment parameter
    my $tL = 1; # the minimum entering segment parameter
    my $dS = subtract_vectors($line->[B], $line->[A]); # the segment direction vector
    
    for (my $i = 0; $i < $#V; $i++) {   # process polygon edge V[i]V[Vi+1]
        my $e = subtract_vectors($V[$i+1], $V[$i]);
        my $N = perp($e, subtract_vectors($line->[A], $V[$i]));
        my $D = -perp($e, $dS);
        if (abs($D) < epsilon) {          # $line is nearly parallel to this edge
            ($N < 0) ? return : next;     # P0 outside this edge ? $line is outside : $line cannot cross edge, thus ignoring
        }
        
        my $t = $N / $D;
        if ($D < 0) { # $line is entering across this edge
            if ($t > $tE) {  # new max $tE
                $tE = $t;
                return if $tE > $tL;  # $line enters after leaving polygon?
            }
        } else { # $line is leaving across this edge
            if ($t < $tL) {  # new min $tL
                $tL = $t;
                return if $tL < $tE;  # $line leaves before entering polygon?
            }
        }
    }
    
    # $tE <= $tL implies that there is a valid intersection subsegment
    return [
        sum_vectors($line->[A], multiply_vector($dS, $tE)),  # = P(tE) = point where S enters polygon
        sum_vectors($line->[A], multiply_vector($dS, $tL)),  # = P(tE) = point where S enters polygon
    ];
}

sub sum_vectors {
    my ($v1, $v2) = @_;
    return [ $v1->[X] + $v2->[X], $v1->[Y] + $v2->[Y] ];
}

sub multiply_vector {
    my ($line, $scalar) = @_;
    return [ $line->[X] * $scalar, $line->[Y] * $scalar ];
}

sub subtract_vectors {
    my ($line2, $line1) = @_;
    return [ $line2->[X] - $line1->[X], $line2->[Y] - $line1->[Y] ];
}

# 2D dot product
sub dot {
    my ($u, $v) = @_;
    return $u->[X] * $v->[X] + $u->[Y] * $v->[Y];
}

# 2D perp product
sub perp {
    my ($u, $v) = @_;
    return $u->[X] * $v->[Y] - $u->[Y] * $v->[X];
}

1;
