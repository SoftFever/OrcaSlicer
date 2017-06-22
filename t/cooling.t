use Test::More;
use strict;
use warnings;

plan tests => 13;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(none all);
use Slic3r;
use Slic3r::Test;

my $gcodegen;
sub buffer {
    my $config = shift || Slic3r::Config->new;
    
    my $print_config = Slic3r::Config::Print->new;
    $print_config->apply_dynamic($config);
    
    $gcodegen = Slic3r::GCode->new;
    $gcodegen->apply_print_config($print_config);
    $gcodegen->set_layer_count(10);
    return Slic3r::GCode::CoolingBuffer->new($gcodegen);
}

my $config = Slic3r::Config->new_from_defaults;
$config->set('disable_fan_first_layers', [ 0 ]);

{
    my $buffer = buffer($config);
    $buffer->gcodegen->set_elapsed_time($buffer->gcodegen->config->slowdown_below_layer_time->[0] + 1);
    my $gcode = $buffer->append('G1 F3000;_EXTRUDE_SET_SPEED\nG1 X100 E1', 0, 0, 0) . $buffer->flush;
    like $gcode, qr/F3000/, 'speed is not altered when elapsed time is greater than slowdown threshold';
}

{
    my $buffer = buffer($config);
    $buffer->gcodegen->set_elapsed_time($buffer->gcodegen->config->slowdown_below_layer_time->[0] - 1);
    my $gcode = $buffer->append(
        "G1 X50 F2500\n" .
        "G1 F3000;_EXTRUDE_SET_SPEED\n" .
        "G1 X100 E1\n" .
        "G1 E4 F400",
        0, 0, 0
    ) . $buffer->flush;
    unlike $gcode, qr/F3000/, 'speed is altered when elapsed time is lower than slowdown threshold';
    like $gcode, qr/F2500/, 'speed is not altered for travel moves';
    like $gcode, qr/F400/, 'speed is not altered for extruder-only moves';
}

{
    my $buffer = buffer($config);
    $buffer->gcodegen->set_elapsed_time($buffer->gcodegen->config->fan_below_layer_time->[0] + 1);
    my $gcode = $buffer->append('G1 X100 E1 F3000', 0, 0, 0) . $buffer->flush;
    unlike $gcode, qr/M106/, 'fan is not activated when elapsed time is greater than fan threshold';
}

{
    my $buffer = buffer($config);
    my $gcode = "";
    for my $obj_id (0 .. 1) {
        # use an elapsed time which is < the slowdown threshold but greater than it when summed twice
        $buffer->gcodegen->set_elapsed_time($buffer->gcodegen->config->slowdown_below_layer_time->[0] - 1);
        $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, 0, 0);
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
            $buffer->gcodegen->set_elapsed_time($buffer->gcodegen->config->fan_below_layer_time->[0] - 1);
            $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, $layer_id, 0);
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
            $buffer->gcodegen->set_elapsed_time($buffer->gcodegen->config->fan_below_layer_time->[0]/2 - 1);
            $gcode .= $buffer->append("G1 X100 E1 F3000\n", $obj_id, $layer_id, 0);
        }
    }
    $gcode .= $buffer->flush;
    like $gcode, qr/M106/, 'fan activation is computed on all objects printing at different Z';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('cooling', [ 1 ]);
    $config->set('bridge_fan_speed', [ 100 ]);
    $config->set('fan_below_layer_time', [ 0 ]);
    $config->set('slowdown_below_layer_time', [ 0 ]);
    $config->set('bridge_speed', 99);
    $config->set('top_solid_layers', 1);     # internal bridges use solid_infil speed
    $config->set('bottom_solid_layers', 1);  # internal bridges use solid_infil speed
    
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

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('cooling', [ 1 ]);
    $config->set('fan_below_layer_time', [ 0 ]);
    $config->set('slowdown_below_layer_time', [ 10 ]);
    $config->set('min_print_speed', [ 0 ]);
    $config->set('start_gcode', '');
    $config->set('first_layer_speed', '100%');
    $config->set('external_perimeter_speed', 99);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my @layer_times = (0);  # in seconds
    my %layer_external = ();  # z => 1
    Slic3r::GCode::Reader->new->parse(my $gcode = Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1') {
            if ($info->{dist_Z}) {
                push @layer_times, 0;
                $layer_external{ $args->{Z} } = 0;
            }
            $layer_times[-1] += abs($info->{dist_XY} || $info->{dist_E} || $info->{dist_Z} || 0) / ($args->{F} // $self->F) * 60;
            if ($args->{F} && $args->{F} == $config->external_perimeter_speed*60) {
                $layer_external{ $self->Z }++;
            }
        }
    });
    @layer_times = grep $_, @layer_times;
    my $all_below = none { $_ < $config->slowdown_below_layer_time->[0] } @layer_times;
    ok $all_below, 'slowdown_below_layer_time is honored';
    
    # check that all layers have at least one unaltered external perimeter speed
    my $external = all { $_ > 0 } values %layer_external;
    ok $external, 'slowdown_below_layer_time does not alter external perimeters';
}

__END__
