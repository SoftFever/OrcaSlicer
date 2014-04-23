use Test::More;
use strict;
use warnings;

plan tests => 24;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scaled_epsilon epsilon scale unscale X Y deg2rad);

{
    my $angle = deg2rad(4);
    foreach my $ccw (1, 0) {
        my $polyline = Slic3r::Polyline->new_scale([0,0], [0,10]);
        {
            my $p3 = Slic3r::Point->new_scale(0, 20);
            $p3->rotate($angle * ($ccw ? 1 : -1), $polyline->[-1]);
            is $ccw, ($p3->[X] < $polyline->[-1][X]) ? 1 : 0, 'third point is rotated correctly';
            $polyline->append($p3);
        }
        ok abs($polyline->length - scale(20)) < scaled_epsilon, 'curved polyline length';
        is $ccw, ($polyline->[2]->ccw(@$polyline[0,1]) > 0) ? 1 : 0, 'curved polyline has wanted orientation';
    
        ok my $arc = Slic3r::GCode::ArcFitting::polyline_to_arc($polyline), 'arc is detected';
        is $ccw, $arc->is_ccw, 'arc orientation is correct';
    
        ok abs($arc->angle - $angle) < epsilon, 'arc relative angle is correct';
        
        ok $arc->start->coincides_with($polyline->[0]), 'arc start point is correct';
        ok $arc->end->coincides_with($polyline->[-1]), 'arc end point is correct';
        
        # since first polyline segment is vertical we expect arc center to have same Y as its first point
        is $arc->center->[Y], 0, 'arc center has correct Y';
    
        my $s1 = Slic3r::Line->new(@$polyline[0,1]);
        my $s2 = Slic3r::Line->new(@$polyline[1,2]);
        ok abs($arc->center->distance_to($s1->midpoint) - $arc->center->distance_to($s2->midpoint)) < scaled_epsilon,
            'arc center is equidistant from both segments\' midpoints';
    }
}

#==========================================================

{
    my $path = Slic3r::Polyline->new_scale(
        [13.532242,2.665496], [18.702911,9.954623], [22.251514,9.238193], [25.800116,9.954623], 
        [28.697942,11.908391], [30.65171,14.806217], [31.36814,18.35482],
        [30.65171,21.903423], [28.697942,24.801249], [25.800116,26.755017], [22.251514,27.471447], 
        [18.702911,26.755017], [15.805085,24.801249], [13.851317,21.903423], [13.134887,18.35482], 
        [86948.77,175149.09], [119825.35,100585],
    );
    
    if (0) {
        require "Slic3r::SVG";
        Slic3r::SVG::output(
            "arc.svg",
            polylines => [$path],
        );
    }
    
    my $af = Slic3r::GCode::ArcFitting->new(max_relative_angle => deg2rad(30));
    my @chunks = $af->detect_arcs($path);
    
    is scalar(@chunks), 3, 'path collection now contains three paths';
    isa_ok $chunks[0], 'Slic3r::Polyline', 'first one is polyline';
    isa_ok $chunks[1], 'Slic3r::GCode::ArcFitting::Arc', 'second one is arc';
    isa_ok $chunks[2], 'Slic3r::Polyline', 'third one is polyline';
}

exit;

#==========================================================

{
    my @points = map [ scale $_->[0], scale $_->[1] ], (
        [10,20], [10.7845909572784,19.9691733373313], [11.5643446504023,19.8768834059514], 
        [12.3344536385591,19.7236992039768], [13.0901699437495,19.5105651629515], 
        [13.8268343236509,19.2387953251129], [14.5399049973955,18.9100652418837], 
        [15.2249856471595,18.5264016435409], [15.8778525229247,18.0901699437495], 
        [16.4944804833018,17.6040596560003]
    );
    my $path1 = Slic3r::ExtrusionPath->new(
        polyline    => Slic3r::Polyline->new(@points),
        role        => EXTR_ROLE_FILL,
        mm3_per_mm  => 0.5,
    );
    my $path2 = Slic3r::ExtrusionPath->new(
        polyline    => Slic3r::Polyline->new(reverse @points),
        role        => EXTR_ROLE_FILL,
        mm3_per_mm  => 0.5,
    );
    
    my @paths1 = $path1->detect_arcs(10, scale 1);
    my @paths2 = $path2->detect_arcs(10, scale 1);
    
    is scalar(@paths1), 1, 'path collection now contains one path';
    is scalar(@paths2), 1, 'path collection now contains one path';
    
    isa_ok $paths1[0], 'Slic3r::ExtrusionPath::Arc', 'path';
    isa_ok $paths2[0], 'Slic3r::ExtrusionPath::Arc', 'path';
    
    my $expected_length = scale 7.06858347057701;
    ok abs($paths1[0]->length - $expected_length) < scaled_epsilon, 'cw oriented arc has correct length';
    ok abs($paths2[0]->length - $expected_length) < scaled_epsilon, 'ccw oriented arc has correct length';

    is $paths1[0]->orientation, 'cw', 'cw orientation was correctly detected';
    is $paths2[0]->orientation, 'ccw', 'ccw orientation was correctly detected';
    is $paths1[0]->flow_spacing, $path1->flow_spacing, 'flow spacing was correctly preserved';
    
    my $center1 = [ map sprintf('%.0f', $_), @{ $paths1[0]->center } ];
    ok abs($center1->[X] - scale 10) < scaled_epsilon && abs($center1->[Y] - scale 10) < scaled_epsilon, 'center was correctly detected';
    
    my $center2 = [ map sprintf('%.0f', $_), @{ $paths2[0]->center } ];
    ok abs($center2->[X] - scale 10) < scaled_epsilon && abs($center1->[Y] - scale 10) < scaled_epsilon, 'center was correctly detected';
}

#==========================================================
