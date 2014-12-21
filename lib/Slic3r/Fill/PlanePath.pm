package Slic3r::Fill::PlanePath;
use Moo;

extends 'Slic3r::Fill::Base';
with qw(Slic3r::Fill::WithDirection);

use Slic3r::Geometry qw(scale X1 Y1 X2 Y2);
use Slic3r::Geometry::Clipper qw(intersection_pl);

sub multiplier () { 1 }

sub get_n {
    my $self = shift;
    my ($path, $bounding_box) = @_;
    
    my ($n_lo, $n_hi) = $path->rect_to_n_range(@$bounding_box);
    return ($n_lo .. $n_hi);
}

sub process_polyline {}

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons
    my $expolygon = $surface->expolygon->clone;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $flow = $params{flow};
    my $distance_between_lines = $flow->scaled_spacing / $params{density} * $self->multiplier;
    my $bounding_box = $expolygon->bounding_box;
    
    (ref $self) =~ /::([^:]+)$/;
    my $path = "Math::PlanePath::$1"->new;
    my @n = $self->get_n($path, [ map +($_ / $distance_between_lines), @{$bounding_box->min_point}, @{$bounding_box->max_point} ]);
    
    my $polyline = Slic3r::Polyline->new(
        map [ map {$_*$distance_between_lines} $path->n_to_xy($_) ], @n,
    );
    return {} if @$polyline <= 1;
    
    $self->process_polyline($polyline, $bounding_box);
    
    my @paths = @{intersection_pl([$polyline], \@$expolygon)};
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("fill.svg",
            polygons => $expolygon,
            polylines => [map $_->p, @paths],
        );
    }
    
    # paths must be rotated back
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return { flow => $flow }, @paths;
}


package Slic3r::Fill::ArchimedeanChords;
use Moo;
extends 'Slic3r::Fill::PlanePath';
use Math::PlanePath::ArchimedeanChords;


package Slic3r::Fill::Flowsnake;
use Moo;
extends 'Slic3r::Fill::PlanePath';
use Math::PlanePath::Flowsnake;
use Slic3r::Geometry qw(X);

# Sorry, this fill is currently broken.

sub process_polyline {
    my $self = shift;
    my ($polyline, $bounding_box) = @_;
    
    $_->[X] += $bounding_box->center->[X] for @$polyline;
}


package Slic3r::Fill::HilbertCurve;
use Moo;
extends 'Slic3r::Fill::PlanePath';
use Math::PlanePath::HilbertCurve;


package Slic3r::Fill::OctagramSpiral;
use Moo;
extends 'Slic3r::Fill::PlanePath';
use Math::PlanePath::OctagramSpiral;

sub multiplier () { sqrt(2) }



1;
