#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 1;

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

__END__
