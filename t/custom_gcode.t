use Test::More tests => 2;
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
    
    my $test = sub {
        my ($conf) = @_;
        $conf ||= $config;
        
        my $print = Slic3r::Test::init_print('2x20x10', config => $conf);
        
        my $last_move_was_z_change = 0;
        Slic3r::Test::GCodeReader->new(gcode => Slic3r::Test::gcode($print))->parse(sub {
            my ($self, $cmd, $args, $info) = @_;
            
            if ($last_move_was_z_change && $cmd ne $config->layer_gcode) {
                fail 'custom layer G-code was not applied after Z change';
            }
            if (!$last_move_was_z_change && $cmd eq $config->layer_gcode) {
                fail 'custom layer G-code was not applied after Z change';
            }
            
            $last_move_was_z_change = (defined $info->{dist_Z} && $info->{dist_Z} > 0);
        });
        
        1;
    };
    
    $config->set('start_gcode', '_MY_CUSTOM_START_GCODE_');  # to avoid dealing with the nozzle lift in start G-code
    $config->set('layer_gcode', '_MY_CUSTOM_LAYER_GCODE_');
    ok $test->(), "custom layer G-code is applied after Z move and before other moves";
}

#==========================================================

{
    my $config = Slic3r::Config->new_from_defaults;
    is $config->replace_options('[temperature_[foo]]', { foo => '0' }),
        200,
        "nested config options";
}

__END__
