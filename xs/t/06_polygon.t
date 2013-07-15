#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 4;

my $square = [  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];

my $polygon = Slic3r::Polygon->new(@$square);
is_deeply $polygon->pp, $square, 'polygon roundtrip';

is ref($polygon->arrayref), 'ARRAY', 'polygon arrayref is unblessed';
isa_ok $polygon->[0], 'Slic3r::Point', 'polygon point is blessed';

my $lines = $polygon->lines;
is_deeply [ map $_->pp, @$lines ], [
    [ [100, 100], [200, 100] ],
    [ [200, 100], [200, 200] ],
    [ [200, 200], [100, 200] ],
    [ [100, 200], [100, 100] ],
], 'polygon lines';

__END__
