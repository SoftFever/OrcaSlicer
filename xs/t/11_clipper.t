#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 5;

my $square = [  # ccw
    [200, 100],
    [200, 200],
    [100, 200],
    [100, 100],
];
my $hole_in_square = [  # cw
    [160, 140],
    [140, 140],
    [140, 160],
    [160, 160],
];
my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);

{
    my $result = Slic3r::Geometry::Clipper::offset([ $square, $hole_in_square ], 5);
    is_deeply [ map $_->pp, @$result ], [ [
        [205, 95],
        [205, 205],
        [95, 205],
        [95, 95],
    ], [
        [155, 145],
        [145, 145],
        [145, 155],
        [155, 155],
    ] ], 'offset';
}

{
    my $result = Slic3r::Geometry::Clipper::offset_ex([ @$expolygon ], 5);
    is_deeply $result->[0]->pp, [ [
        [205, 95],
        [205, 205],
        [95, 205],
        [95, 95],
    ], [
        [145, 145],
        [145, 155],
        [155, 155],
        [155, 145],
    ] ], 'offset_ex';
}

{
    my $result = Slic3r::Geometry::Clipper::offset2_ex([ @$expolygon ], 5, -2);
    is_deeply $result->[0]->pp, [ [
        [203, 97],
        [203, 203],
        [97, 203],
        [97, 97],
    ], [
        [143, 143],
        [143, 157],
        [157, 157],
        [157, 143],
    ] ], 'offset2_ex';
}

{
    my $expolygon2 = Slic3r::ExPolygon->new([
        [20000000, 0],
        [20000000, 20000000],
        [0, 20000000],
        [0, 0],
    ], [
        [5000000, 5000000],
        [5000000, 15000000],
        [15000000, 15000000],
        [15000000, 5000000],
    ]);
    my $result = Slic3r::Geometry::Clipper::offset2_ex([ @$expolygon2 ], -1, +1);
    is_deeply $result->[0]->pp, $expolygon2->pp, 'offset2_ex';
}

{
    my $polygon1 = Slic3r::Polygon->new(@$square);
    my $polygon2 = Slic3r::Polygon->new(reverse @$hole_in_square);
    my $result = Slic3r::Geometry::Clipper::diff_ex([$polygon1], [$polygon2]);
    is_deeply $result->[0]->pp, $expolygon->pp, 'diff_ex';
}

__END__
