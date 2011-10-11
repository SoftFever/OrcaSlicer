package Slic3r::Geometry::Clipper;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(diff_ex diff union_ex);

use Math::Clipper 1.02 ':all';
our $clipper = Math::Clipper->new;

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

1;
