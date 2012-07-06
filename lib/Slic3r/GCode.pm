package Slic3r::GCode;
use Moo;

use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale);

has 'layer'              => (is => 'rw');
has 'shift_x'            => (is => 'rw', default => sub {0} );
has 'shift_y'            => (is => 'rw', default => sub {0} );
has 'z'                  => (is => 'rw', default => sub {0} );
has 'speed'              => (is => 'rw');

has 'extruder_idx'       => (is => 'rw', default => sub {0});
has 'extrusion_distance' => (is => 'rw', default => sub {0} );
has 'elapsed_time'       => (is => 'rw', default => sub {0} );  # seconds
has 'total_extrusion_length' => (is => 'rw', default => sub {0} );
has 'retracted'          => (is => 'rw', default => sub {1} );  # this spits out some plastic at start
has 'lifted'             => (is => 'rw', default => sub {0} );
has 'last_pos'           => (is => 'rw', default => sub { Slic3r::Point->new(0,0) } );
has 'last_speed'         => (is => 'rw', default => sub {""});
has 'last_fan_speed'     => (is => 'rw', default => sub {0});
has 'dec'                => (is => 'ro', default => sub { 3 } );

# calculate speeds (mm/min)
has 'speeds' => (
    is      => 'ro',
    default => sub {{
        travel              => 60 * Slic3r::Config->get('travel_speed'),
        perimeter           => 60 * Slic3r::Config->get('perimeter_speed'),
        small_perimeter     => 60 * Slic3r::Config->get('small_perimeter_speed'),
        external_perimeter  => 60 * Slic3r::Config->get('external_perimeter_speed'),
        infill              => 60 * Slic3r::Config->get('infill_speed'),
        solid_infill        => 60 * Slic3r::Config->get('solid_infill_speed'),
        top_solid_infill    => 60 * Slic3r::Config->get('top_solid_infill_speed'),
        bridge              => 60 * Slic3r::Config->get('bridge_speed'),
        retract             => 60 * Slic3r::Config->get('retract_speed'),
    }},
);

my %role_speeds = (
    &EXTR_ROLE_PERIMETER                    => 'perimeter',
    &EXTR_ROLE_SMALLPERIMETER               => 'small_perimeter',
    &EXTR_ROLE_EXTERNAL_PERIMETER           => 'external_perimeter',
    &EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER   => 'perimeter',
    &EXTR_ROLE_FILL                         => 'infill',
    &EXTR_ROLE_SOLIDFILL                    => 'solid_infill',
    &EXTR_ROLE_TOPSOLIDFILL                 => 'top_solid_infill',
    &EXTR_ROLE_BRIDGE                       => 'bridge',
    &EXTR_ROLE_SKIRT                        => 'perimeter',
    &EXTR_ROLE_SUPPORTMATERIAL              => 'perimeter',
);

use Slic3r::Geometry qw(points_coincide PI X Y);

sub extruder {
    my $self = shift;
    return $Slic3r::extruders->[$self->extruder_idx];
}

sub change_layer {
    my $self = shift;
    my ($layer) = @_;
    
    $self->layer($layer);
    my $z = $Slic3r::z_offset + $layer->print_z * $Slic3r::scaling_factor;
    
    my $gcode = "";
    
    $gcode .= $self->retract(move_z => $z);
    $gcode .= $self->G0(undef, $z, 0, 'move to next layer (' . $layer->id . ')')
        if $self->z != $z && !$self->lifted;
    
    $gcode .= Slic3r::Config->replace_options($Slic3r::layer_gcode) . "\n"
        if $Slic3r::layer_gcode;
    
    return $gcode;
}

sub extrude {
    my $self = shift;
    
    if ($_[0]->isa('Slic3r::ExtrusionLoop')) {
        $self->extrude_loop(@_);
    } else {
        $_[0]->deserialize;
        $self->extrude_path(@_);
    }
}

sub extrude_loop {
    my $self = shift;
    my ($loop, $description) = @_;
    
    # extrude all loops ccw
    $loop->deserialize;
    $loop->polygon->make_counter_clockwise;
    
    # find the point of the loop that is closest to the current extruder position
    # or randomize if requested
    my $last_pos = $self->last_pos;
    if ($Slic3r::randomize_start && $loop->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER) {
        srand $self->layer->id * 10;
        $last_pos = Slic3r::Point->new(scale $Slic3r::print_center->[X], scale $Slic3r::bed_size->[Y]);
        $last_pos->rotate(rand(2*PI), $Slic3r::print_center);
    }
    my $start_index = $loop->nearest_point_index_to($last_pos);
    
    # split the loop at the starting point and make a path
    my $extrusion_path = $loop->split_at_index($start_index);
    $extrusion_path->deserialize;
    
    # clip the path to avoid the extruder to get exactly on the first point of the loop;
    # if polyline was shorter than the clipping distance we'd get a null polyline, so
    # we discard it in that case
    $extrusion_path->clip_end(scale($self->layer ? $self->layer->flow->width : $Slic3r::flow->width) * 0.15);
    return '' if !@{$extrusion_path->polyline};
    
    # extrude along the path
    return $self->extrude_path($extrusion_path, $description);
}

sub extrude_path {
    my $self = shift;
    my ($path, $description, $recursive) = @_;
    
    $path->merge_continuous_lines;
    
    # detect arcs
    if ($Slic3r::gcode_arcs && !$recursive) {
        my $gcode = "";
        foreach my $arc_path ($path->detect_arcs) {
            $arc_path->deserialize;
            $gcode .= $self->extrude_path($arc_path, $description, 1);
        }
        return $gcode;
    }
    
    my $gcode = "";
    
    # retract if distance from previous position is greater or equal to the one
    # specified by the user *and* to the maximum distance between infill lines
    {
        my $distance_from_last_pos = $self->last_pos->distance_to($path->points->[0]) * $Slic3r::scaling_factor;
        my $distance_threshold = $Slic3r::retract_before_travel;
        $distance_threshold = 2 * ($self->layer ? $self->layer->flow->width : $Slic3r::flow->width) / $Slic3r::fill_density * sqrt(2)
            if 0 && $Slic3r::fill_density > 0 && $description =~ /fill/;
    
        if ($distance_from_last_pos >= $distance_threshold) {
            $gcode .= $self->retract(travel_to => $path->points->[0]);
        }
    }
    
    # go to first point of extrusion path
    $gcode .= $self->G0($path->points->[0], undef, 0, "move to first $description point")
        if !points_coincide($self->last_pos, $path->points->[0]);
    
    # compensate retraction
    $gcode .= $self->unretract if $self->retracted;
    
    # calculate extrusion length per distance unit
    my $s = $path->flow_spacing || ($self->layer ? $self->layer->flow->spacing : $Slic3r::flow->spacing);
    my $h = $path->depth_layers * $self->layer->height;
    my $w = ($s - ($self->layer ? $self->layer->flow->min_spacing : $Slic3r::flow->min_spacing) * $Slic3r::overlap_factor) / (1 - $Slic3r::overlap_factor);
    
    my $area;
    if ($path->role == EXTR_ROLE_BRIDGE) {
        $area = ($s**2) * PI/4;
    } elsif ($w >= ($self->extruder->nozzle_diameter + $h)) {
        # rectangle with semicircles at the ends
        $area = $w * $h + ($h**2) / 4 * (PI - 4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $area = $self->extruder->nozzle_diameter * $h * (1 - PI/4) + $h * $w * PI/4;
    }
    
    my $e = $self->extruder->e_per_mmc * $area;
    
    # extrude arc or line
    $self->speed( $role_speeds{$path->role} || die "Unknown role: " . $path->role );
    my $path_length = 0;
    if ($path->isa('Slic3r::ExtrusionPath::Arc')) {
        $path_length = $path->length;
        $gcode .= $self->G2_G3($path->points->[-1], $path->orientation, 
            $path->center, $e * $path_length, $description);
    } else {
        foreach my $line ($path->lines) {
            my $line_length = $line->length;
            $path_length += $line_length;
            $gcode .= $self->G1($line->b, undef, $e * $line_length, $description);
        }
    }
    
    if ($Slic3r::cooling) {
        my $path_time = unscale($path_length) / $self->speeds->{$self->last_speed} * 60;
        if ($self->layer->id == 0) {
            $path_time = $Slic3r::first_layer_speed =~ /^(\d+(?:\.\d+)?)%$/
                ? $path_time / ($1/100)
                : unscale($path_length) / $Slic3r::first_layer_speed * 60;
        }
        $self->elapsed_time($self->elapsed_time + $path_time);
    }
    
    return $gcode;
}

sub retract {
    my $self = shift;
    my %params = @_;
    
    return "" unless $Slic3r::retract_length > 0 
        && !$self->retracted;
    
    # prepare moves
    $self->speed('retract');
    my $retract = [undef, undef, -$Slic3r::retract_length, "retract"];
    my $lift    = ($Slic3r::retract_lift == 0 || defined $params{move_z})
        ? undef
        : [undef, $self->z + $Slic3r::retract_lift, 0, 'lift plate during retraction'];
    
    my $gcode = "";
    if (($Slic3r::g0 || $Slic3r::gcode_flavor eq 'mach3') && $params{travel_to}) {
        if ($lift) {
            # combine lift and retract
            $lift->[2] = $retract->[2];
            $gcode .= $self->G0(@$lift);
        } else {
            # combine travel and retract
            my $travel = [$params{travel_to}, undef, $retract->[2], 'travel and retract'];
            $gcode .= $self->G0(@$travel);
        }
    } elsif (($Slic3r::g0 || $Slic3r::gcode_flavor eq 'mach3') && defined $params{move_z}) {
        # combine Z change and retraction
        my $travel = [undef, $params{move_z}, $retract->[2], 'change layer and retract'];
        $gcode .= $self->G0(@$travel);
    } else {
        $gcode .= $self->G1(@$retract);
        if (defined $params{move_z} && $Slic3r::retract_lift > 0) {
            my $travel = [undef, $params{move_z} + $Slic3r::retract_lift, 0, 'move to next layer (' . $self->layer->id . ') and lift'];
            $gcode .= $self->G0(@$travel);
            $self->lifted(1);
        } elsif ($lift) {
            $gcode .= $self->G1(@$lift);
        }
    }
    $self->retracted(1);
    $self->lifted(1) if $lift;
    
    # reset extrusion distance during retracts
    # this makes sure we leave sufficient precision in the firmware
    $gcode .= $self->reset_e if $Slic3r::gcode_flavor !~ /^(?:mach3|makerbot)$/;
    
    return $gcode;
}

sub unretract {
    my $self = shift;
    $self->retracted(0);
    my $gcode = "";
    
    if ($self->lifted) {
        $gcode .= $self->G0(undef, $self->z - $Slic3r::retract_lift, 0, 'restore layer Z');
        $self->lifted(0);
    }
    
    $self->speed('retract');
    $gcode .= $self->G0(undef, undef, ($Slic3r::retract_length + $Slic3r::retract_restart_extra), 
        "compensate retraction");
    
    return $gcode;
}

sub reset_e {
    my $self = shift;
    
    $self->extrusion_distance(0);
    return sprintf "G92 %s0%s\n", $Slic3r::extrusion_axis, ($Slic3r::gcode_comments ? ' ; reset extrusion distance' : '')
        if $Slic3r::extrusion_axis && !$Slic3r::use_relative_e_distances;
}

sub set_acceleration {
    my $self = shift;
    my ($acceleration) = @_;
    return "" unless $Slic3r::acceleration;
    
    return sprintf "M201 E%s%s\n",
        $acceleration, ($Slic3r::gcode_comments ? ' ; adjust acceleration' : '');
}

sub G0 {
    my $self = shift;
    return $self->G1(@_) if !($Slic3r::g0 || $Slic3r::gcode_flavor eq 'mach3');
    return $self->_G0_G1("G0", @_);
}

sub G1 {
    my $self = shift;
    return $self->_G0_G1("G1", @_);
}

sub _G0_G1 {
    my $self = shift;
    my ($gcode, $point, $z, $e, $comment) = @_;
    my $dec = $self->dec;
    
    if ($point) {
        $gcode .= sprintf " X%.${dec}f Y%.${dec}f", 
            ($point->x * $Slic3r::scaling_factor) + $self->shift_x, 
            ($point->y * $Slic3r::scaling_factor) + $self->shift_y; #**
        $self->last_pos($point);
    }
    if (defined $z && $z != $self->z) {
        $self->z($z);
        $gcode .= sprintf " Z%.${dec}f", $z;
    }
    
    return $self->_Gx($gcode, $e, $comment);
}

sub G2_G3 {
    my $self = shift;
    my ($point, $orientation, $center, $e, $comment) = @_;
    my $dec = $self->dec;
    
    my $gcode = $orientation eq 'cw' ? "G2" : "G3";
    
    $gcode .= sprintf " X%.${dec}f Y%.${dec}f", 
        ($point->x * $Slic3r::scaling_factor) + $self->shift_x, 
        ($point->y * $Slic3r::scaling_factor) + $self->shift_y; #**
    
    # XY distance of the center from the start position
    $gcode .= sprintf " I%.${dec}f J%.${dec}f",
        ($center->[X] - $self->last_pos->[X]) * $Slic3r::scaling_factor,
        ($center->[Y] - $self->last_pos->[Y]) * $Slic3r::scaling_factor;
    
    $self->last_pos($point);
    return $self->_Gx($gcode, $e, $comment);
}

sub _Gx {
    my $self = shift;
    my ($gcode, $e, $comment) = @_;
    my $dec = $self->dec;
    
    # determine speed
    my $speed = ($e ? $self->speed : 'travel');
    
    # output speed if it's different from last one used
    # (goal: reduce gcode size)
    my $append_bridge_off = 0;
    if ($speed ne $self->last_speed) {
        if ($speed eq 'bridge') {
            $gcode = ";_BRIDGE_FAN_START\n$gcode";
        } elsif ($self->last_speed eq 'bridge') {
            $append_bridge_off = 1;
        }
        
        # apply the speed reduction for print moves on bottom layer
        my $speed_f = $self->speeds->{$speed};
        if ($e && $self->layer->id == 0 && $comment !~ /retract/) {
            $speed_f = $Slic3r::first_layer_speed =~ /^(\d+(?:\.\d+)?)%$/
                ? ($speed_f * $1/100)
                : $Slic3r::first_layer_speed * 60;
        }
        $gcode .= sprintf " F%.${dec}f", $speed_f;
        $self->last_speed($speed);
    }
    
    # output extrusion distance
    if ($e && $Slic3r::extrusion_axis) {
        $self->extrusion_distance(0) if $Slic3r::use_relative_e_distances;
        $self->extrusion_distance($self->extrusion_distance + $e);
        $self->total_extrusion_length($self->total_extrusion_length + $e);
        $gcode .= sprintf " %s%.5f", $Slic3r::extrusion_axis, $self->extrusion_distance;
    }
    
    $gcode .= sprintf " ; %s", $comment if $comment && $Slic3r::gcode_comments;
    if ($append_bridge_off) {
        $gcode .= "\n;_BRIDGE_FAN_END";
    }
    return "$gcode\n";
}

sub set_tool {
    my $self = shift;
    my ($tool) = @_;
    
    return "" if $self->extruder_idx == $tool;
    
    $self->extruder_idx($tool);
    return $self->retract
        . sprintf "T%d%s\n", $tool, ($Slic3r::gcode_comments ? ' ; change tool' : '')
        . $self->reset_e
        . $self->unretract;
}

sub set_fan {
    my $self = shift;
    my ($speed, $dont_save) = @_;
    
    if ($self->last_fan_speed != $speed || $dont_save) {
        $self->last_fan_speed($speed) if !$dont_save;
        if ($speed == 0) {
            return sprintf "M107%s\n", ($Slic3r::gcode_comments ? ' ; disable fan' : '');
        } else {
            return sprintf "M106 %s%d%s\n", ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'),
                (255 * $speed / 100), ($Slic3r::gcode_comments ? ' ; enable fan' : '');
        }
    }
    return "";
}

sub set_temperature {
    my $self = shift;
    my ($temperature, $wait, $tool) = @_;
    
    return "" if $wait && $Slic3r::gcode_flavor eq 'makerbot';
    
    my ($code, $comment) = $wait
        ? ('M109', 'wait for temperature to be reached')
        : ('M104', 'set temperature');
    return sprintf "$code %s%d %s; $comment\n",
        ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature,
        (defined $tool && $tool != $self->extruder_idx) ? "T$tool " : "";
}

sub set_bed_temperature {
    my $self = shift;
    my ($temperature, $wait) = @_;
    
    my ($code, $comment) = $wait
        ? (($Slic3r::gcode_flavor eq 'makerbot' ? 'M109' : 'M190'), 'wait for bed temperature to be reached')
        : ('M140', 'set bed temperature');
    return sprintf "$code %s%d ; $comment\n",
        ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature;
}

1;
