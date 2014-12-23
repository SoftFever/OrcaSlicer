use Test::More tests => 9;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Geometry qw(epsilon);
use Slic3r::Test;

my $config = Slic3r::Config->new_from_defaults;

# tolerance, in minutes
# (our time estimation differs from the internal one because of decimals truncation)
my $epsilon = 0.002;

my $test = sub {
    my ($conf) = @_;
    $conf ||= $config;
    
    my $print = Slic3r::Test::init_print('2x20x10', config => $conf);
    
    my $min_time        = 1 / ($conf->vibration_limit * 60);  # minimum time between direction changes in minutes
    my %dir             = (X => 0, Y => 0);
    my %dir_time        = (X => 0, Y => 0);
    my %dir_sleep_time  = (X => 0, Y => 0);
    my $last_cmd_pause  = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd !~ /^G[01]$/) {
            if ($cmd eq 'G4') {
                $last_cmd_pause = (($args->{P} // 0) / 1000 + ($args->{S} // 0)) / 60;  # in minutes
                $dir_sleep_time{$_} += $last_cmd_pause for qw(X Y);
                $last_cmd_pause -= $epsilon;  # error builds up
            }
            return;
        }
        
        # Z moves are slow enough that we can consider any vibration interrupted
        if ($info->{dist_Z}) {
            $dir_time{$_} += 99999 for qw(X Y);
            $last_cmd_pause = 0;
            return;
        } elsif ($info->{dist_E} != 0 && $info->{dist_XY} == 0) {
            my $time = abs($info->{dist_E}) / ($args->{F} // $self->F);  # in minutes
            $dir_time{$_} += $time for qw(X Y);
            $last_cmd_pause = 0;
            return;
        }
        
        # compute move time (this assumes that the speed is XY-bound, which happens very likely)
        my $time = abs($info->{dist_XY}) / ($args->{F} // $self->F);  # in minutes
        
        my $one_axis_would_trigger_limit_without_pause = 0;
        foreach my $axis (qw(X Y)) {
            # get the direction by comparing the new $axis coordinate with the current one
            # 1 = positive, 0 = no change, -1 = negative
            my $dir = $info->{"new_$axis"} <=> $self->$axis;
            
            # are we changing direction on this axis?
            if ($dir != 0 && $dir{$axis} != $dir) {
                # this move changes direction on this axis
                if ($dir{$axis} != 0) {
                    if (($dir_time{$axis} + $dir_sleep_time{$axis}) < ($min_time - $epsilon)) {
                        fail 'vibration limit exceeded';
                    }
                    $one_axis_would_trigger_limit_without_pause = 1
                        if ($dir_time{$axis} - $last_cmd_pause) < $min_time;
                }
                $dir{$axis}             = $dir;
                $dir_time{$axis}        = 0;
                $dir_sleep_time{$axis}  = 0;
            }
            $dir_time{$axis} += $time;
        }
        fail 'no unnecessary pauses are added'
            if $last_cmd_pause > $epsilon && !$one_axis_would_trigger_limit_without_pause;
        
        $last_cmd_pause = 0;
    });
    
    1;
};

$config->set('gcode_comments', 1);
$config->set('perimeters', 1);
foreach my $frequency (5, 10, 15) {
    foreach my $gapfillspeed (20, 50, 100) {
        $config->set('vibration_limit', $frequency);
        ok $test->(), "vibrations limited to ${frequency}Hz (gap fill speed = ${gapfillspeed} mm/s)";
    }
}

__END__
