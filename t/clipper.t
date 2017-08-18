use Test::More;
use strict;
use warnings;

plan tests => 6;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(sum);
use Slic3r;
use Slic3r::Geometry::Clipper qw(intersection_ex union_ex diff_ex diff_pl);

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
    
    is sum(map $_->area, @$intersection), Slic3r::ExPolygon->new(
        [
            [20, 18],
            [10, 18],
            [10, 12],
            [20, 12],
        ],
        [
            [14, 16],
            [16, 16],
            [16, 14],
            [14, 14],
        ],
    )->area, 'hole is preserved after intersection';
}

#==========================================================

{
    my $contour1 = [ [0,0],   [40,0],  [40,40], [0,40]  ];  # ccw
    my $contour2 = [ [10,10], [30,10], [30,30], [10,30] ];  # ccw
    my $hole     = [ [15,15], [15,25], [25,25], [25,15] ];  # cw
    
    my $union = union_ex([ $contour1, $contour2, $hole ]);
    
    is_deeply [ map $_->pp, @$union ], [[ [ [40,40], [0,40], [0,0], [40,0] ] ]],
        'union of two ccw and one cw is a contour with no holes';
    
    my $diff = diff_ex([ $contour1, $contour2 ], [ $hole ]);
    is sum(map $_->area, @$diff),
        Slic3r::ExPolygon->new([ [40,40], [0,40], [0,0], [40,0] ], [ [15,25], [25,25], [25,15], [15,15] ])->area,
        'difference of a cw from two ccw is a contour with one hole';
}

#==========================================================

{
    my $square = Slic3r::Polygon->new_scale( # ccw
        [10, 10],
        [20, 10],
        [20, 20],
        [10, 20],
    );
    my $square_pl = $square->split_at_first_point;
    
    my $res = diff_pl([$square_pl], []);
    is scalar(@$res), 1, 'no-op diff_pl returns the right number of polylines';
    isa_ok $res->[0], 'Slic3r::Polyline', 'no-op diff_pl result';
    is scalar(@{$res->[0]}), scalar(@$square_pl), 'no-op diff_pl returns the unmodified input polyline';
}

__END__
