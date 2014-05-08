#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 17;

my $square = Slic3r::Polygon->new(  # ccw
    [200, 100],
    [200, 200],
    [100, 200],
    [100, 100],
);
my $hole_in_square = Slic3r::Polygon->new(  # cw
    [160, 140],
    [140, 140],
    [140, 160],
    [160, 160],
);
my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);

{
    my $result = Slic3r::Geometry::Clipper::offset([ $square, $hole_in_square ], 5);
    is_deeply [ map $_->pp, @$result ], [ [
        [205, 205],
        [95, 205],
        [95, 95],
        [205, 95],
    ], [
        [145, 145],
        [145, 155],
        [155, 155],
        [155, 145],
    ] ], 'offset';
}

{
    my $result = Slic3r::Geometry::Clipper::offset_ex([ @$expolygon ], 5);
    is_deeply $result->[0]->pp, [ [
        [205, 205],
        [95, 205],
        [95, 95],
        [205, 95],
    ], [
        [145, 145],
        [145, 155],
        [155, 155],
        [155, 145],
    ] ], 'offset_ex';
}

{
    my $result = Slic3r::Geometry::Clipper::offset2_ex([ @$expolygon ], 5, -2);
    is_deeply $result->[0]->pp, [ [
        [203, 203],
        [97, 203],
        [97, 97],
        [203, 97],
    ], [
        [143, 143],
        [143, 157],
        [157, 157],
        [157, 143],
    ] ], 'offset2_ex';
}

{
    my $expolygon2 = Slic3r::ExPolygon->new([
        [20000000, 20000000],
        [0, 20000000],
        [0, 0],
        [20000000, 0],
    ], [
        [5000000, 15000000],
        [15000000, 15000000],
        [15000000, 5000000],
        [5000000, 5000000],
    ]);
    my $result = Slic3r::Geometry::Clipper::offset2_ex([ @$expolygon2 ], -1, +1);
    is $result->[0]->area, $expolygon2->area, 'offset2_ex';
}

{
    my $polygon1 = Slic3r::Polygon->new(@$square);
    my $polygon2 = Slic3r::Polygon->new(reverse @$hole_in_square);
    my $result = Slic3r::Geometry::Clipper::diff_ex([$polygon1], [$polygon2]);
    is $result->[0]->area, $expolygon->area, 'diff_ex';
}

{
    my $polyline = Slic3r::Polyline->new([50,150], [300,150]);
    {
        my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square, $hole_in_square]);
        is scalar(@$result), 2,      'intersection_pl - correct number of result lines';
        # results are in no particular order
        is scalar(grep $_->length == 40, @$result), 2, 'intersection_pl - result lines have correct length';
    }
    {
        my $result = Slic3r::Geometry::Clipper::diff_pl([$polyline], [$square, $hole_in_square]);
        is scalar(@$result), 3,      'diff_pl - correct number of result lines';
        # results are in no particular order
        is scalar(grep $_->length == 50, @$result), 1, 'diff_pl - the left result line has correct length';
        is scalar(grep $_->length == 100, @$result), 1, 'diff_pl - two right result line has correct length';
        is scalar(grep $_->length == 20, @$result), 1, 'diff_pl - the central result line has correct length';
    }
}

if (0) {  # Clipper does not preserve polyline orientation
    my $polyline = Slic3r::Polyline->new([50,150], [300,150]);
    my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square]);
    is scalar(@$result), 1, 'intersection_pl - correct number of result lines';
    is_deeply $result->[0]->pp, [[100,150], [200,150]], 'clipped line orientation is preserved';
}

if (0) {  # Clipper does not preserve polyline orientation
    my $polyline = Slic3r::Polyline->new([300,150], [50,150]);
    my $result = Slic3r::Geometry::Clipper::intersection_pl([$polyline], [$square]);
    is scalar(@$result), 1, 'intersection_pl - correct number of result lines';
    is_deeply $result->[0]->pp, [[200,150], [100,150]], 'clipped line orientation is preserved';
}

if (0) {  # Clipper does not preserve polyline orientation
    my $result = Slic3r::Geometry::Clipper::intersection_ppl([$hole_in_square], [$square]);
    is_deeply $result->[0]->pp, $hole_in_square->split_at_first_point->pp,
        'intersection_ppl - clipping cw polygon as polyline preserves winding order';
}

{
    my $square2 = $square->clone;
    $square2->translate(50,50);
    {
        my $result = Slic3r::Geometry::Clipper::intersection_ppl([$square2], [$square]);
        is scalar(@$result), 1, 'intersection_ppl - result contains a single line';
        is scalar(@{$result->[0]}), 3, 'intersection_ppl - result contains expected number of points';
        # Clipper does not preserve polyline orientation so we only check the middle point
        ###ok $result->[0][0]->coincides_with(Slic3r::Point->new(150,200)), 'intersection_ppl - expected point order';
        ok $result->[0][1]->coincides_with(Slic3r::Point->new(150,150)), 'intersection_ppl - expected point order';
        ###ok $result->[0][2]->coincides_with(Slic3r::Point->new(200,150)), 'intersection_ppl - expected point order';
    }
}

{
    my $square2 = $square->clone;
    $square2->reverse;
    $square2->translate(50,50);
    {
        my $result = Slic3r::Geometry::Clipper::intersection_ppl([$square2], [$square]);
        is scalar(@$result), 1, 'intersection_ppl - result contains a single line';
        is scalar(@{$result->[0]}), 3, 'intersection_ppl - result contains expected number of points';
        # Clipper does not preserve polyline orientation so we only check the middle point
        ###ok $result->[0][0]->coincides_with(Slic3r::Point->new(200,150)), 'intersection_ppl - expected point order';
        ok $result->[0][1]->coincides_with(Slic3r::Point->new(150,150)), 'intersection_ppl - expected point order';
        ###ok $result->[0][2]->coincides_with(Slic3r::Point->new(150,200)), 'intersection_ppl - expected point order';
    }
}

__END__
