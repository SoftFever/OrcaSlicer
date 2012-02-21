package Slic3r::Extruder;
use Moo;

use Slic3r::Geometry qw(scale);

has 'layer'              => (is => 'rw');
has 'shift_x'            => (is => 'rw', default => sub {0} );
has 'shift_y'            => (is => 'rw', default => sub {0} );
has 'z'                  => (is => 'rw', default => sub {0} );
has 'print_feed_rate'    => (is => 'rw');

has 'extrusion_distance' => (is => 'rw', default => sub {0} );
has 'total_extrusion_length' => (is => 'rw', default => sub {0} );
has 'retracted'          => (is => 'rw', default => sub {1} );  # this spits out some plastic at start
has 'lifted'             => (is => 'rw', default => sub {0} );
has 'last_pos'           => (is => 'rw', default => sub { Slic3r::Point->new(0,0) } );
has 'last_f'             => (is => 'rw', default => sub {0});
has 'dec'                => (is => 'ro', default => sub { 3 } );

# calculate speeds
has 'travel_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::travel_speed * 60 },  # mm/min
);
has 'perimeter_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::perimeter_speed * 60 },  # mm/min
);
has 'small_perimeter_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::small_perimeter_speed * 60 },  # mm/min
);
has 'infill_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::infill_speed * 60 },  # mm/min
);
has 'solid_infill_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::solid_infill_speed * 60 },  # mm/min
);
has 'bridge_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::bridge_speed * 60 },  # mm/min
);
has 'retract_speed' => (
    is      => 'ro',
    default => sub { $Slic3r::retract_speed * 60 },  # mm/min
);

use Slic3r::Geometry qw(points_coincide PI X Y);
use XXX;

sub change_layer {
    my $self = shift;
    my ($layer) = @_;
    
    $self->layer($layer);
    my $z = $Slic3r::z_offset + $layer->print_z * $Slic3r::resolution;
    
    my $gcode = "";
    
    $gcode .= $self->retract(move_z => $z);
    $gcode .= $self->G0(undef, $z, 0, 'move to next layer')
        if $self->z != $z;
    
    return $gcode;
}

sub extrude {
    my $self = shift;
    
    return $_[0]->isa('Slic3r::ExtrusionLoop')
        ? $self->extrude_loop(@_)
        : $self->extrude_path(@_);
}

sub extrude_loop {
    my $self = shift;
    my ($loop, $description) = @_;
    
    # extrude all loops ccw
    $loop->polygon->make_counter_clockwise;
    
    # find the point of the loop that is closest to the current extruder position
    my $start_at = $loop->nearest_point_to($self->last_pos);
    
    # split the loop at the starting point and make a path
    my $extrusion_path = $loop->split_at($start_at);
    
    # clip the path to avoid the extruder to get exactly on the first point of the loop
    $extrusion_path->clip_end(scale $Slic3r::flow_width * 0.15);
    
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
        $gcode .= $self->extrude_path($_, $description, 1) for $path->detect_arcs;
        return $gcode;
    }
    
    my $gcode = "";
    
    # retract if distance from previous position is greater or equal to the one
    # specified by the user *and* to the maximum distance between infill lines
    {
        my $distance_from_last_pos = $self->last_pos->distance_to($path->points->[0]) * $Slic3r::resolution;
        my $distance_threshold = $Slic3r::retract_before_travel;
        $distance_threshold = 2 * $Slic3r::flow_width / $Slic3r::fill_density * sqrt(2)
            if $Slic3r::fill_density > 0 && $description =~ /fill/;
    
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
    my $s = $path->flow_spacing || $Slic3r::flow_spacing;
    my $h = $path->depth_layers * $self->layer->height;
    my $w = ($s - $Slic3r::min_flow_spacing * $Slic3r::overlap_factor) / (1 - $Slic3r::overlap_factor);
    
    my $area;
    if ($path->role eq 'bridge') {
        $area = ($s**2) * PI/4;
    } elsif ($w >= ($Slic3r::nozzle_diameter + $h)) {
        # rectangle with semicircles at the ends
        $area = $w * $h + ($h**2) / 4 * (PI - 4);
    } else {
        # rectangle with shrunk semicircles at the ends
        $area = $Slic3r::nozzle_diameter * $h * (1 - PI/4) + $h * $w * PI/4;
    }
    
    my $e = $Slic3r::resolution
        * $area
        * $Slic3r::extrusion_multiplier
        * (4 / (($Slic3r::filament_diameter ** 2) * PI));
    
    # extrude arc or line
    $self->print_feed_rate(
        $path->role =~ /^(perimeter|skirt|support-material)$/o ? $self->perimeter_speed
            : $path->role eq 'small-perimeter'  ? $self->small_perimeter_speed
            : $path->role eq 'fill'             ? $self->infill_speed
            : $path->role eq 'solid-fill'       ? $self->solid_infill_speed
            : $path->role eq 'bridge'           ? $self->bridge_speed
            : die "Unknown role: " . $path->role
    );
    if ($path->isa('Slic3r::ExtrusionPath::Arc')) {
        $gcode .= $self->G2_G3($path->points->[-1], $path->orientation, 
            $path->center, $e * $path->length, $description);
    } else {
        foreach my $line ($path->lines) {
            $gcode .= $self->G1($line->b, undef, $e * $line->length, $description);
        }
    }
    
    return $gcode;
}

sub retract {
    my $self = shift;
    my %params = @_;
    
    return "" unless $Slic3r::retract_length > 0 
        && !$self->retracted;
    
    # prepare moves
    $self->print_feed_rate($self->retract_speed);
    my $retract = [undef, undef, -$Slic3r::retract_length, "retract"];
    my $lift    = ($Slic3r::retract_lift == 0 || defined $params{move_z})
        ? undef
        : [undef, $self->z + $Slic3r::retract_lift, 0, 'lift plate during retraction'];
    
    my $gcode = "";
    if ($Slic3r::g0 && $params{travel_to}) {
        if ($lift) {
            # combine lift and retract
            $lift->[2] = $retract->[2];
            $gcode .= $self->G0(@$lift);
        } else {
            # combine travel and retract
            my $travel = [$params{travel_to}, undef, $retract->[2], 'travel and retract'];
            $gcode .= $self->G0(@$travel);
        }
    } elsif ($Slic3r::g0 && defined $params{move_z}) {
        # combine Z change and retraction
        my $travel = [undef, $params{move_z}, $retract->[2], 'change layer and retract'];
        $gcode .= $self->G0(@$travel);
    } else {
        $gcode .= $self->G1(@$retract);
        if ($lift) {
            $gcode .= $self->G1(@$lift);
        }
    }
    $self->retracted(1);
    $self->lifted(1) if $lift;
    
    # reset extrusion distance during retracts
    # this makes sure we leave sufficient precision in the firmware
    if (!$Slic3r::use_relative_e_distances) {
        $gcode .= "G92 " . $Slic3r::extrusion_axis . "0\n";
        $self->extrusion_distance(0);
    }
    
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
    
    $self->print_feed_rate($self->retract_speed);
    $gcode .= $self->G0(undef, undef, ($Slic3r::retract_length + $Slic3r::retract_restart_extra), 
        "compensate retraction");
    
    return $gcode;
}

sub set_acceleration {
    my $self = shift;
    my ($acceleration) = @_;
    return unless $Slic3r::acceleration;
    
    return sprintf "M201 E%s%s\n",
        $acceleration, ($Slic3r::gcode_comments ? ' ; adjust acceleration' : '');
}

sub G0 {
    my $self = shift;
    return $self->G1(@_) if !$Slic3r::g0;
    return "G0" . $self->G0_G1(@_);
}

sub G1 {
    my $self = shift;
    return "G1" . $self->G0_G1(@_);
}

sub G0_G1 {
    my $self = shift;
    my ($point, $z, $e, $comment) = @_;
    my $dec = $self->dec;
    
    my $gcode = "";
    
    if ($point) {
        $gcode .= sprintf " X%.${dec}f Y%.${dec}f", 
            ($point->x * $Slic3r::resolution) + $self->shift_x, 
            ($point->y * $Slic3r::resolution) + $self->shift_y; #**
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
        ($point->x * $Slic3r::resolution) + $self->shift_x, 
        ($point->y * $Slic3r::resolution) + $self->shift_y; #**
    
    # XY distance of the center from the start position
    $gcode .= sprintf " I%.${dec}f J%.${dec}f",
        ($center->[X] - $self->last_pos->[X]) * $Slic3r::resolution,
        ($center->[Y] - $self->last_pos->[Y]) * $Slic3r::resolution;
    
    $self->last_pos($point);
    return $self->_Gx($gcode, $e, $comment);
}

sub _Gx {
    my $self = shift;
    my ($gcode, $e, $comment) = @_;
    my $dec = $self->dec;
    
    # apply the speed reduction for print moves on bottom layer
    my $speed_multiplier = $e && $self->layer->id == 0 && $comment !~ /retract/
        ? $Slic3r::bottom_layer_speed_ratio 
        : 1;
    
    # determine speed
    my $speed = ($e ? $self->print_feed_rate : $self->travel_speed) * $speed_multiplier;
    
    # output speed if it's different from last one used
    # (goal: reduce gcode size)
    if ($speed != $self->last_f) {
        $gcode .= sprintf " F%.${dec}f", $speed;
        $self->last_f($speed);
    }
    
    # output extrusion distance
    if ($e && $Slic3r::extrusion_axis) {
        $self->extrusion_distance(0) if $Slic3r::use_relative_e_distances;
        $self->extrusion_distance($self->extrusion_distance + $e);
        $self->total_extrusion_length($self->total_extrusion_length + $e);
        $gcode .= sprintf " %s%.5f", $Slic3r::extrusion_axis, $self->extrusion_distance;
    }
    
    $gcode .= sprintf " ; %s", $comment if $comment && $Slic3r::gcode_comments;
    return "$gcode\n";
}

sub set_tool {
    my $self = shift;
    my ($tool) = @_;
    
    return sprintf "T%d%s\n", $tool, ($Slic3r::gcode_comments ? ' ; change tool' : '');
}

1;
