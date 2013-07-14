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
my $surface = Slic3r::Surface->new(
    expolygon => $expolygon,
    surface_type => Slic3r::Surface::S_TYPE_INTERNAL,
);

$surface = $surface->clone;

isa_ok $surface->expolygon, 'Slic3r::ExPolygon::XS', 'expolygon';
is_deeply [ @{$surface->expolygon} ], [$square, $hole_in_square], 'expolygon roundtrip';

is $surface->surface_type, Slic3r::Surface::S_TYPE_INTERNAL, 'surface_type';
$surface->surface_type(Slic3r::Surface::S_TYPE_BOTTOM);
is $surface->surface_type, Slic3r::Surface::S_TYPE_BOTTOM, 'modify surface_type';

$surface->bridge_angle(30);
is $surface->bridge_angle, 30, 'bridge_angle';

$surface->extra_perimeters(2);
is $surface->extra_perimeters, 2, 'extra_perimeters';

__END__
