#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 14;

my $square = [  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];

my $polygon = Slic3r::Polygon->new(@$square);
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
is_deeply $polygon->split_at(Slic3r::Point->new(@{$square->[2]}))->pp, [ @$square[2,3,0,1,2] ], 'split_at';
is $polygon->area, 100*100, 'area';

ok $polygon->is_counter_clockwise, 'is_counter_clockwise';
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
