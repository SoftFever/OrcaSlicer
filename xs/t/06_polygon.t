#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 3;

my $square = [  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];

my $polygon = Slic3r::Polygon::XS->new(@$square);
is_deeply [ @{$polygon->arrayref_pp} ], [ @$square ], 'polygon roundtrip';

my $arrayref = $polygon->arrayref;
isa_ok $arrayref, 'Slic3r::Polygon', 'Perl polygon is blessed';
isa_ok $arrayref->[0], 'Slic3r::Point', 'Perl points are blessed';

__END__
