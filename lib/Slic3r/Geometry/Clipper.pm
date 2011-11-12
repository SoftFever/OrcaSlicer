package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(explode_expolygon explode_expolygons safety_offset
    diff_ex diff union_ex intersection_ex PFT_EVENODD);

use Math::Clipper 1.02 ':all';
our $clipper = Math::Clipper->new;

sub explode_expolygon {
    my ($expolygon) = @_;
    return ($expolygon->{outer}, @{ $expolygon->{holes} });
}

sub explode_expolygons {
    my ($expolygons) = @_;
    return map explode_expolygon($_), @$expolygons;
}

sub safety_offset {
    my ($polygons) = @_;
    return Math::Clipper::offset($polygons, 100, 100, JT_MITER, 2);
}

sub diff_ex {
    my ($subject, $clip) = @_;
    
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($clip);
    return $clipper->ex_execute(CT_DIFFERENCE, PFT_NONZERO, PFT_NONZERO);
}

sub diff {
    return [ map { $_->{outer}, $_->{holes} } diff_ex(@_) ];
}

sub union_ex {
    my ($polygons, $jointype) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($polygons);
    return [
        map Slic3r::ExPolygon->new($_),
            @{ $clipper->ex_execute(CT_UNION, $jointype, $jointype) },
    ];
}

sub intersection_ex {
    my ($subject, $clip, $jointype) = @_;
    $jointype = PFT_NONZERO unless defined $jointype;
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($clip);
    return [
        map Slic3r::ExPolygon->new($_),
            @{ $clipper->ex_execute(CT_INTERSECTION, $jointype, $jointype) },
    ];
}

1;
