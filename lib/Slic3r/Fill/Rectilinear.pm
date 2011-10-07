package Slic3r::Fill::Rectilinear;
use Moo;

extends 'Slic3r::Fill::Base';

use constant X1 => 0;
use constant Y1 => 1;
use constant X2 => 2;
use constant Y2 => 3;

use XXX;

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons so that we can work with vertical lines here
    my $polygons = [ $surface->p ];
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($polygons, $rotate_vector);
    
    my $bounding_box = [ Slic3r::Geometry::bounding_box(map @$_, $polygons) ];
    my $surface_width  = $bounding_box->[X2] - $bounding_box->[X1];
    my $surface_height = $bounding_box->[Y2] - $bounding_box->[Y1];
    
    my $distance_between_lines = $Slic3r::flow_width / $Slic3r::resolution / $params{density};
    
    my @paths = ();
    my $x = $bounding_box->[X1];
    while ($x < $bounding_box->[X2]) {
        my $vertical_line = [ [$x, $bounding_box->[Y2]], [$x, $bounding_box->[Y1]] ];
        push @paths, @{ Slic3r::Geometry::clip_segment_complex_polygon($vertical_line, $polygons) };
        $x += int($distance_between_lines);
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return @paths;
}

1;
