#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 8;

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

my $collection = Slic3r::ExtrusionPath::Collection->new($path);
isa_ok $collection, 'Slic3r::ExtrusionPath::Collection', 'collection object with items in constructor';

$collection->append($collection);
is scalar(@$collection), 2, 'append ExtrusionPath::Collection';

$collection->append($path);
is scalar(@$collection), 3, 'append ExtrusionPath';

$collection->append($loop);
is scalar(@$collection), 4, 'append ExtrusionLoop';

isa_ok $collection->[1], 'Slic3r::ExtrusionPath::Collection', 'correct object returned for collection';
isa_ok $collection->[2], 'Slic3r::ExtrusionPath', 'correct object returned for path';
isa_ok $collection->[3], 'Slic3r::ExtrusionLoop', 'correct object returned for loop';

is scalar(@{$collection->[1]}), 1, 'appended collection was duplicated';


__END__
