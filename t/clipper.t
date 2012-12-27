use Test::More;
use strict;
use warnings;

plan tests => 3;

use Math::Clipper ':all';


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
    my $clipper = Math::Clipper->new;
    $clipper->add_subject_polygons([ $square, $hole_in_square ]);
    $clipper->add_clip_polygons([ $square2 ]);
    my $intersection = $clipper->ex_execute(CT_INTERSECTION, PFT_NONZERO, PFT_NONZERO);
    is_deeply $intersection, [
        {
            holes => [
                [
                    [14, 16],
                    [16, 16],
                    [16, 14],
                    [14, 14],
                ],
            ],
            outer => [
                [10, 18],
                [10, 12],
                [20, 12],
                [20, 18],
            ],
        },
    ], 'hole is preserved after intersection';
}

#==========================================================

{
    my $contour1 = [ [0,0],   [40,0],  [40,40], [0,40]  ];  # ccw
    my $contour2 = [ [10,10], [30,10], [30,30], [10,30] ];  # ccw
    my $hole     = [ [15,15], [15,25], [25,25], [25,15] ];  # cw
    
    my $clipper = Math::Clipper->new;
    $clipper->add_subject_polygons([ $contour1, $contour2, $hole ]);
    my $union = $clipper->ex_execute(CT_UNION, PFT_NONZERO, PFT_NONZERO);
    is_deeply $union, [{ holes => [], outer => [ [0,40], [0,0], [40,0], [40,40] ] }],
        'union of two ccw and one cw is a contour with no holes';
    
    $clipper->clear;
    $clipper->add_subject_polygons([ $contour1, $contour2 ]);
    $clipper->add_clip_polygons([ $hole ]);
    my $diff = $clipper->ex_execute(CT_DIFFERENCE, PFT_NONZERO, PFT_NONZERO);
    is_deeply $diff, [{ holes => [[ [15,25], [25,25], [25,15], [15,15] ]], outer => [ [0,40], [0,0], [40,0], [40,40] ] }],
        'difference of a cw from two ccw is a contour with one hole';
}

