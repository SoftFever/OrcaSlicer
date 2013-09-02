package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

has 'cache'         => (is => 'rw', default => sub {{}});

use Slic3r::Geometry qw(A B X Y MIN scale unscale scaled_epsilon);

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon->clone;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $flow_spacing        = $params{flow_spacing};
    my $min_spacing         = scale $params{flow_spacing};
    my $line_spacing        = $min_spacing / $params{density};
    my $line_oscillation    = $line_spacing - $min_spacing;
    my $is_line_pattern     = $self->isa('Slic3r::Fill::Line');
    my $bounding_box        = $expolygon->bounding_box;
    
    # define flow spacing according to requested density
    if ($params{density} == 1 && !$params{dont_adjust}) {
        $line_spacing = $self->adjust_solid_spacing(
            width       => $bounding_box->size->[X],
            distance    => $line_spacing,
        );
        $flow_spacing = unscale $line_spacing;
    } else {
        # extend bounding box so that our pattern will be aligned with other layers
        $bounding_box->extents->[X][MIN] -= $bounding_box->x_min % $line_spacing;
        $bounding_box->extents->[Y][MIN] -= $bounding_box->y_min % $line_spacing;
    }
    
    # generate the basic pattern
    my $i               = 0;
    my $x               = $bounding_box->x_min;
    my $x_max           = $bounding_box->x_max + scaled_epsilon;
    my @vertical_lines  = ();
    while ($x <= $x_max) {
        my $vertical_line = [ [$x, $bounding_box->y_max], [$x, $bounding_box->y_min] ];
        if ($is_line_pattern && $i % 2) {
            $vertical_line->[A][X] += $line_oscillation;
            $vertical_line->[B][X] -= $line_oscillation;
        }
        push @vertical_lines, $vertical_line;
        $i++;
        $x += $line_spacing;
    }
    
    # clip paths against a slightly offsetted expolygon, so that the first and last paths
    # are kept even if the expolygon has vertical sides
    # the minimum offset for preventing edge lines from being clipped is scaled_epsilon;
    # however we use a larger offset to support expolygons with slightly skewed sides and 
    # not perfectly straight
    my @polylines = map Slic3r::Polyline->new(@$_),
        @{ Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection(
            [ map $_->pp, @{$expolygon->offset_ex($line_spacing*0.05)} ],
            [ @vertical_lines ],
        ) };
    
    # connect lines
    unless ($params{dont_connect}) {
        my ($expolygon_off) = @{$expolygon->offset_ex(scale $params{flow_spacing}/2)};
        my $collection = Slic3r::Polyline::Collection->new(@polylines);
        @polylines = ();
        
        my $tolerance = 10 * scaled_epsilon;
        my $diagonal_distance = $line_spacing * 2;
        my $can_connect = $is_line_pattern
            ? sub {
                ($_[X] >= ($line_spacing - $line_oscillation) - $tolerance) && ($_[X] <= ($line_spacing + $line_oscillation) + $tolerance)
                    && $_[Y] <= $diagonal_distance
            }
            : sub { $_[X] <= $diagonal_distance && $_[Y] <= $diagonal_distance };
        
        foreach my $polyline (@{$collection->chained_path(0)}) {
            if (@polylines) {
                my $first_point = $polyline->first_point;
                my $last_point = $polylines[-1]->last_point;
                my @distance = map abs($first_point->$_ - $last_point->$_), qw(x y);
                
                # TODO: we should also check that both points are on a fill_boundary to avoid 
                # connecting paths on the boundaries of internal regions
                if ($can_connect->(@distance) && $expolygon_off->encloses_line(Slic3r::Line->new($last_point, $first_point), $tolerance)) {
                    $polylines[-1]->append_polyline($polyline);
                    next;
                }
            }
            
            # make a clone before $collection goes out of scope
            push @polylines, $polyline->clone;
        }
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@polylines, $rotate_vector);
    
    return { flow_spacing => $flow_spacing }, @polylines;
}

1;
