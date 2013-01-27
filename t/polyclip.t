use Test::More;
use strict;
use warnings;

plan tests => 24;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Math::Clipper qw(is_counter_clockwise);
use Slic3r;

#==========================================================

is Slic3r::Geometry::point_in_segment([10, 10], [ [5, 10], [20, 10] ]), 1, 'point in horizontal segment';
is Slic3r::Geometry::point_in_segment([30, 10], [ [5, 10], [20, 10] ]), 0, 'point not in horizontal segment';
is Slic3r::Geometry::point_in_segment([10, 10], [ [10, 5], [10, 20] ]), 1, 'point in vertical segment';
is Slic3r::Geometry::point_in_segment([10, 30], [ [10, 5], [10, 20] ]), 0, 'point not in vertical segment';
is Slic3r::Geometry::point_in_segment([15, 15], [ [10, 10], [20, 20] ]), 1, 'point in diagonal segment';
is Slic3r::Geometry::point_in_segment([20, 15], [ [10, 10], [20, 20] ]), 0, 'point not in diagonal segment';

#==========================================================

my $square = [  # ccw
    [10, 10],
    [20, 10],
    [20, 20],
    [10, 20],
];

my $line = Slic3r::Line->new([5, 15], [30, 15]);

my $intersection = Slic3r::Geometry::clip_segment_polygon($line, $square);
is_deeply $intersection, [ [10, 15], [20, 15] ], 'line is clipped to square';

#==========================================================

$intersection = Slic3r::Geometry::clip_segment_polygon([ [0, 15], [8, 15] ], $square);
is $intersection, undef, 'external lines are ignored 1';

#==========================================================

$intersection = Slic3r::Geometry::clip_segment_polygon([ [30, 15], [40, 15] ], $square);
is $intersection, undef, 'external lines are ignored 2';

#==========================================================

$intersection = Slic3r::Geometry::clip_segment_polygon([ [12, 12], [18, 16] ], $square);
is_deeply $intersection, [ [12, 12], [18, 16] ], 'internal lines are preserved';

#==========================================================

{
    my $hole_in_square = [  # cw
        [14, 14],
        [14, 16],
        [16, 16],
        [16, 14],
    ];
    my $expolygon = Slic3r::ExPolygon->new($square, $hole_in_square);
    is $expolygon->encloses_point([10, 10]), 1, 'corner point is recognized';
    is $expolygon->encloses_point([10, 18]), 1, 'point on contour is recognized';
    is $expolygon->encloses_point([14, 15]), 1, 'point on hole contour is recognized';
    is $expolygon->encloses_point([14, 14]), 1, 'point on hole corner is recognized';
    {
        my $intersections = $expolygon->clip_line(Slic3r::Line->new([15,18], [15,15]));
        is_deeply $intersections, [
            [ [15, 18], [15, 16] ],
        ], 'line is clipped to square with hole';
    }
    {
        my $intersections = $expolygon->clip_line(Slic3r::Line->new([15,15], [15,12]));
        is_deeply $intersections, [
            [ [15, 14], [15, 12] ],
        ], 'line is clipped to square with hole';
    }
    {
        my $intersections = $expolygon->clip_line(Slic3r::Line->new([12,18], [18,18]));
        is_deeply $intersections, [
            [ [12,18], [18,18] ],
        ], 'line is clipped to square with hole';
    }
    {
        my $intersections = $expolygon->clip_line($line);
        is_deeply $intersections, [
            [ [10, 15], [14, 15] ],
            [ [16, 15], [20, 15] ],
        ], 'line is clipped to square with hole';
    }
    {
        my $intersections = $expolygon->clip_line(Slic3r::Line->new(reverse @$line));
        is_deeply $intersections, [
            [ [20, 15], [15, 15] ],
            [ [14, 15], [10, 15] ],
        ], 'reverse line is clipped to square with hole';
    }
    {
        my $intersections = $expolygon->clip_line(Slic3r::Line->new([10,18], [20,18]));
        is_deeply $intersections, [
            [ [10, 18], [20, 18] ],
        ], 'tangent line is clipped to square with hole';
    }
    {
        my $polyline = Slic3r::Polyline->new([ [5, 18], [25, 18], [25, 15], [15, 15], [15, 12], [12, 12], [12, 5] ]);
        is_deeply [ map $_, $polyline->clip_with_expolygon($expolygon) ], [
            [ [10, 18], [20, 18] ],
            [ [20, 15], [16, 15] ],
            [ [15, 14], [15, 12], [12, 12], [12, 10] ],
        ], 'polyline is clipped to square with hole';
    }
}

#==========================================================

{
    my $large_circle = [  # ccw
        [151.8639,288.1192], [133.2778,284.6011], [115.0091,279.6997], [98.2859,270.8606], [82.2734,260.7933], 
        [68.8974,247.4181], [56.5622,233.0777], [47.7228,216.3558], [40.1617,199.0172], [36.6431,180.4328], 
        [34.932,165.2312], [37.5567,165.1101], [41.0547,142.9903], [36.9056,141.4295], [40.199,124.1277], 
        [47.7776,106.7972], [56.6335,90.084], [68.9831,75.7557], [82.3712,62.3948], [98.395,52.3429], 
        [115.1281,43.5199], [133.4004,38.6374], [151.9884,35.1378], [170.8905,35.8571], [189.6847,37.991], 
        [207.5349,44.2488], [224.8662,51.8273], [240.0786,63.067], [254.407,75.4169], [265.6311,90.6406], 
        [275.6832,106.6636], [281.9225,124.52], [286.8064,142.795], [287.5061,161.696], [286.7874,180.5972], 
        [281.8856,198.8664], [275.6283,216.7169], [265.5604,232.7294], [254.3211,247.942], [239.9802,260.2776], 
        [224.757,271.5022], [207.4179,279.0635], [189.5605,285.3035], [170.7649,287.4188],
    ];
    is is_counter_clockwise($large_circle), 1, "contour is counter-clockwise";
    
    my $small_circle = [  # cw
        [158.227,215.9007], [164.5136,215.9007], [175.15,214.5007], [184.5576,210.6044], [190.2268,207.8743], 
        [199.1462,201.0306], [209.0146,188.346], [213.5135,177.4829], [214.6979,168.4866], [216.1025,162.3325], 
        [214.6463,151.2703], [213.2471,145.1399], [209.0146,134.9203], [199.1462,122.2357], [189.8944,115.1366], 
        [181.2504,111.5567], [175.5684,108.8205], [164.5136,107.3655], [158.2269,107.3655], [147.5907,108.7656], 
        [138.183,112.6616], [132.5135,115.3919], [123.5943,122.2357], [113.7259,134.92], [109.2269,145.7834], 
        [108.0426,154.7799], [106.638,160.9339], [108.0941,171.9957], [109.4933,178.1264], [113.7259,188.3463], 
        [123.5943,201.0306], [132.8461,208.1296], [141.4901,211.7094], [147.172,214.4458],
    ];
    is is_counter_clockwise($small_circle), 0, "hole is clockwise";
    
    my $expolygon = Slic3r::ExPolygon->new($large_circle, $small_circle);
    $line = Slic3r::Line->new([152.742,288.086671142818], [152.742,34.166466971035]);
    
    my $intersections = $expolygon->clip_line($line);
    is_deeply $intersections, [
        [ [152, 287], [152, 214],  ],
        [ [152, 107], [152, 35] ],
    ], 'line is clipped to square with hole';
}

#==========================================================
