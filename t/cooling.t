use Test::More;
use strict;
use warnings;

plan tests => 15;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
    use local::lib "$FindBin::Bin/../local-lib";
}

use List::Util qw(none all);
use Slic3r;
use Slic3r::Test;

my $gcodegen;
sub buffer {
    my $config = shift;
    if (defined($config)) {
        $config = $config->clone();
    } else {
        $config = Slic3r::Config->new;
    }
    my $config_override = shift;
    foreach my $key (keys %{$config_override}) {
        $config->set($key, ${$config_override}{$key});
    }

    my $print_config = Slic3r::Config::Print->new;
    $print_config->apply_dynamic($config);
    
    $gcodegen = Slic3r::GCode->new;
    $gcodegen->apply_print_config($print_config);
    $gcodegen->set_layer_count(10);
    $gcodegen->set_extruders([ 0 ]);
    return Slic3r::GCode::CoolingBuffer->new($gcodegen);
}

my $gcode1      = "G1 X100 E1 F3000\n";
my $print_time1 = 100 / (3000 / 60); # 2 sec
my $gcode2      = $gcode1 . "G1 X0 E1 F3000\n";
my $print_time2 = 2 * $print_time1; # 4 sec

my $config = Slic3r::Config::new_from_defaults;
# Default cooling settings.
$config->set('bridge_fan_speed',            [ 100 ]);
$config->set('cooling',                     [ 1 ]);
$config->set('fan_always_on',               [ 0 ]);
$config->set('fan_below_layer_time',        [ 60 ]);
$config->set('max_fan_speed',               [ 100 ]);
$config->set('min_print_speed',             [ 10 ]);
$config->set('slowdown_below_layer_time',   [ 5 ]);
# Default print speeds.
$config->set('bridge_speed',                60);
$config->set('external_perimeter_speed',    '50%');
$config->set('first_layer_speed',           30);
$config->set('gap_fill_speed',              20);
$config->set('infill_speed',                80);
$config->set('perimeter_speed',             60);
$config->set('small_perimeter_speed',       15);
$config->set('solid_infill_speed',          20);
$config->set('top_solid_infill_speed',      15);
$config->set('max_print_speed',             80);
# Override for tests.
$config->set('disable_fan_first_layers',    [ 0 ]);

{
    my $gcode_src  = "G1 F3000;_EXTRUDE_SET_SPEED\nG1 X100 E1";
    # Print time of $gcode.
    my $print_time = 100 / (3000 / 60);
    my $buffer = buffer($config, { 'slowdown_below_layer_time' => [ $print_time * 0.999 ] });
    my $gcode = $buffer->process_layer($gcode_src, 0);
    like $gcode, qr/F3000/, 'speed is not altered when elapsed time is greater than slowdown threshold';
}

{
    my $gcode_src  = 
        "G1 X50 F2500\n" .
        "G1 F3000;_EXTRUDE_SET_SPEED\n" .
        "G1 X100 E1\n" .
        "G1 E4 F400",
    # Print time of $gcode.
    my $print_time = 50 / (2500 / 60) + 100 / (3000 / 60) + 4 / (400 / 60);
    my $buffer = buffer($config, { 'slowdown_below_layer_time' => [ $print_time * 1.001 ] });
    my $gcode  = $buffer->process_layer($gcode_src, 0);
    unlike $gcode, qr/F3000/, 'speed is altered when elapsed time is lower than slowdown threshold';
    like $gcode, qr/F2500/, 'speed is not altered for travel moves';
    like $gcode, qr/F400/, 'speed is not altered for extruder-only moves';
}

{
    my $buffer = buffer($config, {
            'fan_below_layer_time'      => [ $print_time1 * 0.88 ],
            'slowdown_below_layer_time' => [ $print_time1 * 0.99 ]
        });
    my $gcode = $buffer->process_layer($gcode1, 0);
    unlike $gcode, qr/M106/, 'fan is not activated when elapsed time is greater than fan threshold';
}

{
    my $gcode .= buffer($config, { 'slowdown_below_layer_time', [ $print_time2 * 0.99 ] })->process_layer($gcode2, 0);
    like $gcode, qr/F3000/, 'slowdown is computed on all objects printing at the same Z';
}

{
    # use an elapsed time which is < the threshold but greater than it when summed twice
    my $buffer = buffer($config, {
            'fan_below_layer_time'      => [ $print_time2 * 0.65], 
            'slowdown_below_layer_time' => [ $print_time2 * 0.7 ] 
        });
    my $gcode = $buffer->process_layer($gcode2, 0) .
                $buffer->process_layer($gcode2, 1);
    unlike $gcode, qr/M106/, 'fan is not activated on all objects printing at different Z';
}

{
    # use an elapsed time which is < the threshold even when summed twice
    my $buffer = buffer($config, {
            'fan_below_layer_time'      => [ $print_time2 + 1 ], 
            'slowdown_below_layer_time' => [ $print_time2 + 2 ] 
        });
    my $gcode = $buffer->process_layer($gcode2, 0) .
                $buffer->process_layer($gcode2, 1);
    like $gcode, qr/M106/, 'fan is activated on all objects printing at different Z';
}

{
    my $buffer = buffer($config, {
            'cooling'                   => [ 1               , 0                ],
            'fan_below_layer_time'      => [ $print_time2 + 1, $print_time2 + 1 ], 
            'slowdown_below_layer_time' => [ $print_time2 + 2, $print_time2 + 2 ]
        });
    $buffer->gcodegen->set_extruders([ 0, 1 ]);
    my $gcode = $buffer->process_layer($gcode1 . "T1\nG1 X0 E1 F3000\n", 0);
    like $gcode, qr/^M106/, 'fan is activated for the 1st tool';
    like $gcode, qr/.*M107/, 'fan is disabled for the 2nd tool';
}

{
    my $config = Slic3r::Config::new_from_defaults;
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
    my $config = Slic3r::Config::new_from_defaults;
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
