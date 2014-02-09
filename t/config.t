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
    $config->set('layer_height', 0.123);
    $config->setenv;
    is $ENV{SLIC3R_LAYER_HEIGHT}, '0.123', 'setenv';
}

__END__
