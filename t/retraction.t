use Test::More tests => 9;
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
    my ($conf) = @_;
    $conf ||= $config;
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $conf);
    
    my $retracted = 1;  # ignore the first travel move from home to first point
    my $lifted = 0;
    Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{dist_Z}) {
            # lift move or lift + change layer
            if (Slic3r::Test::compare($info->{dist_Z}, $conf->retract_lift->[0])
                || (Slic3r::Test::compare($info->{dist_Z}, $conf->layer_height + $conf->retract_lift->[0]) && $conf->retract_lift->[0] > 0)) {
                fail 'only lifting while retracted' if !$retracted && !($conf->g0 && $info->{retracting});
                $lifted = 1;
            }
            if ($info->{dist_Z} < 0) {
                fail 'going down only after lifting' if !$lifted;
                fail 'going down by the same amount of the lift'
                    if !Slic3r::Test::compare($info->{dist_Z}, -$conf->retract_lift->[0]);
                $lifted = 0;
            }
        }
        if ($info->{retracting}) {
            fail 'retracted by the correct amount'
                if !Slic3r::Test::compare(-$info->{dist_E}, $conf->retract_length->[0]);
            fail 'combining retraction and travel with G0'
                if $cmd ne 'G0' && $conf->g0 && ($info->{dist_Z} || $info->{dist_XY});
            $retracted = 1;
        }
        if ($info->{extruding}) {
            fail 'only extruding while not lifted' if $lifted;
            if ($retracted) {
                fail 'unretracted by the correct amount'
                    if !Slic3r::Test::compare($info->{dist_E}, $conf->retract_length->[0] + $conf->retract_restart_extra->[0]);
                $retracted = 0;
            }
        }
        if ($info->{travel} && $info->{dist_XY} >= $conf->retract_before_travel->[0]) {
            fail 'retracted before long travel move' if !$retracted;
        }
    });
    
    1;
};

$config->set('retract_length',          [1.5]);
$config->set('retract_before_travel',   [3]);

my $retract_tests = sub {
    my ($descr) = @_;
    
    ok $test->(), "retraction$descr";
    
    my $conf = $config->clone;
    $conf->set('retract_restart_extra',   [1]);
    ok $test->($conf), "restart extra length$descr";
    
    $conf->set('retract_lift',            [1]);
    ok $test->($conf), "lift$descr";
};

$retract_tests->('');
$config->set('duplicate', 2);
$retract_tests->(' (duplicate)');
$config->set('g0', 1);
$retract_tests->(' (G0 and duplicate)');

__END__
