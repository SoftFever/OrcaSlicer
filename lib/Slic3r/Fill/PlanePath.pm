package Slic3r::Fill::PlanePath;
use Moo;

extends 'Slic3r::Fill::Base';
with qw(Slic3r::Fill::WithDirection);

use Slic3r::Geometry qw(scale X1 Y1 X2 Y2);
use Slic3r::Geometry::Clipper qw(intersection_pl);

sub multiplier () { 1 }

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
    
    # since not all PlanePath infills extend in negative coordinate space,
    # move expolygon in positive coordinate space
    $expolygon->translate(-$bounding_box->x_min, -$bounding_box->y_min);
    
    (ref $self) =~ /::([^:]+)$/;
    my $path = "Math::PlanePath::$1"->new;
    
    my ($n_lo, $n_hi) = $path->rect_to_n_range(
        map { $_ / $distance_between_lines }
            0, 0,
            @{$bounding_box->size},
    );
    
    my $polyline = Slic3r::Polyline->new(
        map [ map { $_ * $distance_between_lines } $path->n_to_xy($_) ], ($n_lo..$n_hi)
    );
    return {} if @$polyline <= 1;
    
    $self->process_polyline($polyline, $bounding_box);
    
    my @paths = @{intersection_pl([$polyline], \@$expolygon)};
    
    if (0) {
        require "Slic3r/SVG.pm";
        Slic3r::SVG::output("fill.svg",
            no_arrows   => 1,
            polygons    => \@$expolygon,
            polylines   => \@paths,
        );
    }
    
    # paths must be repositioned and rotated back
    $_->translate($bounding_box->x_min, $bounding_box->y_min) for @paths;
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
