#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 49;

is Slic3r::TriangleMesh::hello_world(), 'Hello world!',
    'hello world';

my $cube = {
    vertices    => [ [20,20,0], [20,0,0], [0,0,0], [0,20,0], [20,20,20], [0,20,20], [0,0,20], [20,0,20] ],
    facets      => [ [0,1,2], [0,2,3], [4,5,6], [4,6,7], [0,4,7], [0,7,1], [1,7,6], [1,6,2], [2,6,5], [2,5,3], [4,0,3], [4,3,5] ],
};

{
    my $m = Slic3r::TriangleMesh->new;
    $m->ReadFromPerl($cube->{vertices}, $cube->{facets});
    $m->repair;
    my ($vertices, $facets) = ($m->vertices, $m->facets);
    
    is_deeply $vertices, $cube->{vertices}, 'vertices arrayref roundtrip';
    is_deeply $facets, $cube->{facets}, 'facets arrayref roundtrip';
    is scalar(@{$m->normals}), scalar(@$facets), 'normals returns the right number of items';
    
    {
        my $m2 = $m->clone;
        is_deeply $m2->vertices, $cube->{vertices}, 'cloned vertices arrayref roundtrip';
        is_deeply $m2->facets, $cube->{facets}, 'cloned facets arrayref roundtrip';
        $m2->scale(3);  # check that it does not affect $m
    }
    
    {
        my $stats = $m->stats;
        is $stats->{number_of_facets}, scalar(@{ $cube->{facets} }), 'stats.number_of_facets';
        ok abs($stats->{volume} - 20*20*20) < 1E-2, 'stats.volume';
    }
    
    $m->scale(2);
    ok abs($m->stats->{volume} - 40*40*40) < 1E-2, 'scale';
    
    $m->scale_xyz(Slic3r::Pointf3->new(2,1,1));
    ok abs($m->stats->{volume} - 2*40*40*40) < 1E-2, 'scale_xyz';
    
    $m->translate(5,10,0);
    is_deeply $m->vertices->[0], [85,50,0], 'translate';
    
    $m->align_to_origin;
    is_deeply $m->vertices->[2], [0,0,0], 'align_to_origin';
    
    is_deeply $m->size, [80,40,40], 'size';
    
    $m->scale_xyz(Slic3r::Pointf3->new(0.5,1,1));
    $m->rotate(45, Slic3r::Point->new(20,20));
    ok abs($m->size->[0] - sqrt(2)*40) < 1E-4, 'rotate';
    
    {
        my $meshes = $m->split;
        is scalar(@$meshes), 1, 'split';
        isa_ok $meshes->[0], 'Slic3r::TriangleMesh', 'split';
        is_deeply $m->bb3, $meshes->[0]->bb3, 'split populates stats';
    }
    
    my $m2 = Slic3r::TriangleMesh->new;
    $m2->ReadFromPerl($cube->{vertices}, $cube->{facets});
    $m2->repair;
    $m->merge($m2);
    $m->repair;
    is $m->stats->{number_of_facets}, 2 * $m2->stats->{number_of_facets}, 'merge';
    
    {
        my $meshes = $m->split;
        is scalar(@$meshes), 2, 'split';
    }
}

{
    my $m = Slic3r::TriangleMesh->new;
    $m->ReadFromPerl($cube->{vertices}, $cube->{facets});
    $m->repair;
    my @z = (0,2,4,8,6,8,10,12,14,16,18,20);
    my $result = $m->slice(\@z);
    my $SCALING_FACTOR = 0.000001;
    for my $i (0..$#z) {
        is scalar(@{$result->[$i]}), 1, "number of returned polygons per layer (z = " . $z[$i] . ")";
        is $result->[$i][0]->area, 20*20/($SCALING_FACTOR**2), 'size of returned polygon';
    }
}

{
    my $m = Slic3r::TriangleMesh->new;
    $m->ReadFromPerl(
        [ [0,0,0],[0,0,20],[0,5,0],[0,5,20],[50,0,0],[50,0,20],[15,5,0],[35,5,0],[15,20,0],[50,5,0],[35,20,0],[15,5,10],[50,5,20],[35,5,10],[35,20,10],[15,20,10] ],
        [ [0,1,2],[2,1,3],[1,0,4],[5,1,4],[0,2,4],[4,2,6],[7,6,8],[4,6,7],[9,4,7],[7,8,10],[2,3,6],[11,3,12],[7,12,9],[13,12,7],[6,3,11],[11,12,13],[3,1,5],[12,3,5],[5,4,9],[12,5,9],[13,7,10],[14,13,10],[8,15,10],[10,15,14],[6,11,8],[8,11,15],[15,11,13],[14,15,13] ],
    );
    $m->repair;
    {
        # at Z = 10 we have a top horizontal surface
        my $slices = $m->slice([ 5, 10 ]);
        is $slices->[0][0]->area, $slices->[1][0]->area, 'slicing a top tangent plane includes its area';
    }
    $m->flip_z;
    {
        # this second test also checks that performing a second slice on a mesh after
        # a transformation works properly (shared_vertices is correctly invalidated);
        # at Z = -10 we have a bottom horizontal surface
        my $slices = $m->slice([ -5, -10 ]);
        is $slices->[0][0]->area, $slices->[1][0]->area, 'slicing a bottom tangent plane includes its area';
    }
}

{
    my $m = Slic3r::TriangleMesh->new;
    $m->ReadFromPerl($cube->{vertices}, $cube->{facets});
    $m->repair;
    {
        my $upper = Slic3r::TriangleMesh->new;
        my $lower = Slic3r::TriangleMesh->new;
        $m->cut(0, $upper, $lower);
        $upper->repair; $lower->repair;
        is $upper->facets_count, 12, 'upper mesh has all facets except those belonging to the slicing plane';
        is $lower->facets_count,  0, 'lower mesh has no facets';
    }
    {
        my $upper = Slic3r::TriangleMesh->new;
        my $lower = Slic3r::TriangleMesh->new;
        $m->cut(10, $upper, $lower);
        #$upper->repair; $lower->repair;
        # we expect:
        # 2 facets on external horizontal surfaces
        # 3 facets on each side = 12 facets
        # 6 facets on the triangulated side (8 vertices)
        is $upper->facets_count, 2+12+6, 'upper mesh has the expected number of facets';
        is $lower->facets_count, 2+12+6, 'lower mesh has the expected number of facets';
    }
}

__END__
