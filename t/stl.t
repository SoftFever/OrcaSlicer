use Test::More;
use strict;
use warnings;

plan tests => 17;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(X Y Z A B);
use XXX;

my $mesh = Slic3r::TriangleMesh->new;

my @lines;
my $z = 20;
my @points = ([3, 4], [8, 5], [1, 9]);  # XY coordinates of the facet vertices

is_deeply lines(20, 20, 20), [
    [ $points[0], $points[1] ],
    [ $points[1], $points[2] ],
    [ $points[2], $points[0] ],
], 'horizontal';

is_deeply lines(22, 20, 20), [ [ $points[1], $points[2] ] ], 'lower edge on layer';
is_deeply lines(20, 20, 22), [ [ $points[0], $points[1] ] ], 'lower edge on layer';
is_deeply lines(20, 22, 20), [ [ $points[2], $points[0] ] ], 'lower edge on layer';

is_deeply lines(20, 20, 10), [ [ $points[1], $points[0] ] ], 'upper edge on layer';
is_deeply lines(10, 20, 20), [ [ $points[2], $points[1] ] ], 'upper edge on layer';
is_deeply lines(20, 10, 20), [ [ $points[0], $points[2] ] ], 'upper edge on layer';

is_deeply lines(20, 15, 10), [                            ], 'upper vertex on layer';
is_deeply lines(28, 20, 30), [                            ], 'lower vertex on layer';

{
    my @z = (24, 10, 16);
    is_deeply lines(@z), [
        [
            line_plane_intersection([ vertices(@z)->[2], vertices(@z)->[0] ]),
            line_plane_intersection([ vertices(@z)->[0], vertices(@z)->[1] ]),
        ]
    ], 'two edges intersect';
}

{
    my @z = (16, 24, 10);
    is_deeply lines(@z), [
        [
            line_plane_intersection([ vertices(@z)->[1], vertices(@z)->[2] ]),
            line_plane_intersection([ vertices(@z)->[0], vertices(@z)->[1] ]),
        ]
    ], 'two edges intersect';
}

{
    my @z = (10, 16, 24);
    is_deeply lines(@z), [
        [
            line_plane_intersection([ vertices(@z)->[2], vertices(@z)->[0] ]),
            line_plane_intersection([ vertices(@z)->[1], vertices(@z)->[2] ]),
        ]
    ], 'two edges intersect';
}

{
    my @z = (24, 10, 20);
    is_deeply lines(@z), [
        [
            $points[2],
            line_plane_intersection([ vertices(@z)->[0], vertices(@z)->[1] ]),
        ]
    ], 'one vertex on plane and one edge intersects';
}

{
    my @z = (10, 20, 24);
    is_deeply lines(@z), [
        [
            line_plane_intersection([ vertices(@z)->[2], vertices(@z)->[0] ]),
            $points[1],
        ]
    ], 'one vertex on plane and one edge intersects';
}

{
    my @z = (20, 24, 10);
    is_deeply lines(@z), [
        [
            line_plane_intersection([ vertices(@z)->[1], vertices(@z)->[2] ]),
            $points[0],
        ]
    ], 'one vertex on plane and one edge intersects';
}

my @lower = $mesh->intersect_facet(0, vertices(22, 20, 20), $z);
my @upper = $mesh->intersect_facet(0, vertices(20, 20, 10), $z);
is $lower[0]->facet_edge, 'bottom', 'bottom edge on layer';
is $upper[0]->facet_edge, 'top', 'upper edge on layer';

sub vertices {
    [ map [ @{$points[$_]}, $_[$_] ], 0..2 ]
}

sub lines {
    my @lines = $mesh->intersect_facet(0, vertices(@_), $z);
    $_->a->[X] = sprintf('%.0f', $_->a->[X]) for @lines;
    $_->a->[Y] = sprintf('%.0f', $_->a->[Y]) for @lines;
    $_->b->[X] = sprintf('%.0f', $_->b->[X]) for @lines;
    $_->b->[Y] = sprintf('%.0f', $_->b->[Y]) for @lines;
    return [ map $_->points, @lines ];
}

sub line_plane_intersection {
    my ($line) = @_;
    
    return [
        map sprintf('%.0f', $_),
            map +($line->[B][$_] + ($line->[A][$_] - $line->[B][$_]) * ($z - $line->[B][Z]) / ($line->[A][Z] - $line->[B][Z])),
                (X,Y)
    ];
}

__END__
