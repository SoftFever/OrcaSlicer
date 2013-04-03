package Slic3r::GCode::CoolingBuffer;
use Moo;

has 'config'    => (is => 'ro', required => 1);
has 'gcodegen'  => (is => 'ro', required => 1);
has 'gcode'     => (is => 'rw', default => sub {""});
has 'layer_id'  => (is => 'rw');
has 'last_z'    => (is => 'rw');
has 'min_print_speed' => (is => 'lazy');

sub _build_min_print_speed {
    my $self = shift;
    return 60 * $self->config->min_print_speed;
}

sub append {
    my $self = shift;
    my ($gcode, $layer) = @_;
    
    my $return = "";
    if (defined $self->last_z && $self->last_z != $layer->print_z) {
        $return = $self->flush;
        $self->gcodegen->elapsed_time(0);
    }
    
    $self->layer_id($layer->id);
    $self->last_z($layer->print_z);
    $self->gcode($self->gcode . $gcode);
    
    return $return;
}

sub flush {
    my $self = shift;
    
    my $gcode = $self->gcode;
    $self->gcode("");
    
    my $fan_speed = $self->config->fan_always_on ? $self->config->min_fan_speed : 0;
    my $speed_factor = 1;
    if ($self->config->cooling) {
        my $layer_time = $self->gcodegen->elapsed_time;
        Slic3r::debugf "Layer %d estimated printing time: %d seconds\n", $self->layer_id, $layer_time;
        if ($layer_time < $self->config->slowdown_below_layer_time) {
            $fan_speed = $self->config->max_fan_speed;
            $speed_factor = $layer_time / $self->config->slowdown_below_layer_time;
        } elsif ($layer_time < $self->config->fan_below_layer_time) {
            $fan_speed = $self->config->max_fan_speed - ($self->config->max_fan_speed - $self->config->min_fan_speed)
                * ($layer_time - $self->config->slowdown_below_layer_time)
                / ($self->config->fan_below_layer_time - $self->config->slowdown_below_layer_time); #/
        }
        Slic3r::debugf "  fan = %d%%, speed = %d%%\n", $fan_speed, $speed_factor * 100;
        
        if ($speed_factor < 1) {
            my $dec = $self->gcodegen->dec;
            $gcode =~ s/^(?=.*? [XY])(?=.*? E)(?!;_WIPE)(?<!;_BRIDGE_FAN_START\n)(G1 .*?F)(\d+(?:\.\d+)?)/
                my $new_speed = $2 * $speed_factor;
                $1 . sprintf("%.${dec}f", $new_speed < $self->min_print_speed ? $self->min_print_speed : $new_speed)
                /gexm;
        }
        $fan_speed = 0 if $self->layer_id < $self->config->disable_fan_first_layers;
    }
    $gcode = $self->gcodegen->set_fan($fan_speed) . $gcode;
    
    # bridge fan speed
    if (!$self->config->cooling || $self->config->bridge_fan_speed == 0 || $self->layer_id < $self->config->disable_fan_first_layers) {
        $gcode =~ s/^;_BRIDGE_FAN_(?:START|END)\n//gm;
    } else {
        $gcode =~ s/^;_BRIDGE_FAN_START\n/ $self->gcodegen->set_fan($self->config->bridge_fan_speed, 1) /gmex;
        $gcode =~ s/^;_BRIDGE_FAN_END\n/ $self->gcodegen->set_fan($fan_speed, 1) /gmex;
    }
    $gcode =~ s/;_WIPE//g;
    
    return $gcode;
}

1;
