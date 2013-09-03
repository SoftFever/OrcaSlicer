use Test::More;
use strict;
use warnings;

plan tests => 8;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

sub buffer {
    my $config = shift || Slic3r::Config->new_from_defaults;
    my $buffer = Slic3r::GCode::CoolingBuffer->new(
        config      => $config,
        gcodegen    => Slic3r::GCode->new(config => $config, layer_count => 10, extruders => []),
    );
    return $buffer;
}

my $config = Slic3r::Config->new_from_defaults;
$config->set('disable_fan_first_layers', 0);

{
    my $buffer = buffer($config);
    $buffer->gcodegen->elapsed_time($buffer->config->slowdown_below_layer_time + 1);
    my $gcode = $buffer->append('G1 X100 E1 F3000', 0, 0, 0.4) . $buffer->flush;
    like $gcode, qr/F3000/, 'speed is not altered when elapsed time is greater than slowdown threshold';
}

{
    my $buffer = buffer($config);
    $buffer->gcodegen->elapsed_time($buffer->config->slowdown_below_layer_time - 1);
    my $gcode = $buffer->append("G1 X50 F2500\nG1 X100 E1 F3000\nG1 E4 F400", 0, 0, 0.4) . $buffer->flush;
    unlike $gcode, qr/F3000/, 'speed is altered when elapsed time is lower than slowdown threshold';
    like $gcode, qr/F2500/, 'speed is not altered for travel moves';
    like $gcode, qr/F400/, 'speed is not altered for extruder-only moves';
}

{
    my $buffer = buffer($config);
    $buffer->gcodegen->elapsed_time($buffer->config->fan_below_layer_time + 1);
    my $gcode = $buffer->append('G1 X100 E1 F3000', 0, 0, 0.4) . $buffer->flush;
    unlike $gcode, qr/M106/, 'fan is not activated when elapsed time is greater than fan threshold';
}

{
    my $buffer = buffer($config);
    my $gcode = "";
    for my $obj_id (0 .. 1) {
        # use an elapsed time which is < the slowdown threshold but greater than it when summed twice
        $buffer->gcodegen->elapsed_time($buffer->config->slowdown_below_layer_time - 1);
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
            $buffer->gcodegen->elapsed_time($buffer->config->fan_below_layer_time - 1);
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
            $buffer->gcodegen->elapsed_time($buffer->config->fan_below_layer_time/2 - 1);
            $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, $layer_id, 0.4 + 0.4*$layer_id + 0.1*$obj_id); # print same layer at distinct heights
        }
    }
    $gcode .= $buffer->flush;
    like $gcode, qr/M106/, 'fan activation is computed on all objects printing at different Z';
}

__END__
