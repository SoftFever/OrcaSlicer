use Test::More;
use strict;
use warnings;

plan tests => 9;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(X Y Z);
use XXX;

my $mesh = Slic3r::TriangleMesh->new;

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

my @lower = $mesh->intersect_facet(0, vertices(22, 20, 20), $z);
my @upper = $mesh->intersect_facet(0, vertices(20, 20, 10), $z);
is $lower[0]->facet_edge, 'bottom', 'bottom edge on layer';
is $upper[0]->facet_edge, 'top', 'upper edge on layer';

sub vertices {
    [ map [ @{$points[$_]}, $_[$_] ], X,Y,Z ]
}

sub lines {
    my @lines = $mesh->intersect_facet(0, vertices(@_), $z);
    $_->a->[X] = sprintf('%.0f', $_->a->[X]) for @lines;
    $_->a->[Y] = sprintf('%.0f', $_->a->[Y]) for @lines;
    $_->b->[X] = sprintf('%.0f', $_->b->[X]) for @lines;
    $_->b->[Y] = sprintf('%.0f', $_->b->[Y]) for @lines;
    return [ map $_->points, @lines ];
}
