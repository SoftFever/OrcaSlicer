#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 9;

my $point = Slic3r::Point->new(10, 15);
is_deeply [ @$point ], [10, 15], 'point roundtrip';

my $point2 = $point->clone;
$point2->scale(2);
is_deeply [ @$point2 ], [20, 30], 'scale';

$point2->translate(10, -15);
is_deeply [ @$point2 ], [30, 15], 'translate';

ok $point->coincides_with($point->clone), 'coincides_with';
ok !$point->coincides_with($point2), 'coincides_with';

{
    my $point3 = Slic3r::Point->new(4300000, -9880845);
    is $point->[0], $point->x, 'x accessor';
    is $point->[1], $point->y, 'y accessor';  #,,
}

{
    my $nearest = $point->nearest_point([ $point2, Slic3r::Point->new(100, 200) ]);
    ok $nearest->coincides_with($point2), 'nearest_point';
}

{
    my $line = Slic3r::Line->new(
        [18335846,18335845],
        [18335846,1664160],
    );
    $point = Slic3r::Point->new(1664161,18335848);
    is $point->distance_to_line($line), 16671685, 'distance_to_line() does not overflow';
}

__END__
