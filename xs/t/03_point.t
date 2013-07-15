#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 5;

my $point = Slic3r::Point->new(10, 15);
is_deeply [ @$point ], [10, 15], 'point roundtrip';

my $point2 = $point->clone;
$point2->scale(2);
is_deeply [ @$point2 ], [20, 30], 'scale';

$point2->translate(10, -15);
is_deeply [ @$point2 ], [30, 15], 'translate';

ok $point->coincides_with($point->clone), 'coincides_with';
ok !$point->coincides_with($point2), 'coincides_with';

__END__
