#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 2;

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

__END__
