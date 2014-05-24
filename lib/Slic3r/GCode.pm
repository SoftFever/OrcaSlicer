package Slic3r::GCode;
use Moo;

use List::Util qw(min max first);
use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(epsilon scale unscale scaled_epsilon points_coincide PI X Y B);
use Slic3r::Geometry::Clipper qw(union_ex offset_ex);
use Slic3r::Surface ':types';

has 'config'             => (is => 'ro', default => sub { Slic3r::Config::Full->new });
has 'placeholder_parser' => (is => 'rw', default => sub { Slic3r::GCode::PlaceholderParser->new });
has 'standby_points'     => (is => 'rw');
has 'enable_loop_clipping' => (is => 'rw', default => sub {1});
has 'enable_wipe'        => (is => 'rw', default => sub {0});   # at least one extruder has wipe enabled
has 'layer_count'        => (is => 'ro', required => 1 );
has '_layer_index'       => (is => 'rw', default => sub {-1});  # just a counter
has 'layer'              => (is => 'rw');
has '_layer_islands'     => (is => 'rw');
has '_upper_layer_islands'  => (is => 'rw');
has '_seam_position'     => (is => 'ro', default => sub { {} });  # $object => pos
has 'shift_x'            => (is => 'rw', default => sub {0} );
has 'shift_y'            => (is => 'rw', default => sub {0} );
has 'z'                  => (is => 'rw');
has 'extruders'          => (is => 'ro', default => sub {{}});
has 'multiple_extruders' => (is => 'rw', default => sub {0});
has 'extruder'           => (is => 'rw');
has 'external_mp'        => (is => 'rw');
has 'layer_mp'           => (is => 'rw');
has 'new_object'         => (is => 'rw', default => sub {0});
has 'straight_once'      => (is => 'rw', default => sub {1});
has 'elapsed_time'       => (is => 'rw', default => sub {0} );  # seconds
has 'lifted'             => (is => 'rw', default => sub {0} );
has 'last_pos'           => (is => 'rw', default => sub { Slic3r::Point->new(0,0) } );
has 'last_fan_speed'     => (is => 'rw', default => sub {0});
has 'wipe_path'          => (is => 'rw');

sub set_extruders {
    my ($self, $extruder_ids, $print_config) = @_;
    
    foreach my $i (@$extruder_ids) {
        $self->extruders->{$i} = my $e = Slic3r::Extruder->new($i, $print_config);
        $self->enable_wipe(1) if $e->wipe;
    }
    
    # we enable support for multiple extruder if any extruder greater than 0 is used
    # (even if prints only uses that one) since we need to output Tx commands
    # first extruder has index 0
    $self->multiple_extruders(max(@$extruder_ids) > 0);
}

sub set_shift {
    my ($self, @shift) = @_;
    
    # if shift increases (goes towards right), last_pos decreases because it goes towards left
    my @translate = (
        scale ($self->shift_x - $shift[X]),
        scale ($self->shift_y - $shift[Y]),
    );
    $self->last_pos->translate(@translate);
    $self->wipe_path->translate(@translate) if $self->wipe_path;
    
    $self->shift_x($shift[X]);
    $self->shift_y($shift[Y]);
}

sub change_layer {
    my ($self, $layer) = @_;
    
    $self->layer($layer);
    $self->_layer_index($self->_layer_index + 1);
    
    # avoid computing islands and overhangs if they're not needed
    $self->_layer_islands($layer->islands);
    $self->_upper_layer_islands($layer->upper_layer ? $layer->upper_layer->islands : []);
    if ($self->config->avoid_crossing_perimeters) {
        $self->layer_mp(Slic3r::GCode::MotionPlanner->new(
            islands => union_ex([ map @$_, @{$layer->slices} ], 1),
        ));
    }
    
    my $gcode = "";
    if ($self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/) {
        $gcode .= sprintf "M73 P%s%s\n",
            int(99 * ($self->_layer_index / ($self->layer_count - 1))),
            ($self->config->gcode_comments ? ' ; update progress' : '');
    }
    if ($self->config->first_layer_acceleration) {
        if ($layer->id == 0) {
            $gcode .= $self->set_acceleration($self->config->first_layer_acceleration);
        } elsif ($layer->id == 1) {
            $gcode .= $self->set_acceleration($self->config->default_acceleration);
        }
    }
    
    $gcode .= $self->move_z($layer->print_z);
    return $gcode;
}

# this method accepts Z in unscaled coordinates
sub move_z {
    my ($self, $z, $comment) = @_;
    
    my $gcode = "";
    
    $z += $self->config->z_offset;
    my $current_z = $self->z;
    my $nominal_z = defined $current_z ? ($current_z - $self->lifted) : undef;
    
    if (!defined $current_z || $z > $current_z || $z < $nominal_z) {
        # we're moving above the current actual Z (so above the lift height of the current
        # layer if any) or below the current nominal layer
        
        # in both cases, we're going to the nominal Z of the next layer
        $self->lifted(0);
        
        if ($self->extruder->retract_layer_change) {
            # this retraction may alter $self->z
            $gcode .= $self->retract(move_z => $z);
            $current_z = $self->z;  # update current z in case retract() changed it
            $nominal_z = defined $current_z ? ($current_z - $self->lifted) : undef;
        }
        $gcode .= $self->G0(undef, $z, 0, $self->config->travel_speed*60, $comment || ('move to next layer (' . $self->layer->id . ')'))
            if !defined $current_z || abs($z - $nominal_z) > epsilon;
    } elsif ($z < $current_z) {
        # we're moving above the current nominal layer height and below the current actual one.
        # we're basically advancing to next layer, whose nominal Z is still lower than the previous
        # layer Z with lift.
        $self->lifted($current_z - $z);
    }
    
    return $gcode;
}

sub extrude {
    my $self = shift;
    
    $_[0]->isa('Slic3r::ExtrusionLoop')
        ? $self->extrude_loop(@_)
        : $self->extrude_path(@_);
}

sub extrude_loop {
    my ($self, $loop, $description, $speed) = @_;
    
    # make a copy; don't modify the orientation of the original loop object otherwise
    # next copies (if any) would not detect the correct orientation
    $loop = $loop->clone;
    
    # extrude all loops ccw
    my $was_clockwise = $loop->make_counter_clockwise;
    
    # find the point of the loop that is closest to the current extruder position
    # or randomize if requested
    my $last_pos = $self->last_pos;
    if ($self->config->spiral_vase) {
        $loop->split_at($last_pos);
    } elsif ($self->config->seam_position eq 'nearest' || $self->config->seam_position eq 'aligned') {
        my $polygon = $loop->polygon;
        my @candidates = @{$polygon->concave_points(PI*4/3)};
        @candidates = @{$polygon->convex_points(PI*2/3)} if !@candidates;
        @candidates = @{$polygon} if !@candidates;
        
        my @non_overhang = grep !$loop->has_overhang_point($_), @candidates;
        @candidates = @non_overhang if @non_overhang;
        
        if ($self->config->seam_position eq 'nearest') {
            $loop->split_at_vertex($last_pos->nearest_point(\@candidates));
        } elsif ($self->config->seam_position eq 'aligned') {
            if (defined $self->layer && defined $self->_seam_position->{$self->layer->object}) {
                $last_pos = $self->_seam_position->{$self->layer->object};
            }
            my $point = $self->_seam_position->{$self->layer->object} = $last_pos->nearest_point(\@candidates);
            $loop->split_at_vertex($point);
        }
    } elsif ($self->config->seam_position eq 'random') {
        if ($loop->role == EXTRL_ROLE_CONTOUR_INTERNAL_PERIMETER) {
            my $polygon = $loop->polygon;
            my $centroid = $polygon->centroid;
            $last_pos = Slic3r::Point->new($polygon->bounding_box->x_max, $centroid->y);  #))
            $last_pos->rotate(rand(2*PI), $centroid);
        }
        $loop->split_at($last_pos);
    }
    
    # clip the path to avoid the extruder to get exactly on the first point of the loop;
    # if polyline was shorter than the clipping distance we'd get a null polyline, so
    # we discard it in that case
    my $clip_length = $self->enable_loop_clipping
        ? scale($self->extruder->nozzle_diameter) * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER
        : 0;
    
    # get paths
    my @paths = @{$loop->clip_end($clip_length)};
    return '' if !@paths;
    
    # apply the small perimeter speed
    if ($paths[0]->is_perimeter && $loop->length <= &Slic3r::SMALL_PERIMETER_LENGTH) {
        $speed //= $self->config->get_abs_value('small_perimeter_speed');
    }
    $speed //= -1;
    
    # extrude along the path
    my $gcode = join '', map $self->extrude_path($_, $description, $speed), @paths;
    $self->wipe_path($paths[-1]->polyline->clone) if $self->enable_wipe;  # TODO: don't limit wipe to last path
    
    # make a little move inwards before leaving loop
    if ($paths[-1]->role == EXTR_ROLE_EXTERNAL_PERIMETER && defined $self->layer && $self->config->perimeters > 1) {
        my $last_path_polyline = $paths[-1]->polyline;
        # detect angle between last and first segment
        # the side depends on the original winding order of the polygon (left for contours, right for holes)
        my @points = $was_clockwise ? (-2, 1) : (1, -2);
        my $angle = Slic3r::Geometry::angle3points(@$last_path_polyline[0, @points]) / 3;
        $angle *= -1 if $was_clockwise;
        
        # create the destination point along the first segment and rotate it
        # we make sure we don't exceed the segment length because we don't know
        # the rotation of the second segment so we might cross the object boundary
        my $first_segment = Slic3r::Line->new(@$last_path_polyline[0,1]);
        my $distance = min(scale($self->extruder->nozzle_diameter), $first_segment->length);
        my $point = $first_segment->point_at($distance);
        $point->rotate($angle, $last_path_polyline->first_point);
        
        # generate the travel move
        $gcode .= $self->travel_to($point, $paths[-1]->role, "move inwards before travel");
    }
    
    return $gcode;
}

sub extrude_path {
    my ($self, $path, $description, $speed) = @_;
    
    $path->simplify(&Slic3r::SCALED_RESOLUTION);
    
    # go to first point of extrusion path
    my $gcode = "";
    {
        my $first_point = $path->first_point;
        $gcode .= $self->travel_to($first_point, $path->role, "move to first $description point")
            if !defined $self->last_pos || !$self->last_pos->coincides_with($first_point);
    }
    
    # compensate retraction
    $gcode .= $self->unretract;
    
    # adjust acceleration
    my $acceleration;
    if (!$self->config->first_layer_acceleration || $self->layer->id != 0) {
        if ($self->config->perimeter_acceleration && $path->is_perimeter) {
            $acceleration = $self->config->perimeter_acceleration;
        } elsif ($self->config->infill_acceleration && $path->is_fill) {
            $acceleration = $self->config->infill_acceleration;
        } elsif ($self->config->infill_acceleration && $path->is_bridge) {
            $acceleration = $self->config->bridge_acceleration;
        }
        $gcode .= $self->set_acceleration($acceleration) if $acceleration;
    }
    
    # calculate extrusion length per distance unit
    my $e = $self->extruder->e_per_mm3 * $path->mm3_per_mm;
    $e = 0 if !$self->config->get_extrusion_axis;
    
    # set speed
    my $F;
    if ($path->role == EXTR_ROLE_PERIMETER) {
        $F = $self->config->get_abs_value('perimeter_speed');
    } elsif ($path->role == EXTR_ROLE_EXTERNAL_PERIMETER) {
        $F = $self->config->get_abs_value('external_perimeter_speed');
    } elsif ($path->role == EXTR_ROLE_OVERHANG_PERIMETER || $path->role == EXTR_ROLE_BRIDGE) {
        $F = $self->config->get_abs_value('bridge_speed');
    } elsif ($path->role == EXTR_ROLE_FILL) {
        $F = $self->config->get_abs_value('infill_speed');
    } elsif ($path->role == EXTR_ROLE_SOLIDFILL) {
        $F = $self->config->get_abs_value('solid_infill_speed');
    } elsif ($path->role == EXTR_ROLE_TOPSOLIDFILL) {
        $F = $self->config->get_abs_value('top_solid_infill_speed');
    } elsif ($path->role == EXTR_ROLE_GAPFILL) {
        $F = $self->config->get_abs_value('gap_fill_speed');
    } else {
        $F = $speed // -1;
        die "Invalid speed" if $F < 0;   # $speed == -1
    }
    $F *= 60;  # convert mm/sec to mm/min
    
    if ($self->layer->id == 0) {
        $F = $self->config->get_abs_value_over('first_layer_speed', $F/60) * 60;
    }
    
    # extrude arc or line
    $gcode .= ";_BRIDGE_FAN_START\n" if $path->is_bridge;
    my $path_length = unscale $path->length;
    {
        $gcode .= $path->gcode($self->extruder, $e, $F,
            $self->shift_x - $self->extruder->extruder_offset->x,
            $self->shift_y - $self->extruder->extruder_offset->y,  #,,
            $self->config->get_extrusion_axis,
            $self->config->gcode_comments ? " ; $description" : "");

        if ($self->enable_wipe) {
            $self->wipe_path($path->polyline->clone);
            $self->wipe_path->reverse;
        }
    }
    $gcode .= ";_BRIDGE_FAN_END\n" if $path->is_bridge;
    $self->last_pos($path->last_point);
    
    if ($self->config->cooling) {
        my $path_time = $path_length / $F * 60;
        $self->elapsed_time($self->elapsed_time + $path_time);
    }
    
    # reset acceleration
    $gcode .= $self->set_acceleration($self->config->default_acceleration)
        if $acceleration && $self->config->default_acceleration;
    
    return $gcode;
}

sub travel_to {
    my ($self, $point, $role, $comment) = @_;
    
    my $gcode = "";
    my $travel = Slic3r::Line->new($self->last_pos, $point);
    
    # move travel back to original layer coordinates for the island check.
    # note that we're only considering the current object's islands, while we should
    # build a more complete configuration space
    $travel->translate(-$self->shift_x, -$self->shift_y);
    
    # skip retraction if the travel move is contained in an island in the current layer
    # *and* in an island in the upper layer (so that the ooze will not be visible)
    if ($travel->length < scale $self->extruder->retract_before_travel
        || ($self->config->only_retract_when_crossing_perimeters
            && (first { $_->contains_line($travel) } @{$self->_upper_layer_islands})
            && (first { $_->contains_line($travel) } @{$self->_layer_islands}))
        || (defined $role && $role == EXTR_ROLE_SUPPORTMATERIAL && (first { $_->contains_line($travel) } @{$self->layer->support_islands}))
        ) {
        $self->straight_once(0);
        $gcode .= $self->G0($point, undef, 0, $self->config->travel_speed*60, $comment || "");
    } elsif (!$self->config->avoid_crossing_perimeters || $self->straight_once) {
        $self->straight_once(0);
        $gcode .= $self->retract;
        $gcode .= $self->G0($point, undef, 0, $self->config->travel_speed*60, $comment || "");
    } else {
        if ($self->new_object) {
            $self->new_object(0);
            
            # represent $point in G-code coordinates
            $point = $point->clone;
            my @shift = ($self->shift_x, $self->shift_y);
            $point->translate(map scale $_, @shift);
            
            # calculate path (external_mp uses G-code coordinates so we temporary need a null shift)
            $self->set_shift(0,0);
            $gcode .= $self->_plan($self->external_mp, $point, $comment);
            $self->set_shift(@shift);
        } else {
            $gcode .= $self->_plan($self->layer_mp, $point, $comment);
        }
    }
    
    return $gcode;
}

sub _plan {
    my ($self, $mp, $point, $comment) = @_;
    
    my $gcode = "";
    my @travel = @{$mp->shortest_path($self->last_pos, $point)->lines};
    
    # if the path is not contained in a single island we need to retract
    my $need_retract = !$self->config->only_retract_when_crossing_perimeters;
    if (!$need_retract) {
        $need_retract = 1;
        foreach my $island (@{$self->_upper_layer_islands}) {
            # discard the island if at any line is not enclosed in it
            next if first { !$island->contains_line($_) } @travel;
            # okay, this island encloses the full travel path
            $need_retract = 0;
            last;
        }
    }
    
    # do the retract (the travel_to argument is broken)
    $gcode .= $self->retract if $need_retract;
    
    # append the actual path and return
    # use G1 because we rely on paths being straight (G0 may make round paths)
    $gcode .= join '', map $self->G1($_->b, undef, 0, $self->config->travel_speed*60, $comment || ""), @travel;
    return $gcode;
}

sub retract {
    my ($self, %params) = @_;
    
    # get the retraction length and abort if none
    my ($length, $restart_extra, $comment) = $params{toolchange}
        ? ($self->extruder->retract_length_toolchange,  $self->extruder->retract_restart_extra_toolchange,  "retract for tool change")
        : ($self->extruder->retract_length,             $self->extruder->retract_restart_extra,             "retract");
    
    # if we already retracted, reduce the required amount of retraction
    $length -= $self->extruder->retracted;
    return "" unless $length > 0;
    my $gcode = "";
    
    # wipe
    my $wipe_path;
    if ($self->extruder->wipe && $self->wipe_path) {
        my @points = @{$self->wipe_path};
        $wipe_path = Slic3r::Polyline->new($self->last_pos, @{$self->wipe_path}[1..$#{$self->wipe_path}]);
        $wipe_path->clip_end($wipe_path->length - $self->extruder->scaled_wipe_distance($self->config->travel_speed));
    }
    
    # prepare moves
    my $retract = [undef, undef, -$length, $self->extruder->retract_speed_mm_min, $comment];
    my $lift    = ($self->config->retract_lift->[0] == 0 || defined $params{move_z}) && !$self->lifted
        ? undef
        : [undef, $self->z + $self->config->retract_lift->[0], 0, $self->config->travel_speed*60, 'lift plate during travel'];
    
    # check that we have a positive wipe length
    if ($wipe_path) {
        # subdivide the retraction
        my $retracted = 0;
        foreach my $line (@{$wipe_path->lines}) {
            my $segment_length = $line->length;
            # reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
            # due to rounding
            my $e = $retract->[2] * ($segment_length / $self->extruder->scaled_wipe_distance($self->config->travel_speed)) * 0.95;
            $retracted += $e;
            $gcode .= $self->G1($line->b, undef, $e, $self->config->travel_speed*60*0.8, $retract->[3] . ";_WIPE");
        }
        if ($retracted > $retract->[2]) {
            # if we retracted less than we had to, retract the remainder
            # TODO: add regression test
            $gcode .= $self->G1(undef, undef, $retract->[2] - $retracted, $self->extruder->retract_speed_mm_min, $comment);
        }
    } elsif ($self->config->use_firmware_retraction) {
        $gcode .= "G10 ; retract\n";
    } else {
        $gcode .= $self->G1(@$retract);
    }
    if (!$self->lifted) {
        if (defined $params{move_z} && $self->config->retract_lift->[0] > 0) {
            my $travel = [undef, $params{move_z} + $self->config->retract_lift->[0], 0, $self->config->travel_speed*60, 'move to next layer (' . $self->layer->id . ') and lift'];
            $gcode .= $self->G0(@$travel);
            $self->lifted($self->config->retract_lift->[0]);
        } elsif ($lift) {
            $gcode .= $self->G1(@$lift);
        }
    }
    $self->extruder->set_retracted($self->extruder->retracted + $length);
    $self->extruder->set_restart_extra($restart_extra);
    $self->lifted($self->config->retract_lift->[0]) if $lift;
    
    # reset extrusion distance during retracts
    # this makes sure we leave sufficient precision in the firmware
    $gcode .= $self->reset_e;
    
    $gcode .= "M103 ; extruder off\n" if $self->config->gcode_flavor eq 'makerware';
    
    return $gcode;
}

sub unretract {
    my ($self) = @_;
    
    my $gcode = "";
    $gcode .= "M101 ; extruder on\n" if $self->config->gcode_flavor eq 'makerware';
    
    if ($self->lifted) {
        $gcode .= $self->G0(undef, $self->z - $self->lifted, 0, $self->config->travel_speed*60, 'restore layer Z');
        $self->lifted(0);
    }
    
    my $to_unretract = $self->extruder->retracted + $self->extruder->restart_extra;
    if ($to_unretract) {
        if ($self->config->use_firmware_retraction) {
            $gcode .= "G11 ; unretract\n";
        } elsif ($self->config->get_extrusion_axis) {
            # use G1 instead of G0 because G0 will blend the restart with the previous travel move
            $gcode .= sprintf "G1 %s%.5f F%.3f",
                $self->config->get_extrusion_axis,
                $self->extruder->extrude($to_unretract),
                $self->extruder->retract_speed_mm_min;
            $gcode .= " ; compensate retraction" if $self->config->gcode_comments;
            $gcode .= "\n";
        }
        $self->extruder->set_retracted(0);
        $self->extruder->set_restart_extra(0);
    }
    
    return $gcode;
}

sub reset_e {
    my ($self) = @_;
    return "" if $self->config->gcode_flavor =~ /^(?:mach3|makerware|sailfish)$/;
    
    $self->extruder->set_E(0) if $self->extruder;
    return sprintf "G92 %s0%s\n", $self->config->get_extrusion_axis, ($self->config->gcode_comments ? ' ; reset extrusion distance' : '')
        if $self->config->get_extrusion_axis && !$self->config->use_relative_e_distances;
}

sub set_acceleration {
    my ($self, $acceleration) = @_;
    return "" if !$acceleration;
    
    return sprintf "M204 S%s%s\n",
        $acceleration, ($self->config->gcode_comments ? ' ; adjust acceleration' : '');
}

sub G0 {
    my $self = shift;
    return $self->G1(@_) if !($self->config->g0 || $self->config->gcode_flavor eq 'mach3');
    return $self->_G0_G1("G0", @_);
}

sub G1 {
    my $self = shift;
    return $self->_G0_G1("G1", @_);
}

sub _G0_G1 {
    my ($self, $gcode, $point, $z, $e, $F, $comment) = @_;
    
    if ($point) {
        $gcode .= sprintf " X%.3f Y%.3f", 
            ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->extruder->extruder_offset->x,
            ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->extruder->extruder_offset->y; #**
        $self->last_pos($point->clone);
    }
    if (defined $z && (!defined $self->z || $z != $self->z)) {
        $self->z($z);
        $gcode .= sprintf " Z%.3f", $z;
    }
    
    return $self->_Gx($gcode, $e, $F, $comment);
}

sub _Gx {
    my ($self, $gcode, $e, $F, $comment) = @_;
    
    $gcode .= sprintf " F%.3f", $F;
    
    # output extrusion distance
    if ($e && $self->config->get_extrusion_axis) {
        $gcode .= sprintf " %s%.5f", $self->config->get_extrusion_axis, $self->extruder->extrude($e);
    }
    
    $gcode .= " ; $comment" if $comment && $self->config->gcode_comments;
    return "$gcode\n";
}

sub set_extruder {
    my ($self, $extruder_id) = @_;
    
    # return nothing if this extruder was already selected
    return "" if (defined $self->extruder) && ($self->extruder->id == $extruder_id);
    
    # if we are running a single-extruder setup, just set the extruder and return nothing
    if (!$self->multiple_extruders) {
        $self->extruder($self->extruders->{$extruder_id});
        return "";
    }
    
    # trigger retraction on the current extruder (if any) 
    my $gcode = "";
    $gcode .= $self->retract(toolchange => 1) if defined $self->extruder;
    
    # append custom toolchange G-code
    if (defined $self->extruder && $self->config->toolchange_gcode) {
        $gcode .= sprintf "%s\n", $self->placeholder_parser->process($self->config->toolchange_gcode, {
            previous_extruder   => $self->extruder->id,
            next_extruder       => $extruder_id,
        });
    }
    
    # set the current extruder to the standby temperature
    if ($self->standby_points && defined $self->extruder) {
        # move to the nearest standby point
        {
            my $last_pos = $self->last_pos->clone;
            $last_pos->translate(scale +$self->shift_x, scale +$self->shift_y);
            my $standby_point = $last_pos->nearest_point($self->standby_points);
            $standby_point->translate(scale -$self->shift_x, scale -$self->shift_y);
            $gcode .= $self->travel_to($standby_point);
        }
        
        if ($self->config->standby_temperature_delta != 0) {
            my $temp = defined $self->layer && $self->layer->id == 0
                ? $self->extruder->first_layer_temperature
                : $self->extruder->temperature;
            # we assume that heating is always slower than cooling, so no need to block
            $gcode .= $self->set_temperature($temp + $self->config->standby_temperature_delta, 0);
        }
    }
    
    # set the new extruder
    $self->extruder($self->extruders->{$extruder_id});
    $gcode .= sprintf "%s%d%s\n", 
        ($self->config->gcode_flavor eq 'makerware'
            ? 'M135 T'
            : $self->config->gcode_flavor eq 'sailfish'
                ? 'M108 T'
                : 'T'),
        $extruder_id,
        ($self->config->gcode_comments ? ' ; change extruder' : '');
    
    $gcode .= $self->reset_e;
    
    # set the new extruder to the operating temperature
    if ($self->config->ooze_prevention && $self->config->standby_temperature_delta != 0) {
        my $temp = defined $self->layer && $self->layer->id == 0
            ? $self->extruder->first_layer_temperature
            : $self->extruder->temperature;
        $gcode .= $self->set_temperature($temp, 1);
    }
    
    return $gcode;
}

sub set_fan {
    my ($self, $speed, $dont_save) = @_;
    
    if ($self->last_fan_speed != $speed || $dont_save) {
        $self->last_fan_speed($speed) if !$dont_save;
        if ($speed == 0) {
            my $code = $self->config->gcode_flavor eq 'teacup'
                ? 'M106 S0'
                : $self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/
                    ? 'M127'
                    : 'M107';
            return sprintf "$code%s\n", ($self->config->gcode_comments ? ' ; disable fan' : '');
        } else {
            if ($self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/) {
                return sprintf "M126%s\n", ($self->config->gcode_comments ? ' ; enable fan' : '');
            } else {
                return sprintf "M106 %s%d%s\n", ($self->config->gcode_flavor eq 'mach3' ? 'P' : 'S'),
                    (255 * $speed / 100), ($self->config->gcode_comments ? ' ; enable fan' : '');
            }
        }
    }
    return "";
}

sub set_temperature {
    my ($self, $temperature, $wait, $tool) = @_;
    
    return "" if $wait && $self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/;
    
    my ($code, $comment) = ($wait && $self->config->gcode_flavor ne 'teacup')
        ? ('M109', 'wait for temperature to be reached')
        : ('M104', 'set temperature');
    my $gcode = sprintf "$code %s%d %s; $comment\n",
        ($self->config->gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature,
        (defined $tool && ($self->multiple_extruders || $self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/)) ? "T$tool " : "";
    
    $gcode .= "M116 ; wait for temperature to be reached\n"
        if $self->config->gcode_flavor eq 'teacup' && $wait;
    
    return $gcode;
}

sub set_bed_temperature {
    my ($self, $temperature, $wait) = @_;
    
    my ($code, $comment) = ($wait && $self->config->gcode_flavor ne 'teacup')
        ? (($self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/ ? 'M109' : 'M190'), 'wait for bed temperature to be reached')
        : ('M140', 'set bed temperature');
    my $gcode = sprintf "$code %s%d ; $comment\n",
        ($self->config->gcode_flavor eq 'mach3' ? 'P' : 'S'), $temperature;
    
    $gcode .= "M116 ; wait for bed temperature to be reached\n"
        if $self->config->gcode_flavor eq 'teacup' && $wait;
    
    return $gcode;
}

1;
