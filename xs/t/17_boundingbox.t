#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 5;

{
    my @points = (
        Slic3r::Point->new(100, 200),
        Slic3r::Point->new(500, -600),
    );
    my $bb = Slic3r::Geometry::BoundingBox->new_from_points(\@points);
    isa_ok $bb, 'Slic3r::Geometry::BoundingBox', 'new_from_points';
    is_deeply $bb->min_point->pp, [100,-600], 'min_point';
    is_deeply $bb->max_point->pp, [500,200], 'max_point';
}

{
    my $bb = Slic3r::Geometry::BoundingBox->new;
    $bb->merge_point(Slic3r::Point->new(10, 10));
    is_deeply $bb->min_point->pp, [10,10], 'min_point equals to the only defined point';
    is_deeply $bb->max_point->pp, [10,10], 'max_point equals to the only defined point';
}

__END__
