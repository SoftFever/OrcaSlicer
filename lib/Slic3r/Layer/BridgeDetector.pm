package Slic3r::Layer::BridgeDetector;
use Moo;

use List::Util qw(first sum max min);
use Slic3r::Geometry qw(PI unscale scaled_epsilon rad2deg epsilon directions_parallel_within);
use Slic3r::Geometry::Clipper qw(intersection_pl intersection_ex union offset diff_pl union_ex
    intersection_ppl);

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
        push @$edges, @{intersection_ppl($grown, [ $lower->contour ])};
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
    
    if (!@$anchors) {
        $self->angle(undef);
        return undef;
    }
    
    # Outset the bridge expolygon by half the amount we used for detecting anchors;
    # we'll use this one to clip our test lines and be sure that their endpoints
    # are inside the anchors and not on their contours leading to false negatives.
    my $clip_area = $self->expolygon->offset_ex(+$self->extrusion_width/2);
    
    # we'll now try several directions using a rudimentary visibility check:
    # bridge in several directions and then sum the length of lines having both
    # endpoints within anchors
    
    # we test angles according to configured resolution
    my @angles = map { $_*$self->resolution } 0..(PI/$self->resolution);
    
    # we also test angles of each bridge contour
    push @angles, map $_->direction, map @{$_->lines}, @{$self->expolygon};
    
    # we also test angles of each open supporting edge
    # (this finds the optimal angle for C-shaped supports)
    push @angles,
        map Slic3r::Line->new($_->first_point, $_->last_point)->direction,
        grep { !$_->first_point->coincides_with($_->last_point) }
        @edges;
    
    # remove duplicates
    my $min_resolution = PI/180; # 1 degree
    # proceed in reverse order so that when we compare first value with last one (-1)
    # we remove the greatest one (PI) in case they are parallel (PI, 0)
    @angles = reverse sort @angles;
    for (my $i = 0; $i <= $#angles; ++$i) {
        if (directions_parallel_within($angles[$i], $angles[$i-1], $min_resolution)) {
            splice @angles, $i, 1;
            --$i;
        }
    }
    
    my %directions_coverage     = ();  # angle => score
    my %directions_avg_length   = ();  # angle => score
    my $line_increment = $self->extrusion_width;
    my %unique_angles = map { $_ => 1 } @angles;
    for my $angle (@angles) {
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
        @clipped_lines = grep {
            my $line = $_;
            (first { $_->contains_point($line->a) } @$my_anchors)
                && (first { $_->contains_point($line->b) } @$my_anchors);
        } @clipped_lines;
        
        my @lengths = map $_->length, @clipped_lines;
        
        # sum length of bridged lines
        $directions_coverage{$angle} = sum(@lengths) // 0;
        
        ### The following produces more correct results in some cases and more broken in others.
        ### TODO: investigate, as it looks more reliable than line clipping.
        ###$directions_coverage{$angle} = sum(map $_->area, @{$self->coverage($angle)}) // 0;
        
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
    
    if ($self->angle >= PI) {
        $self->angle($self->angle - PI);
    }
    
    Slic3r::debugf "  Optimal infill angle is %d degrees\n", rad2deg($self->angle);
    
    return $self->angle;
}

sub coverage {
    my ($self, $angle) = @_;
    
    if (!defined $angle) {
        return [] if !defined($angle = $self->angle);
    }
    
    # Clone our expolygon and rotate it so that we work with vertical lines.
    my $expolygon = $self->expolygon->clone;
    $expolygon->rotate(PI/2 - $angle, [0,0]);
    
    # Outset the bridge expolygon by half the amount we used for detecting anchors;
    # we'll use this one to generate our trapezoids and be sure that their vertices
    # are inside the anchors and not on their contours leading to false negatives.
    my $grown = $expolygon->offset_ex(+$self->extrusion_width/2);
    
    # Compute trapezoids according to a vertical orientation
    my $trapezoids = [ map @{$_->get_trapezoids2(PI/2)}, @$grown ];
    
    # get anchors and rotate them too
    my $anchors = [ map $_->clone, @{$self->_anchors} ];
    $_->rotate(PI/2 - $angle, [0,0]) for @$anchors;
    
    my @covered = ();  # polygons
    foreach my $trapezoid (@$trapezoids) {
        my @polylines = map $_->as_polyline, @{$trapezoid->lines};
        my @supported = @{intersection_pl(\@polylines, [map @$_, @$anchors])};
        
        # not nice, we need a more robust non-numeric check
        @supported = grep $_->length >= $self->extrusion_width, @supported;
        
        if (@supported >= 2) {
            push @covered, $trapezoid;
        }
    }
    
    # merge trapezoids and rotate them back
    my $coverage = union(\@covered);
    $_->rotate(-(PI/2 - $angle), [0,0]) for @$coverage;
    
    # intersect trapezoids with actual bridge area to remove extra margins
    $coverage = intersection_ex($coverage, [ @{$self->expolygon} ]);
    
    if (0) {
        my @lines = map @{$_->lines}, @$trapezoids;
        $_->rotate(-(PI/2 - $angle), [0,0]) for @lines;
        
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "coverage_" . rad2deg($angle) . ".svg",
            expolygons          => [$self->expolygon],
            green_expolygons    => $self->_anchors,
            red_expolygons      => $coverage,
            lines               => \@lines,
        );
    }
    
    return $coverage;
}

# this method returns the bridge edges (as polylines) that are not supported
# but would allow the entire bridge area to be bridged with detected angle
# if supported too
sub unsupported_edges {
    my ($self, $angle) = @_;
    
    if (!defined $angle) {
        return [] if !defined($angle = $self->angle);
    }
    
    # get bridge edges (both contour and holes)
    my @bridge_edges = map $_->split_at_first_point, @{$self->expolygon};
    $_->[0]->translate(1,0) for @bridge_edges;  # workaround for Clipper bug, see comments in Slic3r::Polygon::clip_as_polyline()
    
    # get unsupported edges
    my $grown_lower = offset([ map @$_, @{$self->lower_slices} ], +$self->extrusion_width);
    my $unsupported = diff_pl(
        \@bridge_edges,
        $grown_lower,
    );
    
    # split into individual segments and filter out edges parallel to the bridging angle
    # TODO: angle tolerance should probably be based on segment length and flow width,
    # so that we build supports whenever there's a chance that at least one or two bridge
    # extrusions would be anchored within such length (i.e. a slightly non-parallel bridging
    # direction might still benefit from anchors if long enough)
    my $angle_tolerance = PI/180*5;
    @$unsupported = map $_->as_polyline,
        grep !directions_parallel_within($_->direction, $angle, $angle_tolerance),
        map @{$_->lines},
        @$unsupported;
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output(
            "unsupported_" . rad2deg($angle) . ".svg",
            expolygons          => [$self->expolygon],
            green_expolygons    => $self->_anchors,
            red_expolygons      => union_ex($grown_lower),
            no_arrows           => 1,
            polylines           => \@bridge_edges,
            red_polylines       => $unsupported,
        );
    }
    
    return $unsupported;
}

1;
