#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 9;

use constant PI => 4 * atan2(1, 1);

{
    my @points = (
        Slic3r::Point->new(100,100),
        Slic3r::Point->new(100,200),
        Slic3r::Point->new(200,200),
        Slic3r::Point->new(200,100),
        Slic3r::Point->new(150,150),
    );
    my $hull = Slic3r::Geometry::convex_hull(\@points);
    isa_ok $hull, 'Slic3r::Polygon', 'convex_hull returns a Polygon';
    is scalar(@$hull), 4, 'convex_hull returns the correct number of points';
}

# directions_parallel() and directions_parallel_within() are tested
# also with Slic3r::Line::parallel_to() tests in 10_line.t
{
    ok Slic3r::Geometry::directions_parallel_within(0, 0, 0), 'directions_parallel_within';
    ok Slic3r::Geometry::directions_parallel_within(0, PI, 0), 'directions_parallel_within';
    ok Slic3r::Geometry::directions_parallel_within(0, 0, PI/180), 'directions_parallel_within';
    ok Slic3r::Geometry::directions_parallel_within(0, PI, PI/180), 'directions_parallel_within';
    ok !Slic3r::Geometry::directions_parallel_within(PI/2, PI, 0), 'directions_parallel_within';
    ok !Slic3r::Geometry::directions_parallel_within(PI/2, PI, PI/180), 'directions_parallel_within';
}

{
    my $positions = Slic3r::Geometry::arrange(4, Slic3r::Pointf->new(20, 20),
        5, Slic3r::Geometry::BoundingBoxf->new);
    is scalar(@$positions), 4, 'arrange() returns expected number of positions';
}

__END__
