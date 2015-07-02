package Slic3r::GCode;
use strict;
use warnings;

use List::Util qw(min max first);
use Slic3r::ExtrusionLoop ':roles';
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(epsilon scale unscale PI X Y B);
use Slic3r::Geometry::Clipper qw(union_ex);

sub set_extruders {
    my ($self, $extruder_ids) = @_;
    
    $self->writer->set_extruders($extruder_ids);
    
    # enable wipe path generation if any extruder has wipe enabled
    $self->wipe->set_enable(defined first { $self->config->get_at('wipe', $_) } @$extruder_ids);
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

1;
