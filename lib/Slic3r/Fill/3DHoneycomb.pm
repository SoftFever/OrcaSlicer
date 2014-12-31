package Slic3r::Fill::3DHoneycomb;
use Moo;

extends 'Slic3r::Fill::Base';

use POSIX qw(ceil fmod);
use Slic3r::Geometry qw(scale scaled_epsilon);
use Slic3r::Geometry::Clipper qw(intersection_pl);

# require bridge flow since most of this pattern hangs in air
sub use_bridge_flow { 1 }

sub fill_surface {
    my ($self, $surface, %params) = @_;
    
    my $expolygon = $surface->expolygon;
    my $bb = $expolygon->bounding_box;
    my $size = $bb->size;
    
    my $distance = scale($self->spacing) / $params{density};
    
    # align bounding box to a multiple of our honeycomb grid module
    # (a module is 2*$distance since one $distance half-module is 
    # growing while the other $distance half-module is shrinking)
    {
        my $min = $bb->min_point;
        $min->translate(
            -($bb->x_min % (2*$distance)),
            -($bb->y_min % (2*$distance)),
        );
        $bb->merge_point($min);
    }
    
    # generate pattern
    my @polylines = map Slic3r::Polyline->new(@$_),
        makeGrid(
            scale($self->z),
            $distance,
            ceil($size->x / $distance) + 1,
            ceil($size->y / $distance) + 1,  #//
            (($self->layer_id / $surface->thickness_layers) % 2) + 1,
        );
    
    # move pattern in place
    $_->translate($bb->x_min, $bb->y_min) for @polylines;
    
    # clip pattern to boundaries
    @polylines = @{intersection_pl(\@polylines, \@$expolygon)};
    
    # connect lines
    unless ($params{dont_connect} || !@polylines) {  # prevent calling leftmost_point() on empty collections
        my ($expolygon_off) = @{$expolygon->offset_ex(scaled_epsilon)};
        my $collection = Slic3r::Polyline::Collection->new(@polylines);
        @polylines = ();
        foreach my $polyline (@{$collection->chained_path_from($collection->leftmost_point, 0)}) {
            # try to append this polyline to previous one if any
            if (@polylines) {
                my $line = Slic3r::Line->new($polylines[-1]->last_point, $polyline->first_point);
                if ($line->length <= 1.5*$distance && $expolygon_off->contains_line($line)) {
                    $polylines[-1]->append_polyline($polyline);
                    next;
                }
            }
            
            # make a clone before $collection goes out of scope
            push @polylines, $polyline->clone;
        }
    }
    
    # TODO: return ExtrusionLoop objects to get better chained paths
    return @polylines;
}


=head1 DESCRIPTION

Creates a contiguous sequence of points at a specified height that make
up a horizontal slice of the edges of a space filling truncated
octahedron tesselation. The octahedrons are oriented so that the
square faces are in the horizontal plane with edges parallel to the X
and Y axes.

Credits: David Eccles (gringer).

=head2 makeGrid(z, gridSize, gridWidth, gridHeight, curveType)

Generate a set of curves (array of array of 2d points) that describe a
horizontal slice of a truncated regular octahedron with a specified
grid square size.

=cut

sub makeGrid {
    my ($z, $gridSize, $gridWidth, $gridHeight, $curveType) = @_;
    my $scaleFactor = $gridSize;
    my $normalisedZ = $z / $scaleFactor;
    my @points = makeNormalisedGrid($normalisedZ, $gridWidth, $gridHeight, $curveType);
    foreach my $lineRef (@points) {
        foreach my $pointRef (@$lineRef) {
            $pointRef->[0] *= $scaleFactor;
            $pointRef->[1] *= $scaleFactor;
        }
    }
    return @points;
}

=head1 FUNCTIONS
=cut

=head2 colinearPoints(offset, gridLength)

Generate an array of points that are in the same direction as the
basic printing line (i.e. Y points for columns, X points for rows)

Note: a negative offset only causes a change in the perpendicular
direction

=cut

sub colinearPoints {
    my ($offset, $baseLocation, $gridLength) = @_;
    
    my @points = ();
    push @points, $baseLocation - abs($offset/2);
    for (my $i = 0; $i < $gridLength; $i++) {
        push @points, $baseLocation + $i + abs($offset/2);
        push @points, $baseLocation + ($i+1) - abs($offset/2);
    }
    push @points, $baseLocation + $gridLength + abs($offset/2);
    return @points;
}

=head2 colinearPoints(offset, baseLocation, gridLength)

Generate an array of points for the dimension that is perpendicular to
the basic printing line (i.e. X points for columns, Y points for rows)

=cut

sub perpendPoints {
    my ($offset, $baseLocation, $gridLength) = @_;
    
    my @points = ();
    my $side = 2*(($baseLocation) % 2) - 1;
    push @points, $baseLocation - $offset/2 * $side;
    for (my $i = 0; $i < $gridLength; $i++) {
        $side = 2*(($i+$baseLocation) % 2) - 1;
        push @points, $baseLocation + $offset/2 * $side;
        push @points, $baseLocation + $offset/2 * $side;
    }
    push @points, $baseLocation - $offset/2 * $side;
    
    return @points;
}

=head2 trim(pointArrayRef, minX, minY, maxX, maxY)

Trims an array of points to specified rectangular limits. Point
components that are outside these limits are set to the limits.

=cut

sub trim {
    my ($pointArrayRef, $minX, $minY, $maxX, $maxY) = @_;
  
    foreach (@$pointArrayRef) {
        $_->[0] = ($_->[0] < $minX) ? $minX : (($_->[0] > $maxX) ? $maxX : $_->[0]);
        $_->[1] = ($_->[1] < $minY) ? $minY : (($_->[1] > $maxY) ? $maxY : $_->[1]);
    }
}

=head2 makeNormalisedGrid(z, gridWidth, gridHeight, curveType)

Generate a set of curves (array of array of 2d points) that describe a
horizontal slice of a truncated regular octahedron with edge length 1.

curveType specifies which lines to print, 1 for vertical lines
(columns), 2 for horizontal lines (rows), and 3 for both.

=cut

sub makeNormalisedGrid {
    my ($z, $gridWidth, $gridHeight, $curveType) = @_;
    
    ## offset required to create a regular octagram
    my $octagramGap = 0.5;
    
    # sawtooth wave function for range f($z) = [-$octagramGap .. $octagramGap]
    my $a = sqrt(2);  # period
    my $wave = abs(fmod($z, $a) - $a/2)/$a*4 - 1;
    my $offset = $wave * $octagramGap;
    
    my @points = ();
    if (($curveType & 1) != 0) {
        for (my $x = 0; $x <= $gridWidth; $x++) {
            my @xPoints = perpendPoints($offset, $x, $gridHeight);
            my @yPoints = colinearPoints($offset, 0, $gridHeight);
            # This is essentially @newPoints = zip(@xPoints, @yPoints)
            my @newPoints = map [ $xPoints[$_], $yPoints[$_] ], 0..$#xPoints;
            
            # trim points to grid edges
            #trim(\@newPoints, 0, 0, $gridWidth, $gridHeight);
            
            if ($x % 2 == 0){
                push @points, [ @newPoints ];
            } else {
                push @points, [ reverse @newPoints ];
            }
        }
    }
    if (($curveType & 2) != 0) {
        for (my $y = 0; $y <= $gridHeight; $y++) {
            my @xPoints = colinearPoints($offset, 0, $gridWidth);
            my @yPoints = perpendPoints($offset, $y, $gridWidth);
            my @newPoints = map [ $xPoints[$_], $yPoints[$_] ], 0..$#xPoints;
            
            # trim points to grid edges
            #trim(\@newPoints, 0, 0, $gridWidth, $gridHeight);
            
            if ($y % 2 == 0) {
                push @points, [ @newPoints ];
            } else {
                push @points, [ reverse @newPoints ];
            }
        }
    }
    return @points;
}

1;
