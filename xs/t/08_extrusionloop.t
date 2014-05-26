#!/usr/bin/perl

use strict;
use warnings;

use List::Util qw(sum);
use Slic3r::XS;
use Test::More tests => 45;

{
    my $square = [
        [100, 100],
        [200, 100],
        [200, 200],
        [100, 200],
    ];
    my $square_p = Slic3r::Polygon->new(@$square);

    my $loop = Slic3r::ExtrusionLoop->new;
    $loop->append(Slic3r::ExtrusionPath->new(
        polyline    => $square_p->split_at_first_point,
        role        => Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER,
        mm3_per_mm  => 1,
    ));

    isa_ok $loop, 'Slic3r::ExtrusionLoop';
    isa_ok $loop->polygon, 'Slic3r::Polygon', 'loop polygon';
    is $loop->polygon->area, $square_p->area, 'polygon area';
    is $loop->length, $square_p->length(), 'loop length';

    $loop = $loop->clone;

    is scalar(@$loop), 1, 'loop contains one path';
    {
        my $path = $loop->[0];
        isa_ok $path, 'Slic3r::ExtrusionPath::Ref';
        is $path->role, Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER, 'role';
        $path->role(Slic3r::ExtrusionPath::EXTR_ROLE_FILL);
        is $path->role, Slic3r::ExtrusionPath::EXTR_ROLE_FILL, 'modify role';
    }

    $loop->split_at_vertex($square_p->[2]);
    is scalar(@$loop), 1, 'splitting a single-path loop results in a single path';
    is scalar(@{$loop->[0]->polyline}), 5, 'path has correct number of points';
    ok $loop->[0]->polyline->[0]->coincides_with($square_p->[2]), 'expected point order';
    ok $loop->[0]->polyline->[1]->coincides_with($square_p->[3]), 'expected point order';
    ok $loop->[0]->polyline->[2]->coincides_with($square_p->[0]), 'expected point order';
    ok $loop->[0]->polyline->[3]->coincides_with($square_p->[1]), 'expected point order';
    ok $loop->[0]->polyline->[4]->coincides_with($square_p->[2]), 'expected point order';
}

{
    my $polyline1 = Slic3r::Polyline->new([100,100], [200,100], [200,200]);
    my $polyline2 = Slic3r::Polyline->new([200,200], [100,200], [100,100]);
    
    my $loop = Slic3r::ExtrusionLoop->new_from_paths(
        Slic3r::ExtrusionPath->new(
            polyline    => $polyline1,
            role        => Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER,
            mm3_per_mm  => 1,
        ),
        Slic3r::ExtrusionPath->new(
            polyline    => $polyline2,
            role        => Slic3r::ExtrusionPath::EXTR_ROLE_OVERHANG_PERIMETER,
            mm3_per_mm  => 1,
        ),
    );
    my $tot_len = sum($polyline1->length, $polyline2->length);
    is $loop->length, $tot_len, 'length';
    is scalar(@$loop), 2, 'loop contains two paths';
    
    {
        # check splitting at intermediate point
        my $loop2 = $loop->clone;
        isa_ok $loop2, 'Slic3r::ExtrusionLoop';
        $loop2->split_at_vertex($polyline1->[1]);
        is $loop2->length, $tot_len, 'length after splitting is unchanged';
        is scalar(@$loop2), 3, 'loop contains three paths after splitting';
        ok $loop2->[0]->polyline->[0]->coincides_with($polyline1->[1]), 'expected starting point';
        ok $loop2->[-1]->polyline->[-1]->coincides_with($polyline1->[1]), 'expected ending point';
        ok $loop2->[0]->polyline->[-1]->coincides_with($loop2->[1]->polyline->[0]), 'paths have common point';
        ok $loop2->[1]->polyline->[-1]->coincides_with($loop2->[2]->polyline->[0]), 'paths have common point';
        is $loop2->[0]->role, Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER, 'expected order after splitting';
        is $loop2->[1]->role, Slic3r::ExtrusionPath::EXTR_ROLE_OVERHANG_PERIMETER, 'expected order after splitting';
        is $loop2->[2]->role, Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER, 'expected order after splitting';
        is scalar(@{$loop2->[0]->polyline}), 2, 'path has correct number of points';
        is scalar(@{$loop2->[1]->polyline}), 3, 'path has correct number of points';
        is scalar(@{$loop2->[2]->polyline}), 2, 'path has correct number of points';
    
        my @paths = @{$loop2->clip_end(3)};
        is sum(map $_->length, @paths), $loop2->length - 3, 'returned paths have expected length';
    }
    
    {
        # check splitting at endpoint
        my $loop2 = $loop->clone;
        $loop2->split_at_vertex($polyline2->[0]);
        is $loop2->length, $tot_len, 'length after splitting is unchanged';
        is scalar(@$loop2), 2, 'loop contains two paths after splitting';
        ok $loop2->[0]->polyline->[0]->coincides_with($polyline2->[0]), 'expected starting point';
        ok $loop2->[-1]->polyline->[-1]->coincides_with($polyline2->[0]), 'expected ending point';
        ok $loop2->[0]->polyline->[-1]->coincides_with($loop2->[1]->polyline->[0]), 'paths have common point';
        ok $loop2->[1]->polyline->[-1]->coincides_with($loop2->[0]->polyline->[0]), 'paths have common point';
        is $loop2->[0]->role, Slic3r::ExtrusionPath::EXTR_ROLE_OVERHANG_PERIMETER, 'expected order after splitting';
        is $loop2->[1]->role, Slic3r::ExtrusionPath::EXTR_ROLE_EXTERNAL_PERIMETER, 'expected order after splitting';
        is scalar(@{$loop2->[0]->polyline}), 3, 'path has correct number of points';
        is scalar(@{$loop2->[1]->polyline}), 3, 'path has correct number of points';
    }
    
    {
        my $loop2 = $loop->clone;
        my $point = Slic3r::Point->new(250,150);
        $loop2->split_at($point);
        is $loop2->length, $tot_len, 'length after splitting is unchanged';
        is scalar(@$loop2), 3, 'loop contains three paths after splitting';
        my $expected_start_point = Slic3r::Point->new(200,150);
        ok $loop2->[0]->polyline->[0]->coincides_with($expected_start_point), 'expected starting point';
        ok $loop2->[-1]->polyline->[-1]->coincides_with($expected_start_point), 'expected ending point';
    }
}

__END__
