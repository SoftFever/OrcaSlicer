package Slic3r::Geometry;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(
    PI X Y Z A B X1 Y1 X2 Y2 Z1 Z2 MIN MAX epsilon slope 
    line_point_belongs_to_segment points_coincide distance_between_points 
    normalize tan move_points_3D
    point_in_polygon point_in_segment segment_in_segment
    polyline_lines polygon_lines
    point_along_segment polygon_segment_having_point polygon_has_subsegment
    deg2rad rad2deg
    rotate_points move_points
    dot perp
    line_intersection bounding_box bounding_box_intersect
    angle3points
    chained_path chained_path_from collinear scale unscale
    rad2deg_dir bounding_box_center line_intersects_any douglas_peucker
    polyline_remove_short_segments normal triangle_normal polygon_is_convex
    scaled_epsilon bounding_box_3D size_3D size_2D
    convex_hull directions_parallel directions_parallel_within
);


use constant PI => 4 * atan2(1, 1);
use constant A => 0;
use constant B => 1;
use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;
use constant Z1 => 4;
use constant Z2 => 5;
use constant MIN => 0;
use constant MAX => 1;
our $parallel_degrees_limit = abs(deg2rad(0.1));

sub epsilon () { 1E-4 }
sub scaled_epsilon () { epsilon / &Slic3r::SCALING_FACTOR }

sub scale   ($) { $_[0] / &Slic3r::SCALING_FACTOR }
sub unscale ($) { $_[0] * &Slic3r::SCALING_FACTOR }

sub tan {
    my ($angle) = @_;
    return (sin $angle) / (cos $angle);
}

sub slope {
    my ($line) = @_;
    return undef if abs($line->[B][X] - $line->[A][X]) < epsilon;  # line is vertical
    return ($line->[B][Y] - $line->[A][Y]) / ($line->[B][X] - $line->[A][X]);
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
    return sqrt((($p1->[X] - $p2->[X])**2) + ($p1->[Y] - $p2->[Y])**2);
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
    my $line_p = $line->pp;
    my @line_x = sort { $a <=> $b } $line_p->[A][X], $line_p->[B][X];
    my @line_y = sort { $a <=> $b } $line_p->[A][Y], $line_p->[B][Y];
    
    # check whether the point is in the segment bounding box
    return 0 unless $x >= ($line_x[0] - epsilon) && $x <= ($line_x[1] + epsilon)
        && $y >= ($line_y[0] - epsilon) && $y <= ($line_y[1] + epsilon);
    
    # if line is vertical, check whether point's X is the same as the line
    if ($line_p->[A][X] == $line_p->[B][X]) {
        return abs($x - $line_p->[A][X]) < epsilon ? 1 : 0;
    }
    
    # calculate the Y in line at X of the point
    my $y3 = $line_p->[A][Y] + ($line_p->[B][Y] - $line_p->[A][Y])
        * ($x - $line_p->[A][X]) / ($line_p->[B][X] - $line_p->[A][X]);
    return abs($y3 - $y) < epsilon ? 1 : 0;
}

sub segment_in_segment {
    my ($needle, $haystack) = @_;
    
    # a segment is contained in another segment if its endpoints are contained
    return point_in_segment($needle->[A], $haystack) && point_in_segment($needle->[B], $haystack);
}

sub polyline_lines {
    my ($polyline) = @_;
    my @points = @$polyline;
    return map Slic3r::Line->new(@points[$_, $_+1]), 0 .. $#points-1;
}

sub polygon_lines {
    my ($polygon) = @_;
    return polyline_lines([ @$polygon, $polygon->[0] ]);
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
    
    return Slic3r::Point->new(@$point);
}

# given a $polygon, return the (first) segment having $point
sub polygon_segment_having_point {
    my ($polygon, $point) = @_;
    
    foreach my $line (@{ $polygon->lines }) {
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

# polygon must be simple (non complex) and ccw
sub polygon_is_convex {
    my ($points) = @_;
    for (my $i = 0; $i <= $#$points; $i++) {
        my $angle = angle3points($points->[$i-1], $points->[$i-2], $points->[$i]);
        return 0 if $angle < PI;
    }
    return 1;
}

sub rotate_points {
    my ($radians, $center, @points) = @_;
    $center //= [0,0];
    return map {
        [
            $center->[X] + cos($radians) * ($_->[X] - $center->[X]) - sin($radians) * ($_->[Y] - $center->[Y]),
            $center->[Y] + cos($radians) * ($_->[Y] - $center->[Y]) + sin($radians) * ($_->[X] - $center->[X]),
        ]
    } @points;
}

sub move_points {
    my ($shift, @points) = @_;
    return map {
        my @p = @$_;
        Slic3r::Point->new($shift->[X] + $p[X], $shift->[Y] + $p[Y]);
    } @points;
}

sub move_points_3D {
    my ($shift, @points) = @_;
    return map [
        $shift->[X] + $_->[X],
        $shift->[Y] + $_->[Y],
        $shift->[Z] + $_->[Z],
    ], @points;
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

sub normalize {
    my ($line) = @_;
    
    my $len = sqrt( ($line->[X]**2) + ($line->[Y]**2) + ($line->[Z]**2) )
        or return [0, 0, 0];  # to avoid illegal division by zero
    return [ map $_ / $len, @$line ];
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
    
    my @x = map $_->x, @$points;
    my @y = map $_->y, @$points;    #,,
    my @bb = (undef, undef, undef, undef);
    for (0..$#x) {
        $bb[X1] = $x[$_] if !defined $bb[X1] || $x[$_] < $bb[X1];
        $bb[X2] = $x[$_] if !defined $bb[X2] || $x[$_] > $bb[X2];
        $bb[Y1] = $y[$_] if !defined $bb[Y1] || $y[$_] < $bb[Y1];
        $bb[Y2] = $y[$_] if !defined $bb[Y2] || $y[$_] > $bb[Y2];
    }
    
    return @bb[X1,Y1,X2,Y2];
}

sub bounding_box_center {
    my ($bounding_box) = @_;
    return Slic3r::Point->new(
        ($bounding_box->[X2] + $bounding_box->[X1]) / 2,
        ($bounding_box->[Y2] + $bounding_box->[Y1]) / 2,
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

sub douglas_peucker {
    my ($points, $tolerance) = @_;
    no warnings "recursion";
    
    my $results = [];
    my $dmax = 0;
    my $index = 0;
    for my $i (1..$#$points) {
        my $d = $points->[$i]->distance_to(Slic3r::Line->new($points->[0], $points->[-1]));
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

1;
