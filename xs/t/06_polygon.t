#!/usr/bin/perl

use strict;
use warnings;

use List::Util qw(first);
use Slic3r::XS;
use Test::More tests => 20;

use constant PI => 4 * atan2(1, 1);

my $square = [  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];

my $polygon = Slic3r::Polygon->new(@$square);
my $cw_polygon = $polygon->clone;
$cw_polygon->reverse;

ok $polygon->is_valid, 'is_valid';
is_deeply $polygon->pp, $square, 'polygon roundtrip';

is ref($polygon->arrayref), 'ARRAY', 'polygon arrayref is unblessed';
isa_ok $polygon->[0], 'Slic3r::Point::Ref', 'polygon point is blessed';

my $lines = $polygon->lines;
is_deeply [ map $_->pp, @$lines ], [
    [ [100, 100], [200, 100] ],
    [ [200, 100], [200, 200] ],
    [ [200, 200], [100, 200] ],
    [ [100, 200], [100, 100] ],
], 'polygon lines';

is_deeply $polygon->split_at_first_point->pp, [ @$square[0,1,2,3,0] ], 'split_at_first_point';
is_deeply $polygon->split_at_index(2)->pp, [ @$square[2,3,0,1,2] ], 'split_at_index';
is_deeply $polygon->split_at_vertex(Slic3r::Point->new(@{$square->[2]}))->pp, [ @$square[2,3,0,1,2] ], 'split_at';
is $polygon->area, 100*100, 'area';

ok $polygon->is_counter_clockwise, 'is_counter_clockwise';
ok !$cw_polygon->is_counter_clockwise, 'is_counter_clockwise';
{
    my $clone = $polygon->clone;
    $clone->reverse;
    ok !$clone->is_counter_clockwise, 'is_counter_clockwise';
    $clone->make_counter_clockwise;
    ok $clone->is_counter_clockwise, 'make_counter_clockwise';
    $clone->make_counter_clockwise;
    ok $clone->is_counter_clockwise, 'make_counter_clockwise';
}

ok ref($polygon->first_point) eq 'Slic3r::Point', 'first_point';

ok $polygon->contains_point(Slic3r::Point->new(150,150)), 'ccw contains_point';
ok $cw_polygon->contains_point(Slic3r::Point->new(150,150)), 'cw contains_point';

{
    my @points = (Slic3r::Point->new(100,0));
    foreach my $i (1..5) {
        my $point = $points[0]->clone;
        $point->rotate(PI/3*$i, [0,0]);
        push @points, $point;
    }
    my $hexagon = Slic3r::Polygon->new(@points);
    my $triangles = $hexagon->triangulate_convex;
    is scalar(@$triangles), 4, 'right number of triangles';
    ok !(defined first { $_->is_clockwise } @$triangles), 'all triangles are ccw';
}

{
    is_deeply $polygon->centroid->pp, [150,150], 'centroid';
}

# this is not a test: this just demonstrates bad usage, where $polygon->clone gets
# DESTROY'ed before the derived object ($point), causing bad memory access
if (0) {
    my $point;
    {
        $point = $polygon->clone->[0];
    }
    $point->scale(2);
}

__END__
