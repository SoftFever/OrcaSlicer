package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

use Slic3r::Geometry qw(X1 Y1 X2 Y2);
use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $expolygon = $surface->expolygon;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $bounding_box = [ $expolygon->bounding_box ];
    my $distance_between_lines = $params{flow_width} / $Slic3r::resolution / $params{density};
    
    my @paths = ();
    my $x = $bounding_box->[X1];
    while ($x < $bounding_box->[X2]) {
        my $vertical_line = [ [$x, $bounding_box->[Y2]], [$x, $bounding_box->[Y1]] ];
        push @paths, @{ $expolygon->clip_line($vertical_line) };
        $x += int($distance_between_lines);
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return @paths;
}

1;
