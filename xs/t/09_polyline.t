#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 5;

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

__END__
