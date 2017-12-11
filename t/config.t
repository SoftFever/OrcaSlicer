use Test::More tests => 1;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use Slic3r;
use Slic3r::Test;

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('perimeter_extrusion_width', '250%');
    ok $config->validate, 'percent extrusion width is validated';
}

__END__
