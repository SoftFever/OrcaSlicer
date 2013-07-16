#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 2;

my $square = [  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];
my $hole_in_square = [  # cw
    [140, 140],
    [140, 160],
    [160, 160],
    [160, 140],
];
my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);

{
    my $result = @{Slic3r::Geometry::Clipper::offset_ex([ @$expolygon ], 5)};
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
    my $result = @{Slic3r::Geometry::Clipper::offset2_ex([ @$expolygon ], 5, -2)};
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
    ] ], 'offset_ex';
}

__END__
