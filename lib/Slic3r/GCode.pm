package Slic3r::GCode;
use strict;
use warnings;

use List::Util qw(min max first);
use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(epsilon scale unscale PI X Y B);
use Slic3r::Geometry::Clipper qw(union_ex);

sub apply_print_config {
    my ($self, $print_config) = @_;
    
    $self->writer->apply_print_config($print_config);
    $self->config->apply_print_config($print_config);
}

sub set_extruders {
    my ($self, $extruder_ids) = @_;
    
    $self->writer->set_extruders($extruder_ids);
    
    # enable wipe path generation if any extruder has wipe enabled
    $self->wipe->set_enable(defined first { $self->config->get_at('wipe', $_) } @$extruder_ids);
}

sub set_origin {
    my ($self, $pointf) = @_;
    
    # if origin increases (goes towards right), last_pos decreases because it goes towards left
    my @translate = (
        scale ($self->origin->x - $pointf->x),
        scale ($self->origin->y - $pointf->y),  #-
    );
    $self->last_pos->translate(@translate);
    $self->wipe->path->translate(@translate) if $self->wipe->has_path;
    
    $self->_set_origin($pointf);
}

sub preamble {
    my ($self) = @_;
    
    my $gcode = $self->writer->preamble;
    
    # Perform a *silent* move to z_offset: we need this to initialize the Z
    # position of our writer object so that any initial lift taking place
    # before the first layer change will raise the extruder from the correct
    # initial Z instead of 0.
    $self->writer->travel_to_z($self->config->z_offset, '');
    
    return $gcode;
}

sub change_layer {
    my ($self, $layer) = @_;
    
    {
        my $l = $layer->isa('Slic3r::Layer::Support')
            ? $layer->as_layer
            : $layer;
        $self->set_layer($l);
    }
    $self->set_layer_index($self->layer_index + 1);
    $self->set_first_layer($layer->id == 0);
    
    # avoid computing islands and overhangs if they're not needed
    if ($self->config->avoid_crossing_perimeters) {
        $self->avoid_crossing_perimeters->init_layer_mp(
            union_ex([ map @$_, @{$layer->slices} ], 1),
        );
    }
    
    my $gcode = "";
    if ($self->layer_count > 0) {
        $gcode .= $self->writer->update_progress($self->layer_index, $self->layer_count);
    }
    
    my $z = $layer->print_z + $self->config->z_offset;  # in unscaled coordinates
    if ($self->config->get_at('retract_layer_change', $self->writer->extruder->id) && $self->writer->will_move_z($z)) {
        $gcode .= $self->retract;
    }
    $gcode .= $self->writer->travel_to_z($z, 'move to next layer (' . $self->layer_index . ')');
    
    # forget last wiping path as wiping after raising Z is pointless
    $self->wipe->reset_path;
    
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
        # simplify polygon in order to skip false positives in concave/convex detection
        # ($loop is always ccw as $polygon->simplify only works on ccw polygons)
        my $polygon = $loop->polygon;
        my @simplified = @{$polygon->simplify(scale $self->config->get_at('nozzle_diameter', $self->writer->extruder->id)/2)};
        
        # restore original winding order so that concave and convex detection always happens
        # on the right/outer side of the polygon
        if ($was_clockwise) {
            $_->reverse for @simplified;
        }
        
        # concave vertices have priority
        my @candidates = map @{$_->concave_points(PI*4/3)}, @simplified;
        
        # if no concave points were found, look for convex vertices
        @candidates = map @{$_->convex_points(PI*2/3)}, @simplified if !@candidates;
        
        # retrieve the last start position for this object
        if ($self->has_layer) {
            if ($self->_has_seam_position($self->layer->object)) {
                $last_pos = $self->_seam_position($self->layer->object);
            }
        }
        
        my $point;
        if ($self->config->seam_position eq 'nearest') {
            @candidates = @$polygon if !@candidates;
            $point = $last_pos->nearest_point(\@candidates);
            if (!$loop->split_at_vertex($point)) {
                # On 32-bit Linux, Clipper will change some point coordinates by 1 unit
                # while performing simplify_polygons(), thus split_at_vertex() won't 
                # find them anymore.
                $loop->split_at($point);
            }
        } elsif (@candidates) {
            my @non_overhang = grep !$loop->has_overhang_point($_), @candidates;
            @candidates = @non_overhang if @non_overhang;
            $point = $last_pos->nearest_point(\@candidates);
            if (!$loop->split_at_vertex($point)) {
                $loop->split_at($point);
            }
        } else {
            $point = $last_pos->projection_onto_polygon($polygon);
            $loop->split_at($point);
        }
        $self->_set_seam_position($self->layer->object, $point)
            if $self->has_layer;
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
        ? scale($self->config->get_at('nozzle_diameter', $self->writer->extruder->id)) * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER
        : 0;
    
    # get paths
    my @paths = @{$loop->clip_end($clip_length)};
    return '' if !@paths;
    
    # apply the small perimeter speed
    if ($paths[0]->is_perimeter && $loop->length <= &Slic3r::SMALL_PERIMETER_LENGTH) {
        $speed //= $self->config->get_abs_value('small_perimeter_speed');
    }
    
    # extrude along the path
    my $gcode = join '', map $self->_extrude_path($_, $description, $speed), @paths;
    
    # reset acceleration
    $gcode .= $self->writer->set_acceleration($self->config->default_acceleration);
    
    $self->wipe->set_path($paths[0]->polyline->clone) if $self->wipe->enable;  # TODO: don't limit wipe to last path
    
    # make a little move inwards before leaving loop
    if ($paths[-1]->role == EXTR_ROLE_EXTERNAL_PERIMETER && $self->has_layer && $self->config->perimeters > 1) {
        my $last_path_polyline = $paths[-1]->polyline;
        # detect angle between last and first segment
        # the side depends on the original winding order of the polygon (left for contours, right for holes)
        my @points = ($paths[0][1], $paths[-1][-2]);
        @points = reverse @points if $was_clockwise;
        my $angle = $paths[0]->first_point->ccw_angle(@points) / 3;
        
        # turn left if contour, turn right if hole
        $angle *= -1 if $was_clockwise;
        
        # create the destination point along the first segment and rotate it
        # we make sure we don't exceed the segment length because we don't know
        # the rotation of the second segment so we might cross the object boundary
        my $first_segment = Slic3r::Line->new(@{$paths[0]->polyline}[0,1]);
        my $distance = min(scale($self->config->get_at('nozzle_diameter', $self->writer->extruder->id)), $first_segment->length);
        my $point = $first_segment->point_at($distance);
        $point->rotate($angle, $first_segment->a);
        
        # generate the travel move
        $gcode .= $self->writer->travel_to_xy($self->point_to_gcode($point), "move inwards before travel");
    }
    
    return $gcode;
}

sub extrude_path {
    my ($self, $path, $description, $speed) = @_;
    
    my $gcode = $self->_extrude_path($path, $description, $speed);
    
    # reset acceleration
    $gcode .= $self->writer->set_acceleration($self->config->default_acceleration);
    
    return $gcode;
}

sub _extrude_path {
    my ($self, $path, $description, $speed) = @_;
    
    $path->simplify(&Slic3r::SCALED_RESOLUTION);
    
    # go to first point of extrusion path
    my $gcode = "";
    {
        my $first_point = $path->first_point;
        $gcode .= $self->travel_to($first_point, $path->role, "move to first $description point")
            if !$self->last_pos_defined || !$self->last_pos->coincides_with($first_point);
    }
    
    # compensate retraction
    $gcode .= $self->unretract;
    
    # adjust acceleration
    {
        my $acceleration;
        if ($self->config->first_layer_acceleration && $self->first_layer) {
            $acceleration = $self->config->first_layer_acceleration;
        } elsif ($self->config->perimeter_acceleration && $path->is_perimeter) {
            $acceleration = $self->config->perimeter_acceleration;
        } elsif ($self->config->bridge_acceleration && $path->is_bridge) {
            $acceleration = $self->config->bridge_acceleration;
        } elsif ($self->config->infill_acceleration && $path->is_infill) {
            $acceleration = $self->config->infill_acceleration;
        } else {
            $acceleration = $self->config->default_acceleration;
        }
        $gcode .= $self->writer->set_acceleration($acceleration);
    }
    
    # calculate extrusion length per distance unit
    my $e_per_mm = $self->writer->extruder->e_per_mm3 * $path->mm3_per_mm;
    $e_per_mm = 0 if !$self->writer->extrusion_axis;
    
    # set speed
    $speed //= -1;
    if ($speed == -1) {
        if ($path->role == EXTR_ROLE_PERIMETER) {
            $speed = $self->config->get_abs_value('perimeter_speed');
        } elsif ($path->role == EXTR_ROLE_EXTERNAL_PERIMETER) {
            $speed = $self->config->get_abs_value('external_perimeter_speed');
        } elsif ($path->role == EXTR_ROLE_OVERHANG_PERIMETER || $path->role == EXTR_ROLE_BRIDGE) {
            $speed = $self->config->get_abs_value('bridge_speed');
        } elsif ($path->role == EXTR_ROLE_FILL) {
            $speed = $self->config->get_abs_value('infill_speed');
        } elsif ($path->role == EXTR_ROLE_SOLIDFILL) {
            $speed = $self->config->get_abs_value('solid_infill_speed');
        } elsif ($path->role == EXTR_ROLE_TOPSOLIDFILL) {
            $speed = $self->config->get_abs_value('top_solid_infill_speed');
        } elsif ($path->role == EXTR_ROLE_GAPFILL) {
            $speed = $self->config->get_abs_value('gap_fill_speed');
        } else {
            die "Invalid speed";
        }
    }
    if ($self->first_layer) {
        $speed = $self->config->get_abs_value_over('first_layer_speed', $speed);
    }
    if ($self->volumetric_speed != 0) {
        $speed ||= $self->volumetric_speed / $path->mm3_per_mm;
    }
    if ($self->config->max_volumetric_speed > 0) {
        # Cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        $speed = min(
            $speed,
            $self->config->max_volumetric_speed / $path->mm3_per_mm,
        );
    }
    my $F = $speed * 60;  # convert mm/sec to mm/min
    
    # extrude arc or line
    $gcode .= ";_BRIDGE_FAN_START\n" if $path->is_bridge && $self->enable_cooling_markers;
    my $path_length = unscale $path->length;
    {
        my $extruder_offset = $self->config->get_at('extruder_offset', $self->writer->extruder->id);
        $gcode .= $path->gcode($self->writer->extruder, $e_per_mm, $F,
            $self->origin->x - $extruder_offset->x,
            $self->origin->y - $extruder_offset->y,  #-
            $self->writer->extrusion_axis,
            $self->config->gcode_comments ? " ; $description" : "");

        if ($self->wipe->enable) {
            $self->wipe->set_path($path->polyline->clone);
            $self->wipe->path->reverse;
        }
    }
    $gcode .= ";_BRIDGE_FAN_END\n" if $path->is_bridge && $self->enable_cooling_markers;
    $self->set_last_pos($path->last_point);
    
    if ($self->config->cooling) {
        my $path_time = $path_length / $F * 60;
        $self->set_elapsed_time($self->elapsed_time + $path_time);
    }
    
    return $gcode;
}

# This method accepts $point in print coordinates.
sub travel_to {
    my ($self, $point, $role, $comment) = @_;
    
    # Define the travel move as a line between current position and the taget point.
    # This is expressed in print coordinates, so it will need to be translated by
    # $self->origin in order to get G-code coordinates.
    my $travel = Slic3r::Polyline->new($self->last_pos, $point);
    
    # check whether a straight travel move would need retraction
    my $needs_retraction = $self->needs_retraction($travel, $role);
    
    # if a retraction would be needed, try to use avoid_crossing_perimeters to plan a
    # multi-hop travel path inside the configuration space
    if ($needs_retraction
        && $self->config->avoid_crossing_perimeters
        && !$self->avoid_crossing_perimeters->disable_once) {
        $travel = $self->avoid_crossing_perimeters->travel_to($point, $self->origin, $self->last_pos);
        
        # check again whether the new travel path still needs a retraction
        $needs_retraction = $self->needs_retraction($travel, $role);
    }
    
    # Re-allow avoid_crossing_perimeters for the next travel moves
    $self->avoid_crossing_perimeters->set_disable_once(0);
    $self->avoid_crossing_perimeters->set_use_external_mp_once(0);
    
    # generate G-code for the travel move
    my $gcode = "";
    $gcode .= $self->retract if $needs_retraction;
    
    # use G1 because we rely on paths being straight (G0 may make round paths)
    $gcode .= $self->writer->travel_to_xy($self->point_to_gcode($_->b), $comment)
        for @{$travel->lines};
    
    return $gcode;
}

sub needs_retraction {
    my ($self, $travel, $role) = @_;
    
    if ($travel->length < scale $self->config->get_at('retract_before_travel', $self->writer->extruder->id)) {
        # skip retraction if the move is shorter than the configured threshold
        return 0;
    }
    
    if (defined $role && $role == EXTR_ROLE_SUPPORTMATERIAL && $self->layer->as_support_layer->support_islands->contains_polyline($travel)) {
        # skip retraction if this is a travel move inside a support material island
        return 0;
    }
    
    if ($self->config->only_retract_when_crossing_perimeters && $self->has_layer) {
        if ($self->config->fill_density > 0
            && $self->layer->any_internal_region_slice_contains_polyline($travel)) {
            # skip retraction if travel is contained in an internal slice *and*
            # internal infill is enabled (so that stringing is entirely not visible)
            return 0;
        } elsif ($self->layer->any_bottom_region_slice_contains_polyline($travel)
            && defined $self->layer->upper_layer
            && $self->layer->upper_layer->slices->contains_polyline($travel)
            && ($self->config->bottom_solid_layers >= 2 || $self->config->fill_density > 0)) {
            # skip retraction if travel is contained in an *infilled* bottom slice
            # but only if it's also covered by an *infilled* upper layer's slice
            # so that it's not visible from above (a bottom surface might not have an
            # upper slice in case of a thin membrane)
            return 0;
        }
    }
    
    # retract if only_retract_when_crossing_perimeters is disabled or doesn't apply
    return 1;
}

sub retract {
    my ($self, $toolchange) = @_;
    
    return "" if !defined $self->writer->extruder;
    
    my $gcode = "";
    
    # wipe (if it's enabled for this extruder and we have a stored wipe path)
    if ($self->config->get_at('wipe', $self->writer->extruder->id) && $self->wipe->has_path) {
        $gcode .= $self->wipe->wipe($self, $toolchange);
    }
    
    # The parent class will decide whether we need to perform an actual retraction
    # (the extruder might be already retracted fully or partially). We call these 
    # methods even if we performed wipe, since this will ensure the entire retraction
    # length is honored in case wipe path was too short.p
    $gcode .= $toolchange ? $self->writer->retract_for_toolchange : $self->writer->retract;
    
    $gcode .= $self->writer->reset_e;
    $gcode .= $self->writer->lift
        if $self->writer->extruder->retract_length > 0 || $self->config->use_firmware_retraction;
    
    return $gcode;
}

sub unretract {
    my ($self) = @_;
    
    my $gcode = "";
    $gcode .= $self->writer->unlift;
    $gcode .= $self->writer->unretract;
    return $gcode;
}

# convert a model-space scaled point into G-code coordinates
sub point_to_gcode {
    my ($self, $point) = @_;
    
    my $extruder_offset = $self->config->get_at('extruder_offset', $self->writer->extruder->id);
    return Slic3r::Pointf->new(
        ($point->x * &Slic3r::SCALING_FACTOR) + $self->origin->x - $extruder_offset->x,
        ($point->y * &Slic3r::SCALING_FACTOR) + $self->origin->y - $extruder_offset->y,  #**
    );
}

sub set_extruder {
    my ($self, $extruder_id) = @_;
    
    $self->placeholder_parser->set('current_extruder', $extruder_id);
    return "" if !$self->writer->need_toolchange($extruder_id);
    
    # if we are running a single-extruder setup, just set the extruder and return nothing
    if (!$self->writer->multiple_extruders) {
        return $self->writer->toolchange($extruder_id);
    }
    
    # prepend retraction on the current extruder
    my $gcode = $self->retract(1);
    
    # append custom toolchange G-code
    if (defined $self->writer->extruder && $self->config->toolchange_gcode) {
        my $pp = $self->placeholder_parser->clone;
        $pp->set('previous_extruder' => $self->writer->extruder->id);
        $pp->set('next_extruder'     => $extruder_id);
        $gcode .= sprintf "%s\n", $pp->process($self->config->toolchange_gcode);
    }
    
    # if ooze prevention is enabled, park current extruder in the nearest
    # standby point and set it to the standby temperature
    $gcode .= $self->ooze_prevention->pre_toolchange($self)
        if $self->ooze_prevention->enable && defined $self->writer->extruder;
    
    # append the toolchange command
    $gcode .= $self->writer->toolchange($extruder_id);
    
    # set the new extruder to the operating temperature
    $gcode .= $self->ooze_prevention->post_toolchange($self)
        if $self->ooze_prevention->enable;
    
    return $gcode;
}

package Slic3r::GCode::OozePrevention;
use strict;
use warnings;

use Slic3r::Geometry qw(scale);

sub pre_toolchange {
    my ($self, $gcodegen) = @_;
    
    my $gcode = "";
    
    # move to the nearest standby point
    if (@{$self->standby_points}) {
        # get current position in print coordinates
        my $pos = Slic3r::Point->new_scale(@{$gcodegen->writer->get_position}[0,1]);
        
        my $standby_point = Slic3r::Pointf->new_unscale(@{$pos->nearest_point($self->standby_points)});
        # We don't call $gcodegen->travel_to() because we don't need retraction (it was already
        # triggered by the caller) nor avoid_crossing_perimeters and also because the coordinates
        # of the destination point must not be transformed by origin nor current extruder offset.
        $gcode .= $gcodegen->writer->travel_to_xy($standby_point, 'move to standby position');
    }
    
    if ($gcodegen->config->standby_temperature_delta != 0) {
        my $temp = $gcodegen->has_layer && $gcodegen->layer->id == 0
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
        my $temp = $gcodegen->has_layer && $gcodegen->layer->id == 0
            ? $gcodegen->config->get_at('first_layer_temperature', $gcodegen->writer->extruder->id)
            : $gcodegen->config->get_at('temperature', $gcodegen->writer->extruder->id);
        $gcode .= $gcodegen->writer->set_temperature($temp, 1);
    }
    
    return $gcode;
}

package Slic3r::GCode::Wipe;
use strict;
use warnings;

use Slic3r::Geometry qw(scale);

sub wipe {
    my ($self, $gcodegen, $toolchange) = @_;
    
    my $gcode = "";
    
    # Reduce feedrate a bit; travel speed is often too high to move on existing material.
    # Too fast = ripping of existing material; too slow = short wipe path, thus more blob.
    my $wipe_speed = $gcodegen->writer->config->get('travel_speed') * 0.8;
    
    # get the retraction length
    my $length = $toolchange
        ? $gcodegen->writer->extruder->retract_length_toolchange
        : $gcodegen->writer->extruder->retract_length;
    
    if ($length) {
        # Calculate how long we need to travel in order to consume the required
        # amount of retraction. In other words, how far do we move in XY at $wipe_speed
        # for the time needed to consume retract_length at retract_speed?
        my $wipe_dist = scale($length / $gcodegen->writer->extruder->retract_speed * $wipe_speed);
    
        # Take the stored wipe path and replace first point with the current actual position
        # (they might be different, for example, in case of loop clipping).
        my $wipe_path = Slic3r::Polyline->new(
            $gcodegen->last_pos,
            @{$self->path}[1..$#{$self->path}],
        );
        # 
        $wipe_path->clip_end($wipe_path->length - $wipe_dist);
    
        # subdivide the retraction in segments
        my $retracted = 0;
        foreach my $line (@{$wipe_path->lines}) {
            my $segment_length = $line->length;
            # Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
            # due to rounding (TODO: test and/or better math for this)
            my $dE = $length * ($segment_length / $wipe_dist) * 0.95;
            $gcode .= $gcodegen->writer->set_speed($wipe_speed*60);
            $gcode .= $gcodegen->writer->extrude_to_xy(
                $gcodegen->point_to_gcode($line->b),
                -$dE,
                'wipe and retract' . ($gcodegen->enable_cooling_markers ? ';_WIPE' : ''),
            );
            $retracted += $dE;
        }
        $gcodegen->writer->extruder->set_retracted($gcodegen->writer->extruder->retracted + $retracted);
        
        # prevent wiping again on same path
        $self->reset_path;
    }
    
    return $gcode;
}

1;
