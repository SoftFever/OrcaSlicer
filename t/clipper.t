use Test::More;

plan tests => 1;

use Math::Clipper ':all';

my $clipper = Math::Clipper->new;

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

my $square = [  # ccw
    [5, 12],
    [25, 12],
    [25, 18],
    [5, 18],
];

$clipper->add_subject_polygons([ $square, $hole_in_square ]);
$clipper->add_clip_polygons([ $square ]);
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
            [5, 18],
            [5, 12],
            [25, 12],
            [25, 18],
        ],
    },
], 'hole is preserved after intersection';
