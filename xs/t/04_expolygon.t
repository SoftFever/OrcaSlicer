#!/usr/bin/perl

use strict;
use warnings;

use List::Util qw(first sum);
use Slic3r::XS;
use Test::More tests => 33;

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

my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);

ok $expolygon->is_valid, 'is_valid';
is ref($expolygon->pp), 'ARRAY', 'expolygon pp is unblessed';
is_deeply $expolygon->pp, [$square, $hole_in_square], 'expolygon roundtrip';

is ref($expolygon->arrayref), 'ARRAY', 'expolygon arrayref is unblessed';
isa_ok $expolygon->[0], 'Slic3r::Polygon::Ref', 'expolygon polygon is blessed';
isa_ok $expolygon->contour, 'Slic3r::Polygon::Ref', 'expolygon contour is blessed';
isa_ok $expolygon->holes->[0], 'Slic3r::Polygon::Ref', 'expolygon hole is blessed';
isa_ok $expolygon->[0][0], 'Slic3r::Point::Ref', 'expolygon point is blessed';

{
    my $expolygon2 = $expolygon->clone;
    my $polygon = $expolygon2->[0];
    $polygon->scale(2);
    is $expolygon2->[0][0][0], $polygon->[0][0], 'polygons are returned by reference';
}

is_deeply $expolygon->clone->pp, [$square, $hole_in_square], 'clone';

is $expolygon->area, 100*100-20*20, 'area';

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->scale(2.5);
    is_deeply $expolygon2->pp, [
        [map [ 2.5*$_->[0], 2.5*$_->[1] ], @$square],
        [map [ 2.5*$_->[0], 2.5*$_->[1] ], @$hole_in_square]
        ], 'scale';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->translate(10, -5);
    is_deeply $expolygon2->pp, [
        [map [ $_->[0]+10, $_->[1]-5 ], @$square],
        [map [ $_->[0]+10, $_->[1]-5 ], @$hole_in_square]
        ], 'translate';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->rotate(PI/2, Slic3r::Point->new(150,150));
    is_deeply $expolygon2->pp, [
        [ @$square[1,2,3,0] ],
        [ @$hole_in_square[3,0,1,2] ]
        ], 'rotate around Point';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->rotate(PI/2, [150,150]);
    is_deeply $expolygon2->pp, [
        [ @$square[1,2,3,0] ],
        [ @$hole_in_square[3,0,1,2] ]
        ], 'rotate around pure-Perl Point';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->scale(2);
    my $collection = Slic3r::ExPolygon::Collection->new($expolygon->pp, $expolygon2->pp);
    is_deeply $collection->pp, [ $expolygon->pp, $expolygon2->pp ],
        'expolygon collection (pure Perl) roundtrip';
    
    my $collection2 = Slic3r::ExPolygon::Collection->new($expolygon, $expolygon2);
    is_deeply $collection->pp, $collection2->pp,
        'expolygon collection (XS) roundtrip';
    
    $collection->clear;
    is scalar(@$collection), 0, 'clear collection';
    
    $collection->append($expolygon);
    is scalar(@$collection), 1, 'append to collection';
    
    my $exp = $collection->[0];
    $exp->scale(3);
    is $collection->[0][0][0][0], $exp->[0][0][0], 'collection items are returned by reference';
    
    is_deeply $collection->[0]->clone->pp, $collection->[0]->pp, 'clone collection item';
}

{
    my $expolygon = Slic3r::ExPolygon->new($square);
    my $polygons = $expolygon->get_trapezoids2(PI/2);
    is scalar(@$polygons), 1, 'correct number of trapezoids returned';
    is scalar(@{$polygons->[0]}), 4, 'trapezoid has 4 points';
    is $polygons->[0]->area, $expolygon->area, 'trapezoid has correct area';
}

{
    my $polygons = $expolygon->get_trapezoids2(PI/2);
    is scalar(@$polygons), 4, 'correct number of trapezoids returned';
    
    # trapezoid polygons might have more than 4 points in case of collinear segments
    $polygons = [ map @{$_->simplify(1)}, @$polygons ];
    ok !defined(first { @$_ != 4 } @$polygons), 'all trapezoids have 4 points';
    
    is scalar(grep { $_->area == 40*100 } @$polygons), 2, 'trapezoids have expected area';
    is scalar(grep { $_->area == 20*40 } @$polygons), 2, 'trapezoids have expected area';
}

{
    my $expolygon = Slic3r::ExPolygon->new([ [0,100],[100,0],[200,0],[300,100],[200,200],[100,200] ]);
    my $polygons = $expolygon->get_trapezoids2(PI/2);
    is scalar(@$polygons), 3, 'correct number of trapezoids returned';
    is scalar(grep { $_->area == 100*200/2 } @$polygons), 2, 'trapezoids have expected area';
    is scalar(grep { $_->area == 100*200 } @$polygons), 1, 'trapezoids have expected area';
}

{
    my $triangles = $expolygon->triangulate_pp;
    is scalar(@$triangles), 8, 'expected number of triangles';
    is sum(map $_->area, @$triangles), $expolygon->area, 'sum of triangles area equals original expolygon area';
}

__END__
