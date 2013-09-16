package Slic3r::ExtrusionPath;
use strict;
use warnings;

use parent qw(Exporter);

our @EXPORT_OK = qw(EXTR_ROLE_PERIMETER EXTR_ROLE_EXTERNAL_PERIMETER 
    EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER EXTR_ROLE_OVERHANG_PERIMETER
    EXTR_ROLE_FILL EXTR_ROLE_SOLIDFILL EXTR_ROLE_TOPSOLIDFILL EXTR_ROLE_BRIDGE 
    EXTR_ROLE_INTERNALBRIDGE EXTR_ROLE_SKIRT EXTR_ROLE_SUPPORTMATERIAL EXTR_ROLE_GAPFILL);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

use Slic3r::Geometry qw(PI X Y epsilon deg2rad rotate_points);

sub clip_with_polygon {
    my $self = shift;
    my ($polygon) = @_;
    
    return $self->clip_with_expolygon(Slic3r::ExPolygon->new($polygon));
}

sub clip_with_expolygon {
    my $self = shift;
    my ($expolygon) = @_;
    
    return map $self->clone(polyline => $_),
        $self->polyline->clip_with_expolygon($expolygon);
}

sub intersect_expolygons {
    my $self = shift;
    my ($expolygons_pp) = @_;
    
    return map $self->clone(polyline => Slic3r::Polyline->new(@$_)),
        @{Boost::Geometry::Utils::multi_polygon_multi_linestring_intersection($expolygons_pp, [$self->pp])};
}

sub subtract_expolygons {
    my $self = shift;
    my ($expolygons_pp) = @_;
    
    return map $self->clone(polyline => Slic3r::Polyline->new(@$_)),
        @{Boost::Geometry::Utils::multi_linestring_multi_polygon_difference([$self->pp], $expolygons_pp)};
}

sub simplify {
    my $self = shift;
    $self->polyline($self->polyline->simplify(@_));
}

sub clip_end {
    my $self = shift;
    my $polyline = $self->polyline;
    $polyline->clip_end(@_);
    $self->polyline($polyline);
}

sub length {
    my $self = shift;
    return $self->polyline->length;
}

sub points {
    my $self = shift;
    return $self->polyline;
}

sub is_perimeter {
    my $self = shift;
    return $self->role == EXTR_ROLE_PERIMETER
        || $self->role == EXTR_ROLE_EXTERNAL_PERIMETER
        || $self->role == EXTR_ROLE_OVERHANG_PERIMETER
        || $self->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER;
}

sub is_fill {
    my $self = shift;
    return $self->role == EXTR_ROLE_FILL
        || $self->role == EXTR_ROLE_SOLIDFILL
        || $self->role == EXTR_ROLE_TOPSOLIDFILL;
}

sub is_bridge {
    my $self = shift;
    return $self->role == EXTR_ROLE_BRIDGE
        || $self->role == EXTR_ROLE_INTERNALBRIDGE
        || $self->role == EXTR_ROLE_OVERHANG_PERIMETER;
}

1;
