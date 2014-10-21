package Slic3r::GCode::Base;
use Moo;

use List::Util qw(min max first);
use Slic3r::Geometry qw(X Y epsilon);

has 'gcode_config'          => (is => 'ro', default => sub { Slic3r::Config::GCode->new });
has '_extrusion_axis'       => (is => 'rw', default => sub { 'E' });
has '_extruders'            => (is => 'ro', default => sub {{}});
has '_extruder'             => (is => 'rw');
has '_multiple_extruders'   => (is => 'rw', default => sub { 0 });
has '_last_acceleration'    => (is => 'rw', default => sub { 0 });
has '_last_fan_speed'       => (is => 'rw', default => sub { 0 });
has '_lifted'               => (is => 'rw', default => sub { 0 });
has '_pos'                  => (is => 'rw', default => sub { Slic3r::Pointf3->new });

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

sub update_progress {
    my ($self, $percent) = @_;
    
    return "" if $self->gcode_config->gcode_flavor !~ /^(?:makerware|sailfish)$/;
    return sprintf "M73 P%s%s\n",
        int($percent),
        $self->_comment('update progress');
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
        
        $gcode .= $self->reset_e(1);
    }
    return $gcode;
}

sub reset_e {
    my ($self, $force) = @_;
    
    return "" if $self->config->gcode_flavor =~ /^(?:mach3|makerware|sailfish)$/;
    
    if (defined $self->_extruder) {
        return "" if $self->_extruder->E == 0 && !$force;
        $self->_extruder->set_E(0) if $self->_extruder;
    }
    
    if ($self->_extrusion_axis ne '' && !$self->gcode_config->use_relative_e_distances) {
        return sprintf "G92 %s0%s\n", $self->gcode_config->extrusion_axis, ($self->config->gcode_comments ? ' ; reset extrusion distance' : '');
    } else {
        return "";
    }
}

sub _comment {
    my ($self, $comment) = @_;
    
    return "" if (!defined $comment) || ($comment eq '') || !$self->config->gcode_comments;
    return " ; $comment";
}

sub set_speed {
    my ($self, $F, $comment) = @_;
    
    return sprintf "G1 F%.3f%s\n",
        $F,
        $self->_comment($comment);
}

sub travel_to_xy {
    my ($self, $pointf, $comment) = @_;
    
    $self->_pos->set_x($pointf->x);
    $self->_pos->set_y($pointf->y);     # ))
    return sprintf "G1 X%.3f Y%.3f F%.3f%s\n",
        @$pointf,
        $self->gcode_config->travel_speed*60,
        $self->_comment($comment);
}

sub travel_to_xyz {
    my ($self, $pointf3, $comment) = @_;
    
    # If target Z is lower than current Z but higher than nominal Z we
    # don't perform the Z move but we only move in the XY plane and
    # adjust the nominal Z by reducing the lift amount that will be 
    # used for unlift.
    if (!$self->will_move_z($pointf3->z)) {
        my $nominal_z = $self->_pos->z - $self->_lifted;
        $self->_lifted($self->_lifted - ($pointf3->z - $nominal_z));
        return $self->travel_to_xy(Slic3r::Pointf->new(@$pointf3[X,Y]));
    }
    
    # In all the other cases, we perform an actual XYZ move and cancel
    # the lift.
    $self->_lifted(0);
    return $self->_travel_to_xyz($pointf3, $comment);
}

sub _travel_to_xyz {
    my ($self, $pointf3, $comment) = @_;
    
    $self->_pos($pointf3);
    return sprintf "G1 X%.3f Y%.3f Z%.3f F%.3f%s\n",
        @$pointf3,
        $self->gcode_config->travel_speed*60,
        $self->_comment($comment);
}

sub travel_to_z {
    my ($self, $z, $comment) = @_;
    
    # If target Z is lower than current Z but higher than nominal Z
    # we don't perform the move but we only adjust the nominal Z by
    # reducing the lift amount that will be used for unlift.
    if (!$self->will_move_z($z)) {
        my $nominal_z = $self->_pos->z - $self->_lifted;
        $self->_lifted($self->_lifted - ($z - $nominal_z));
        return "";
    }
    
    # In all the other cases, we perform an actual Z move and cancel
    # the lift.
    $self->_lifted(0);
    return $self->_travel_to_z($z, $comment);
}

sub _travel_to_z {
    my ($self, $z, $comment) = @_;
    
    $self->_pos->set_z($z);
    return sprintf "G1 Z%.3f F%.3f%s\n",
        $z,
        $self->gcode_config->travel_speed*60,
        $self->_comment($comment);
}

sub will_move_z {
    my ($self, $z) = @_;
    
    # If target Z is lower than current Z but higher than nominal Z
    # we don't perform an actual Z move.
    if ($self->_lifted > 0) {
        my $nominal_z = $self->_pos->z - $self->_lifted;
        if ($z >= $nominal_z && $z <= $self->_pos->z) {
            return 0;
        }
    }
    return 1;
}

sub extrude_to_xy {
    my ($self, $pointf, $dE, $comment) = @_;
    
    $self->_pos->set_x($pointf->x);
    $self->_pos->set_y($pointf->y);     # ))
    $self->_extruder->extrude($dE);
    return sprintf "G1 X%.3f Y%.3f %s%.5f%s\n",
        @$pointf,
        $self->_extrusion_axis,
        $self->_extruder->E,
        $self->_comment($comment);
}

sub extrude_to_xyz {
    my ($self, $pointf3, $dE, $comment) = @_;
    
    $self->_pos($pointf3);
    $self->_lifted(0);
    $self->_extruder->extrude($dE);
    return sprintf "G1 X%.3f Y%.3f Z%.3f %s%.5f%s\n",
        @$pointf3,
        $self->_extrusion_axis,
        $self->_extruder->E,
        $self->_comment($comment);
}

sub retract {
    my ($self) = @_;
    
    return $self->_retract(
        $self->_extruder->retract_length,
        $self->_extruder->retract_restart_extra,
        'retract',
    );
}

sub retract_for_toolchange {
    my ($self) = @_;
    
    return $self->_retract(
        $self->_extruder->retract_length_toolchange,
        $self->_extruder->retract_restart_extra_toolchange,
        'retract for toolchange',
    );
}

sub _retract {
    my ($self, $length, $restart_extra, $comment) = @_;
    
    if ($self->gcode_config->use_firmware_retraction) {
        return "G10 ; retract\n";
    }
    
    my $gcode = "";
    my $dE = $self->_extruder->retract($length, $restart_extra);
    if ($dE != 0) {
        $gcode = sprintf "G1 %s%.5f F%.3f%s\n",
            $self->_extrusion_axis,
            $self->_extruder->E,
            $self->_extruder->retract_speed_mm_min,
            $self->_comment($comment . " dE = $dE");
    }
    
    $gcode .= "M103 ; extruder off\n"
        if $self->gcode_config->gcode_flavor eq 'makerware';
    
    return $gcode;
}

sub unretract {
    my ($self, $comment) = @_;
    
    my $gcode = "";
    
    $gcode .= "M101 ; extruder on\n"
        if $self->gcode_config->gcode_flavor eq 'makerware';
    
    if ($self->gcode_config->use_firmware_retraction) {
        $gcode .= "G11 ; unretract\n";
        $gcode .= $self->reset_e;
        return $gcode;
    }
    
    my $dE = $self->_extruder->unretract;
    if ($dE != 0) {
        # use G1 instead of G0 because G0 will blend the restart with the previous travel move
        $gcode .= sprintf "G1 %s%.5f F%.3f%s\n",
            $self->_extrusion_axis,
            $self->_extruder->E,
            $self->_extruder->retract_speed_mm_min,
            $self->_comment($comment);
    }
    
    return $gcode;
}

# If this method is called more than once before calling unlift(),
# it will not perform subsequent lifts, even if Z was raised manually
# (i.e. with travel_to_z()) and thus _lifted was reduced.
sub lift {
    my ($self) = @_;
    
    if ($self->_lifted == 0 && $self->gcode_config->retract_lift->[0] > 0) {
        my $to_lift = $self->gcode_config->retract_lift->[0];
        $self->_lifted($to_lift);
        return $self->_travel_to_z($self->_pos->z + $to_lift, 'lift Z');
    }
    return "";
}

sub unlift {
    my ($self) = @_;
    
    my $gcode = "";
    if ($self->_lifted > 0) {
        $gcode .= $self->_travel_to_z($self->_pos->z - $self->_lifted, 'restore layer Z');
        $self->_lifted(0);
    }
    return $gcode;
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
