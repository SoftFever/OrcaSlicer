package Slic3r::GCode;
use Moo;

use List::Util qw(max first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(scale unscale scaled_epsilon points_coincide PI X Y B);

has 'multiple_extruders' => (is => 'ro', default => sub {0} );
has 'layer'              => (is => 'rw');
has 'move_z_callback'    => (is => 'rw');
has 'shift_x'            => (is => 'rw', default => sub {0} );
has 'shift_y'            => (is => 'rw', default => sub {0} );
has 'z'                  => (is => 'rw');
has 'speed'              => (is => 'rw');

has 'extruder'           => (is => 'rw');
has 'extrusion_distance' => (is => 'rw', default => sub {0} );
has 'elapsed_time'       => (is => 'rw', default => sub {0} );  # seconds
has 'total_extrusion_length' => (is => 'rw', default => sub {0} );
has 'lifted'             => (is => 'rw', default => sub {0} );
has 'last_pos'           => (is => 'rw', default => sub { Slic3r::Point->new(0,0) } );
has 'last_speed'         => (is => 'rw', default => sub {""});
has 'last_f'             => (is => 'rw', default => sub {""});
has 'last_fan_speed'     => (is => 'rw', default => sub {0});
has 'dec'                => (is => 'ro', default => sub { 3 } );

# used for vibration limit:
has 'last_dir'           => (is => 'ro', default => sub { [0,0] });
has 'dir_time'           => (is => 'ro', default => sub { [0,0] });

# calculate speeds (mm/min)
has 'speeds' => (
    is      => 'ro',
    default => sub {+{
        map { $_ => 60 * $Slic3r::Config->get_value("${_}_speed") }
            qw(travel perimeter small_perimeter external_perimeter infill
                solid_infill top_solid_infill support_material bridge gap_fill),
    }},
);

# assign speeds to roles
my %role_speeds = (
    &EXTR_ROLE_PERIMETER                    => 'perimeter',
    &EXTR_ROLE_EXTERNAL_PERIMETER           => 'external_perimeter',
    &EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER   => 'perimeter',
    &EXTR_ROLE_FILL                         => 'infill',
    &EXTR_ROLE_SOLIDFILL                    => 'solid_infill',
    &EXTR_ROLE_TOPSOLIDFILL                 => 'top_solid_infill',
    &EXTR_ROLE_BRIDGE                       => 'bridge',
    &EXTR_ROLE_SKIRT                        => 'perimeter',
    &EXTR_ROLE_SUPPORTMATERIAL              => 'support_material',
    &EXTR_ROLE_GAPFILL                      => 'gap_fill',
);

sub set_shift {
    my $self = shift;
    my @shift = @_;
    
    $self->last_pos->translate(
        scale ($shift[X] - $self->shift_x),
        scale ($shift[Y] - $self->shift_y),
    );
    
    $self->shift_x($shift[X]);
    $self->shift_y($shift[Y]);
}

# this method accepts Z in scaled coordinates
sub move_z {
    my $self = shift;
    my ($z, $comment) = @_;
    
    $z *= &Slic3r::SCALING_FACTOR;
    $z += $Slic3r::Config->z_offset;
    
    my $gcode = "";
    my $current_z = $self->z;
    if (!defined $current_z || $current_z != ($z + $self->lifted)) {
        $gcode .= $self->retract(move_z => $z);
        $self->speed('travel');
        $gcode .= $self->G0(undef, $z, 0, $comment || ('move to next layer (' . $self->layer->id . ')'))
            unless ($current_z // -1) != ($self->z // -1);
        $gcode .= $self->move_z_callback->() if defined $self->move_z_callback;
    }
    
    return $gcode;
}

sub extrude {
    my $self = shift;
    
    ($_[0]->isa('Slic3r::ExtrusionLoop') || $_[0]->isa('Slic3r::ExtrusionLoop::Packed'))
        ? $self->extrude_loop(@_)
        : $self->extrude_path(@_);
}

sub extrude_loop {
    my $self = shift;
    my ($loop, $description) = @_;
    
    # extrude all loops ccw
    $loop = $loop->unpack if $loop->isa('Slic3r::ExtrusionLoop::Packed');
    my $was_clockwise = $loop->polygon->make_counter_clockwise;
    
    # find the point of the loop that is closest to the current extruder position
    # or randomize if requested
    my $last_pos = $self->last_pos;
    if ($Slic3r::Config->randomize_start && $loop->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER) {
        $last_pos = Slic3r::Point->new(scale $Slic3r::Config->print_center->[X], scale $Slic3r::Config->bed_size->[Y]);
        $last_pos->rotate(rand(2*PI), $Slic3r::Config->print_center);
    }
    my $start_index = $loop->nearest_point_index_to($last_pos);
    
    # split the loop at the starting point and make a path
    my $extrusion_path = $loop->split_at_index($start_index);
    
    # clip the path to avoid the extruder to get exactly on the first point of the loop;
    # if polyline was shorter than the clipping distance we'd get a null polyline, so
    # we discard it in that case
    $extrusion_path->clip_end(scale $extrusion_path->flow_spacing * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_SPACING);
    return '' if !@{$extrusion_path->polyline};
    
    # extrude along the path
    my $gcode = $self->extrude_path($extrusion_path, $description);
    
    # make a little move inwards before leaving loop
    if ($loop->role == EXTR_ROLE_EXTERNAL_PERIMETER) {
        # detect angle between last and first segment
        # the side depends on the original winding order of the polygon (left for contours, right for holes)
        my @points = $was_clockwise ? (-2, 1) : (1, -2);
        my $angle = Slic3r::Geometry::angle3points(@{$extrusion_path->polyline}[0, @points]) / 3;
        $angle *= -1 if $was_clockwise;
        
        # create the destination point along the first segment and rotate it
        my $point = Slic3r::Geometry::point_along_segment(@{$extrusion_path->polyline}[0,1], scale $extrusion_path->flow_spacing);
        bless $point, 'Slic3r::Point';
        $point->rotate($angle, $extrusion_path->polyline->[0]);
        
        # generate the travel move
        $self->speed('travel');
        $gcode .= $self->G0($point, undef, 0, "move inwards before travel");
    }
    
    return $gcode;
}

sub extrude_path {
    my $self = shift;
    my ($path, $description, $recursive) = @_;
    
    $path = $path->unpack if $path->isa('Slic3r::ExtrusionPath::Packed');
    $path->simplify(&Slic3r::SCALED_RESOLUTION);
    
    # detect arcs
    if ($Slic3r::Config->gcode_arcs && !$recursive) {
        my $gcode = "";
        foreach my $arc_path ($path->detect_arcs) {
            $gcode .= $self->extrude_path($arc_path, $description, 1);
        }
        return $gcode;
    }
    
    my $gcode = "";
    
    # skip retract for support material
    {
        # retract if distance from previous position is greater or equal to the one specified by the user
        my $travel = Slic3r::Line->new($self->last_pos->clone, $path->points->[0]->clone);
        if ($travel->length >= scale $self->extruder->retract_before_travel
            && ($path->role != EXTR_ROLE_SUPPORTMATERIAL || !$self->layer->support_islands_enclose_line($travel))) {
            # move travel back to original layer coordinates.
            # note that we're only considering the current object's islands, while we should
            # build a more complete configuration space
            $travel->translate(-$self->shift_x, -$self->shift_y);
            if (!$Slic3r::Config->only_retract_when_crossing_perimeters || $path->role != EXTR_ROLE_FILL || !first { $_->encloses_line($travel, scaled_epsilon) } @{$self->layer->slices}) {
                $gcode .= $self->retract(travel_to => $path->points->[0]);
            }
        }
    }
    
    # go to first point of extrusion path
    $self->speed('travel');
    $gcode .= $self->G0($path->points->[0], undef, 0, "move to first $description point")
        if !points_coincide($self->last_pos, $path->points->[0]);
    
    # compensate retraction
    $gcode .= $self->unretract;
    
    my $area;  # mm^3 of extrudate per mm of tool movement 
    if ($path->role == EXTR_ROLE_BRIDGE) {
        my $s = $path->flow_spacing;
        $area = ($s**2) * PI/4;
    } else {
        my $s = $path->flow_spacing;
        my $h = $path->height // $self->layer->height;
        $area = $self->extruder->mm3_per_mm($s, $h);
    }
    
    # calculate extrusion length per distance unit
    my $e = $self->extruder->e_per_mm3 * $area;
    
    # set speed
    $self->speed( $role_speeds{$path->role} || die "Unknown role: " . $path->role );
    if ($path->role == EXTR_ROLE_PERIMETER || $path->role == EXTR_ROLE_EXTERNAL_PERIMETER || $path->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER) {
        if (abs($path->length) <= &Slic3r::SMALL_PERIMETER_LENGTH) {
            $self->speed('small_perimeter');
        }
    }
    
    # extrude arc or line
    my $path_length = 0;
    if ($path->isa('Slic3r::ExtrusionPath::Arc')) {
        $path_length = unscale $path->length;
        $gcode .= $self->G2_G3($path->points->[-1], $path->orientation, 
            $path->center, $e * unscale $path_length, $description);
    } else {
        foreach my $line ($path->lines) {
            my $line_length = unscale $line->length;
            $path_length += $line_length;
            $gcode .= $self->G1($line->[B], undef, $e * $line_length, $description);
        }
    }
    
    if ($Slic3r::Config->cooling) {
        my $path_time = $path_length / $self->speeds->{$self->last_speed} * 60;
        if ($self->layer->id == 0) {
            $path_time = $Slic3r::Config->first_layer_speed =~ /^(\d+(?:\.\d+)?)%$/
                ? $path_time / ($1/100)
                : $path_length / $Slic3r::Config->first_layer_speed * 60;
        }
        $self->elapsed_time($self->elapsed_time + $path_time);
    }
    
    return $gcode;
}

sub retract {
    my $self = shift;
    my %params = @_;
    
    # get the retraction length and abort if none
    my ($length, $restart_extra, $comment) = $params{toolchange}
        ? ($self->extruder->retract_length_toolchange,  $self->extruder->retract_restart_extra_toolchange,  "retract for tool change")
        : ($self->extruder->retract_length,             $self->extruder->retract_restart_extra,             "retract");
    
    # if we already retracted, reduce the required amount of retraction
    $length -= $self->extruder->retracted;
    return "" unless $length > 0;
    
    # prepare moves
    $self->speed('retract');
    my $retract = [undef, undef, -$length, $comment];
    my $lift    = ($self->extruder->retract_lift == 0 || defined $params{move_z})
        ? undef
        : [undef, $self->z + $self->extruder->retract_lift, 0, 'lift plate during travel'];
    
    my $gcode = "";
    if (($Slic3r::Config->g0 || $Slic3r::Config->gcode_flavor eq 'mach3') && $params{travel_to}) {
        if ($lift) {
            # combine lift and retract
            $lift->[2] = $retract->[2];
            $gcode .= $self->G0(@$lift);
        } else {
            # combine travel and retract
            my $travel = [$params{travel_to}, undef, $retract->[2], "travel and $comment"];
            $gcode .= $self->G0(@$travel);
        }
    } elsif (($Slic3r::Config->g0 || $Slic3r::Config->gcode_flavor eq 'mach3') && defined $params{move_z}) {
        # combine Z change and retraction
        my $travel = [undef, $params{move_z}, $retract->[2], "change layer and $comment"];
        $gcode .= $self->G0(@$travel);
    } else {
        $gcode .= $self->G1(@$retract);
        if (defined $params{move_z} && $self->extruder->retract_lift > 0) {
            my $travel = [undef, $params{move_z} + $self->extruder->retract_lift, 0, 'move to next layer (' . $self->layer->id . ') and lift'];
            $gcode .= $self->G0(@$travel);
            $self->lifted($self->extruder->retract_lift);
        } elsif ($lift) {
            $gcode .= $self->G1(@$lift);
        }
    }
    $self->extruder->retracted($self->extruder->retracted + $length);
    $self->extruder->restart_extra($restart_extra);
    $self->lifted($self->extruder->retract_lift) if $lift;
    
    # reset extrusion distance during retracts
    # this makes sure we leave sufficient precision in the firmware
    $gcode .= $self->reset_e if $Slic3r::Config->gcode_flavor !~ /^(?:mach3|makerbot)$/;
    
    return $gcode;
}

sub unretract {
    my $self = shift;
    
    my $gcode = "";
    
    if ($self->lifted) {
        $self->speed('travel');
        $gcode .= $self->G0(undef, $self->z - $self->lifted, 0, 'restore layer Z');
        $self->lifted(0);
    }
    
    my $to_unretract = $self->extruder->retracted + $self->extruder->restart_extra;
    if ($to_unretract) {
        $self->speed('retract');
        $gcode .= $self->G0(undef, undef, $to_unretract, "compensate retraction");
        $self->extruder->retracted(0);
        $self->extruder->restart_extra(0);
    }
    
    return $gcode;
}

sub reset_e {
    my $self = shift;
    
    $self->extrusion_distance(0);
    return sprintf "G92 %s0%s\n", $Slic3r::Config->extrusion_axis, ($Slic3r::Config->gcode_comments ? ' ; reset extrusion distance' : '')
        if $Slic3r::Config->extrusion_axis && !$Slic3r::Config->use_relative_e_distances;
}

sub set_acceleration {
    my $self = shift;
    my ($acceleration) = @_;
    return "" if !$acceleration;
    
    return sprintf "M204 S%s%s\n",
        $acceleration, ($Slic3r::Config->gcode_comments ? ' ; adjust acceleration' : '');
}

sub G0 {
    my $self = shift;
    return $self->G1(@_) if !($Slic3r::Config->g0 || $Slic3r::Config->gcode_flavor eq 'mach3');
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
            ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->extruder->extruder_offset->[X], 
            ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->extruder->extruder_offset->[Y]; #**
        $gcode = $self->_limit_frequency($point) . $gcode;
        $self->last_pos($point->clone);
    }
    if (defined $z && (!defined $self->z || $z != $self->z)) {
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
        ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->extruder->extruder_offset->[X], 
        ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->extruder->extruder_offset->[Y]; #**
    
    # XY distance of the center from the start position
    $gcode .= sprintf " I%.${dec}f J%.${dec}f",
        ($center->[X] - $self->last_pos->[X]) * &Slic3r::SCALING_FACTOR,
        ($center->[Y] - $self->last_pos->[Y]) * &Slic3r::SCALING_FACTOR;
    
    $self->last_pos($point);
    return $self->_Gx($gcode, $e, $comment);
}

sub _Gx {
    my $self = shift;
    my ($gcode, $e, $comment) = @_;
    my $dec = $self->dec;
    
    # output speed if it's different from last one used
    # (goal: reduce gcode size)
    my $append_bridge_off = 0;
    my $F;
    if ($self->speed ne $self->last_speed) {
        if ($self->speed eq 'bridge') {
            $gcode = ";_BRIDGE_FAN_START\n$gcode";
        } elsif ($self->last_speed eq 'bridge') {
            $append_bridge_off = 1;
        }
        
        # apply the speed reduction for print moves on bottom layer
        $F = $self->speed eq 'retract'
            ? ($self->extruder->retract_speed_mm_min)
            : $self->speeds->{$self->speed} // $self->speed;
        if ($e && $self->layer && $self->layer->id == 0 && $comment !~ /retract/) {
            $F = $Slic3r::Config->first_layer_speed =~ /^(\d+(?:\.\d+)?)%$/
                ? ($F * $1/100)
                : $Slic3r::Config->first_layer_speed * 60;
        }
        $self->last_speed($self->speed);
        $self->last_f($F);
    }
    $gcode .= sprintf " F%.${dec}f", $F if defined $F;
    
    # output extrusion distance
    if ($e && $Slic3r::Config->extrusion_axis) {
        $self->extrusion_distance(0) if $Slic3r::Config->use_relative_e_distances;
        $self->extrusion_distance($self->extrusion_distance + $e);
        $self->total_extrusion_length($self->total_extrusion_length + $e);
        $gcode .= sprintf " %s%.5f", $Slic3r::Config->extrusion_axis, $self->extrusion_distance;
    }
    
    $gcode .= sprintf " ; %s", $comment if $comment && $Slic3r::Config->gcode_comments;
    if ($append_bridge_off) {
        $gcode .= "\n;_BRIDGE_FAN_END";
    }
    return "$gcode\n";
}

sub set_extruder {
    my $self = shift;
    my ($extruder) = @_;
    
    # return nothing if this extruder was already selected
    return "" if (defined $self->extruder) && ($self->extruder->id == $extruder->id);
    
    # if we are running a single-extruder setup, just set the extruder and return nothing
    if (!$self->multiple_extruders) {
        $self->extruder($extruder);
        return "";
    }
    
    # trigger retraction on the current extruder (if any) 
    my $gcode = "";
    $gcode .= $self->retract(toolchange => 1) if defined $self->extruder;
    
    # append custom toolchange G-code
    if (defined $self->extruder && $Slic3r::Config->toolchange_gcode) {
        $gcode .= sprintf "%s\n", $Slic3r::Config->replace_options($Slic3r::Config->toolchange_gcode, {
            previous_extruder   => $self->extruder->id,
            next_extruder       => $extruder->id,
        });
    }
    
    # set the new extruder
    $self->extruder($extruder);
    $gcode .= sprintf "T%d%s\n", $extruder->id, ($Slic3r::Config->gcode_comments ? ' ; change extruder' : '');
    $gcode .= $self->reset_e;
    
    return $gcode;
}

sub set_fan {
    my $self = shift;
    my ($speed, $dont_save) = @_;
    
    if ($self->last_fan_speed != $speed || $dont_save) {
        $self->last_fan_speed($speed) if !$dont_save;
        if ($speed == 0) {
            my $code = $Slic3r::Config->gcode_flavor eq 'teacup'
                ? 'M106 S0'
                : 'M107';
            return sprintf "$code%s\n", ($Slic3r::Config->gcode_comments ? ' ; disable fan' : '');
        } else {
            return sprintf "M106 %s%d%s\n", ($Slic3r::Config->gcode_flavor eq 'mach3' ? 'P' : 'S'),
                (255 * $speed / 100), ($Slic3r::Config->gcode_comments ? ' ; enable fan' : '');
        }
    }
    return "";
}

sub set_temperature {
    my $self = shift;
    my ($temperature, $wait, $tool) = @_;
    
    return "" if $wait && $Slic3r::Config->gcode_flavor eq 'makerbot';
    
    my ($code, $comment) = ($wait && $Slic3r::Config->gcode_flavor ne 'teacup')
        ? ('M109', 'wait for temperature to be reached')
        : ('M104', 'set temperature');
    my $gcode = sprintf "$code %s%d %s; $comment\n",
        ($Slic3r::Config->gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature,
        (defined $tool && $self->multiple_extruders) ? "T$tool " : "";
    
    $gcode .= "M116 ; wait for temperature to be reached\n"
        if $Slic3r::Config->gcode_flavor eq 'teacup' && $wait;
    
    return $gcode;
}

sub set_bed_temperature {
    my $self = shift;
    my ($temperature, $wait) = @_;
    
    my ($code, $comment) = ($wait && $Slic3r::Config->gcode_flavor ne 'teacup')
        ? (($Slic3r::Config->gcode_flavor eq 'makerbot' ? 'M109'
            : 'M190'), 'wait for bed temperature to be reached')
        : ('M140', 'set bed temperature');
    my $gcode = sprintf "$code %s%d ; $comment\n",
        ($Slic3r::Config->gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature;
    
    $gcode .= "M116 ; wait for bed temperature to be reached\n"
        if $Slic3r::Config->gcode_flavor eq 'teacup' && $wait;
    
    return $gcode;
}

# http://hydraraptor.blogspot.it/2010/12/frequency-limit.html
sub _limit_frequency {
    my $self = shift;
    my ($point) = @_;
    
    return '' if $Slic3r::Config->vibration_limit == 0;
    my $min_time = 1 / ($Slic3r::Config->vibration_limit * 60);  # in minutes
    
    # calculate the move vector and move direction
    my $vector = Slic3r::Line->new($self->last_pos, $point)->vector;
    my @dir = map { $vector->[B][$_] <=> 0 } X,Y;
    
    my $time = (unscale $vector->length) / $self->speeds->{$self->speed};  # in minutes
    if ($time > 0) {
        my @pause = ();
        foreach my $axis (X,Y) {
            if ($dir[$axis] != 0 && $self->last_dir->[$axis] != $dir[$axis]) {
                if ($self->last_dir->[$axis] != 0) {
                    # this axis is changing direction: check whether we need to pause
                    if ($self->dir_time->[$axis] < $min_time) {
                        push @pause, ($min_time - $self->dir_time->[$axis]);
                    }
                }
                $self->last_dir->[$axis] = $dir[$axis];
                $self->dir_time->[$axis] = 0;
            }
            $self->dir_time->[$axis] += $time;
        }
        
        if (@pause) {
            return sprintf "G4 P%d\n", max(@pause) * 60 * 1000;
        }
    }
    
    return '';
}

1;
