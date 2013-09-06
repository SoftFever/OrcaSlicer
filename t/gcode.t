use Test::More tests => 2;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(scale);
use Slic3r::Test;

{
    my $gcodegen = Slic3r::GCode->new(
        config      => Slic3r::Config->new_from_defaults,
        layer_count => 1,
        extruders   => [],
    );
    $gcodegen->set_shift(10, 10);
    is_deeply $gcodegen->last_pos->arrayref, [scale -10, scale -10], 'last_pos is shifted correctly';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('wipe', [1]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $have_wipe = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        $have_wipe = 1 if $info->{retracting} && $info->{dist_XY} > 0;
    });
    
    ok $have_wipe, "wipe";
}

__END__
