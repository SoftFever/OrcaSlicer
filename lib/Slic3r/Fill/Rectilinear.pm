package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(X1 Y1 X2 Y2 A B X Y scale unscale epsilon);
use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my ($expolygon_off) = $expolygon->offset_ex(scale 0.2);
    my $bounding_box = [ $expolygon_off->bounding_box ];
    
    my $min_spacing = scale $params{flow_spacing};
    my $distance_between_lines = $min_spacing / $params{density};
    my $line_oscillation = $distance_between_lines - $min_spacing;
    
    my $flow_spacing;
    if ($params{density} == 1) {
        $distance_between_lines = $self->adjust_solid_spacing(
            width       => $bounding_box->[X2] - $bounding_box->[X1],
            distance    => $distance_between_lines,
        );
        $flow_spacing = unscale $distance_between_lines;
    }
    
    my $overlap_distance = scale $Slic3r::flow_width * 0.4;
    
    my @paths = ();
    my $x = $bounding_box->[X1];
    my $is_line_pattern = $self->isa('Slic3r::Fill::Line');
    for (my $i = 0; $x <= $bounding_box->[X2] + scale epsilon; $i++) {
        my $vertical_line = Slic3r::Line->new([$x, $bounding_box->[Y2]], [$x, $bounding_box->[Y1]]);
        if ($is_line_pattern && $i % 2) {
            $vertical_line->[A][X] += $line_oscillation;
            $vertical_line->[B][X] -= $line_oscillation;
        }
        my @clipped_lines = @{ $expolygon->clip_line($vertical_line) };
        for (@clipped_lines) {
            $_->[0][Y] += $overlap_distance;
            $_->[-1][Y] -= $overlap_distance;
        }
        push @paths, @clipped_lines;
        $x += $distance_between_lines;
    }
    
    # connect lines
    {
        my $collection = Slic3r::ExtrusionPath::Collection->new(
            paths => [ map Slic3r::ExtrusionPath->new(polyline => Slic3r::Polyline->new(@$_), role => 'bogus'), @paths ],
        );
        @paths = ();
        
        my $can_connect =
            $is_line_pattern ? sub {
                ($_[X] >= ($distance_between_lines - $line_oscillation) - epsilon) && ($_[X] <= ($distance_between_lines + $line_oscillation) + epsilon)
                    && abs($_[Y]) <= $distance_between_lines * 5
            } : sub {
                ($_[X] >= $distance_between_lines - epsilon) && ($_[X] <= $distance_between_lines + epsilon)   # $_[X] == $distance_between_lines +/- epsilon
                    && abs($_[Y]) <= $distance_between_lines * 5
            };
        
        foreach my $path ($collection->shortest_path) {
            if (@paths) {
                my @distance = map +($path->points->[0][$_] - $paths[-1][-1][$_]), (X,Y);
                
                # TODO: we should also check that both points are on a fill_boundary to avoid 
                # connecting paths on the boundaries of internal regions
                if ($can_connect->(@distance, $paths[-1][-1], $path->points->[0])
                    && $expolygon_off->encloses_line([ $paths[-1][-1], $path->points->[0] ])) {
                    push @{$paths[-1]}, @{$path->points};
                    next;
                }
            }
            push @paths, $path->points;
        }
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return { flow_spacing => $flow_spacing }, @paths;
}

1;
