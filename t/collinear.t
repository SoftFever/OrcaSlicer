use Test::More;
use strict;
use warnings;

plan tests => 11;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(collinear);

#==========================================================

{
    my @lines = (
        [ [0,4], [4,2] ],
        [ [2,3], [8,0] ],
        [ [6,1], [8,0] ],
    );
    is collinear($lines[0], $lines[1]), 1, 'collinear';
    is collinear($lines[1], $lines[2]), 1, 'collinear';
    is collinear($lines[0], $lines[2]), 1, 'collinear';
}

#==========================================================

{
    # horizontal
    my @lines = (
        [ [0,1], [5,1] ],
        [ [2,1], [8,1] ],
    );
    is collinear($lines[0], $lines[1]), 1, 'collinear';
}

#==========================================================

{
    # vertical
    my @lines = (
        [ [1,0], [1,5] ],
        [ [1,2], [1,8] ],
    );
    is collinear($lines[0], $lines[1]), 1, 'collinear';
}

#==========================================================

{
    # non overlapping
    my @lines = (
        [ [0,1], [5,1] ],
        [ [7,1], [10,1] ],
    );
    is collinear($lines[0], $lines[1], 1), 0, 'non overlapping';
    is collinear($lines[0], $lines[1], 0), 1, 'overlapping';
}

#==========================================================

{
    # with one common point
    my @lines = (
        [ [0,4], [4,2] ],
        [ [4,2], [8,0] ],
    );
    is collinear($lines[0], $lines[1], 1), 1, 'one common point';
    is collinear($lines[0], $lines[1], 0), 1, 'one common point';
}

#==========================================================

{
    # not collinear
    my @lines = (
        [ [290000000,690525600], [285163380,684761540] ],
        [ [285163380,684761540], [193267599,575244400] ],
    );
    is collinear($lines[0], $lines[1], 0), 0, 'not collinear';
    is collinear($lines[0], $lines[1], 1), 0, 'not collinear';
}

#==========================================================
