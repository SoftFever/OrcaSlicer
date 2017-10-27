use Test::More tests => 26;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(any);
use Slic3r;
use Slic3r::Test qw(_eq);

{
    my $config = Slic3r::Config::new_from_defaults;
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
        my $lift_dist = 0; # track lifted distance for toolchanges and extruders with different retract_lift values
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
                if (_eq($info->{dist_Z}, $print->print->config->get_at('retract_lift', $tool))
                    || (_eq($info->{dist_Z}, $conf->layer_height + $print->print->config->get_at('retract_lift', $tool)) && $print->print->config->get_at('retract_lift', $tool) > 0)) {
                    fail 'only lifting while retracted' if !$retracted[$tool];
                    fail 'double lift' if $lifted;
                    $lifted = 1;
                    $lift_dist = $info->{dist_Z};
                }
                if ($info->{dist_Z} < 0) {
                    fail 'going down only after lifting' if !$lifted;
                    fail 'going down by the same amount of the lift or by the amount needed to get to next layer'
                        if !_eq($info->{dist_Z}, -$lift_dist)
                            && !_eq($info->{dist_Z}, -lift_dist + $conf->layer_height);
                    $lift_dist = 0;
                    $lifted = 0;
                }
                fail 'move Z at travel speed' if ($args->{F} // $self->F) != $conf->travel_speed * 60;
            }
            if ($info->{retracting}) {
                $retracted[$tool] = 1;
                $retracted_length[$tool] += -$info->{dist_E};
                if (_eq($retracted_length[$tool], $print->print->config->get_at('retract_length', $tool))) {
                    # okay
                } elsif (_eq($retracted_length[$tool], $print->print->config->get_at('retract_length_toolchange', $tool))) {
                    $wait_for_toolchange = 1;
                } else {
                    fail 'retracted by the correct amount';
                }
            }
            if ($info->{extruding}) {
                fail 'only extruding while not lifted' if $lifted;
                if ($retracted[$tool]) {
                    my $expected_amount = $retracted_length[$tool] + $print->print->config->get_at('retract_restart_extra', $tool);
                    if ($changed_tool && $toolchange_count[$tool] > 1) {
                        $expected_amount = $print->print->config->get_at('retract_length_toolchange', $tool) + $print->print->config->get_at('retract_restart_extra_toolchange', $tool);
                        $changed_tool = 0;
                    }
                    fail 'unretracted by the correct amount' && exit
                        if !_eq($info->{dist_E}, $expected_amount);
                    $retracted[$tool] = 0;
                    $retracted_length[$tool] = 0;
                }
            }
            if ($info->{travel} && $info->{dist_XY} >= $print->print->config->get_at('retract_before_travel', $tool)) {
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
    
        $conf->set('retract_lift',            [1, 2]);
        ok $test->($conf), "lift$descr";
    };

    $retract_tests->('');

    $duplicate = 2;
    $retract_tests->(' (duplicate)');

    $duplicate = 1;
    $config->set('infill_extruder', 2);
    $config->set('skirts', 4);
    $config->set('skirt_height', 3);
    $retract_tests->(' (dual extruder with multiple skirt layers)');
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('start_gcode', '');  # prevent any default priming Z move from affecting our lift detection
    $config->set('retract_length', [0]);
    $config->set('retract_layer_change', [0]);
    $config->set('retract_lift', [0.2]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $retracted = 0;
    my $layer_changes_with_retraction = 0;
    my $retractions = my $z_restores = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($info->{retracting}) {
            $retracted = 1;
            $retractions++;
        } elsif ($info->{extruding} && $retracted) {
            $retracted = 0;
        }
        
        if ($info->{dist_Z} && $retracted) {
            $layer_changes_with_retraction++;
        }
        if ($info->{dist_Z} && $args->{Z} < $self->Z) {
            $z_restores++;
        }
    });
    
    is $layer_changes_with_retraction, 0, 'no retraction on layer change';
    is $retractions, 0, 'no retractions';
    is $z_restores, 0, 'no lift';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('use_firmware_retraction', 1);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $retracted = 0;
    my $double_retractions = my $double_unretractions = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G10') {
            $double_retractions++ if $retracted;
            $retracted = 1;
        } elsif ($cmd eq 'G11') {
            $double_unretractions++ if !$retracted;
            $retracted = 0;
        }
    });
    
    is $double_retractions, 0, 'no double retractions';
    is $double_unretractions, 0, 'no double unretractions';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('use_firmware_retraction', 1);
    $config->set('retract_length', [0]);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my $retracted = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G10') {
            $retracted = 1;
        }
    });
    
    ok $retracted, 'retracting also when --retract-length is 0 but --use-firmware-retraction is enabled';
}

{
    my $config = Slic3r::Config::new_from_defaults;
    $config->set('start_gcode', '');
    $config->set('retract_lift', [3, 4]);
    
    my @lifted_at = ();
    my $test = sub {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config, duplicate => 2);
        @lifted_at = ();
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($cmd eq 'G1' && $info->{dist_Z} < 0) {
                push @lifted_at, $info->{new_Z};
            }
        });
    };
    
    $config->set('retract_lift_above', [0, 0]);
    $config->set('retract_lift_below', [0, 0]);
    $test->();
    ok !!@lifted_at, 'lift takes place when above/below == 0';
    
    $config->set('retract_lift_above', [5, 6]);
    $config->set('retract_lift_below', [15, 13]);
    $test->();
    ok !!@lifted_at, 'lift takes place when above/below != 0';
    ok !(any { $_ < $config->get_at('retract_lift_above', 0) } @lifted_at),
        'Z is not lifted below the configured value';
    ok !(any { $_ > $config->get_at('retract_lift_below', 0) } @lifted_at),
        'Z is not lifted above the configured value';
        
    # check lifting with different values for 2. extruder
    $config->set('perimeter_extruder', 2);
    $config->set('infill_extruder', 2);
    $config->set('retract_lift_above', [0, 0]);
    $config->set('retract_lift_below', [0, 0]);
    $test->();
    ok !!@lifted_at, 'lift takes place when above/below == 0  for 2. extruder';
    
    $config->set('retract_lift_above', [5, 6]);
    $config->set('retract_lift_below', [15, 13]);
    $test->();
    ok !!@lifted_at, 'lift takes place when above/below != 0 for 2. extruder';
    ok !(any { $_ < $config->get_at('retract_lift_above', 1) } @lifted_at),
        'Z is not lifted below the configured value for 2. extruder';
    ok !(any { $_ > $config->get_at('retract_lift_below', 1) } @lifted_at),
        'Z is not lifted above the configured value for 2. extruder';
}

__END__