#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 18;

my $points = [
    [100, 100],
    [200, 100],
    [200, 200],
];

my $path = Slic3r::ExtrusionPath->new(
    polyline => Slic3r::Polyline->new(@$points),
    role     => Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER,
    mm3_per_mm => 1,
);

my $loop = Slic3r::ExtrusionLoop->new_from_paths(
    Slic3r::ExtrusionPath->new(
        polyline => Slic3r::Polygon->new(@$points)->split_at_first_point,
        role     => Slic3r::ExtrusionPath::EXTR_ROLE_FILL,
        mm3_per_mm => 1,
    ),
);

my $collection = Slic3r::ExtrusionPath::Collection->new(
    $path,
);
isa_ok $collection, 'Slic3r::ExtrusionPath::Collection', 'collection object with items in constructor';
ok !$collection->no_sort, 'no_sort is false by default';

$collection->append($collection);
is scalar(@$collection), 2, 'append ExtrusionPath::Collection';

$collection->append($path);
is scalar(@$collection), 3, 'append ExtrusionPath';

$collection->append($loop);
is scalar(@$collection), 4, 'append ExtrusionLoop';

isa_ok $collection->[1], 'Slic3r::ExtrusionPath::Collection::Ref', 'correct object returned for collection';
isa_ok $collection->[2], 'Slic3r::ExtrusionPath::Ref', 'correct object returned for path';
isa_ok $collection->[3], 'Slic3r::ExtrusionLoop::Ref', 'correct object returned for loop';
is ref($collection->[2]->clone), 'Slic3r::ExtrusionPath', 'correct object returned for cloned path';
is ref($collection->[3]->clone), 'Slic3r::ExtrusionLoop', 'correct object returned for cloned loop';

is scalar(@{$collection->[1]}), 1, 'appended collection was duplicated';

{
    my $collection_loop = $collection->[3];
    $collection_loop->polygon->scale(2);
    is_deeply $collection->[3]->polygon->pp, $collection_loop->polygon->pp, 'items are returned by reference';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(
        map Slic3r::ExtrusionPath->new(polyline => $_, role => 0, mm3_per_mm => 1),
            Slic3r::Polyline->new([0,15], [0,18], [0,20]),
            Slic3r::Polyline->new([0,10], [0,8], [0,5]),
    );
    is_deeply
        [ map $_->y, map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(0,30), 0)} ],
        [20, 18, 15, 10, 8, 5],
        'chained_path_from';
    is_deeply
        [ map $_->y, map @{$_->polyline}, @{$collection->chained_path(0)} ],
        [15, 18, 20, 10, 8, 5],
        'chained_path';
}

{
    my $collection = Slic3r::ExtrusionPath::Collection->new(
        map Slic3r::ExtrusionPath->new(polyline => $_, role => 0, mm3_per_mm => 1),
            Slic3r::Polyline->new([15,0], [10,0], [4,0]),
            Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    );
    is_deeply
        [ map $_->x, map @{$_->polyline}, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained_path_from';
    
    $collection->no_sort(1);
    my @foo = @{$collection->chained_path(0)};
    pass 'chained_path with no_sort';
}

{
    my $coll2 = $collection->clone;
    ok !$coll2->no_sort, 'expected no_sort value';
    $coll2->no_sort(1);
    ok $coll2->clone->no_sort, 'no_sort is kept after clone';
}

__END__
