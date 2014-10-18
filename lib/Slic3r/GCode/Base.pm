package Slic3r::GCode::Base;
use Moo;

use List::Util qw(min max first);

has 'gcode_config'          => (is => 'ro', default => sub { Slic3r::Config::GCode->new });
has '_extrusion_axis'       => (is => 'rw', default => sub { 'E' });
has '_extruders'            => (is => 'ro', default => sub {{}});
has '_extruder'             => (is => 'rw');
has '_multiple_extruders'   => (is => 'rw', default => sub { 0 });
has '_last_acceleration'    => (is => 'rw', default => sub { 0 });
has '_last_fan_speed'       => (is => 'rw', default => sub { 0 });

sub apply_print_config {
    my ($self, $print_config) = @_;
    
    $self->gcode_config->apply_print_config($print_config);
    
    if ($self->gcode_config->gcode_flavor eq 'mach3') {
        $self->_extrusion_axis('A');
    } elsif ($self->gcode_config->gcode_flavor eq 'no-extrusion') {
        $self->_extrusion_axis('');
    } else {
        $self->_extrusion_axis($self->gcode_config->extrusion_axis);
    }
}

sub set_extruders {
    my ($self, $extruder_ids) = @_;
    
    foreach my $i (@$extruder_ids) {
        $self->_extruders->{$i} = my $e = Slic3r::Extruder->new($i, $self->config);
    }
    
    # we enable support for multiple extruder if any extruder greater than 0 is used
    # (even if prints only uses that one) since we need to output Tx commands
    # first extruder has index 0
    $self->_multiple_extruders(max(@$extruder_ids) > 0);
}

sub set_temperature {
    my ($self, $temperature, $wait, $tool) = @_;
    
    return "" if $wait && $self->gcode_config->gcode_flavor =~ /^(?:makerware|sailfish)$/;
    
    my ($code, $comment) = ($wait && $self->gcode_config->gcode_flavor ne 'teacup')
        ? ('M109', 'wait for temperature to be reached')
        : ('M104', 'set temperature');
    my $gcode = sprintf "$code %s%d %s; $comment\n",
        ($self->gcode_config->gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature,
        (defined $tool && ($self->_multiple_extruders || $self->gcode_config->gcode_flavor =~ /^(?:makerware|sailfish)$/)) ? "T$tool " : "";
    
    $gcode .= "M116 ; wait for temperature to be reached\n"
        if $self->gcode_config->gcode_flavor eq 'teacup' && $wait;
    
    return $gcode;
}

sub set_bed_temperature {
    my ($self, $temperature, $wait) = @_;
    
    my ($code, $comment) = ($wait && $self->gcode_config->gcode_flavor ne 'teacup')
        ? (($self->gcode_config->gcode_flavor =~ /^(?:makerware|sailfish)$/ ? 'M109' : 'M190'), 'wait for bed temperature to be reached')
        : ('M140', 'set bed temperature');
    my $gcode = sprintf "$code %s%d ; $comment\n",
        ($self->gcode_config->gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature;
    
    $gcode .= "M116 ; wait for bed temperature to be reached\n"
        if $self->gcode_config->gcode_flavor eq 'teacup' && $wait;
    
    return $gcode;
}

sub set_fan {
    my ($self, $speed, $dont_save) = @_;
    
    if ($self->_last_fan_speed != $speed || $dont_save) {
        $self->_last_fan_speed($speed) if !$dont_save;
        if ($speed == 0) {
            my $code = $self->gcode_config->gcode_flavor eq 'teacup'
                ? 'M106 S0'
                : $self->gcode_config->gcode_flavor =~ /^(?:makerware|sailfish)$/
                    ? 'M127'
                    : 'M107';
            return sprintf "$code%s\n", ($self->gcode_config->gcode_comments ? ' ; disable fan' : '');
        } else {
            if ($self->gcode_config->gcode_flavor =~ /^(?:makerware|sailfish)$/) {
                return sprintf "M126%s\n", ($self->gcode_config->gcode_comments ? ' ; enable fan' : '');
            } else {
                return sprintf "M106 %s%d%s\n", ($self->gcode_config->gcode_flavor eq 'mach3' ? 'P' : 'S'),
                    (255 * $speed / 100), ($self->gcode_config->gcode_comments ? ' ; enable fan' : '');
            }
        }
    }
    return "";
}

sub set_acceleration {
    my ($self, $acceleration) = @_;
    
    return "" if !$acceleration || $acceleration == $self->_last_acceleration;
    
    $self->_last_acceleration($acceleration);
    return sprintf "M204 S%s%s\n",
        $acceleration, ($self->gcode_config->gcode_comments ? ' ; adjust acceleration' : '');
}

sub need_toolchange {
    my ($self, $extruder_id) = @_;
    
    # return false if this extruder was already selected
    return (!defined $self->_extruder) || ($self->_extruder->id != $extruder_id);
}

sub set_extruder {
    my ($self, $extruder_id) = @_;
    
    return "" if !$self->need_toolchange;
    return $self->_toolchange($extruder_id);
}

sub _toolchange {
    my ($self, $extruder_id) = @_;
    
    # set the new extruder
    $self->_extruder($self->_extruders->{$extruder_id});
    
    # return the toolchange command
    # if we are running a single-extruder setup, just set the extruder and return nothing
    my $gcode = "";
    if ($self->_multiple_extruders) {
        $gcode .= sprintf "%s%d%s\n",
            ($self->gcode_config->gcode_flavor eq 'makerware'
                ? 'M135 T'
                : $self->gcode_config->gcode_flavor eq 'sailfish'
                    ? 'M108 T'
                    : 'T'),
            $extruder_id,
            ($self->gcode_config->gcode_comments ? ' ; change extruder' : '');
        
        $gcode .= $self->reset_e;
    }
    return $gcode;
}

sub reset_e {
    my ($self) = @_;
    
    return "" if $self->config->gcode_flavor =~ /^(?:mach3|makerware|sailfish)$/;
    
    $self->_extruder->set_E(0) if $self->_extruder;
    if ($self->_extrusion_axis ne '' && !$self->gcode_config->use_relative_e_distances) {
        return sprintf "G92 %s0%s\n", $self->gcode_config->extrusion_axis, ($self->config->gcode_comments ? ' ; reset extrusion distance' : '');
    } else {
        return "";
    }
}

sub has_multiple_extruders {
    my ($self) = @_;
    return $self->_multiple_extruders;
}

sub extruder {
    my ($self) = @_;
    return $self->_extruder;
}

sub extruders {
    my ($self) = @_;
    return [ sort { $a->id <=> $b->id } values %{$self->_extruders} ];
}

1;
