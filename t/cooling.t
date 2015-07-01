use Test::More;
use strict;
use warnings;

plan tests => 11;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;
use Slic3r::Test;

sub buffer {
    my $config = shift || Slic3r::Config->new;
    
    my $print_config = Slic3r::Config::Print->new;
    $print_config->apply_dynamic($config);
    
    my $gcodegen = Slic3r::GCode->new;
    $gcodegen->apply_print_config($print_config);
    $gcodegen->set_layer_count(10);
    my $buffer = Slic3r::GCode::CoolingBuffer->new(
        config      => $print_config,
        gcodegen    => $gcodegen,
    );
    return $buffer;
}

my $config = Slic3r::Config->new_from_defaults;
$config->set('disable_fan_first_layers', 0);

{
    my $buffer = buffer($config);
    $buffer->gcodegen->set_elapsed_time($buffer->config->slowdown_below_layer_time + 1);
    my $gcode = $buffer->append('G1 X100 E1 F3000', 0, 0, 0.4) . $buffer->flush;
    like $gcode, qr/F3000/, 'speed is not altered when elapsed time is greater than slowdown threshold';
}

{
    my $buffer = buffer($config);
    $buffer->gcodegen->set_elapsed_time($buffer->config->slowdown_below_layer_time - 1);
    my $gcode = $buffer->append("G1 X50 F2500\nG1 X100 E1 F3000\nG1 E4 F400", 0, 0, 0.4) . $buffer->flush;
    unlike $gcode, qr/F3000/, 'speed is altered when elapsed time is lower than slowdown threshold';
    like $gcode, qr/F2500/, 'speed is not altered for travel moves';
    like $gcode, qr/F400/, 'speed is not altered for extruder-only moves';
}

{
    my $buffer = buffer($config);
    $buffer->gcodegen->set_elapsed_time($buffer->config->fan_below_layer_time + 1);
    my $gcode = $buffer->append('G1 X100 E1 F3000', 0, 0, 0.4) . $buffer->flush;
    unlike $gcode, qr/M106/, 'fan is not activated when elapsed time is greater than fan threshold';
}

{
    my $buffer = buffer($config);
    my $gcode = "";
    for my $obj_id (0 .. 1) {
        # use an elapsed time which is < the slowdown threshold but greater than it when summed twice
        $buffer->gcodegen->set_elapsed_time($buffer->config->slowdown_below_layer_time - 1);
        $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, 0, 0.4);
    }
    $gcode .= $buffer->flush;
    like $gcode, qr/F3000/, 'slowdown is computed on all objects printing at same Z';
}

{
    my $buffer = buffer($config);
    my $gcode = "";
    for my $layer_id (0 .. 1) {
        for my $obj_id (0 .. 1) {
            # use an elapsed time which is < the threshold but greater than it when summed twice
            $buffer->gcodegen->set_elapsed_time($buffer->config->fan_below_layer_time - 1);
            $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, $layer_id, 0.4 + 0.4*$layer_id + 0.1*$obj_id); # print same layer at distinct heights
        }
    }
    $gcode .= $buffer->flush;
    unlike $gcode, qr/M106/, 'fan activation is computed on all objects printing at different Z';
}

{
    my $buffer = buffer($config);
    my $gcode = "";
    for my $layer_id (0 .. 1) {
        for my $obj_id (0 .. 1) {
            # use an elapsed time which is < the threshold even when summed twice
            $buffer->gcodegen->set_elapsed_time($buffer->config->fan_below_layer_time/2 - 1);
            $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, $layer_id, 0.4 + 0.4*$layer_id + 0.1*$obj_id); # print same layer at distinct heights
        }
    }
    $gcode .= $buffer->flush;
    like $gcode, qr/M106/, 'fan activation is computed on all objects printing at different Z';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('cooling', 1);
    $config->set('bridge_fan_speed', 100);
    $config->set('fan_below_layer_time', 0);
    $config->set('slowdown_below_layer_time', 0);
    $config->set('bridge_speed', 99);
    $config->set('top_solid_layers', 1);     # internal bridges use solid_infil speed
    $config->set('bottom_solid_layers', 1);  # internal bridges use solid_infil speed
    $config->set('vibration_limit', 30);     # test that fan is turned on even when vibration limit (or other G-code post-processor) is enabled
    
    my $print = Slic3r::Test::init_print('overhang', config => $config);
    my $fan = 0;
    my $fan_with_incorrect_speeds = my $fan_with_incorrect_print_speeds = 0;
    my $bridge_with_no_fan = 0;
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'M106') {
            $fan = $args->{S};
            $fan_with_incorrect_speeds++ if $fan != 255;
        } elsif ($cmd eq 'M107') {
            $fan = 0;
        } elsif ($info->{extruding} && $info->{dist_XY} > 0) {
            $fan_with_incorrect_print_speeds++
                if ($fan > 0) && ($args->{F} // $self->F) != 60*$config->bridge_speed;
            $bridge_with_no_fan++
                if !$fan && ($args->{F} // $self->F) == 60*$config->bridge_speed;
        }
    });
    ok !$fan_with_incorrect_speeds, 'bridge fan speed is applied correctly';
    ok !$fan_with_incorrect_print_speeds, 'bridge fan is only turned on for bridges';
    ok !$bridge_with_no_fan, 'bridge fan is turned on for all bridges';
}

__END__
