#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 24;

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
    my $line = Slic3r::Line->new([0,0], [100,0]);
    is +Slic3r::Point->new(0,0)  ->distance_to_line($line),  0, 'distance_to_line()';
    is +Slic3r::Point->new(100,0)->distance_to_line($line),  0, 'distance_to_line()';
    is +Slic3r::Point->new(50,0) ->distance_to_line($line),  0, 'distance_to_line()';
    is +Slic3r::Point->new(150,0)->distance_to_line($line), 50, 'distance_to_line()';
    is +Slic3r::Point->new(0,50) ->distance_to_line($line), 50, 'distance_to_line()';
    is +Slic3r::Point->new(50,50)->distance_to_line($line), 50, 'distance_to_line()';
    is +Slic3r::Point->new(50,50) ->perp_distance_to_line($line), 50, 'perp_distance_to_line()';
    is +Slic3r::Point->new(150,50)->perp_distance_to_line($line), 50, 'perp_distance_to_line()';
}

{
    my $line = Slic3r::Line->new([50,50], [125,-25]);
    is +Slic3r::Point->new(100,0)->distance_to_line($line),  0, 'distance_to_line()';
}

{
    my $line = Slic3r::Line->new(
        [18335846,18335845],
        [18335846,1664160],
    );
    $point = Slic3r::Point->new(1664161,18335848);
    is $point->perp_distance_to_line($line), 16671685, 'perp_distance_to_line() does not overflow';
}

{
    my $p0 = Slic3r::Point->new(76975850,89989996);
    my $p1 = Slic3r::Point->new(76989990,109989991);
    my $p2 = Slic3r::Point->new(76989987,89989994);
    ok $p0->ccw($p1, $p2) < 0, 'ccw() does not overflow';
}

{
    my $point = Slic3r::Point->new(15,15);
    my $line = Slic3r::Line->new([10,10], [20,10]);
    is_deeply $point->projection_onto_line($line)->pp, [15,10], 'project_onto_line';
    
    $point = Slic3r::Point->new(0, 15);
    is_deeply $point->projection_onto_line($line)->pp, [10,10], 'project_onto_line';
    
    $point = Slic3r::Point->new(25, 15);
    is_deeply $point->projection_onto_line($line)->pp, [20,10], 'project_onto_line';
    
    $point = Slic3r::Point->new(10,10);
    is_deeply $point->projection_onto_line($line)->pp, [10,10], 'project_onto_line';
    
    $point = Slic3r::Point->new(12, 10);
    is_deeply $point->projection_onto_line($line)->pp, [12,10], 'project_onto_line';
}

__END__
