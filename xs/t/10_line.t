#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 40;

use constant PI         => 4 * atan2(1, 1);
use constant EPSILON    => 1E-4;

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

{
    ok +Slic3r::Line->new([0,0],[200,0])->parallel_to_line(Slic3r::Line->new([200,200],[0,200])), 'parallel_to';
}

foreach my $base_angle (0, PI/4, PI/2, PI) {
    my $line = Slic3r::Line->new([0,0], [100,0]);
    $line->rotate($base_angle, [0,0]);
    my $clone = $line->clone;
    ok $line->parallel_to_line($clone), 'line is parallel to self';
    $clone->reverse;
    ok $line->parallel_to_line($clone), 'line is parallel to self + PI';
    ok $line->parallel_to($line->direction), 'line is parallel to its direction';
    ok $line->parallel_to($line->direction + PI), 'line is parallel to its direction + PI';
    ok $line->parallel_to($line->direction - PI), 'line is parallel to its direction - PI';
    {
        my $line2 = $line->clone;
        $line2->reverse;
        ok $line->parallel_to_line($line2), 'line is parallel to its opposite';
    }
    {
        my $line2 = $line->clone;
        $line2->rotate(+(EPSILON)/2, [0,0]);
        ok $line->parallel_to_line($line2), 'line is parallel within epsilon';
    }
    {
        my $line2 = $line->clone;
        $line2->rotate(-(EPSILON)/2, [0,0]);
        ok $line->parallel_to_line($line2), 'line is parallel within epsilon';
    }
}

{
    my $a = Slic3r::Line->new([100, 0], [200, 0]);
    my $b = Slic3r::Line->new([300, 300], [300, 100]);
    my $r = $a->intersection_infinite($b);
    is_deeply $r->pp, [300, 0], 'intersection_infinite';
}

__END__
