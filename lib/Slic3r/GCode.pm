package Slic3r::GCode;
use Moo;

use List::Util qw(min first);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Geometry qw(epsilon scale unscale scaled_epsilon points_coincide PI X Y B);
use Slic3r::Geometry::Clipper qw(union_ex);
use Slic3r::Surface ':types';

has 'config'             => (is => 'ro', required => 1);
has 'extruders'          => (is => 'ro', required => 1);
has 'multiple_extruders' => (is => 'lazy');
has 'standby_points'     => (is => 'rw');
has 'enable_loop_clipping' => (is => 'rw', default => sub {1});
has 'enable_wipe'        => (is => 'lazy');   # at least one extruder has wipe enabled
has 'layer_count'        => (is => 'ro', required => 1 );
has 'layer'              => (is => 'rw');
has '_layer_islands'     => (is => 'rw');
has '_upper_layer_islands'  => (is => 'rw');
has '_layer_overhangs_pp' => (is => 'rw');
has 'shift_x'            => (is => 'rw', default => sub {0} );
has 'shift_y'            => (is => 'rw', default => sub {0} );
has 'z'                  => (is => 'rw');
has 'speed'              => (is => 'rw');

has 'speeds'             => (is => 'lazy');  # mm/min
has 'external_mp'        => (is => 'rw');
has 'layer_mp'           => (is => 'rw');
has 'new_object'         => (is => 'rw', default => sub {0});
has 'straight_once'      => (is => 'rw', default => sub {1});
has 'extruder'           => (is => 'rw');
has 'elapsed_time'       => (is => 'rw', default => sub {0} );  # seconds
has 'lifted'             => (is => 'rw', default => sub {0} );
has 'last_pos'           => (is => 'rw', default => sub { Slic3r::Point->new(0,0) } );
has 'last_speed'         => (is => 'rw', default => sub {""});
has 'last_f'             => (is => 'rw', default => sub {""});
has 'last_fan_speed'     => (is => 'rw', default => sub {0});
has 'wipe_path'          => (is => 'rw');

sub _build_speeds {
    my $self = shift;
    return {
        map { $_ => 60 * $self->config->get_value("${_}_speed") }
            qw(travel perimeter small_perimeter external_perimeter infill
                solid_infill top_solid_infill support_material bridge gap_fill retract),
    };
}

# assign speeds to roles
my %role_speeds = (
    &EXTR_ROLE_PERIMETER                    => 'perimeter',
    &EXTR_ROLE_EXTERNAL_PERIMETER           => 'external_perimeter',
    &EXTR_ROLE_OVERHANG_PERIMETER           => 'bridge',
    &EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER   => 'perimeter',
    &EXTR_ROLE_FILL                         => 'infill',
    &EXTR_ROLE_SOLIDFILL                    => 'solid_infill',
    &EXTR_ROLE_TOPSOLIDFILL                 => 'top_solid_infill',
    &EXTR_ROLE_BRIDGE                       => 'bridge',
    &EXTR_ROLE_INTERNALBRIDGE               => 'solid_infill',
    &EXTR_ROLE_SKIRT                        => 'perimeter',
    &EXTR_ROLE_SUPPORTMATERIAL              => 'support_material',
    &EXTR_ROLE_GAPFILL                      => 'gap_fill',
);

sub _build_multiple_extruders {
    my $self = shift;
    return @{$self->extruders} > 1;
}

sub _build_enable_wipe {
    my $self = shift;
    return (first { $_->wipe } @{$self->extruders}) ? 1 : 0;
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
    
    # avoid computing islands and overhangs if they're not needed
    $self->_layer_islands($layer->islands);
    $self->_upper_layer_islands($layer->upper_layer ? $layer->upper_layer->islands : []);
    $self->_layer_overhangs_pp(
        # clone ExPolygons because they come from Surface objects but will be used outside here
        ($layer->id > 0 && ($layer->config->overhangs || $Slic3r::Config->start_perimeters_at_non_overhang))
            ? [ map $_->expolygon->pp, grep $_->surface_type == S_TYPE_BOTTOM, map @{$_->slices}, @{$layer->regions} ]
            : []
        );
    if ($self->config->avoid_crossing_perimeters) {
        $self->layer_mp(Slic3r::GCode::MotionPlanner->new(
            islands => union_ex([ map @$_, @{$layer->slices} ], 1),
        ));
    }
    
    my $gcode = "";
    if ($self->config->gcode_flavor =~ /^(?:makerware|sailfish)$/) {
        $gcode .= sprintf "M73 P%s%s\n",
            int(99 * ($layer->id / ($self->layer_count - 1))),
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
    
    $z += $self->config->z_offset;
    
    my $gcode = "";
    my $current_z = $self->z;
    if (!defined $self->z || $z > $self->z) {
        # if we're going over the current Z we won't be lifted anymore
        $self->lifted(0);
        
        # this retraction may alter $self->z
        $gcode .= $self->retract(move_z => $z) if $self->extruder->retract_layer_change;
        $self->speed('travel');
        $gcode .= $self->G0(undef, $z, 0, $comment || ('move to next layer (' . $self->layer->id . ')'))
            if !defined $self->z || abs($z - ($self->z - $self->lifted)) > epsilon;
    } elsif ($z < $self->z && $z > ($self->z - $self->lifted + epsilon)) {
        # we're moving to a layer height which is greater than the nominal current one
        # (nominal = actual - lifted) and less than the actual one.  we're basically
        # advancing to next layer, whose nominal Z is still lower than the previous
        # layer Z with lift.
        $self->lifted($self->z - $z);
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
    my ($self, $loop, $description) = @_;
    
    # extrude all loops ccw
    my $was_clockwise = $loop->make_counter_clockwise;
    my $polygon = $loop->polygon;
    
    # find candidate starting points
    # start looking for concave vertices not being overhangs
    my @concave = ();
    if ($Slic3r::Config->start_perimeters_at_concave_points) {
        @concave = $polygon->concave_points;
    }
    my @candidates = ();
    if ($Slic3r::Config->start_perimeters_at_non_overhang) {
        @candidates = grep !Boost::Geometry::Utils::point_covered_by_multi_polygon($_->pp, $self->_layer_overhangs_pp), @concave;
    }
    if (!@candidates) {
        # if none, look for any concave vertex
        @candidates = @concave;
        if (!@candidates) {
            # if none, look for any non-overhang vertex
            if ($Slic3r::Config->start_perimeters_at_non_overhang) {
                @candidates = grep !Boost::Geometry::Utils::point_covered_by_multi_polygon($_->pp, $self->_layer_overhangs_pp), @$polygon;
            }
            if (!@candidates) {
                # if none, all points are valid candidates
                @candidates = @{$polygon};
            }
        }
    }
    
    # find the point of the loop that is closest to the current extruder position
    # or randomize if requested
    my $last_pos = $self->last_pos;
    if ($self->config->randomize_start && $loop->role == EXTR_ROLE_CONTOUR_INTERNAL_PERIMETER) {
        $last_pos = Slic3r::Point->new(scale $self->config->print_center->[X], scale $self->config->bed_size->[Y]);
        $last_pos->rotate(rand(2*PI), $self->config->print_center);
    }
    
    # split the loop at the starting point and make a path
    my $start_at = $last_pos->nearest_point(\@candidates);
    my $extrusion_path = $loop->split_at($start_at);
    
    # clip the path to avoid the extruder to get exactly on the first point of the loop;
    # if polyline was shorter than the clipping distance we'd get a null polyline, so
    # we discard it in that case
    $extrusion_path->clip_end(scale $extrusion_path->flow_spacing * &Slic3r::LOOP_CLIPPING_LENGTH_OVER_SPACING)
        if $self->enable_loop_clipping;
    return '' if !@{$extrusion_path->polyline};
    
    my @paths = ();
    # detect overhanging/bridging perimeters
    if ($self->layer->config->overhangs && $extrusion_path->is_perimeter && @{$self->_layer_overhangs_pp}) {
        # get non-overhang paths by subtracting overhangs from the loop
        push @paths,
            $extrusion_path->subtract_expolygons($self->_layer_overhangs_pp);
        
        # get overhang paths by intersecting overhangs with the loop
        push @paths,
            map {
                $_->role(EXTR_ROLE_OVERHANG_PERIMETER);
                $_->flow_spacing($self->extruder->bridge_flow->width);
                $_
            }
            $extrusion_path->intersect_expolygons($self->_layer_overhangs_pp);
        
        # reapply the nearest point search for starting point
        # (clone because the collection gets DESTROY'ed)
        my $collection = Slic3r::ExtrusionPath::Collection->new(@paths);
        @paths = map $_->clone, @{$collection->chained_path_from($start_at, 1)};
    } else {
        push @paths, $extrusion_path;
    }
    
    # apply the small perimeter speed
    my %params = ();
    if ($extrusion_path->is_perimeter && abs($extrusion_path->length) <= &Slic3r::SMALL_PERIMETER_LENGTH) {
        $params{speed} = 'small_perimeter';
    }
    
    # extrude along the path
    my $gcode = join '', map $self->extrude_path($_, $description, %params), @paths;
    $self->wipe_path($extrusion_path->polyline->clone) if $self->enable_wipe;
    
    # make a little move inwards before leaving loop
    if ($loop->role == EXTR_ROLE_EXTERNAL_PERIMETER && defined $self->layer && $self->layer->object->config->perimeters > 1) {
        # detect angle between last and first segment
        # the side depends on the original winding order of the polygon (left for contours, right for holes)
        my @points = $was_clockwise ? (-2, 1) : (1, -2);
        my $angle = Slic3r::Geometry::angle3points(@{$extrusion_path->polyline}[0, @points]) / 3;
        $angle *= -1 if $was_clockwise;
        
        # create the destination point along the first segment and rotate it
        # we make sure we don't exceed the segment length because we don't know
        # the rotation of the second segment so we might cross the object boundary
        my $first_segment = Slic3r::Line->new(@{$extrusion_path->polyline}[0,1]);
        my $distance = min(scale $extrusion_path->flow_spacing, $first_segment->length);
        my $point = $first_segment->point_at($distance);
        $point->rotate($angle, $extrusion_path->first_point);
        
        # generate the travel move
        $gcode .= $self->travel_to($point, $loop->role, "move inwards before travel");
    }
    
    return $gcode;
}

sub extrude_path {
    my ($self, $path, $description, %params) = @_;
    
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
    
    my $area;  # mm^3 of extrudate per mm of tool movement 
    if ($path->is_bridge) {
        my $s = $path->flow_spacing;
        $area = ($s**2) * PI/4;
    } else {
        my $s = $path->flow_spacing;
        my $h = (defined $path->height && $path->height != -1) ? $path->height : $self->layer->height;
        $area = $self->extruder->mm3_per_mm($s, $h);
    }
    
    # calculate extrusion length per distance unit
    my $e = $self->extruder->e_per_mm3 * $area;
    $e = 0 if !$self->config->extrusion_axis;
    
    # set speed
    $self->speed( $params{speed} || $role_speeds{$path->role} || die "Unknown role: " . $path->role );
    my $F = $self->speeds->{$self->speed} // $self->speed;
    if ($self->layer->id == 0) {
        $F = $self->config->first_layer_speed =~ /^(\d+(?:\.\d+)?)%$/
            ? sprintf("%.3f", $F * $1/100)
            : $self->config->first_layer_speed * 60;
    }
    
    # extrude arc or line
    $gcode .= ";_BRIDGE_FAN_START\n" if $path->is_bridge;
    my $path_length = 0;
    {
        my $local_F = $F;
        foreach my $line (@{$path->lines}) {
            $path_length += my $line_length = unscale $line->length;
            
            # calculate extrusion length for this line
            my $E = 0;
            $E = $self->extruder->extrude($e * $line_length) if $e;
            
            # compose G-code line
            my $point = $line->b;
            $gcode .= sprintf "G1 X%.3f Y%.3f",
                ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->extruder->extruder_offset->[X], 
                ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->extruder->extruder_offset->[Y];  #**
            $gcode .= sprintf(" %s%.5f", $self->config->extrusion_axis, $E)
                if $E;
            $gcode .= " F$local_F"
                if $local_F;
            $gcode .= " ; $description"
                if $self->config->gcode_comments;
            $gcode .= "\n";
            
            # only include F in the first line
            $local_F = 0;
        }
        if ($self->enable_wipe) {
            $self->wipe_path($path->polyline->clone);
            $self->wipe_path->reverse;
        }
    }
    $gcode .= ";_BRIDGE_FAN_END\n" if $path->is_bridge;
    $self->last_pos($path->last_point);
    
    if ($self->config->cooling) {
        my $path_time = $path_length / $F * 60;
        if ($self->layer->id == 0) {
            $path_time = $self->config->first_layer_speed =~ /^(\d+(?:\.\d+)?)%$/
                ? $path_time / ($1/100)
                : $path_length / $self->config->first_layer_speed * 60;
        }
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
            && (first { $_->encloses_line($travel, scaled_epsilon) } @{$self->_upper_layer_islands})
            && (first { $_->encloses_line($travel, scaled_epsilon) } @{$self->_layer_islands}))
        || (defined $role && $role == EXTR_ROLE_SUPPORTMATERIAL && (first { $_->encloses_line($travel, scaled_epsilon) } @{$self->layer->support_islands}))
        ) {
        $self->straight_once(0);
        $self->speed('travel');
        $gcode .= $self->G0($point, undef, 0, $comment || "");
    } elsif (!$self->config->avoid_crossing_perimeters || $self->straight_once) {
        $self->straight_once(0);
        $gcode .= $self->retract;
        $self->speed('travel');
        $gcode .= $self->G0($point, undef, 0, $comment || "");
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
            next if first { !$island->encloses_line($_, scaled_epsilon) } @travel;
            # okay, this island encloses the full travel path
            $need_retract = 0;
            last;
        }
    }
    
    # do the retract (the travel_to argument is broken)
    $gcode .= $self->retract if $need_retract;
    
    # append the actual path and return
    $self->speed('travel');
    # use G1 because we rely on paths being straight (G0 may make round paths)
    $gcode .= join '', map $self->G1($_->[B], undef, 0, $comment || ""), @travel;
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
        $wipe_path->clip_end($wipe_path->length - $self->extruder->scaled_wipe_distance);
    }
    
    # prepare moves
    my $retract = [undef, undef, -$length, $comment];
    my $lift    = ($self->extruder->retract_lift == 0 || defined $params{move_z}) && !$self->lifted
        ? undef
        : [undef, $self->z + $self->extruder->retract_lift, 0, 'lift plate during travel'];
    
    # check that we have a positive wipe length
    if ($wipe_path) {
        $self->speed($self->speeds->{travel} * 0.8);
        
        # subdivide the retraction
        my $retracted = 0;
        foreach my $line (@{$wipe_path->lines}) {
            my $segment_length = $line->length;
            # reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
            # due to rounding
            my $e = $retract->[2] * ($segment_length / $self->extruder->scaled_wipe_distance) * 0.95;
            $retracted += $e;
            $gcode .= $self->G1($line->b, undef, $e, $retract->[3] . ";_WIPE");
        }
        if ($retracted > $retract->[2]) {
            # if we retracted less than we had to, retract the remainder
            # TODO: add regression test
            $self->speed('retract');
            $gcode .= $self->G1(undef, undef, $retract->[2] - $retracted, $comment);
        }
    } elsif ($self->config->use_firmware_retraction) {
        $gcode .= "G10 ; retract\n";
    } else {
        $self->speed('retract');
        $gcode .= $self->G1(@$retract);
    }
    if (!$self->lifted) {
        $self->speed('travel');
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
    $gcode .= $self->reset_e;
    
    $gcode .= "M103 ; extruder off\n" if $self->config->gcode_flavor eq 'makerware';
    
    return $gcode;
}

sub unretract {
    my ($self) = @_;
    
    my $gcode = "";
    $gcode .= "M101 ; extruder on\n" if $self->config->gcode_flavor eq 'makerware';
    
    if ($self->lifted) {
        $self->speed('travel');
        $gcode .= $self->G0(undef, $self->z - $self->lifted, 0, 'restore layer Z');
        $self->lifted(0);
    }
    
    my $to_unretract = $self->extruder->retracted + $self->extruder->restart_extra;
    if ($to_unretract) {
        $self->speed('retract');
        if ($self->config->use_firmware_retraction) {
            $gcode .= "G11 ; unretract\n";
        } elsif ($self->config->extrusion_axis) {
            # use G1 instead of G0 because G0 will blend the restart with the previous travel move
            $gcode .= sprintf "G1 E%.5f F%.3f",
                $self->extruder->extrude($to_unretract),
                $self->extruder->retract_speed_mm_min;
            $gcode .= " ; compensate retraction" if $self->config->gcode_comments;
            $gcode .= "\n";
        }
        $self->extruder->retracted(0);
        $self->extruder->restart_extra(0);
    }
    
    return $gcode;
}

sub reset_e {
    my ($self) = @_;
    return "" if $self->config->gcode_flavor =~ /^(?:mach3|makerware|sailfish)$/;
    
    $self->extruder->E(0) if $self->extruder;
    return sprintf "G92 %s0%s\n", $self->config->extrusion_axis, ($self->config->gcode_comments ? ' ; reset extrusion distance' : '')
        if $self->config->extrusion_axis && !$self->config->use_relative_e_distances;
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
    my ($self, $gcode, $point, $z, $e, $comment) = @_;
    
    if ($point) {
        $gcode .= sprintf " X%.3f Y%.3f", 
            ($point->x * &Slic3r::SCALING_FACTOR) + $self->shift_x - $self->extruder->extruder_offset->[X], 
            ($point->y * &Slic3r::SCALING_FACTOR) + $self->shift_y - $self->extruder->extruder_offset->[Y]; #**
        $self->last_pos($point->clone);
    }
    if (defined $z && (!defined $self->z || $z != $self->z)) {
        $self->z($z);
        $gcode .= sprintf " Z%.3f", $z;
    }
    
    return $self->_Gx($gcode, $e, $comment);
}

sub _Gx {
    my ($self, $gcode, $e, $comment) = @_;
    
    my $F = $self->speed eq 'retract'
        ? ($self->extruder->retract_speed_mm_min)
        : $self->speeds->{$self->speed} // $self->speed;
    $self->last_speed($self->speed);
    $self->last_f($F);
    $gcode .= sprintf " F%.3f", $F;
    
    # output extrusion distance
    if ($e && $self->config->extrusion_axis) {
        $gcode .= sprintf " %s%.5f", $self->config->extrusion_axis, $self->extruder->extrude($e);
    }
    
    $gcode .= " ; $comment" if $comment && $self->config->gcode_comments;
    return "$gcode\n";
}

sub set_extruder {
    my ($self, $extruder) = @_;
    
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
    if (defined $self->extruder && $self->config->toolchange_gcode) {
        $gcode .= sprintf "%s\n", $self->print->replace_variables($self->config->toolchange_gcode, {
            previous_extruder   => $self->extruder->id,
            next_extruder       => $extruder->id,
        });
    }
    
    # set the current extruder to the standby temperature
    if ($self->config->ooze_prevention && defined $self->extruder) {
        # move to the nearest standby point
        $gcode .= $self->travel_to($self->last_pos->nearest_point($self->standby_points));
        
        my $temp = defined $self->layer && $self->layer->id == 0
            ? $self->extruder->first_layer_temperature
            : $self->extruder->temperature;
        # we assume that heating is always slower than cooling, so no need to block
        $gcode .= $self->set_temperature($temp + $self->config->standby_temperature_delta, 0);
    }
    
    # set the new extruder
    $self->extruder($extruder);
    $gcode .= sprintf "%s%d%s\n", 
        ($self->config->gcode_flavor eq 'makerware'
            ? 'M135 T'
            : $self->config->gcode_flavor eq 'sailfish'
                ? 'M108 T'
                : 'T'),
        $extruder->id,
        ($self->config->gcode_comments ? ' ; change extruder' : '');
    
    $gcode .= $self->reset_e;
    
    # set the new extruder to the operating temperature
    if ($self->config->ooze_prevention) {
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
