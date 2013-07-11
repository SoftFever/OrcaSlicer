#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 6;

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

my $expolygon = Slic3r::ExPolygon::XS->new($square, $hole_in_square);
is_deeply [ @$expolygon ], [$square, $hole_in_square], 'expolygon roundtrip';

isa_ok $expolygon->arrayref, 'Slic3r::ExPolygon', 'Perl expolygon is blessed';
isa_ok $expolygon->[0], 'Slic3r::Polygon', 'Perl polygons are blessed';
isa_ok $expolygon->[0][0], 'Slic3r::Point', 'Perl polygon points are blessed';

my $clone = $expolygon->clone;
is_deeply [ @$clone ], [$square, $hole_in_square], 'clone';
# TODO: check that modifying the clone doesn't modify the original one

$expolygon->scale(1.5);
is_deeply [ @$expolygon ], [
    [map [ 1.5*$_->[0], 1.5*$_->[1] ], @$square],
    [map [ 1.5*$_->[0], 1.5*$_->[1] ], @$hole_in_square]
    ], 'scale';

__END__
