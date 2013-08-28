use Test::More;
use strict;
use warnings;

plan skip_all => 'arcs are currently disabled';
plan tests => 13;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scaled_epsilon scale X Y);

{
    my $path = Slic3r::ExtrusionPath->new(polyline => Slic3r::Polyline->new(
        [135322.42,26654.96], [187029.11,99546.23], [222515.14,92381.93], [258001.16,99546.23], 
        [286979.42,119083.91], [306517.1,148062.17], [313681.4,183548.2],
        [306517.1,219034.23], [286979.42,248012.49], [258001.16,267550.17], [222515.14,274714.47], 
        [187029.11,267550.17], [158050.85,248012.49], [138513.17,219034.23], [131348.87,183548.2], 
        [86948.77,175149.09], [119825.35,100585],
    ), role => EXTR_ROLE_FILL, flow_spacing => 0.5);
    
    my @paths = $path->detect_arcs(30);
    
    is scalar(@paths), 3, 'path collection now contains three paths';
    isa_ok $paths[1], 'Slic3r::ExtrusionPath::Arc', 'second one';
}

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
        flow_spacing => 0.5,
    );
    my $path2 = Slic3r::ExtrusionPath->new(
        polyline    => Slic3r::Polyline->new(reverse @points),
        role        => EXTR_ROLE_FILL,
        flow_spacing => 0.5,
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
