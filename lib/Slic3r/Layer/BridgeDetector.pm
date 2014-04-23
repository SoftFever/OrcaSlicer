package Slic3r::Layer::BridgeDetector;
use Moo;

use List::Util qw(first sum max);
use Slic3r::Geometry qw(PI unscale scaled_epsilon rad2deg epsilon);
use Slic3r::Geometry::Clipper qw(intersection_pl intersection_ex);

has 'expolygon'         => (is => 'ro', required => 1);
has 'lower_slices'      => (is => 'rw', required => 1);  # ExPolygons or ExPolygonCollection
has 'extrusion_width'   => (is => 'rw', required => 1);  # scaled
has 'resolution'        => (is => 'rw', default => sub { PI/36 });

has '_edges'            => (is => 'rw'); # Polylines representing the supporting edges
has '_anchors'          => (is => 'rw'); # ExPolygons
has 'angle'             => (is => 'rw');

sub BUILD {
    my ($self) = @_;
    
    # outset our bridge by an arbitrary amout; we'll use this outer margin
    # for detecting anchors
    my $grown = $self->expolygon->offset(+$self->extrusion_width);
    
    # detect what edges lie on lower slices
    $self->_edges(my $edges = []);
    foreach my $lower (@{$self->lower_slices}) {
        # turn bridge contour and holes into polylines and then clip them
        # with each lower slice's contour
        push @$edges, map @{$_->clip_as_polyline([$lower->contour])}, @$grown;
    }
    Slic3r::debugf "  bridge has %d support(s)\n", scalar(@$edges);
    
    # detect anchors as intersection between our bridge expolygon and the lower slices
    $self->_anchors(intersection_ex(
        $grown,
        [ map @$_, @{$self->lower_slices} ],
        1,  # safety offset required to avoid Clipper from detecting empty intersection while Boost actually found some @edges
    ));
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("bridge.svg",
            expolygons      => [ $self->expolygon ],
            red_expolygons  => $self->lower_slices,
            polylines       => $self->_edges,
        );
    }
}

sub detect_angle {
    my ($self) = @_;
    
    return undef if !@{$self->_edges};
    
    my @edges = @{$self->_edges};
    my $anchors = $self->_anchors;
    
    if (@edges == 2) {
        my @chords = map Slic3r::Line->new($_->[0], $_->[-1]), @edges;
        my @midpoints = map $_->midpoint, @chords;
        my $line_between_midpoints = Slic3r::Line->new(@midpoints);
        $self->angle($line_between_midpoints->direction);
    } elsif (@edges == 1 && !$edges[0][0]->coincides_with($edges[0][-1])) {
        # Don't use this logic if $edges[0] is actually a closed loop
        # TODO: this case includes both U-shaped bridges and plain overhangs;
        # we need a trapezoidation algorithm to detect the actual bridged area
        # and separate it from the overhang area.
        # in the mean time, we're treating as overhangs all cases where
        # our supporting edge is a straight line
        if (@{$edges[0]} > 2) {
            my $line = Slic3r::Line->new($edges[0]->[0], $edges[0]->[-1]);
            $self->angle($line->direction);
        }
    } elsif (@edges) {
        # Outset the bridge expolygon by half the amount we used for detecting anchors;
        # we'll use this one to clip our test lines and be sure that their endpoints
        # are inside the anchors and not on their contours leading to false negatives.
        my $clip_area = $self->expolygon->offset_ex(+$self->extrusion_width/2);
        
        if (@$anchors) {
            # we'll now try several directions using a rudimentary visibility check:
            # bridge in several directions and then sum the length of lines having both
            # endpoints within anchors
            my %directions_coverage     = ();  # angle => score
            my %directions_avg_length   = ();  # angle => score
            my $line_increment = $self->extrusion_width;
            for (my $angle = 0; $angle < PI; $angle += $self->resolution) {
                my $my_clip_area    = [ map $_->clone, @$clip_area ];
                my $my_anchors      = [ map $_->clone, @$anchors ];
                
                # rotate everything - the center point doesn't matter
                $_->rotate(-$angle, [0,0]) for @$my_clip_area, @$my_anchors;
            
                # generate lines in this direction
                my $bounding_box = Slic3r::Geometry::BoundingBox->new_from_points([ map @$_, map @$_, @$my_anchors ]);
            
                my @lines = ();
                for (my $y = $bounding_box->y_min; $y <= $bounding_box->y_max; $y+= $line_increment) {
                    push @lines, Slic3r::Polyline->new(
                        [$bounding_box->x_min, $y],
                        [$bounding_box->x_max, $y],
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
            
            # if no direction produced coverage, then there's no bridge direction
            return undef if !defined first { $_ > 0 } values %directions_coverage;
            
            # the best direction is the one causing most lines to be bridged (thus most coverage)
            # and shortest max line length
            my @sorted_directions = sort {
                my $cmp;
                my $coverage_diff = $directions_coverage{$a} - $directions_coverage{$b};
                if (abs($coverage_diff) < $self->extrusion_width) {
                    $cmp = $directions_avg_length{$b} <=> $directions_avg_length{$a};
                } else {
                    $cmp = ($coverage_diff > 0) ? 1 : -1;
                }
                $cmp;
            } keys %directions_coverage;
            
            $self->angle($sorted_directions[-1]);
        }
    }
    
    if (defined $self->angle) {
        if ($self->angle >= PI) {
            $self->angle($self->angle - PI);
        }
        
        Slic3r::debugf "  Optimal infill angle is %d degrees\n", rad2deg($self->angle);
    }
    
    return $self->angle;
}

1;
