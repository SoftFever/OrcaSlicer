#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 5;

is Slic3r::TriangleMesh::XS::hello_world(), 'Hello world!',
    'hello world';

my $cube = {
    vertices    => [ [20,20,0], [20,0,0], [0,0,0], [0,20,0], [20,20,20], [0,20,20], [0,0,20], [20,0,20] ],
    facets      => [ [0,1,2], [0,2,3], [4,5,6], [4,6,7], [0,4,7], [0,7,1], [1,7,6], [1,6,2], [2,6,5], [2,5,3], [4,0,3], [4,3,5] ],
};

{
    my $m = Slic3r::TriangleMesh::XS->new;
    $m->ReadFromPerl($cube->{vertices}, $cube->{facets});
    $m->Repair;
    my ($vertices, $facets) = @{$m->ToPerl};
    is_deeply $vertices, $cube->{vertices}, 'vertices arrayref roundtrip';
    is_deeply $facets, $cube->{facets}, 'facets arrayref roundtrip';
    
    my $stats = $m->stats;
    is $stats->{number_of_facets}, scalar(@{ $cube->{facets} }), 'stats.number_of_facets';
    ok abs($stats->{volume} - 20*20*20) < 1E-3, 'stats.volume';
}

__END__
