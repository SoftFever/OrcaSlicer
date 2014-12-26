use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first sum);
use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('avoid_crossing_perimeters', 2);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 2);
    ok my $gcode = Slic3r::Test::gcode($print), "no crash with avoid_crossing_perimeters and multiple objects";
}

__END__
