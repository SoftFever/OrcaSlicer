use Test::More;
use strict;
use warnings;

plan tests => 6;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

{
    my $path = Slic3r::ExtrusionPath->cast([
        [135322.42,26654.96], [187029.11,99546.23], [222515.14,92381.93], [258001.16,99546.23], 
        [286979.42,119083.91], [306517.1,148062.17], [313681.4,183548.2],
        [306517.1,219034.23], [286979.42,248012.49], [258001.16,267550.17], [222515.14,274714.47], 
        [187029.11,267550.17], [158050.85,248012.49], [138513.17,219034.23], [131348.87,183548.2], 
        [86948.77,175149.09], [119825.35,100585],
    ]);
    
    my $collection = Slic3r::ExtrusionPath::Collection->new(paths => [$path]);
    $collection->detect_arcs(30);
    
    is scalar(@{$collection->paths}), 3, 'path collection now contains three paths';
    isa_ok $collection->paths->[1], 'Slic3r::ExtrusionPath::Arc', 'second one';
}

#==========================================================

{
    my $path = Slic3r::ExtrusionPath->cast([
        [10,20], [10.7845909572784,19.9691733373313], [11.5643446504023,19.8768834059514], 
        [12.3344536385591,19.7236992039768], [13.0901699437495,19.5105651629515], 
        [13.8268343236509,19.2387953251129], [14.5399049973955,18.9100652418837], 
        [15.2249856471595,18.5264016435409], [15.8778525229247,18.0901699437495], 
        [16.4944804833018,17.6040596560003],
    ]);
    my $collection = Slic3r::ExtrusionPath::Collection->new(paths => [$path]);
    $collection->detect_arcs(10, 1);
    
    is scalar(@{$collection->paths}), 1, 'path collection now contains one path';
    isa_ok $collection->paths->[0], 'Slic3r::ExtrusionPath::Arc', 'path';
    is $collection->paths->[0]->orientation, 'cw', 'orientation was correctly detected';
    my $center = [ map sprintf('%.0f', $_), @{ $collection->paths->[0]->center } ];
    is_deeply $center, [10,10], 'center was correctly detected';
}

#==========================================================
