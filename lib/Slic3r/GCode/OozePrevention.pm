package Slic3r::GCode::OozePrevention;
use Moo;

use Slic3r::Geometry qw(scale);

has 'standby_points'    => (is => 'rw', required => 1);

sub pre_toolchange {
    my ($self, $gcodegen) = @_;
    
    my $gcode = "";
    
    # move to the nearest standby point
    if (@{$self->standby_points}) {
        my $last_pos = $gcodegen->last_pos->clone;
        $last_pos->translate(scale +$gcodegen->origin->x, scale +$gcodegen->origin->y);  #))
        my $standby_point = $last_pos->nearest_point($self->standby_points);
        $standby_point->translate(scale -$gcodegen->origin->x, scale -$gcodegen->origin->y);  #))
        $gcode .= $gcodegen->travel_to($standby_point);
    }
    
    if ($gcodegen->config->standby_temperature_delta != 0) {
        my $temp = defined $gcodegen->layer && $gcodegen->layer->id == 0
            ? $gcodegen->config->get_at('first_layer_temperature', $gcodegen->writer->extruder->id)
            : $gcodegen->config->get_at('temperature', $gcodegen->writer->extruder->id);
        # we assume that heating is always slower than cooling, so no need to block
        $gcode .= $gcodegen->writer->set_temperature($temp + $gcodegen->config->standby_temperature_delta, 0);
    }
    
    return $gcode;
}

sub post_toolchange {
    my ($self, $gcodegen) = @_;
    
    my $gcode = "";
    
    if ($gcodegen->config->standby_temperature_delta != 0) {
        my $temp = defined $gcodegen->layer && $gcodegen->layer->id == 0
            ? $gcodegen->config->get_at('first_layer_temperature', $gcodegen->writer->extruder->id)
            : $gcodegen->config->get_at('temperature', $gcodegen->writer->extruder->id);
        $gcode .= $gcodegen->writer->set_temperature($temp, 1);
    }
    
    return $gcode;
}

1;
