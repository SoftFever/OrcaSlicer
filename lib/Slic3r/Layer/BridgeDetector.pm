package Slic3r::Layer::BridgeDetector;
use Moo;

use List::Util qw(first sum max);
use Slic3r::Geometry qw(PI unscale scaled_epsilon rad2deg epsilon);
use Slic3r::Geometry::Clipper qw(intersection_pl intersection_ex);

has 'lower_slices'      => (is => 'rw', required => 1);  # ExPolygons or ExPolygonCollection
has 'infill_flow'       => (is => 'rw', required => 1);
has 'resolution'        => (is => 'rw', default => sub { PI/36 });

sub detect_angle {
    my ($self, $expolygon) = @_;
    
    my $anchors_offset = $self->infill_flow->scaled_width;
    
    my $grown = $expolygon->offset(+$anchors_offset);
    my @lower = @{$self->lower_slices};       # expolygons
    
    # detect what edges lie on lower slices
    my @edges = (); # polylines
    foreach my $lower (@lower) {
        # turn bridge contour and holes into polylines and then clip them
        # with each lower slice's contour
        push @edges, map @{$_->clip_as_polyline([$lower->contour])}, @$grown;
    }
    
    Slic3r::debugf "  bridge has %d support(s)\n", scalar(@edges);
    return undef if !@edges;
    
    my $bridge_angle = undef;
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge.svg",
            expolygons      => [ $expolygon ],
            red_expolygons  => [ @lower ],
            polylines       => [ @edges ],
        );
    }
    
    if (@edges == 2) {
        my @chords = map Slic3r::Line->new($_->[0], $_->[-1]), @edges;
        my @midpoints = map $_->midpoint, @chords;
        my $line_between_midpoints = Slic3r::Line->new(@midpoints);
        $bridge_angle = $line_between_midpoints->direction;
    } elsif (@edges == 1 && !$edges[0][0]->coincides_with($edges[0][-1])) {
        # Don't use this logic if $edges[0] is actually a closed loop
        # TODO: this case includes both U-shaped bridges and plain overhangs;
        # we need a trapezoidation algorithm to detect the actual bridged area
        # and separate it from the overhang area.
        # in the mean time, we're treating as overhangs all cases where
        # our supporting edge is a straight line
        if (@{$edges[0]} > 2) {
            my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
            $bridge_angle = $line->direction;
        }
    } elsif (@edges) {
        # Outset the bridge expolygon by half the amount we used for detecting anchors;
        # we'll use this one to clip our test lines and be sure that their endpoints
        # are inside the anchors and not on their contours leading to false negatives.
        my $clip_area = $expolygon->offset_ex(+$anchors_offset/2);
        
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
            my %directions_coverage     = ();  # angle => score
            my %directions_avg_length   = ();  # angle => score
            my $line_increment = $self->infill_flow->scaled_width;
            for (my $angle = 0; $angle < PI; $angle += $self->resolution) {
                my $my_clip_area    = [ map $_->clone, @$clip_area ];
                my $my_anchors      = [ map $_->clone, @$anchors ];
                
                # rotate everything - the center point doesn't matter
                $_->rotate($angle, [0,0]) for @$my_clip_area, @$my_anchors;
            
                # generate lines in this direction
                my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_points([ map @$_, map @$_, @$my_anchors ]);
            
                my @lines = ();
                for (my $x = $bounding_box->x_min; $x <= $bounding_box->x_max; $x += $line_increment) {
                    push @lines, Slic3r::Polyline->new(
                        [$x, $bounding_box->y_min + scaled_epsilon],
                        [$x, $bounding_box->y_max - scaled_epsilon],
                    );
                }
                
                my @clipped_lines = map Slic3r::Line->new(@$_), @{ intersection_pl(\@lines, [ map @$_, @$my_clip_area ]) };
                
                # remove any line not having both endpoints within anchors
                # NOTE: these calls to contains_point() probably need to check whether the point 
                # is on the anchor boundaries too
                @clipped_lines = grep {
                    my $line = $_;
                    (first { $_->contains_point($line->a) } @$my_anchors)
                        && (first { $_->contains_point($line->b) } @$my_anchors);
                } @clipped_lines;
                
                my @lengths = map $_->length, @clipped_lines;
                
                # sum length of bridged lines
                $directions_coverage{$angle} = sum(@lengths) // 0;
            
                # max length of bridged lines
                $directions_avg_length{$angle} = @lengths ? (max(@lengths)) : -1;
            }
            
            # the best direction is the one causing most lines to be bridged (thus most coverage)
            # and shortest max line length
            my @sorted_directions = sort {
                my $cmp;
                my $coverage_diff = $directions_coverage{$a} - $directions_coverage{$b};
                if (abs($coverage_diff) < $self->infill_flow->scaled_width) {
                    $cmp = $directions_avg_length{$b} <=> $directions_avg_length{$a};
                } else {
                    $cmp = ($coverage_diff > 0) ? 1 : -1;
                }
                $cmp;
            } keys %directions_coverage;
            
            $bridge_angle = $sorted_directions[-1];
        }
    }
    
    if (defined $bridge_angle) {
        if ($bridge_angle >= PI - epsilon) {
            $bridge_angle -= PI;
        }
        
        Slic3r::debugf "  Optimal infill angle is %d degrees\n", rad2deg($bridge_angle)
            if defined $bridge_angle;
    }
    
    return $bridge_angle;
}

1;
