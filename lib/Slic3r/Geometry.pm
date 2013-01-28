package Slic3r::Geometry;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(
    PI X Y Z A B X1 Y1 X2 Y2 MIN MAX epsilon slope line_atan lines_parallel 
    line_point_belongs_to_segment points_coincide distance_between_points 
    comparable_distance_between_points
    line_length midpoint point_in_polygon point_in_segment segment_in_segment
    point_is_on_left_of_segment polyline_lines polygon_lines nearest_point
    point_along_segment polygon_segment_having_point polygon_has_subsegment
    polygon_has_vertex polyline_length can_connect_points deg2rad rad2deg
    rotate_points move_points remove_coinciding_points clip_segment_polygon
    sum_vectors multiply_vector subtract_vectors dot perp polygon_points_visibility
    line_intersection bounding_box bounding_box_intersect same_point same_line
    longest_segment angle3points three_points_aligned line_direction
    polyline_remove_parallel_continuous_edges polyline_remove_acute_vertices
    polygon_remove_acute_vertices polygon_remove_parallel_continuous_edges
    shortest_path collinear scale unscale merge_collinear_lines
    rad2deg_dir bounding_box_center line_intersects_any douglas_peucker
    polyline_remove_short_segments normal triangle_normal polygon_is_convex
    scaled_epsilon bounding_box_3D size_3D size_2D
);


use constant PI => 4 * atan2(1, 1);
use constant A => 0;
use constant B => 1;
use constant X => 0;
use constant Y => 1;
use constant Z => 2;
use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;
use constant MIN => 0;
use constant MAX => 1;
our $parallel_degrees_limit = abs(deg2rad(0.1));

sub epsilon () { 1E-4 }
sub scaled_epsilon () { epsilon / &Slic3r::SCALING_FACTOR }

sub scale   ($) { $_[0] / &Slic3r::SCALING_FACTOR }
sub unscale ($) { $_[0] * &Slic3r::SCALING_FACTOR }

sub slope {
    my ($line) = @_;
    return undef if abs($line->[B][X] - $line->[A][X]) < epsilon;  # line is vertical
    return ($line->[B][Y] - $line->[A][Y]) / ($line->[B][X] - $line->[A][X]);
}

sub line_atan {
    my ($line) = @_;
    return atan2($line->[B][Y] - $line->[A][Y], $line->[B][X] - $line->[A][X]);
}

sub line_direction {
    my ($line) = @_;
    my $atan2 = line_atan($line);
    return ($atan2 == PI) ? 0
        : ($atan2 < 0) ? ($atan2 + PI)
        : $atan2;
}

sub lines_parallel {
    my ($line1, $line2) = @_;
    
    return abs(line_direction($line1) - line_direction($line2)) < $parallel_degrees_limit;
}

sub three_points_aligned {
    my ($p1, $p2, $p3) = @_;
    return lines_parallel([$p1, $p2], [$p2, $p3]);
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

sub same_point {
    my ($p1, $p2) = @_;
    return $p1->[X] == $p2->[X] && $p1->[Y] == $p2->[Y];
}

sub same_line {
    my ($line1, $line2) = @_;
    return same_point($line1->[A], $line2->[A]) && same_point($line1->[B], $line2->[B]);
}

sub distance_between_points {
    my ($p1, $p2) = @_;
    return sqrt((($p1->[X] - $p2->[X])**2) + ($p1->[Y] - $p2->[Y])**2);
}

sub comparable_distance_between_points {
    my ($p1, $p2) = @_;
    return (($p1->[X] - $p2->[X])**2) + (($p1->[Y] - $p2->[Y])**2);
}

sub point_line_distance {
    my ($point, $line) = @_;
    return distance_between_points($point, $line->[A])
        if same_point($line->[A], $line->[B]);
    
    my $n = ($line->[B][X] - $line->[A][X]) * ($line->[A][Y] - $point->[Y])
        - ($line->[A][X] - $point->[X]) * ($line->[B][Y] - $line->[A][Y]);
    
    my $d = sqrt((($line->[B][X] - $line->[A][X]) ** 2) + (($line->[B][Y] - $line->[A][Y]) ** 2));
    
    return abs($n) / $d;
}

sub line_length {
    my ($line) = @_;
    return distance_between_points(@$line[A, B]);
}

sub longest_segment {
    my (@lines) = @_;
    
    my ($longest, $maxlength);
    foreach my $line (@lines) {
        my $line_length = line_length($line);
        if (!defined $longest || $line_length > $maxlength) {
            $longest = $line;
            $maxlength = $line_length;
        }
    }
    return $longest;
}

sub midpoint {
    my ($line) = @_;
    return [ ($line->[B][X] + $line->[A][X]) / 2, ($line->[B][Y] + $line->[A][Y]) / 2 ];
}

# this will check whether a point is in a polygon regardless of polygon orientation
sub point_in_polygon {
    my ($point, $polygon) = @_;
    
    my ($x, $y) = @$point;
    my $n = @$polygon;
    my @x = map $_->[X], @$polygon;
    my @y = map $_->[Y], @$polygon;
    
    # Derived from the comp.graphics.algorithms FAQ,
    # courtesy of Wm. Randolph Franklin
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
        return 1 if polygon_segment_having_point($polygon, $point);
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
        return abs($x - $line->[A][X]) < epsilon ? 1 : 0;
    }
    
    # calculate the Y in line at X of the point
    my $y3 = $line->[A][Y] + ($line->[B][Y] - $line->[A][Y])
        * ($x - $line->[A][X]) / ($line->[B][X] - $line->[A][X]);
    return abs($y3 - $y) < epsilon ? 1 : 0;
}

sub segment_in_segment {
    my ($needle, $haystack) = @_;
    
    # a segment is contained in another segment if its endpoints are contained
    return point_in_segment($needle->[A], $haystack) && point_in_segment($needle->[B], $haystack);
}

sub point_is_on_left_of_segment {
    my ($point, $line) = @_;
    
    return (($line->[B][X] - $line->[A][X])*($point->[Y] - $line->[A][Y]) 
        - ($line->[B][Y] - $line->[A][Y])*($point->[X] - $line->[A][X])) > 0;
}

sub polyline_lines {
    my ($polygon) = @_;
    
    my @lines = ();
    my $last_point;
    foreach my $point (@$polygon) {
        push @lines, Slic3r::Line->new($last_point, $point) if $last_point;
        $last_point = $point;
    }
    
    return @lines;
}

sub polygon_lines {
    my ($polygon) = @_;
    return polyline_lines([ @$polygon, $polygon->[0] ]);
}

sub nearest_point {
    my ($point, $points) = @_;
    my $index = nearest_point_index(@_);
    return undef if !defined $index;
    return $points->[$index];
}

sub nearest_point_index {
    my ($point, $points) = @_;
    
    my ($nearest_point_index, $distance) = ();
    for my $i (0..$#$points) {
        my $d = comparable_distance_between_points($point, $points->[$i]);
        if (!defined $distance || $d < $distance) {
            $nearest_point_index = $i;
            $distance = $d;
            return $i if $distance < epsilon;
        }
    }
    return $nearest_point_index;
}

# given a segment $p1-$p2, get the point at $distance from $p1 along segment
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

# given a $polygon, return the (first) segment having $point
sub polygon_segment_having_point {
    my ($polygon, $point) = @_;
    
    foreach my $line (polygon_lines($polygon)) {
        return $line if point_in_segment($point, $line);
    }
    return undef;
}

# return true if the given segment is contained in any edge of the polygon
sub polygon_has_subsegment {
    my ($polygon, $segment) = @_;
    foreach my $line (polygon_lines($polygon)) {
        return 1 if segment_in_segment($segment, $line);
    }
    return 0;
}

sub polygon_has_vertex {
    my ($polygon, $point) = @_;
    foreach my $p (@$polygon) {
        return 1 if points_coincide($p, $point);
    }
    return 0;
}

# polygon must be simple (non complex) and ccw
sub polygon_is_convex {
    my ($points) = @_;
    for (my $i = 0; $i <= $#$points; $i++) {
        my $angle = angle3points($points->[$i-1], $points->[$i-2], $points->[$i]);
        return 0 if $angle < PI;
    }
    return 1;
}

sub polyline_length {
    my ($polyline) = @_;
    my $length = 0;
    $length += line_length($_) for polygon_lines($polyline);
    return $length;
}

sub can_connect_points {
    my ($p1, $p2, $polygons) = @_;
    
    # check that the two points are visible from each other
    return 0 if grep !polygon_points_visibility($_, $p1, $p2), @$polygons;
    
    # get segment where $p1 lies
    my $p1_segment;
    for (@$polygons) {
        $p1_segment = polygon_segment_having_point($_, $p1);
        last if $p1_segment;
    }
    
    # defensive programming, this shouldn't happen
    if (!$p1_segment) {
        die sprintf "Point %f,%f wasn't found in polygon contour or holes!", @$p1;
    }
    
    # check whether $p2 is internal or external  (internal = on the left)
    return point_is_on_left_of_segment($p2, $p1_segment)
        || point_in_segment($p2, $p1_segment);
}

sub deg2rad {
    my ($degrees) = @_;
    return PI() * $degrees / 180;
}

sub rad2deg {
    my ($rad) = @_;
    return $rad / PI() * 180;
}

sub rad2deg_dir {
    my ($rad) = @_;
    $rad = ($rad < PI) ? (-$rad + PI/2) : ($rad + PI/2);
    $rad += PI if $rad < 0;
    return rad2deg($rad);
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
    return map Slic3r::Point->new($shift->[X] + $_->[X], $shift->[Y] + $_->[Y]), @points;
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

sub normal {
    my ($line1, $line2) = @_;
    
    return [
         ($line1->[Y] * $line2->[Z]) - ($line1->[Z] * $line2->[Y]),
        -($line2->[Z] * $line1->[X]) + ($line2->[X] * $line1->[Z]),
         ($line1->[X] * $line2->[Y]) - ($line1->[Y] * $line2->[X]),
    ];
}

sub triangle_normal {
    my ($v1, $v2, $v3) = @_;
    
    my $u = [ map +($v2->[$_] - $v1->[$_]), (X,Y,Z) ];
    my $v = [ map +($v3->[$_] - $v1->[$_]), (X,Y,Z) ];
    
    return normal($u, $v);
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

sub polygon_points_visibility {
    my ($polygon, $p1, $p2) = @_;
    
    my $our_line = [ $p1, $p2 ];
    foreach my $line (polygon_lines($polygon)) {
        my $intersection = line_intersection($our_line, $line, 1) or next;
        next if grep points_coincide($intersection, $_), $p1, $p2;
        return 0;
    }
    
    return 1;
}

sub line_intersects_any {
    my ($line, $lines) = @_;
    for (@$lines) {
        return 1 if line_intersection($line, $_, 1);
    }
    return 0;
}

sub line_intersection {
    my ($line1, $line2, $require_crossing) = @_;
    $require_crossing ||= 0;
    
    my $intersection = _line_intersection(map @$_, @$line1, @$line2);
    return (ref $intersection && $intersection->[1] == $require_crossing) 
        ? $intersection->[0] 
        : undef;
}

sub collinear {
    my ($line1, $line2, $require_overlapping) = @_;
    my $intersection = _line_intersection(map @$_, @$line1, @$line2);
    return 0 unless !ref($intersection) 
        && ($intersection eq 'parallel collinear'
            || ($intersection eq 'parallel vertical' && abs($line1->[A][X] - $line2->[A][X]) < epsilon));
    
    if ($require_overlapping) {
        my @box_a = bounding_box([ $line1->[0], $line1->[1] ]);
        my @box_b = bounding_box([ $line2->[0], $line2->[1] ]);
        return 0 unless bounding_box_intersect( 2, @box_a, @box_b );
    }
    
    return 1;
}

sub merge_collinear_lines {
    my ($lines) = @_;
    my $line_count = @$lines;
    
    for (my $i = 0; $i <= $#$lines-1; $i++) {
        for (my $j = $i+1; $j <= $#$lines; $j++) {
            # lines are collinear and overlapping?
            next unless collinear($lines->[$i], $lines->[$j], 1);
            
            # lines have same orientation?
            next unless ($lines->[$i][A][X] <=> $lines->[$i][B][X]) == ($lines->[$j][A][X] <=> $lines->[$j][B][X])
                && ($lines->[$i][A][Y] <=> $lines->[$i][B][Y]) == ($lines->[$j][A][Y] <=> $lines->[$j][B][Y]);
            
            # resulting line
            my @x = sort { $a <=> $b } ($lines->[$i][A][X], $lines->[$i][B][X], $lines->[$j][A][X], $lines->[$j][B][X]);
            my @y = sort { $a <=> $b } ($lines->[$i][A][Y], $lines->[$i][B][Y], $lines->[$j][A][Y], $lines->[$j][B][Y]);
            my $new_line = Slic3r::Line->new([$x[0], $y[0]], [$x[-1], $y[-1]]);
            for (X, Y) {
                ($new_line->[A][$_], $new_line->[B][$_]) = ($new_line->[B][$_], $new_line->[A][$_])
                    if $lines->[$i][A][$_] > $lines->[$i][B][$_];
            }
            
            # save new line and remove found one
            $lines->[$i] = $new_line;
            splice @$lines, $j, 1;
            $j--;
        }
    }
    
    Slic3r::debugf "  merging %d lines resulted in %d lines\n", $line_count, scalar(@$lines);
    
    return $lines;
}

sub _line_intersection {
  my ( $x0, $y0, $x1, $y1, $x2, $y2, $x3, $y3 ) = @_;

  my ($x, $y);  # The as-yet-undetermined intersection point.

  my $dy10 = $y1 - $y0; # dyPQ, dxPQ are the coordinate differences
  my $dx10 = $x1 - $x0; # between the points P and Q.
  my $dy32 = $y3 - $y2;
  my $dx32 = $x3 - $x2;

  my $dy10z = abs( $dy10 ) < epsilon; # Is the difference $dy10 "zero"?
  my $dx10z = abs( $dx10 ) < epsilon;
  my $dy32z = abs( $dy32 ) < epsilon;
  my $dx32z = abs( $dx32 ) < epsilon;

  my $dyx10;                            # The slopes.
  my $dyx32;
  
  $dyx10 = $dy10 / $dx10 unless $dx10z;
  $dyx32 = $dy32 / $dx32 unless $dx32z;

  # Now we know all differences and the slopes;
  # we can detect horizontal/vertical special cases.
  # E.g., slope = 0 means a horizontal line.

  unless ( defined $dyx10 or defined $dyx32 ) {
    return "parallel vertical";
  }
  elsif ( $dy10z and not $dy32z ) { # First line horizontal.
    $y = $y0;
    $x = $x2 + ( $y - $y2 ) * $dx32 / $dy32;
  }
  elsif ( not $dy10z and $dy32z ) { # Second line horizontal.
    $y = $y2;
    $x = $x0 + ( $y - $y0 ) * $dx10 / $dy10;
  }
  elsif ( $dx10z and not $dx32z ) { # First line vertical.
    $x = $x0;
    $y = $y2 + $dyx32 * ( $x - $x2 );
  }
  elsif ( not $dx10z and $dx32z ) { # Second line vertical.
    $x = $x2;
    $y = $y0 + $dyx10 * ( $x - $x0 );
  }
  elsif ( abs( $dyx10 - $dyx32 ) < epsilon ) {
    # The slopes are suspiciously close to each other.
    # Either we have parallel collinear or just parallel lines.

    # The bounding box checks have already weeded the cases
    # "parallel horizontal" and "parallel vertical" away.

    my $ya = $y0 - $dyx10 * $x0;
    my $yb = $y2 - $dyx32 * $x2;
    
    return "parallel collinear" if abs( $ya - $yb ) < epsilon;
    return "parallel";
  }
  else {
    # None of the special cases matched.
    # We have a "honest" line intersection.

    $x = ($y2 - $y0 + $dyx10*$x0 - $dyx32*$x2)/($dyx10 - $dyx32);
    $y = $y0 + $dyx10 * ($x - $x0);
  }

  my $h10 = $dx10 ? ($x - $x0) / $dx10 : ($dy10 ? ($y - $y0) / $dy10 : 1);
  my $h32 = $dx32 ? ($x - $x2) / $dx32 : ($dy32 ? ($y - $y2) / $dy32 : 1);

  return [Slic3r::Point->new($x, $y), $h10 >= 0 && $h10 <= 1 && $h32 >= 0 && $h32 <= 1];
}

# http://paulbourke.net/geometry/lineline2d/
sub _line_intersection2 {
    my ($line1, $line2) = @_;
    
    my $denom = ($line2->[B][Y] - $line2->[A][Y]) * ($line1->[B][X] - $line1->[A][X])
        - ($line2->[B][X] - $line2->[A][X]) * ($line1->[B][Y] - $line1->[A][Y]);
    my $numerA = ($line2->[B][X] - $line2->[A][X]) * ($line1->[A][Y] - $line2->[A][Y])
        - ($line2->[B][Y] - $line2->[A][Y]) * ($line1->[A][X] - $line2->[A][X]);
    my $numerB = ($line1->[B][X] - $line1->[A][X]) * ($line1->[A][Y] - $line2->[A][Y])
        - ($line1->[B][Y] - $line1->[A][Y]) * ($line1->[A][X] - $line2->[A][X]);
    
    # are the lines coincident?
    if (abs($numerA) < epsilon && abs($numerB) < epsilon && abs($denom) < epsilon) {
        return Slic3r::Point->new(
            ($line1->[A][X] + $line1->[B][X]) / 2,
            ($line1->[A][Y] + $line1->[B][Y]) / 2,
        );
    }
    
    # are the lines parallel?
    if (abs($denom) < epsilon) {
        return undef;
    }
    
    # is the intersection along the segments?
    my $muA = $numerA / $denom;
    my $muB = $numerB / $denom;
    if ($muA < 0 || $muA > 1 || $muB < 0 || $muB > 1) {
        return undef;
    }
    
    return Slic3r::Point->new(
        $line1->[A][X] + $muA * ($line1->[B][X] - $line1->[A][X]),
        $line1->[A][Y] + $muA * ($line1->[B][Y] - $line1->[A][Y]),
    );
}

# 2D
sub bounding_box {
    my ($points) = @_;
    
    my @x = (undef, undef);
    my @y = (undef, undef);
    for (@$points) {
        $x[MIN] = $_->[X] if !defined $x[MIN] || $_->[X] < $x[MIN];
        $x[MAX] = $_->[X] if !defined $x[MAX] || $_->[X] > $x[MAX];
        $y[MIN] = $_->[Y] if !defined $y[MIN] || $_->[Y] < $y[MIN];
        $y[MAX] = $_->[Y] if !defined $y[MAX] || $_->[Y] > $y[MAX];
    }
    
    return ($x[0], $y[0], $x[-1], $y[-1]);
}

sub bounding_box_center {
    my @bounding_box = bounding_box(@_);
    return Slic3r::Point->new(
        ($bounding_box[X2] + $bounding_box[X1]) / 2,
        ($bounding_box[Y2] + $bounding_box[Y1]) / 2,
    );
}

sub size_2D {
    my @bounding_box = bounding_box(@_);
    return (
        ($bounding_box[X2] - $bounding_box[X1]),
        ($bounding_box[Y2] - $bounding_box[Y1]),
    );
}

# bounding_box_intersect($d, @a, @b)
#   Return true if the given bounding boxes @a and @b intersect
#   in $d dimensions.  Used by line_intersection().
sub bounding_box_intersect {
    my ( $d, @bb ) = @_; # Number of dimensions and box coordinates.
    my @aa = splice( @bb, 0, 2 * $d ); # The first box.
    # (@bb is the second one.)
    
    # Must intersect in all dimensions.
    for ( my $i_min = 0; $i_min < $d; $i_min++ ) {
        my $i_max = $i_min + $d; # The index for the maximum.
        return 0 if ( $aa[ $i_max ] + epsilon ) < $bb[ $i_min ];
        return 0 if ( $bb[ $i_max ] + epsilon ) < $aa[ $i_min ];
    }
    
    return 1;
}

# 3D
sub bounding_box_3D {
    my ($points) = @_;
    
    my @extents = (map [undef, undef], X,Y,Z);
    foreach my $point (@$points) {
        for (X,Y,Z) {
            $extents[$_][MIN] = $point->[$_] if !defined $extents[$_][MIN] || $point->[$_] < $extents[$_][MIN];
            $extents[$_][MAX] = $point->[$_] if !defined $extents[$_][MAX] || $point->[$_] > $extents[$_][MAX];
        }
    }
    return @extents;
}

sub size_3D {
    my ($points) = @_;
    
    my @extents = bounding_box_3D($points);
    return map $extents[$_][MAX] - $extents[$_][MIN], (X,Y,Z);
}

# this assumes a CCW rotation from $p2 to $p3 around $p1
sub angle3points {
    my ($p1, $p2, $p3) = @_;
    # p1 is the center
    
    my $angle = atan2($p2->[X] - $p1->[X], $p2->[Y] - $p1->[Y])
              - atan2($p3->[X] - $p1->[X], $p3->[Y] - $p1->[Y]);
    
    # we only want to return only positive angles
    return $angle <= 0 ? $angle + 2*PI() : $angle;
}

sub polyline_remove_parallel_continuous_edges {
    my ($points, $isPolygon) = @_;
    
    for (my $i = $isPolygon ? 0 : 2; $i <= $#$points && @$points >= 3; $i++) {
        if (Slic3r::Geometry::lines_parallel([$points->[$i-2], $points->[$i-1]], [$points->[$i-1], $points->[$i]])) {
            # we can remove $points->[$i-1]
            splice @$points, $i-1, 1;
            $i--;
        }
    }
}

sub polygon_remove_parallel_continuous_edges {
    my ($points) = @_;
    return polyline_remove_parallel_continuous_edges($points, 1);
}

sub polyline_remove_acute_vertices {
    my ($points, $isPolygon) = @_;
    for (my $i = $isPolygon ? -1 : 1; $i < $#$points; $i++) {
        my $angle = angle3points($points->[$i], $points->[$i-1], $points->[$i+1]);
        if ($angle < 0.01 || $angle >= 2*PI - 0.01) {
            # we can remove $points->[$i]
            splice @$points, $i, 1;
            $i--;
        }
    }
}

sub polygon_remove_acute_vertices {
    my ($points) = @_;
    return polyline_remove_acute_vertices($points, 1);
}

sub polyline_remove_short_segments {
    my ($points, $min_length, $isPolygon) = @_;
    for (my $i = $isPolygon ? 0 : 1; $i < $#$points; $i++) {
        if (distance_between_points($points->[$i-1], $points->[$i]) < $min_length) {
            # we can remove $points->[$i]
            splice @$points, $i, 1;
            $i--;
        }
    }
}

# accepts an arrayref; each item should be an arrayref whose first
# item is the point to be used for the shortest path, and the second
# one is the value to be returned in output (if the second item
# is not provided, the point will be returned)
sub shortest_path {
    my ($items, $start_near) = @_;
    
    my %values = map +($_->[0] => $_->[1] || $_->[0]), @$items;
    my @points = map $_->[0], @$items;
    
    my $result = [];
    my $last_point;
    if (!$start_near) {
        $start_near = shift @points;
        push @$result, $values{$start_near} if $start_near;
    }
    while (@points) {
        $start_near = nearest_point($start_near, [@points]);
        @points = grep $_ ne $start_near, @points;
        push @$result, $values{$start_near};
    }
    
    return $result;
}

sub douglas_peucker {
    my ($points, $tolerance) = @_;
    no warnings "recursion";
    
    my $results = [];
    my $dmax = 0;
    my $index = 0;
    for my $i (1..$#$points) {
        my $d = point_line_distance($points->[$i], [ $points->[0], $points->[-1] ]);
        if ($d > $dmax) {
            $index = $i;
            $dmax = $d;
        }
    }
    if ($dmax >= $tolerance) {
        my $dp1 = douglas_peucker([ @$points[0..$index] ], $tolerance);
        $results = [
            @$dp1[0..($#$dp1-1)],
            @{douglas_peucker([ @$points[$index..$#$points] ], $tolerance)},
        ];
    } else {
        $results = [ $points->[0], $points->[-1] ];
    }
    return $results;
}

sub douglas_peucker2 {
    my ($points, $tolerance) = @_;
    
    my $anchor = 0;
    my $floater = $#$points;
    my @stack = ();
    my %keep = ();
    
    push @stack, [$anchor, $floater];
    while (@stack) {
        ($anchor, $floater) = @{pop @stack};
        
        # initialize line segment
        my ($anchor_x, $anchor_y, $seg_len);
        if (grep $points->[$floater][$_] != $points->[$anchor][$_], X, Y) {
            $anchor_x = $points->[$floater][X] - $points->[$anchor][X];
            $anchor_y = $points->[$floater][Y] - $points->[$anchor][Y];
            $seg_len = sqrt(($anchor_x ** 2) + ($anchor_y ** 2));
            # get the unit vector
            $anchor_x /= $seg_len;
            $anchor_y /= $seg_len;
        } else {
            $anchor_x = $anchor_y = $seg_len = 0;
        }
        
        # inner loop:
        my $max_dist = 0;
        my $farthest = $anchor + 1;
        for my $i (($anchor + 1) .. $floater) {
            my $dist_to_seg = 0;
            # compare to anchor
            my $vecX = $points->[$i][X] - $points->[$anchor][X];
            my $vecY = $points->[$i][Y] - $points->[$anchor][Y];
            $seg_len = sqrt(($vecX ** 2) + ($vecY ** 2));
            # dot product:
            my $proj = $vecX * $anchor_x + $vecY * $anchor_y;
            if ($proj < 0) {
                $dist_to_seg = $seg_len;
            } else {
                # compare to floater
                $vecX = $points->[$i][X] - $points->[$floater][X];
                $vecY = $points->[$i][Y] - $points->[$floater][Y];
                $seg_len = sqrt(($vecX ** 2) + ($vecY ** 2));
                # dot product:
                $proj = $vecX * (-$anchor_x) + $vecY * (-$anchor_y);
                if ($proj < 0) {
                    $dist_to_seg = $seg_len
                } else {  # calculate perpendicular distance to line (pythagorean theorem):
                    $dist_to_seg = sqrt(abs(($seg_len ** 2) - ($proj ** 2)));
                }
                if ($max_dist < $dist_to_seg) {
                    $max_dist = $dist_to_seg;
                    $farthest = $i;
                }
            }
        }
        
        if ($max_dist <= $tolerance) { # use line segment
            $keep{$_} = 1 for $anchor, $floater;
        } else {
            push @stack, [$anchor, $farthest];
            push @stack, [$farthest, $floater];
        }
    }
    
    return [ map $points->[$_], sort keys %keep ];
}

sub arrange {
    my ($total_parts, $partx, $party, $areax, $areay, $dist, $Config) = @_;
    
    my $linint = sub {
        my ($value, $oldmin, $oldmax, $newmin, $newmax) = @_;
        return ($value - $oldmin) * ($newmax - $newmin) / ($oldmax - $oldmin) + $newmin;
    };
    
    # use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
    $partx += $dist;
    $party += $dist;
    
    # margin needed for the skirt
    my $skirt_margin;		
    if ($Config->skirts > 0) {
        my $flow = Slic3r::Flow->new(
            layer_height    => $Config->get_value('first_layer_height'),
            nozzle_diameter => $Config->nozzle_diameter->[0],  # TODO: actually look for the extruder used for skirt
            width           => $Config->get_value('first_layer_extrusion_width'),
        );
        $skirt_margin = ($flow->spacing * $Config->skirts + $Config->skirt_distance) * 2;
    } else {
        $skirt_margin = 0;		
    }
    
    # this is how many cells we have available into which to put parts
    my $cellw = int(($areax - $skirt_margin + $dist) / $partx);
    my $cellh = int(($areay - $skirt_margin + $dist) / $party);
    
    die "$total_parts parts won't fit in your print area!\n" if $total_parts > ($cellw * $cellh);
    
    # width and height of space used by cells
    my $w = $cellw * $partx;
    my $h = $cellh * $party;
    
    # left and right border positions of space used by cells
    my $l = ($areax - $w) / 2;
    my $r = $l + $w;
    
    # top and bottom border positions
    my $t = ($areay - $h) / 2;
    my $b = $t + $h;
    
    # list of cells, sorted by distance from center
    my @cellsorder;
    
    # work out distance for all cells, sort into list
    for my $i (0..$cellw-1) {
        for my $j (0..$cellh-1) {
            my $cx = $linint->($i + 0.5, 0, $cellw, $l, $r);
            my $cy = $linint->($j + 0.5, 0, $cellh, $t, $b);
            
            my $xd = abs(($areax / 2) - $cx);
            my $yd = abs(($areay / 2) - $cy);
            
            my $c = {
                location => [$cx, $cy],
                index => [$i, $j],
                distance => $xd * $xd + $yd * $yd - abs(($cellw / 2) - ($i + 0.5)),
            };
            
            BINARYINSERTIONSORT: {
                my $index = $c->{distance};
                my $low = 0;
                my $high = @cellsorder;
                while ($low < $high) {
                    my $mid = ($low + (($high - $low) / 2)) | 0;
                    my $midval = $cellsorder[$mid]->[0];
                    
                    if ($midval < $index) {
                        $low = $mid + 1;
                    } elsif ($midval > $index) {
                        $high = $mid;
                    } else {
                        splice @cellsorder, $mid, 0, [$index, $c];
                        last BINARYINSERTIONSORT;
                    }
                }
                splice @cellsorder, $low, 0, [$index, $c];
            }
        }
    }
    
    # the extents of cells actually used by objects
    my ($lx, $ty, $rx, $by) = (0, 0, 0, 0);

    # now find cells actually used by objects, map out the extents so we can position correctly
    for my $i (1..$total_parts) {
        my $c = $cellsorder[$i - 1];
        my $cx = $c->[1]->{index}->[0];
        my $cy = $c->[1]->{index}->[1];
        if ($i == 1) {
            $lx = $rx = $cx;
            $ty = $by = $cy;
        } else {
            $rx = $cx if $cx > $rx;
            $lx = $cx if $cx < $lx;
            $by = $cy if $cy > $by;
            $ty = $cy if $cy < $ty;
        }
    }
    # now we actually place objects into cells, positioned such that the left and bottom borders are at 0
    my @positions = ();
    for (1..$total_parts) {
        my $c = shift @cellsorder;
        my $cx = $c->[1]->{index}->[0] - $lx;
        my $cy = $c->[1]->{index}->[1] - $ty;

        push @positions, [$cx * $partx, $cy * $party];
    }
    return @positions;
}

1;
