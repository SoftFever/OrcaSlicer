use Test::More tests => 13;
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
        Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
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
    my $parser = Slic3r::GCode::PlaceholderParser->new;
    $parser->apply_config(my $config = Slic3r::Config->new_from_defaults);
    is $parser->process('[temperature_[foo]]', { foo => '1' }),
        $config->temperature->[0],
        "nested config options";
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('output_filename_format', '[travel_speed]_[layer_height].gcode');
    $config->set('start_gcode', "TRAVEL:[travel_speed] HEIGHT:[layer_height]\n");
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $output_file = $print->expanded_output_filepath;
    ok $output_file !~ /\[travel_speed\]/, 'print config options are replaced in output filename';
    ok $output_file !~ /\[layer_height\]/, 'region config options are replaced in output filename';
    
    my $gcode = Slic3r::Test::gcode($print);
    my ($t, $h) = map $config->$_, qw(travel_speed layer_height);
    ok $gcode =~ /TRAVEL:$t/, 'print config options are replaced in custom G-code';
    ok $gcode =~ /HEIGHT:$h/, 'region config options are replaced in custom G-code';
}

{
    my $config = Slic3r::Config->new;
    $config->set('extruder', 2);
    $config->set('first_layer_temperature', [200,205]);
    
    {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        ok $gcode =~ /M104 S205 T1/, 'temperature set correctly for non-zero yet single extruder';
        ok $gcode !~ /M104 S\d+ T0/, 'unused extruder correctly ignored';
    }
    
    $config->set('infill_extruder', 1);
    {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        ok $gcode =~ /M104 S200 T0/, 'temperature set correctly for first extruder';
        ok $gcode =~ /M104 S205 T1/, 'temperature set correctly for second extruder';
    }
    
    $config->set('start_gcode', qq!
;__temp0:[first_layer_temperature_0]__
;__temp1:[first_layer_temperature_1]__
;__temp2:[first_layer_temperature_2]__
    !);
    {
        my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
        my $gcode = Slic3r::Test::gcode($print);
        # we use the [infill_extruder] placeholder to make sure this test doesn't
        # catch a false positive caused by the unparsed start G-code option itself
        # being embedded in the G-code
        ok $gcode =~ /temp0:200/, 'temperature placeholder for first extruder correctly populated';
        ok $gcode =~ /temp1:205/, 'temperature placeholder for second extruder correctly populated';
        ok $gcode =~ /temp2:200/, 'tempearture placeholder for unused extruder populated with first value';
    }
}

__END__
