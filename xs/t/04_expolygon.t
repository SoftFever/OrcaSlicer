#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 9;

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
is_deeply [ @$expolygon ], [$square, $hole_in_square], 'expolygon roundtrip';

isa_ok $expolygon->arrayref, 'Slic3r::ExPolygon', 'Perl expolygon is blessed';
isa_ok $expolygon->[0], 'Slic3r::Polygon', 'Perl polygons are blessed';
isa_ok $expolygon->[0][0], 'Slic3r::Point', 'Perl polygon points are blessed';

{
    my $clone = $expolygon->clone;
    is_deeply [ @$clone ], [$square, $hole_in_square], 'clone';
    # The following tests implicitely check that modifying clones
    # does not modify the original one.
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->scale(2.5);
    is_deeply [ @$expolygon2 ], [
        [map [ 2.5*$_->[0], 2.5*$_->[1] ], @$square],
        [map [ 2.5*$_->[0], 2.5*$_->[1] ], @$hole_in_square]
        ], 'scale';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->translate(10, -5);
    is_deeply [ @$expolygon2 ], [
        [map [ $_->[0]+10, $_->[1]-5 ], @$square],
        [map [ $_->[0]+10, $_->[1]-5 ], @$hole_in_square]
        ], 'translate';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->rotate(PI/2, Slic3r::Point::XS->new(150,150));
    is_deeply [ @$expolygon2 ], [
        [ @$square[1,2,3,0] ],
        [ @$hole_in_square[3,0,1,2] ]
        ], 'rotate around Point::XS';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->rotate(PI/2, [150,150]);
    is_deeply [ @$expolygon2 ], [
        [ @$square[1,2,3,0] ],
        [ @$hole_in_square[3,0,1,2] ]
        ], 'rotate around pure-Perl Point';
}

__END__
