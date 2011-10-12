use Test::More;
use strict;
use warnings;

plan tests => 9;

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

is_deeply lines(22, 20, 20), [ [ $points[2], $points[1] ] ], 'lower edge on layer';
is_deeply lines(20, 20, 10), [ [ $points[0], $points[1] ] ], 'upper edge on layer';
is_deeply lines(20, 15, 10), [                            ], 'upper vertex on layer';
is_deeply lines(28, 20, 30), [                            ], 'lower vertex on layer';
is_deeply lines(24, 10, 16), [ [ [4, 4],     [2, 6]     ] ], 'two edges intersect';
is_deeply lines(24, 10, 20), [ [ [4, 4],     [1, 9]     ] ], 'one vertex on plane and one edge intersects';

my @lower = $stl->intersect_facet(vertices(22, 20, 20), $z);
my @upper = $stl->intersect_facet(vertices(20, 20, 10), $z);
isa_ok $lower[0], 'Slic3r::Line::FacetEdge::Bottom', 'bottom edge on layer';
isa_ok $upper[0], 'Slic3r::Line::FacetEdge::Top', 'upper edge on layer';

sub vertices {
    [ map [ @{$points[$_]}, $_[$_] ], 0..2 ]
}

sub lines {
    [ map [ map [ map sprintf('%.0f', $_), @$_ ], @$_ ], $stl->intersect_facet(vertices(@_), $z) ];
}
