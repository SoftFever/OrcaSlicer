package Slic3r::ExtrusionPath;
use strict;
use warnings;

use parent qw(Exporter);

our @EXPORT_OK = qw(EXTR_ROLE_PERIMETER EXTR_ROLE_EXTERNAL_PERIMETER 
    EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER EXTR_ROLE_OVERHANG_PERIMETER
    EXTR_ROLE_FILL EXTR_ROLE_SOLIDFILL EXTR_ROLE_TOPSOLIDFILL EXTR_ROLE_BRIDGE 
    EXTR_ROLE_INTERNALBRIDGE EXTR_ROLE_SKIRT EXTR_ROLE_SUPPORTMATERIAL EXTR_ROLE_GAPFILL);
our %EXPORT_TAGS = (roles => \@EXPORT_OK);

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

sub simplify {
    my $self = shift;
    $self->polyline($self->polyline->simplify(@_));
}

sub points {
    my $self = shift;
    return $self->polyline;
}

1;
