#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 15;

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
my $surface = Slic3r::Surface->new(
    expolygon => $expolygon,
    surface_type => Slic3r::Surface::S_TYPE_INTERNAL,
);

$surface = $surface->clone;

isa_ok $surface->expolygon, 'Slic3r::ExPolygon::Ref', 'expolygon';
is_deeply [ @{$surface->expolygon->pp} ], [$square, $hole_in_square], 'expolygon roundtrip';
is scalar(@{$surface->polygons}), 2, 'polygons roundtrip';

is $surface->surface_type, Slic3r::Surface::S_TYPE_INTERNAL, 'surface_type';
$surface->surface_type(Slic3r::Surface::S_TYPE_BOTTOM);
is $surface->surface_type, Slic3r::Surface::S_TYPE_BOTTOM, 'modify surface_type';

$surface->bridge_angle(30);
is $surface->bridge_angle, 30, 'bridge_angle';

$surface->extra_perimeters(2);
is $surface->extra_perimeters, 2, 'extra_perimeters';

{
    my $surface2 = $surface->clone;
    $surface2->expolygon->scale(2);
    isnt $surface2->expolygon->area, $expolygon->area, 'expolygon is returned by reference';
}

{
    my $collection = Slic3r::Surface::Collection->new;
    $collection->append($_) for $surface, $surface->clone;
    is scalar(@$collection), 2, 'collection has the right number of items';
    is_deeply $collection->[0]->expolygon->pp, [$square, $hole_in_square],
        'collection returns a correct surface expolygon';
    $collection->clear;
    is scalar(@$collection), 0, 'clear collection';
    $collection->append($surface);
    is scalar(@$collection), 1, 'append to collection';
    
    my $item = $collection->[0];
    isa_ok $item, 'Slic3r::Surface::Ref';
    $item->surface_type(Slic3r::Surface::S_TYPE_INTERNAL);
    is $item->surface_type, $collection->[0]->surface_type, 'collection returns items by reference';
}

{
    my $collection = Slic3r::Surface::Collection->new;
    $collection->append($_) for
        Slic3r::Surface->new(expolygon => $expolygon, surface_type => Slic3r::Surface::S_TYPE_BOTTOM),
        Slic3r::Surface->new(expolygon => $expolygon, surface_type => Slic3r::Surface::S_TYPE_BOTTOM),
        Slic3r::Surface->new(expolygon => $expolygon, surface_type => Slic3r::Surface::S_TYPE_TOP);
    is scalar(@{$collection->group}), 2, 'group() returns correct number of groups';
}

__END__
