#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 2;

my $point = Slic3r::Point::XS->new(10, 15);
is_deeply [ @$point ], [10, 15], 'point roundtrip';

isa_ok $point->arrayref, 'Slic3r::Point', 'Perl point is blessed';

__END__
