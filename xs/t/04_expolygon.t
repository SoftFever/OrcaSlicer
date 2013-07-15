#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 15;

use constant PI => 4 * atan2(1, 1);

my $square = [  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];
my $hole_in_square = [  # cw
    [140, 140],
    [140, 160],
    [160, 160],
    [160, 140],
];

my $expolygon = Slic3r::ExPolygon::XS->new($square, $hole_in_square);
is_deeply [ @{$expolygon->arrayref_pp} ], [$square, $hole_in_square], 'expolygon roundtrip';

my $arrayref = $expolygon->arrayref;
isa_ok $arrayref, 'Slic3r::ExPolygon', 'Perl expolygon is blessed';

my $arrayref_pp = $expolygon->arrayref_pp;
isa_ok $arrayref_pp, 'Slic3r::ExPolygon', 'Perl expolygon is blessed';
isa_ok $arrayref_pp->[0], 'Slic3r::Polygon', 'Perl polygons are blessed';
isnt ref($arrayref_pp->[0][0]), 'Slic3r::Point', 'Perl polygon points are not blessed';

{
    my $clone = $expolygon->clone;
    is_deeply [ @{$clone->arrayref_pp} ], [$square, $hole_in_square], 'clone';
    # The following tests implicitely check that modifying clones
    # does not modify the original one.
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->scale(2.5);
    is_deeply [ @{$expolygon2->arrayref_pp} ], [
        [map [ 2.5*$_->[0], 2.5*$_->[1] ], @$square],
        [map [ 2.5*$_->[0], 2.5*$_->[1] ], @$hole_in_square]
        ], 'scale';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->translate(10, -5);
    is_deeply [ @{$expolygon2->arrayref_pp} ], [
        [map [ $_->[0]+10, $_->[1]-5 ], @$square],
        [map [ $_->[0]+10, $_->[1]-5 ], @$hole_in_square]
        ], 'translate';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->rotate(PI/2, Slic3r::Point->new(150,150));
    is_deeply [ @{$expolygon2->arrayref_pp} ], [
        [ @$square[1,2,3,0] ],
        [ @$hole_in_square[3,0,1,2] ]
        ], 'rotate around Point';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->rotate(PI/2, [150,150]);
    is_deeply [ @{$expolygon2->arrayref_pp} ], [
        [ @$square[1,2,3,0] ],
        [ @$hole_in_square[3,0,1,2] ]
        ], 'rotate around pure-Perl Point';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->scale(2);
    my $collection = Slic3r::ExPolygon::Collection->new($expolygon->arrayref_pp, $expolygon2->arrayref_pp);
    is_deeply [ @{$collection->arrayref_pp} ], [ $expolygon->arrayref_pp, $expolygon2->arrayref_pp ],
        'expolygon collection';
    my $collection2 = Slic3r::ExPolygon::Collection->new($expolygon, $expolygon2);
    is_deeply [ @{$collection->arrayref_pp} ], [ @{$collection2->arrayref_pp} ],
        'expolygon collection with XS expolygons';
    
    $collection->clear;
    is scalar(@$collection), 0, 'clear collection';
    $collection->append($expolygon);
    is scalar(@$collection), 1, 'append to collection';
    
    is_deeply $collection->[0]->clone->arrayref_pp, $expolygon->arrayref_pp, 'clone collection item';
}

__END__
