use Test::More;

plan tests => 9;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

my $square = [
    [10, 10],
    [20, 10],
    [20, 20],
    [10, 20],
];

my $line = [ [5, 15], [30, 15] ];

my $intersection = Slic3r::Geometry::clip_segment_polygon($line, $square);
is_deeply $intersection, [ [10, 15], [20, 15] ], 'line is clipped to square';

$intersection = Slic3r::Geometry::clip_segment_polygon([ [0, 15], [8, 15] ], $square);
is $intersection, undef, 'external lines are ignored 1';

$intersection = Slic3r::Geometry::clip_segment_polygon([ [30, 15], [40, 15] ], $square);
is $intersection, undef, 'external lines are ignored 2';

$intersection = Slic3r::Geometry::clip_segment_polygon([ [12, 12], [18, 16] ], $square);
is_deeply $intersection, [ [12, 12], [18, 16] ], 'internal lines are preserved';

is Slic3r::Geometry::point_in_segment([10, 10], [ [5, 10], [20, 10] ]), 1, 'point in horizontal segment';
is Slic3r::Geometry::point_in_segment([30, 10], [ [5, 10], [20, 10] ]), 0, 'point not in horizontal segment';
is Slic3r::Geometry::point_in_segment([10, 10], [ [10, 5], [10, 20] ]), 1, 'point in vertical segment';
is Slic3r::Geometry::point_in_segment([10, 30], [ [10, 5], [10, 20] ]), 0, 'point not in vertical segment';
is Slic3r::Geometry::point_in_segment([15, 15], [ [10, 10], [20, 20] ]), 1, 'point in diagonal segment';
is Slic3r::Geometry::point_in_segment([20, 15], [ [10, 10], [20, 20] ]), 0, 'point not in diagonal segment';
