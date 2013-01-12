use Test::More;
use strict;
use warnings;

plan tests => 26;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(line_atan line_direction rad2deg_dir angle3points PI);

#==========================================================

{
    is line_atan([ [0, 0],  [10, 0] ]),  (0),      'E atan2';
    is line_atan([ [10, 0], [0, 0]  ]),  (PI),     'W atan2';
    is line_atan([ [0, 0],  [0, 10] ]),  (PI/2),   'N atan2';
    is line_atan([ [0, 10], [0, 0]  ]), -(PI/2),   'S atan2';
    
    is line_atan([ [10, 10], [0, 0] ]), -(PI*3/4), 'SW atan2';
    is line_atan([ [0, 0], [10, 10] ]),  (PI*1/4), 'NE atan2';
    is line_atan([ [0, 10], [10, 0] ]), -(PI*1/4), 'SE atan2';
    is line_atan([ [10, 0], [0, 10] ]),  (PI*3/4), 'NW atan2';
}

#==========================================================

{
    is line_direction([ [0, 0],  [10, 0] ]), (0),      'E direction';
    is line_direction([ [10, 0], [0, 0]  ]), (0),      'W direction';
    is line_direction([ [0, 0],  [0, 10] ]), (PI/2),   'N direction';
    is line_direction([ [0, 10], [0, 0]  ]), (PI/2),   'S direction';
    
    is line_direction([ [10, 10], [0, 0] ]), (PI*1/4), 'SW direction';
    is line_direction([ [0, 0], [10, 10] ]), (PI*1/4), 'NE direction';
    is line_direction([ [0, 10], [10, 0] ]), (PI*3/4), 'SE direction';
    is line_direction([ [10, 0], [0, 10] ]), (PI*3/4), 'NW direction';
}

#==========================================================

{
    is rad2deg_dir(0),        90, 'E (degrees)';
    is rad2deg_dir(PI),      270, 'W (degrees)';
    is rad2deg_dir(PI/2),      0, 'N (degrees)';
    is rad2deg_dir(-(PI/2)), 180, 'S (degrees)';
    is rad2deg_dir(PI*1/4),   45, 'NE (degrees)';
    is rad2deg_dir(PI*3/4),  135, 'NW (degrees)';
    is rad2deg_dir(PI/6),     60, '30°';
    is rad2deg_dir(PI/6*2),   30, '60°';
}

#==========================================================

{
    is angle3points([0,0], [10,0], [0,10]), PI/2, 'CW angle3points';
    is angle3points([0,0], [0,10], [10,0]), PI/2*3, 'CCW angle3points';
}

#==========================================================
