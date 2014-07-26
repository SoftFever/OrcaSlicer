#!/usr/bin/perl

use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../../lib";
}

use Slic3r::XS;
use Test::More tests => 22;

my $square = Slic3r::Polygon->new(  # ccw
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
);
my $hole_in_square = Slic3r::Polygon->new(  # cw
    [140, 140],
    [140, 160],
    [160, 160],
    [160, 140],
);
$_->scale(1/0.000001) for $square, $hole_in_square;

my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);

{
    my $mp = Slic3r::MotionPlanner->new([ $expolygon ]);
    isa_ok $mp, 'Slic3r::MotionPlanner';
    
    my $from = Slic3r::Point->new(120, 120);
    my $to = Slic3r::Point->new(180,180);
    $_->scale(1/0.000001) for $from, $to;
    my $path = $mp->shortest_path($from, $to);
    ok $path->is_valid(), 'return path is valid';
    ok $path->length > Slic3r::Line->new($from, $to)->length, 'path length is greater than straight line';
    ok $path->first_point->coincides_with($from), 'first path point coincides with initial point';
    ok $path->last_point->coincides_with($to), 'last path point coincides with destination point';
    ok $expolygon->contains_polyline($path), 'path is fully contained in expolygon';
}

{
    my $mp = Slic3r::MotionPlanner->new([ $expolygon ]);
    isa_ok $mp, 'Slic3r::MotionPlanner';
    
    my $from = Slic3r::Point->new(80, 100);
    my $to = Slic3r::Point->new(220,200);
    $_->scale(1/0.000001) for $from, $to;
    
    my $path = $mp->shortest_path($from, $to);
    ok $path->is_valid(), 'return path is valid';
    ok $path->length > Slic3r::Line->new($from, $to)->length, 'path length is greater than straight line';
    ok $path->first_point->coincides_with($from), 'first path point coincides with initial point';
    ok $path->last_point->coincides_with($to), 'last path point coincides with destination point';
    is scalar(@{ Slic3r::Geometry::Clipper::intersection_pl([$path], [@$expolygon]) }), 0, 'path has no intersection with expolygon';
}

{
    my $expolygon2 = $expolygon->clone;
    $expolygon2->translate(300/0.000001, 0);
    my $mp = Slic3r::MotionPlanner->new([ $expolygon, $expolygon2 ]);
    isa_ok $mp, 'Slic3r::MotionPlanner';
    
    my $from = Slic3r::Point->new(120, 120);
    my $to = Slic3r::Point->new(120 + 300, 120);
    $_->scale(1/0.000001) for $from, $to;
    ok $expolygon->contains_point($from), 'start point is contained in first expolygon';
    ok $expolygon2->contains_point($to), 'end point is contained in second expolygon';
    my $path = $mp->shortest_path($from, $to);
    ok $path->is_valid(), 'return path is valid';
    ok $path->length > Slic3r::Line->new($from, $to)->length, 'path length is greater than straight line';
}

{
    my $expolygons = [
        Slic3r::ExPolygon->new([[123800962,89330311],[123959159,89699438],[124000004,89898430],[124000012,110116427],[123946510,110343065],[123767391,110701303],[123284087,111000001],[102585791,111000009],[102000004,110414223],[102000004,89585787],[102585790,89000000],[123300022,88999993]]),
        Slic3r::ExPolygon->new([[97800954,89330311],[97959151,89699438],[97999996,89898430],[98000004,110116427],[97946502,110343065],[97767383,110701303],[97284079,111000001],[76585783,111000009],[75999996,110414223],[75999996,89585787],[76585782,89000000],[97300014,88999993]]),
    ];
    my $mp = Slic3r::MotionPlanner->new($expolygons);
    isa_ok $mp, 'Slic3r::MotionPlanner';
    
    my $from = Slic3r::Point->new(79120520, 107839491);
    my $to = Slic3r::Point->new(104664164, 108335852);
    ok $expolygons->[1]->contains_point($from), 'start point is contained in second expolygon';
    ok $expolygons->[0]->contains_point($to), 'end point is contained in first expolygon';
    my $path = $mp->shortest_path($from, $to);
    ok $path->is_valid(), 'return path is valid';
    ok $path->length > Slic3r::Line->new($from, $to)->length, 'path length is greater than straight line';
}

__END__
