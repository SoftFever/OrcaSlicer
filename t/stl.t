use Test::More;

plan tests => 7;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

my $stl = Slic3r::STL->new;

my @lines;
my $z = 20;
my @points = ([3, 4], [8, 5], [1, 9]);

is_deeply lines(20, 20, 20), [
    [ $points[0], $points[1] ],
    [ $points[1], $points[2] ],
    [ $points[2], $points[0] ],
], 'horizontal';

is_deeply lines(22, 20, 20), [ [ $points[1], $points[2] ] ], 'lower edge on layer';
is_deeply lines(20, 20, 10), [ [ $points[0], $points[1] ] ], 'upper edge on layer';
is_deeply lines(20, 15, 10), [                            ], 'upper vertex on layer';
is_deeply lines(28, 20, 30), [                            ], 'lower vertex on layer';
is_deeply lines(24, 10, 16), [ [ [4, 4],     [2, 6]     ] ], 'two edges intersect';
is_deeply lines(24, 10, 20), [ [ [4, 4],     [1, 9]     ] ], 'one vertex on plane and one edge intersects';

sub vertices {
    [ map [ @{$points[$_]}, $_[$_] ], 0..2 ]
}

sub lines {
    [ map [ map ref $_ eq 'Slic3r::Point' ? $_->p : [ map sprintf('%.0f', $_), @$_ ], @$_ ], $stl->intersect_facet(vertices(@_), $z, $dz) ];
}
