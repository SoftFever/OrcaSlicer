#!/usr/bin/perl

use strict;
use warnings;

use List::Util qw(sum);
use Slic3r::XS;
use Test::More tests => 23;

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

{
    # Clipper bug #96 (our issue #2028)
    my $subject = Slic3r::Polyline->new(
        [44735000,31936670],[55270000,31936670],[55270000,25270000],[74730000,25270000],[74730000,44730000],[68063296,44730000],[68063296,55270000],[74730000,55270000],[74730000,74730000],[55270000,74730000],[55270000,68063296],[44730000,68063296],[44730000,74730000],[25270000,74730000],[25270000,55270000],[31936670,55270000],[31936670,44730000],[25270000,44730000],[25270000,25270000],[44730000,25270000],[44730000,31936670]
    );
    my $clip = [
        Slic3r::Polygon->new([75200000,45200000],[54800000,45200000],[54800000,24800000],[75200000,24800000]),
    ];
    my $result = Slic3r::Geometry::Clipper::intersection_pl([$subject], $clip);
    is scalar(@$result), 1, 'intersection_pl - result is not empty';
}

{
    my $subject = Slic3r::Polygon->new(
        [44730000,31936670],[55270000,31936670],[55270000,25270000],[74730000,25270000],[74730000,44730000],[68063296,44730000],[68063296,55270000],[74730000,55270000],[74730000,74730000],[55270000,74730000],[55270000,68063296],[44730000,68063296],[44730000,74730000],[25270000,74730000],[25270000,55270000],[31936670,55270000],[31936670,44730000],[25270000,44730000],[25270000,25270000],[44730000,25270000]
    );
    my $clip = [
        Slic3r::Polygon->new([75200000,45200000],[54800000,45200000],[54800000,24800000],[75200000,24800000]),
    ];
    my $result = Slic3r::Geometry::Clipper::intersection_ppl([$subject], $clip);
    is scalar(@$result), 1, 'intersection_ppl - result is not empty';
}

{
    # Clipper bug #122
    my $subject = [
        Slic3r::Polyline->new([1975,1975],[25,1975],[25,25],[1975,25],[1975,1975]),
    ];
    my $clip = [
        Slic3r::Polygon->new([2025,2025],[-25,2025],[-25,-25],[2025,-25]),
        Slic3r::Polygon->new([525,525],[525,1475],[1475,1475],[1475,525]),
    ];
    my $result = Slic3r::Geometry::Clipper::intersection_pl($subject, $clip);
    is scalar(@$result), 1, 'intersection_pl - result is not empty';
    is scalar(@{$result->[0]}), 5, 'intersection_pl - result is not empty';
}

{
    # Clipper bug #126
    my $subject = Slic3r::Polyline->new(
        [200000,19799999],[200000,200000],[24304692,200000],[15102879,17506106],[13883200,19799999],[200000,19799999],
    );
    my $clip = [
        Slic3r::Polygon->new([15257205,18493894],[14350057,20200000],[-200000,20200000],[-200000,-200000],[25196917,-200000]),
    ];
    my $result = Slic3r::Geometry::Clipper::intersection_pl([$subject], $clip);
    is scalar(@$result), 1, 'intersection_pl - result is not empty';
    is $result->[0]->length, $subject->length, 'intersection_pl - result has same length as subject polyline';
}

if (0) {
    # Disabled until Clipper bug #127 is fixed
    my $subject = [
        Slic3r::Polyline->new([-90000000,-100000000],[-90000000,100000000]),  # vertical
        Slic3r::Polyline->new([-100000000,-10000000],[100000000,-10000000]),  # horizontal
        Slic3r::Polyline->new([-100000000,0],[100000000,0]),                  # horizontal
        Slic3r::Polyline->new([-100000000,10000000],[100000000,10000000]),    # horizontal
    ];
    my $clip = Slic3r::Polygon->new(  # a circular, convex, polygon
        [99452190,10452846],[97814760,20791169],[95105652,30901699],[91354546,40673664],[86602540,50000000],
        [80901699,58778525],[74314483,66913061],[66913061,74314483],[58778525,80901699],[50000000,86602540],
        [40673664,91354546],[30901699,95105652],[20791169,97814760],[10452846,99452190],[0,100000000],
        [-10452846,99452190],[-20791169,97814760],[-30901699,95105652],[-40673664,91354546],
        [-50000000,86602540],[-58778525,80901699],[-66913061,74314483],[-74314483,66913061],
        [-80901699,58778525],[-86602540,50000000],[-91354546,40673664],[-95105652,30901699],
        [-97814760,20791169],[-99452190,10452846],[-100000000,0],[-99452190,-10452846],
        [-97814760,-20791169],[-95105652,-30901699],[-91354546,-40673664],[-86602540,-50000000],
        [-80901699,-58778525],[-74314483,-66913061],[-66913061,-74314483],[-58778525,-80901699],
        [-50000000,-86602540],[-40673664,-91354546],[-30901699,-95105652],[-20791169,-97814760],
        [-10452846,-99452190],[0,-100000000],[10452846,-99452190],[20791169,-97814760],
        [30901699,-95105652],[40673664,-91354546],[50000000,-86602540],[58778525,-80901699],
        [66913061,-74314483],[74314483,-66913061],[80901699,-58778525],[86602540,-50000000],
        [91354546,-40673664],[95105652,-30901699],[97814760,-20791169],[99452190,-10452846],[100000000,0]
    );
    my $result = Slic3r::Geometry::Clipper::intersection_pl($subject, [$clip]);
    is scalar(@$result), scalar(@$subject), 'intersection_pl - expected number of polylines';
    is sum(map scalar(@$_), @$result), scalar(@$subject)*2,
        'intersection_pl - expected number of points in polylines';
}

__END__
