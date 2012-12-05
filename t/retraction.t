use Test::More tests => 3;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;

my $config = Slic3r::Config->new_from_defaults;

my $test = sub {
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $retracted = 1;  # ignore the first travel move from home to first point
    my $lifted = 0;
    Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{dist_Z}) {
            # lift move or lift + change layer
            if (Slic3r::Test::compare($info->{dist_Z}, $config->retract_lift->[0])
                || (Slic3r::Test::compare($info->{dist_Z}, $config->layer_height + $config->retract_lift->[0]) && $config->retract_lift->[0] > 0)) {
                fail 'only lifting while retracted' if !$retracted;
                $lifted = 1;
            }
            if ($info->{dist_Z} < 0) {
                fail 'going down only after lifting' if !$lifted;
                fail 'going down by the same amount of the lift'
                    if !Slic3r::Test::compare($info->{dist_Z}, -$config->retract_lift->[0]);
                $lifted = 0;
            }
        }
        if ($info->{retracting}) {
            fail 'retracted by the correct amount'
                if !Slic3r::Test::compare(-$info->{dist_E}, $config->retract_length->[0]);
            $retracted = 1;
        }
        if ($info->{extruding}) {
            fail 'only extruding while not lifted' if $lifted;
            if ($retracted) {
                fail 'unretracted by the correct amount'
                    if !Slic3r::Test::compare($info->{dist_E}, $config->retract_length->[0] + $config->retract_restart_extra->[0]);
                $retracted = 0;
            }
        }
        if ($info->{travel} && $info->{dist_XY} >= $config->retract_before_travel->[0]) {
            fail 'retracted before long travel move' if !$retracted;
        }
    });
    
    1;
};

$config->set('retract_length',          [1.5]);
$config->set('retract_before_travel',   [3]);
ok $test->(), 'retraction';

$config->set('retract_restart_extra',   [1]);
ok $test->(), 'restart extra length';

$config->set('retract_lift',            [1]);
ok $test->(), 'lift';

__END__
