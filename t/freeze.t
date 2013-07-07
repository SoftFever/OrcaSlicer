use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;
use Storable qw(nstore retrieve);
use Time::HiRes qw(gettimeofday tv_interval);

{
    my $t0 = [gettimeofday];
    my $print = Slic3r::Test::init_print('20mm_cube', scale => 2);
    my $gcode = Slic3r::Test::gcode($print);
    ###diag sprintf 'Slicing took %s seconds', tv_interval($t0);
    
    my $t1 = [gettimeofday];
    nstore $print, 'print.dat';
    $print = retrieve 'print.dat';
    unlink 'print.dat';
    ###diag sprintf 'Freezing and retrieving took %s seconds', tv_interval($t1);
    
    isa_ok $print, 'Slic3r::Print', 'restored Print object';
}

__END__
