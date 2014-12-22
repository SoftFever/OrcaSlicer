package Slic3r::Fill::PlanePath;
use Moo;

extends 'Slic3r::Fill::Base';
with qw(Slic3r::Fill::WithDirection);

use Slic3r::Geometry qw(scale X1 Y1 X2 Y2);
use Slic3r::Geometry::Clipper qw(intersection_pl);

sub angles () { [0] }
sub multiplier () { 1 }

sub process_polyline {}

sub fill_surface {
    my $self = shift;
    my ($surface, %params) = @_;
    
    # rotate polygons
    my $expolygon = $surface->expolygon->clone;
    my $rotate_vector = $self->infill_direction($surface);
    $self->rotate_points($expolygon, $rotate_vector);
    
    my $distance_between_lines = scale($self->spacing) / $params{density} * $self->multiplier;
    
    # align infill across layers using the object's bounding box
    my $bb_polygon = $self->bounding_box->polygon;
    $self->rotate_points($bb_polygon, $rotate_vector);
    my $bounding_box = $bb_polygon->bounding_box;
    
    (ref $self) =~ /::([^:]+)$/;
    my $path = "Math::PlanePath::$1"->new;
    
    my $translate = Slic3r::Point->new(0,0);  # vector
    if ($path->x_negative || $path->y_negative) {
        # if the curve extends on both positive and negative coordinate space,
        # center our expolygon around origin
        $translate = $bounding_box->center->negative;
    } else {
        # if the curve does not extend in negative coordinate space,
        # move expolygon entirely in positive coordinate space
        $translate = $bounding_box->min_point->negative;
    }
    $expolygon->translate(@$translate);
    $bounding_box->translate(@$translate);
    
    my ($n_lo, $n_hi) = $path->rect_to_n_range(
        map { $_ / $distance_between_lines }
            @{$bounding_box->min_point},
            @{$bounding_box->max_point},
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
            no_arrows       => 1,
            polygons        => \@$expolygon,
            green_polygons  => [ $bounding_box->polygon ],
            polylines       => [ $polyline ],
            red_polylines   => \@paths,
        );
    }
    
    # paths must be repositioned and rotated back
    $_->translate(@{$translate->negative}) for @paths;
    $self->rotate_points_back(\@paths, $rotate_vector);
    
    return @paths;
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
