use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('raft_layers', 3);
    $config->set('brim_width',  6);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'no conflict between raft/support and brim';
}

__END__
