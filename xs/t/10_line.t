#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 6;

my $points = [
    [100, 100],
    [200, 100],
];

my $line = Slic3r::Line->new(@$points);
is_deeply $line->pp, $points, 'line roundtrip';

is ref($line->arrayref), 'ARRAY', 'line arrayref is unblessed';
isa_ok $line->[0], 'Slic3r::Point::Ref', 'line point is blessed';

{
    my $clone = $line->clone;
    $clone->reverse;
    is_deeply $clone->pp, [ reverse @$points ], 'reverse';
}

{
    my $line2 = Slic3r::Line->new($line->a->clone, $line->b->clone);
    is_deeply $line2->pp, $points, 'line roundtrip with cloned points';
}

{
    my $clone = $line->clone;
    $clone->translate(10, -5);
    is_deeply $clone->pp, [
        [110, 95],
        [210, 95],
    ], 'translate';
}

__END__
