#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 4;

my $square = [
    [100, 100],
    [200, 100],
    [200, 200],
    [100, 200],
];

my $loop = Slic3r::ExtrusionLoop->new(
    polygon  => Slic3r::Polygon::XS->new(@$square),
    role     => Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER,
);
isa_ok $loop->as_polygon, 'Slic3r::Polygon::XS', 'loop polygon';
is_deeply [ @{ $loop->as_polygon } ], [ @$square ], 'polygon points roundtrip';

$loop = $loop->clone;

is $loop->role, Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER, 'role';
$loop->role(Slic3r::ExtrusionPath::EXTR_ROLE_FILL);
is $loop->role, Slic3r::ExtrusionPath::EXTR_ROLE_FILL, 'modify role';

__END__
