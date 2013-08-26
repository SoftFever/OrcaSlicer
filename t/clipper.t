use Test::More;
use strict;
use warnings;

plan tests => 3;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry::Clipper qw(intersection_ex union_ex diff_ex);

{
    my $square = [ # ccw
        [10, 10],
        [20, 10],
        [20, 20],
        [10, 20],
    ];
    my $hole_in_square = [  # cw
        [14, 14],
        [14, 16],
        [16, 16],
        [16, 14],
    ];
    my $square2 = [  # ccw
        [5, 12],
        [25, 12],
        [25, 18],
        [5, 18],
    ];
    my $intersection = intersection_ex([ $square, $hole_in_square ], [ $square2 ]);
    
    is_deeply [ map $_->pp, @$intersection ], [[
        [
            [20, 12],
            [20, 18],
            [10, 18],
            [10, 12],
        ],
        [
            [14, 14],
            [14, 16],
            [16, 16],
            [16, 14],
        ],
    ]], 'hole is preserved after intersection';
}

#==========================================================

{
    my $contour1 = [ [0,0],   [40,0],  [40,40], [0,40]  ];  # ccw
    my $contour2 = [ [10,10], [30,10], [30,30], [10,30] ];  # ccw
    my $hole     = [ [15,15], [15,25], [25,25], [25,15] ];  # cw
    
    my $union = union_ex([ $contour1, $contour2, $hole ]);
    
    is_deeply [ map $_->pp, @$union ], [[ [ [40,0], [40,40], [0,40], [0,0] ] ]],
        'union of two ccw and one cw is a contour with no holes';
    
    my $diff = diff_ex([ $contour1, $contour2 ], [ $hole ]);
    is_deeply [ map $_->pp, @$diff ], [[ [ [40,0], [40,40], [0,40], [0,0] ], [ [15,15], [15,25], [25,25], [25,15] ] ]],
        'difference of a cw from two ccw is a contour with one hole';
}

