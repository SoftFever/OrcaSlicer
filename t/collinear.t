use Test::More;
use strict;
use warnings;

plan tests => 11;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use Slic3r::Geometry qw(collinear);

#==========================================================

{
    my @lines = (
        Slic3r::Line->new([0,4], [4,2]),
        Slic3r::Line->new([2,3], [8,0]),
        Slic3r::Line->new([6,1], [8,0]),
    );
    is collinear($lines[0], $lines[1]), 1, 'collinear';
    is collinear($lines[1], $lines[2]), 1, 'collinear';
    is collinear($lines[0], $lines[2]), 1, 'collinear';
}

#==========================================================

{
    # horizontal
    my @lines = (
        Slic3r::Line->new([0,1], [5,1]),
        Slic3r::Line->new([2,1], [8,1]),
    );
    is collinear($lines[0], $lines[1]), 1, 'collinear';
}

#==========================================================

{
    # vertical
    my @lines = (
        Slic3r::Line->new([1,0], [1,5]),
        Slic3r::Line->new([1,2], [1,8]),
    );
    is collinear($lines[0], $lines[1]), 1, 'collinear';
}

#==========================================================

{
    # non overlapping
    my @lines = (
        Slic3r::Line->new([0,1], [5,1]),
        Slic3r::Line->new([7,1], [10,1]),
    );
    is collinear($lines[0], $lines[1], 1), 0, 'non overlapping';
    is collinear($lines[0], $lines[1], 0), 1, 'overlapping';
}

#==========================================================

{
    # with one common point
    my @lines = (
        Slic3r::Line->new([0,4], [4,2]),
        Slic3r::Line->new([4,2], [8,0]),
    );
    is collinear($lines[0], $lines[1], 1), 1, 'one common point';
    is collinear($lines[0], $lines[1], 0), 1, 'one common point';
}

#==========================================================

{
    # not collinear
    my @lines = (
        Slic3r::Line->new([290000000,690525600], [285163380,684761540]),
        Slic3r::Line->new([285163380,684761540], [193267599,575244400]),
    );
    is collinear($lines[0], $lines[1], 0), 0, 'not collinear';
    is collinear($lines[0], $lines[1], 1), 0, 'not collinear';
}

#==========================================================
