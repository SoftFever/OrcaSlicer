use Test::More;
use strict;
use warnings;

plan skip_all => 'temporarily disabled';
plan tests => 4;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use Slic3r::Test;

{
    # We only need to slice at one height, so we'll build a non-manifold mesh
    # that still produces complete loops at that height. Triangular walls are 
    # enough for this purpose.
    # Basically we want to check what happens when three concentric loops happen
    # to be at the same height, the two external ones being ccw and the other being
    # a hole, thus cw.
    my (@vertices, @facets) = ();
    Slic3r::Test::add_facet($_, \@vertices, \@facets) for
        # external surface below the slicing Z
        [ [0,0,0],   [20,0,10],   [0,0,10]    ],
        [ [20,0,0],  [20,20,10],  [20,0,10]   ],
        [ [20,20,0], [0,20,10],   [20,20,10]  ],
        [ [0,20,0],  [0,0,10],    [0,20,10]   ],
        
        # external insetted surface above the slicing Z
        [ [2,2,10],   [18,2,10],  [2,2,20]    ],
        [ [18,2,10],  [18,18,10], [18,2,20]   ],
        [ [18,18,10], [2,18,10],  [18,18,20]  ],
        [ [2,18,10],  [2,2,10],   [2,18,20]   ],
        
        # insetted hole below the slicing Z
        [ [15,5,0],   [5,5,10],   [15,5,10]   ],
        [ [15,15,0],  [15,5,10],  [15,15,10]  ],
        [ [5,15,0],   [15,15,10], [5,15,10]   ],
        [ [5,5,0],    [5,15,10],  [5,5,10]    ];
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->ReadFromPerl(\@vertices, \@facets);
    $mesh->analyze;
    my @lines = map $mesh->intersect_facet($_, 10), 0..$#facets;
    my $loops = Slic3r::TriangleMesh::make_loops(\@lines);
    is scalar(@$loops), 3, 'correct number of loops detected';
    is scalar(grep $_->is_counter_clockwise, @$loops), 2, 'correct number of ccw loops detected';
    
    my @surfaces = Slic3r::Layer::Region::_merge_loops($loops, 0);
    is scalar(@surfaces), 1, 'one surface detected';
    is scalar(@{$surfaces[0]->expolygon})-1, 1, 'surface has one hole';
}

__END__
