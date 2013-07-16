package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

has 'cache'         => (is => 'rw', default => sub {{}});

use Slic3r::Geometry qw(A B X Y scale unscale scaled_epsilon);

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon->clone;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $expolygon_off = $expolygon->offset_ex(scale $params{flow_spacing}/2)->[0];
    return {} if !defined $expolygon_off;  # skip some very small polygons (which shouldn't arrive here)
    
    my $flow_spacing = $params{flow_spacing};
    my $min_spacing = scale $params{flow_spacing};
    my $distance_between_lines = $min_spacing / $params{density};
    my $line_oscillation = $distance_between_lines - $min_spacing;
    my $is_line_pattern = $self->isa('Slic3r::Fill::Line');
    
    my $cache_id = sprintf "d%s_s%.2f_a%.2f",
        $params{density}, $params{flow_spacing}, $rotate_vector->[0][0];
    
    if (!$self->cache->{$cache_id}) {
        # compute bounding box
        my $bounding_box;
        {
            my $bb_polygon = $self->bounding_box->polygon;
            $bb_polygon->scale(sqrt 2);
            $self->rotate_points($bb_polygon, $rotate_vector);
            $bounding_box = $bb_polygon->bounding_box;
        }
        
        # define flow spacing according to requested density
        if ($params{density} == 1 && !$params{dont_adjust}) {
            $distance_between_lines = $self->adjust_solid_spacing(
                width       => $bounding_box->size->[X],
                distance    => $distance_between_lines,
            );
            $flow_spacing = unscale $distance_between_lines;
        }
        
        # generate the basic pattern
        my $x = $bounding_box->x_min;
        my @vertical_lines = ();
        for (my $i = 0; $x <= $bounding_box->x_max + scaled_epsilon; $i++) {
            my $vertical_line = Slic3r::Line->new([$x, $bounding_box->y_max], [$x, $bounding_box->y_min]);
            if ($is_line_pattern && $i % 2) {
                $vertical_line->[A][X] += $line_oscillation;
                $vertical_line->[B][X] -= $line_oscillation;
            }
            push @vertical_lines, $vertical_line;
            $x += $distance_between_lines;
        }
        
        $self->cache->{$cache_id} = [@vertical_lines];
    }
    
    # clip paths against a slightly offsetted expolygon, so that the first and last paths
    # are kept even if the expolygon has vertical sides
    my @polylines = map Slic3r::Polyline->new(@$_),
        @{ Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection(
            [ map $_->pp, @{ $expolygon->offset_ex(scaled_epsilon) } ],
            [ map $_->pp, @{ $self->cache->{$cache_id} } ],
        ) };
    
    # connect lines
    unless ($params{dont_connect}) {
        my $collection = Slic3r::Polyline::Collection->new(
            polylines => [ @polylines ],
        );
        @polylines = ();
        
        my $tolerance = 10 * scaled_epsilon;
        my $diagonal_distance = $distance_between_lines * 2;
        my $can_connect = $is_line_pattern
            ? sub {
                ($_[X] >= ($distance_between_lines - $line_oscillation) - $tolerance) && ($_[X] <= ($distance_between_lines + $line_oscillation) + $tolerance)
                    && $_[Y] <= $diagonal_distance
            }
            : sub { $_[X] <= $diagonal_distance && $_[Y] <= $diagonal_distance };
        
        foreach my $polyline ($collection->chained_path) {
            if (@polylines) {
                my $last_point = $polylines[-1][-1]->pp;
                my @distance = map abs($polyline->[0][$_] - $last_point->[$_]), (X,Y);
                
                # TODO: we should also check that both points are on a fill_boundary to avoid 
                # connecting paths on the boundaries of internal regions
                if ($can_connect->(@distance) && $expolygon_off->encloses_line(Slic3r::Line->new($last_point, $polyline->[0]), $tolerance)) {
                    $polylines[-1]->append(@$polyline);
                    next;
                }
            }
            push @polylines, $polyline;
        }
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@polylines, $rotate_vector);
    
    return { flow_spacing => $flow_spacing }, @polylines;
}

1;
