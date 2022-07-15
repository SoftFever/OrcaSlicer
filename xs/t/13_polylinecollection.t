#!/usr/bin/perl

use strict;
use warnings;

use Slic3r::XS;
use Test::More tests => 3;

{
    my $collection = Slic3r::Polyline::Collection->new(
        Slic3r::Polyline->new([0,15], [0,18], [0,20]),
        Slic3r::Polyline->new([0,10], [0,8], [0,5]),
    );
    is_deeply
        [ map $_->y, map @$_, @{$collection->chained_path_from(Slic3r::Point->new(0,30), 0)} ],
        [20, 18, 15, 10, 8, 5],
        'chained_path_from';
    is_deeply
        [ map $_->y, map @$_, @{$collection->chained_path(0)} ],
        [15, 18, 20, 10, 8, 5],
        'chained_path';
}

{
    my $collection = Slic3r::Polyline::Collection->new(
        Slic3r::Polyline->new([15,0], [10,0], [4,0]),
        Slic3r::Polyline->new([10,5], [15,5], [20,5]),
    );
    is_deeply
        [ map $_->x, map @$_, @{$collection->chained_path_from(Slic3r::Point->new(30,0), 0)} ],
        [reverse 4, 10, 15, 10, 15, 20],
        'chained_path_from';
}

__END__
