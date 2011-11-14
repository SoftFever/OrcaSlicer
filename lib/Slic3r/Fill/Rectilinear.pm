package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(X1 Y1 X2 Y2 A B X);
use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $bounding_box = [ $expolygon->bounding_box ];
    my $flow_width_res = $params{flow_width} / $Slic3r::resolution;
    my $distance_between_lines = $flow_width_res / $params{density};
    
    my @paths = ();
    my $x = $bounding_box->[X1];
    my $is_line_pattern = $self->isa('Slic3r::Fill::Line');
    my $i = 0;
    while ($x < $bounding_box->[X2]) {
        my $vertical_line = [ [$x, $bounding_box->[Y2]], [$x, $bounding_box->[Y1]] ];
        if ($is_line_pattern && $i % 2) {
            $vertical_line->[A][X] -= ($distance_between_lines - $flow_width_res);
            $vertical_line->[B][X] += ($distance_between_lines - $flow_width_res);
        }
        push @paths, @{ $expolygon->clip_line($vertical_line) };
        $x += int($distance_between_lines);
        $i++;
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return @paths;
}

1;
