package Slic3r::Layer::BridgeDetector;
use Moo;

use List::Util qw(first sum);
use Slic3r::Geometry qw(PI);
use Slic3r::Geometry::Clipper qw(intersection_pl intersection_ex);

has 'lower_slices'      => (is => 'ro', required => 1);  # ExPolygons or ExPolygonCollection
has 'perimeter_flow'    => (is => 'ro', required => 1);
has 'infill_flow'       => (is => 'ro', required => 1);

sub detect_angle {
    my ($self, $expolygon) = @_;
    
    my $grown = $expolygon->offset(+$self->perimeter_flow->scaled_width);
    my @lower = @{$self->lower_slices};       # expolygons
    
    # detect what edges lie on lower slices
    my @edges = (); # polylines
    foreach my $lower (@lower) {
        # turn bridge contour and holes into polylines and then clip them
        # with each lower slice's contour
        my @clipped = @{intersection_pl([ map $_->split_at_first_point, @$grown ], [$lower->contour])};
        if (@clipped == 2) {
            # If the split_at_first_point() call above happens to split the polygon inside the clipping area
            # we would get two consecutive polylines instead of a single one, so we use this ugly hack to 
            # recombine them back into a single one in order to trigger the @edges == 2 logic below.
            # This needs to be replaced with something way better.
            if (points_coincide($clipped[0][0], $clipped[-1][-1])) {
                @clipped = (Slic3r::Polyline->new(@{$clipped[-1]}, @{$clipped[0]}));
            }
            if (points_coincide($clipped[-1][0], $clipped[0][-1])) {
                @clipped = (Slic3r::Polyline->new(@{$clipped[0]}, @{$clipped[1]}));
            }
        }
        push @edges, @clipped;
    }
    
    Slic3r::debugf "  bridge has %d support(s)\n", scalar(@edges);
    return undef if !@edges;
    
    my $bridge_angle = undef;
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge_$expolygon.svg",
            expolygons      => [ $expolygon ],
            red_expolygons  => [ @lower ],
            polylines       => [ @edges ],
        );
    }
    
    if (@edges == 2) {
        my @chords = map Slic3r::Line->new($_->[0], $_->[-1]), @edges;
        my @midpoints = map $_->midpoint, @chords;
        my $line_between_midpoints = Slic3r::Line->new(@midpoints);
        $bridge_angle = Slic3r::Geometry::rad2deg_dir($line_between_midpoints->direction);
    } elsif (@edges == 1) {
        # TODO: this case includes both U-shaped bridges and plain overhangs;
        # we need a trapezoidation algorithm to detect the actual bridged area
        # and separate it from the overhang area.
        # in the mean time, we're treating as overhangs all cases where
        # our supporting edge is a straight line
        if (@{$edges[0]} > 2) {
            my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
            $bridge_angle = Slic3r::Geometry::rad2deg_dir($line->direction);
        }
    } elsif (@edges) {
        # inset the bridge expolygon; we'll use this one to clip our test lines
        my $inset = $expolygon->offset_ex($self->infill_flow->scaled_width);
        
        # detect anchors as intersection between our bridge expolygon and the lower slices
        my $anchors = intersection_ex(
            $grown,
            [ map @$_, @lower ],
            1,  # safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some @edges
        );
        
        if (@$anchors) {
            # we'll now try several directions using a rudimentary visibility check:
            # bridge in several directions and then sum the length of lines having both
            # endpoints within anchors
            my %directions = ();  # angle => score
            my $angle_increment = PI/36; # 5°
            my $line_increment = $self->infill_flow->scaled_width;
            for (my $angle = 0; $angle <= PI; $angle += $angle_increment) {
                # rotate everything - the center point doesn't matter
                $_->rotate($angle, [0,0]) for @$inset, @$anchors;
            
                # generate lines in this direction
                my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_points([ map @$_, map @$_, @$anchors ]);
            
                my @lines = ();
                for (my $x = $bounding_box->x_min; $x <= $bounding_box->x_max; $x += $line_increment) {
                    push @lines, Slic3r::Polyline->new([$x, $bounding_box->y_min], [$x, $bounding_box->y_max]);
                }
            
                my @clipped_lines = map Slic3r::Line->new(@$_), @{ intersection_pl(\@lines, [ map @$_, @$inset ]) };
            
                # remove any line not having both endpoints within anchors
                # NOTE: these calls to contains_point() probably need to check whether the point 
                # is on the anchor boundaries too
                @clipped_lines = grep {
                    my $line = $_;
                    !(first { $_->contains_point($line->a) } @$anchors)
                        && !(first { $_->contains_point($line->b) } @$anchors);
                } @clipped_lines;
            
                # sum length of bridged lines
                $directions{-$angle} = sum(map $_->length, @clipped_lines) // 0;
            }
        
            # this could be slightly optimized with a max search instead of the sort
            my @sorted_directions = sort { $directions{$a} <=> $directions{$b} } keys %directions;
    
            # the best direction is the one causing most lines to be bridged
            $bridge_angle = Slic3r::Geometry::rad2deg_dir($sorted_directions[-1]);
        }
    }
    
    Slic3r::debugf "  Optimal infill angle is %d degrees\n", $bridge_angle
        if defined $bridge_angle;
    
    return $bridge_angle;
}

1;
