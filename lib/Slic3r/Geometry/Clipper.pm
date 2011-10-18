package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(explode_expolygon explode_expolygons safety_offset
    diff_ex diff union_ex intersection_ex);

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
    my ($polygons) = @_;
    $clipper->clear;
    $clipper->add_subject_polygons($polygons);
    return $clipper->ex_execute(CT_UNION, PFT_NONZERO, PFT_NONZERO);
}

sub intersection_ex {
    my ($subject, $clip) = @_;
    
    $clipper->clear;
    $clipper->add_subject_polygons($subject);
    $clipper->add_clip_polygons($clip);
    return $clipper->ex_execute(CT_INTERSECTION, PFT_NONZERO, PFT_NONZERO);
}

1;
