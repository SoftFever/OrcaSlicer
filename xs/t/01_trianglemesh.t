#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 7;

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
    my ($vertices, $facets) = ($m->vertices, $m->facets);
    
    is_deeply $vertices, $cube->{vertices}, 'vertices arrayref roundtrip';
    is_deeply $facets, $cube->{facets}, 'facets arrayref roundtrip';
    
    {
        my $stats = $m->stats;
        is $stats->{number_of_facets}, scalar(@{ $cube->{facets} }), 'stats.number_of_facets';
        ok abs($stats->{volume} - 20*20*20) < 1E-2, 'stats.volume';
    }
    
    $m->scale(2);
    ok abs($m->stats->{volume} - 40*40*40) < 1E-2, 'scale';
    
    $m->translate(5,10,0);
    is_deeply $m->vertices->[0], [45,50,0], 'translate';
}

__END__
