use Test::More;
use strict;
use warnings;

plan skip_all => 'temporarily disabled';
plan tests => 16;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

# temporarily disable compilation errors due to constant not being exported anymore
sub Slic3r::TriangleMesh::I_B {}
sub Slic3r::TriangleMesh::I_FACET_EDGE {}
sub Slic3r::TriangleMesh::FE_BOTTOM {
sub Slic3r::TriangleMesh::FE_TOP {}}

use Slic3r;
use Slic3r::Geometry qw(X Y Z A B);

my @lines;
my $z = 20;
my @points = ([3, 4], [8, 5], [1, 9]);  # XY coordinates of the facet vertices

# NOTE:
# the first point of the intersection lines is replaced by -1 because TriangleMesh.pm
# is saving memory and doesn't store point A anymore since it's not actually needed.

# We disable this test because intersect_facet() now assumes we never feed a horizontal
# facet to it.
# is_deeply lines(20, 20, 20), [
#     [ -1, $points[1] ],  # $points[0]
#     [ -1, $points[2] ],  # $points[1]
#     [ -1, $points[0] ],  # $points[2]
# ], 'horizontal';

is_deeply lines(22, 20, 20), [ [ -1, $points[2] ] ], 'lower edge on layer';  # $points[1]
is_deeply lines(20, 20, 22), [ [ -1, $points[1] ] ], 'lower edge on layer';  # $points[0]
is_deeply lines(20, 22, 20), [ [ -1, $points[0] ] ], 'lower edge on layer';  # $points[2]

is_deeply lines(20, 20, 10), [ [ -1, $points[0] ] ], 'upper edge on layer';  # $points[1]
is_deeply lines(10, 20, 20), [ [ -1, $points[1] ] ], 'upper edge on layer';  # $points[2]
is_deeply lines(20, 10, 20), [ [ -1, $points[2] ] ], 'upper edge on layer';  # $points[0]

is_deeply lines(20, 15, 10), [                            ], 'upper vertex on layer';
is_deeply lines(28, 20, 30), [                            ], 'lower vertex on layer';

{
    my @z = (24, 10, 16);
    is_deeply lines(@z), [
        [
            -1, # line_plane_intersection([ vertices(@z)->[0], vertices(@z)->[1] ]),
            line_plane_intersection([ vertices(@z)->[2], vertices(@z)->[0] ]),
        ]
    ], 'two edges intersect';
}

{
    my @z = (16, 24, 10);
    is_deeply lines(@z), [
        [
            -1, # line_plane_intersection([ vertices(@z)->[1], vertices(@z)->[2] ]),
            line_plane_intersection([ vertices(@z)->[0], vertices(@z)->[1] ]),
        ]
    ], 'two edges intersect';
}

{
    my @z = (10, 16, 24);
    is_deeply lines(@z), [
        [
            -1, # line_plane_intersection([ vertices(@z)->[2], vertices(@z)->[0] ]),
            line_plane_intersection([ vertices(@z)->[1], vertices(@z)->[2] ]),
        ]
    ], 'two edges intersect';
}

{
    my @z = (24, 10, 20);
    is_deeply lines(@z), [
        [
            -1, # line_plane_intersection([ vertices(@z)->[0], vertices(@z)->[1] ]),
            $points[2],
        ]
    ], 'one vertex on plane and one edge intersects';
}

{
    my @z = (10, 20, 24);
    is_deeply lines(@z), [
        [
            -1, # line_plane_intersection([ vertices(@z)->[2], vertices(@z)->[0] ]),
            $points[1],
        ]
    ], 'one vertex on plane and one edge intersects';
}

{
    my @z = (20, 24, 10);
    is_deeply lines(@z), [
        [
            -1, # line_plane_intersection([ vertices(@z)->[1], vertices(@z)->[2] ]),
            $points[0],
        ]
    ], 'one vertex on plane and one edge intersects';
}

my @lower = intersect(22, 20, 20);
my @upper = intersect(20, 20, 10);
is $lower[0][Slic3r::TriangleMesh::I_FACET_EDGE], Slic3r::TriangleMesh::FE_BOTTOM, 'bottom edge on layer';
is $upper[0][Slic3r::TriangleMesh::I_FACET_EDGE], Slic3r::TriangleMesh::FE_TOP, 'upper edge on layer';

my $mesh;

sub intersect {
    $mesh = Slic3r::TriangleMesh->new(
        facets      => [],
        vertices    => [],
    );
    push @{$mesh->facets}, [ [0,0,0], @{vertices(@_)} ];
    $mesh->analyze;
    return map Slic3r::TriangleMesh::unpack_line($_), $mesh->intersect_facet($#{$mesh->facets}, $z);
}

sub vertices {
    push @{$mesh->vertices}, map [ @{$points[$_]}, $_[$_] ], 0..2;
    [ ($#{$mesh->vertices}-2) .. $#{$mesh->vertices} ]
}

sub lines {
    my @lines = intersect(@_);
    #$_->a->[X] = sprintf('%.0f', $_->a->[X]) for @lines;
    #$_->a->[Y] = sprintf('%.0f', $_->a->[Y]) for @lines;
    $_->[Slic3r::TriangleMesh::I_B][X] = sprintf('%.0f', $_->[Slic3r::TriangleMesh::I_B][X]) for @lines;
    $_->[Slic3r::TriangleMesh::I_B][Y] = sprintf('%.0f', $_->[Slic3r::TriangleMesh::I_B][Y]) for @lines;
    return [ map [ -1, $_->[Slic3r::TriangleMesh::I_B] ], @lines ];
}

sub line_plane_intersection {
    my ($line) = @_;
    @$line = map $mesh->vertices->[$_], @$line;
    
    return [
        map sprintf('%.0f', $_),
            map +($line->[B][$_] + ($line->[A][$_] - $line->[B][$_]) * ($z - $line->[B][Z]) / ($line->[A][Z] - $line->[B][Z])),
                (X,Y)
    ];
}

__END__
