#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 5;

my $points = [
    [100, 100],
    [200, 100],
    [200, 200],
];

my $path = Slic3r::ExtrusionPath->new(
    polyline => Slic3r::Polyline->new(@$points),
    role     => Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER,
);

my $loop = Slic3r::ExtrusionLoop->new(
    polygon => Slic3r::Polygon->new(@$points),
    role     => Slic3r::ExtrusionPath::EXTR_ROLE_FILL,
);

my $collection = Slic3r::ExtrusionPath::Collection->new;
isa_ok $collection, 'Slic3r::ExtrusionPath::Collection', 'collection object';

$collection->append($path);
is scalar(@$collection), 1, 'append ExtrusionPath';

$collection->append($loop);
is scalar(@$collection), 2, 'append ExtrusionLoop';

isa_ok $collection->[0], 'Slic3r::ExtrusionPath', 'correct object returned for path';
isa_ok $collection->[1], 'Slic3r::ExtrusionLoop', 'correct object returned for loop';

__END__
