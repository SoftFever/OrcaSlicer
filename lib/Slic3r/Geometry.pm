package Slic3r::Geometry;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);

# Exported by this module. The last section starting with convex_hull is exported by Geometry.xsp
our @EXPORT_OK = qw(
    PI epsilon 

    angle3points
    collinear
    dot
    line_intersection
    normalize
    point_in_segment
    polyline_lines
    polygon_is_convex
    polygon_segment_having_point
    scale
    unscale
    scaled_epsilon
    size_2D

    X Y Z
    convex_hull
    chained_path_from
    deg2rad
    rad2deg
    rad2deg_dir
);

use constant PI => 4 * atan2(1, 1);
use constant A => 0;
use constant B => 1;
use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;

sub epsilon () { 1E-4 }
sub scaled_epsilon () { epsilon / &Slic3r::SCALING_FACTOR }

sub scale   ($) { $_[0] / &Slic3r::SCALING_FACTOR }
sub unscale ($) { $_[0] * &Slic3r::SCALING_FACTOR }

# used by geometry.t, polygon_segment_having_point
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

# used by geometry.t
sub polyline_lines {
    my ($polyline) = @_;
    my @points = @$polyline;
    return map Slic3r::Line->new(@points[$_, $_+1]), 0 .. $#points-1;
}

# given a $polygon, return the (first) segment having $point
# used by geometry.t
sub polygon_segment_having_point {
    my ($polygon, $point) = @_;
    
    foreach my $line (@{ $polygon->lines }) {
        return $line if point_in_segment($point, $line);
    }
    return undef;
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

sub normalize {
    my ($line) = @_;
    
    my $len = sqrt( ($line->[X]**2) + ($line->[Y]**2) + ($line->[Z]**2) )
        or return [0, 0, 0];  # to avoid illegal division by zero
    return [ map $_ / $len, @$line ];
}

# 2D dot product
# used by 3DScene.pm
sub dot {
    my ($u, $v) = @_;
    return $u->[X] * $v->[X] + $u->[Y] * $v->[Y];
}

sub line_intersection {
    my ($line1, $line2, $require_crossing) = @_;
    $require_crossing ||= 0;
    
    my $intersection = _line_intersection(map @$_, @$line1, @$line2);
    return (ref $intersection && $intersection->[1] == $require_crossing) 
        ? $intersection->[0] 
        : undef;
}

# Used by test cases.
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

# used by ExPolygon::size
sub size_2D {
    my @bounding_box = bounding_box(@_);
    return (
        ($bounding_box[X2] - $bounding_box[X1]),
        ($bounding_box[Y2] - $bounding_box[Y1]),
    );
}

# Used by sub collinear, which is used by test cases.
# bounding_box_intersect($d, @a, @b)
#   Return true if the given bounding boxes @a and @b intersect
#   in $d dimensions.  Used by sub collinear.
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

# Used by test cases.
# this assumes a CCW rotation from $p2 to $p3 around $p1
sub angle3points {
    my ($p1, $p2, $p3) = @_;
    # p1 is the center
    
    my $angle = atan2($p2->[X] - $p1->[X], $p2->[Y] - $p1->[Y])
              - atan2($p3->[X] - $p1->[X], $p3->[Y] - $p1->[Y]);
    
    # we only want to return only positive angles
    return $angle <= 0 ? $angle + 2*PI() : $angle;
}

1;
