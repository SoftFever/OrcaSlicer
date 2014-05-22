#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 14;

my $points = [
    [100, 100],
    [200, 100],
    [200, 200],
];

my $polyline = Slic3r::Polyline->new(@$points);

is_deeply $polyline->pp, $points, 'polyline roundtrip';

is ref($polyline->arrayref), 'ARRAY', 'polyline arrayref is unblessed';
isa_ok $polyline->[0], 'Slic3r::Point::Ref', 'polyline point is blessed';

my $lines = $polyline->lines;
is_deeply [ map $_->pp, @$lines ], [
    [ [100, 100], [200, 100] ],
    [ [200, 100], [200, 200] ],
], 'polyline lines';

$polyline->append_polyline($polyline->clone);
is_deeply $polyline->pp, [ @$points, @$points ], 'append_polyline';

{
    my $len = $polyline->length;
    $polyline->clip_end($len/3);
    ok abs($polyline->length - ($len-($len/3))) < 1, 'clip_end';
}

{
    my $polyline = Slic3r::Polyline->new(
        [0,0], [50,50], [100,0], [125,-25], [150,50],
    );
    $polyline->simplify(25);
    is_deeply $polyline->pp, [ [0, 0], [50, 50], [125, -25], [150, 50] ], 'Douglas-Peucker';
}

{
    my $polyline = Slic3r::Polyline->new(@$points);
    is $polyline->length, 100*2, 'length';
    $polyline->extend_end(50);
    is $polyline->length, 100*2 + 50, 'extend_end';
    $polyline->extend_start(50);
    is $polyline->length, 100*2 + 50 + 50, 'extend_start';
}

{
    my $polyline = Slic3r::Polyline->new(@$points);
    my $p1 = Slic3r::Polyline->new;
    my $p2 = Slic3r::Polyline->new;
    my $point = Slic3r::Point->new(150, 100);
    $polyline->split_at($point, $p1, $p2);
    is scalar(@$p1), 2, 'split_at';
    is scalar(@$p2), 3, 'split_at';
    ok $p1->last_point->coincides_with($point), 'split_at';
    ok $p2->first_point->coincides_with($point), 'split_at';
}

__END__
