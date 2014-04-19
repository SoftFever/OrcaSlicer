use Test::More tests => 17;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test qw(_eq);

{
    my $config = Slic3r::Config->new_from_defaults;
    my $duplicate = 1;

    my $test = sub {
        my ($conf) = @_;
        $conf ||= $config;
    
        my $print = Slic3r::Test::init_print('20mm_cube', config => $conf, duplicate => $duplicate);
    
        my $tool = 0;
        my @toolchange_count = (); # track first usages so that we don't expect retract_length_toolchange when extruders are used for the first time
        my @retracted = (1);  # ignore the first travel move from home to first point
        my @retracted_length = (0);
        my $lifted = 0;
        my $changed_tool = 0;
        my $wait_for_toolchange = 0;
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($cmd =~ /^T(\d+)/) {
                $tool = $1;
                $changed_tool = 1;
                $wait_for_toolchange = 0;
                $toolchange_count[$tool] //= 0;
                $toolchange_count[$tool]++;
            } elsif ($cmd =~ /^G[01]$/ && !$args->{Z}) { # ignore lift taking place after retraction
                fail 'toolchange happens right after retraction' if $wait_for_toolchange;
            }
        
            if ($info->{dist_Z}) {
                # lift move or lift + change layer
                if (_eq($info->{dist_Z}, $print->config->get_at('retract_lift', $tool))
                    || (_eq($info->{dist_Z}, $conf->layer_height + $print->config->get_at('retract_lift', $tool)) && $print->config->get_at('retract_lift', $tool) > 0)) {
                    fail 'only lifting while retracted' if !$retracted[$tool] && !($conf->g0 && $info->{retracting});
                    fail 'double lift' if $lifted;
                    $lifted = 1;
                }
                if ($info->{dist_Z} < 0) {
                    fail 'going down only after lifting' if !$lifted;
                    fail 'going down by the same amount of the lift or by the amount needed to get to next layer'
                        if !_eq($info->{dist_Z}, -$print->config->get_at('retract_lift', $tool))
                            && !_eq($info->{dist_Z}, -$print->config->get_at('retract_lift', $tool) + $conf->layer_height);
                    $lifted = 0;
                }
                fail 'move Z at travel speed' if ($args->{F} // $self->F) != $conf->travel_speed * 60;
            }
            if ($info->{retracting}) {
                $retracted[$tool] = 1;
                $retracted_length[$tool] += -$info->{dist_E};
                if (_eq($retracted_length[$tool], $print->config->get_at('retract_length', $tool))) {
                    # okay
                } elsif (_eq($retracted_length[$tool], $print->config->get_at('retract_length_toolchange', $tool))) {
                    $wait_for_toolchange = 1;
                } else {
                    fail 'retracted by the correct amount';
                }
                fail 'combining retraction and travel with G0'
                    if $cmd ne 'G0' && $conf->g0 && ($info->{dist_Z} || $info->{dist_XY});
            }
            if ($info->{extruding}) {
                fail 'only extruding while not lifted' if $lifted;
                if ($retracted[$tool]) {
                    my $expected_amount = $retracted_length[$tool] + $print->config->get_at('retract_restart_extra', $tool);
                    if ($changed_tool && $toolchange_count[$tool] > 1) {
                        $expected_amount = $print->config->get_at('retract_length_toolchange', $tool) + $print->config->get_at('retract_restart_extra_toolchange', $tool);
                        $changed_tool = 0;
                    }
                    fail 'unretracted by the correct amount'
                        if !_eq($info->{dist_E}, $expected_amount);
                    $retracted[$tool] = 0;
                    $retracted_length[$tool] = 0;
                }
            }
            if ($info->{travel} && $info->{dist_XY} >= $print->config->get_at('retract_before_travel', $tool)) {
                fail 'retracted before long travel move' if !$retracted[$tool];
            }
        });
    
        1;
    };

    $config->set('first_layer_height',      $config->layer_height);
    $config->set('first_layer_speed',       '100%');
    $config->set('start_gcode',             '');  # to avoid dealing with the nozzle lift in start G-code
    $config->set('retract_length',          [1.5]);
    $config->set('retract_before_travel',   [3]);
    $config->set('only_retract_when_crossing_perimeters', 0);

    my $retract_tests = sub {
        my ($descr) = @_;
    
        ok $test->(), "retraction$descr";
    
        my $conf = $config->clone;
        $conf->set('retract_restart_extra',   [1]);
        ok $test->($conf), "restart extra length$descr";
    
        $conf->set('retract_restart_extra',   [-1]);
        ok $test->($conf), "negative restart extra length$descr";
    
        $conf->set('retract_lift',            [1]);
        ok $test->($conf), "lift$descr";
    };

    $retract_tests->('');

    $duplicate = 2;
    $retract_tests->(' (duplicate)');

    $config->set('g0', 1);
    $retract_tests->(' (G0 and duplicate)');

    $duplicate = 1;
    $config->set('g0', 0);
    $config->set('infill_extruder', 2);
    $config->set('skirts', 4);
    $config->set('skirt_height', 3);
    $retract_tests->(' (dual extruder with multiple skirt layers)');
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('retract_layer_change', [0]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $retracted = 0;
    my $layer_changes_with_retraction = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{retracting}) {
            $retracted = 1;
        } elsif ($info->{extruding} && $retracted) {
            $retracted = 0;
        }
        
        if ($info->{dist_Z} && $retracted) {
            $layer_changes_with_retraction++;
        }
    });
    
    is $layer_changes_with_retraction, 0, 'no retraction on layer change';
}

__END__
