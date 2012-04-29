package Slic3r::Print;
use Moo;

use Math::ConvexHull 1.0.4 qw(convex_hull);
use Slic3r::Geometry qw(X Y Z PI scale unscale move_points);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex offset JT_ROUND);

has 'objects'           => (is => 'rw', default => sub {[]});
has 'copies'            => (is => 'rw', default => sub {[]});  # obj_idx => [copies...]
has 'total_x_length'    => (is => 'rw'); # including duplicates
has 'total_y_length'    => (is => 'rw'); # including duplicates
has 'total_extrusion_length' => (is => 'rw');

# ordered collection of extrusion paths to build skirt loops
has 'skirt' => (
    is      => 'rw',
    #isa     => 'ArrayRef[Slic3r::ExtrusionLoop]',
    default => sub { [] },
);

sub add_object_from_mesh {
    my $self = shift;
    my ($mesh) = @_;
    
    $mesh->rotate($Slic3r::rotate);
    $mesh->scale($Slic3r::scale / $Slic3r::scaling_factor);
    $mesh->align_to_origin;
    
    # initialize print object
    my @size = $mesh->size;
    my $object = Slic3r::Print::Object->new(
        x_length => $size[X],
        y_length => $size[Y],
    );
    
    # process facets
    {
        my $apply_lines = sub {
            my $lines = shift;
            foreach my $layer_id (keys %$lines) {
                my $layer = $object->layer($layer_id);
                $layer->add_line($_) for @{ $lines->{$layer_id} };
            }
        };
        Slic3r::parallelize(
            disable => ($#{$mesh->facets} < 500),  # don't parallelize when too few facets
            items => [ 0..$#{$mesh->facets} ],
            thread_cb => sub {
                my $q = shift;
                my $result_lines = {};
                while (defined (my $facet_id = $q->dequeue)) {
                    my $lines = $mesh->slice_facet($object, $facet_id);
                    foreach my $layer_id (keys %$lines) {
                        $result_lines->{$layer_id} ||= [];
                        push @{ $result_lines->{$layer_id} }, @{ $lines->{$layer_id} };
                    }
                }
                return $result_lines;
            },
            collect_cb => sub {
                $apply_lines->($_[0]);
            },
            no_threads_cb => sub {
                for (0..$#{$mesh->facets}) {
                    my $lines = $mesh->slice_facet($object, $_);
                    $apply_lines->($lines);
                }
            },
        );
    }
    die "Invalid input file\n" if !@{$object->layers};
    
    # remove last layer if empty
    # (we might have created it because of the $max_layer = ... + 1 code below)
    pop @{$object->layers} if !@{$object->layers->[-1]->surfaces} && !@{$object->layers->[-1]->lines};
    
    foreach my $layer (@{ $object->layers }) {
        Slic3r::debugf "Making surfaces for layer %d (slice z = %f):\n",
            $layer->id, unscale $layer->slice_z if $Slic3r::debug;
        
        # layer currently has many lines representing intersections of
        # model facets with the layer plane. there may also be lines
        # that we need to ignore (for example, when two non-horizontal
        # facets share a common edge on our plane, we get a single line;
        # however that line has no meaning for our layer as it's enclosed
        # inside a closed polyline)
        
        # build surfaces from sparse lines
        $layer->make_surfaces($mesh->make_loops($layer));
        
        # free memory
        $layer->lines(undef);
    }
    
    # detect slicing errors
    my $warning_thrown = 0;
    for my $i (0 .. $#{$object->layers}) {
        my $layer = $object->layers->[$i];
        next unless $layer->slicing_errors;
        if (!$warning_thrown) {
            warn "The model has overlapping or self-intersecting facets. I tried to repair it, "
                . "however you might want to check the results or repair the input file and retry.\n";
            $warning_thrown = 1;
        }
        
        # try to repair the layer surfaces by merging all contours and all holes from
        # neighbor layers
        Slic3r::debugf "Attempting to repair layer %d\n", $i;
        
        my (@upper_surfaces, @lower_surfaces);
        for (my $j = $i+1; $j <= $#{$object->layers}; $j++) {
            if (!$object->layers->[$j]->slicing_errors) {
                @upper_surfaces = @{$object->layers->[$j]->slices};
                last;
            }
        }
        for (my $j = $i-1; $j >= 0; $j--) {
            if (!$object->layers->[$j]->slicing_errors) {
                @lower_surfaces = @{$object->layers->[$j]->slices};
                last;
            }
        }
        
        my $union = union_ex([
            map $_->expolygon->contour, @upper_surfaces, @lower_surfaces,
        ]);
        my $diff = diff_ex(
            [ map @$_, @$union ],
            [ map $_->expolygon->holes, @upper_surfaces, @lower_surfaces, ],
        );
        
        @{$layer->slices} = map Slic3r::Surface->new
            (expolygon => $_, surface_type => 'internal'),
            @$diff;
    }
    
    # remove empty layers from bottom
    while (@{$object->layers} && !@{$object->layers->[0]->slices} && !@{$object->layers->[0]->thin_walls}) {
        shift @{$object->layers};
        for (my $i = 0; $i <= $#{$object->layers}; $i++) {
            $object->layers->[$i]->id($i);
        }
    }
    
    warn "No layers were detected. You might want to repair your STL file and retry.\n"
        if !@{$object->layers};
    
    push @{$self->objects}, $object;
    return $object;
}

sub layer_count {
    my $self = shift;
    my $count = 0;
    foreach my $object (@{$self->objects}) {
        $count = @{$object->layers} if @{$object->layers} > $count;
    }
    return $count;
}

sub arrange_objects {
    my $self = shift;
    
    my $dist = scale $Slic3r::duplicate_distance;

    if ($Slic3r::duplicate_grid->[X] > 1 || $Slic3r::duplicate_grid->[Y] > 1) {
        if (@{$self->objects} > 1) {
            die "Grid duplication is not supported with multiple objects\n";
        }
        my $object = $self->objects->[0];
        $self->total_x_length($object->x_length * $Slic3r::duplicate_grid->[X] + $dist * ($Slic3r::duplicate_grid->[X] - 1));
        $self->total_y_length($object->y_length * $Slic3r::duplicate_grid->[Y] + $dist * ($Slic3r::duplicate_grid->[Y] - 1));
        
        # generate offsets for copies
        push @{$self->copies}, [];
        for my $x_copy (1..$Slic3r::duplicate_grid->[X]) {
            for my $y_copy (1..$Slic3r::duplicate_grid->[Y]) {
                push @{$self->copies->[0]}, [
                    ($object->x_length + $dist) * ($x_copy-1),
                    ($object->y_length + $dist) * ($y_copy-1),
                ];
            }
        }
    } elsif ($Slic3r::duplicate > 1 || @{$self->objects} > 1) {
        my $total_parts = @{$self->objects} * $Slic3r::duplicate;
        my $linint = sub {
            my ($value, $oldmin, $oldmax, $newmin, $newmax) = @_;
            return ($value - $oldmin) * ($newmax - $newmin) / ($oldmax - $oldmin) + $newmin;
        };

        # use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
        my $partx = my $party = 0;
        foreach my $object (@{$self->objects}) {
            $partx = $object->x_length if $object->x_length > $partx;
            $party = $object->y_length if $object->y_length > $party;
        }
        $partx = unscale($partx) + $Slic3r::duplicate_distance;
        $party = unscale($party) + $Slic3r::duplicate_distance;

        # margin needed for the skirt
        my $skirt_margin;		
        if ($Slic3r::skirts > 0) {
            $skirt_margin = ($Slic3r::flow_spacing * $Slic3r::skirts + $Slic3r::skirt_distance) * 2;
        } else {
            $skirt_margin = 0;		
        }

        # this is how many cells we have available into which to put parts
        my $cellw = int(($Slic3r::bed_size->[X] - $skirt_margin + $Slic3r::duplicate_distance) / $partx);
        my $cellh = int(($Slic3r::bed_size->[Y] - $skirt_margin + $Slic3r::duplicate_distance) / $party);

        die "$total_parts parts won't fit in your print area!\n" if $total_parts > ($cellw * $cellh);

        # width and height of space used by cells
        my $w = $cellw * $partx;
        my $h = $cellh * $party;

        # left and right border positions of space used by cells
        my $l = ($Slic3r::bed_size->[X] - $w) / 2;
        my $r = $l + $w;

        # top and bottom border positions
        my $t = ($Slic3r::bed_size->[Y] - $h) / 2;
        my $b = $t + $h;

        # list of cells, sorted by distance from center
        my @cellsorder;

        # work out distance for all cells, sort into list
        for my $i (0..$cellw-1) {
            for my $j (0..$cellh-1) {
                my $cx = $linint->($i + 0.5, 0, $cellw, $l, $r);
                my $cy = $linint->($j + 0.5, 0, $cellh, $t, $b);

                my $xd = abs(($Slic3r::bed_size->[X] / 2) - $cx);
                my $yd = abs(($Slic3r::bed_size->[Y] / 2) - $cy);

                my $c = {
                    location => [$cx, $cy],
                    index => [$i, $j],
                    distance => $xd * $xd + $yd * $yd - abs(($cellw / 2) - ($i + 0.5)),
                };

                BINARYINSERTIONSORT: {
                    my $index = $c->{distance};
                    my $low = 0;
                    my $high = @cellsorder;
                    while ($low < $high) {
                        my $mid = ($low + (($high - $low) / 2)) | 0;
                        my $midval = $cellsorder[$mid]->[0];
        
                        if ($midval < $index) {
                            $low = $mid + 1;
                        } elsif ($midval > $index) {
                            $high = $mid;
                        } else {
                            splice @cellsorder, $mid, 0, [$index, $c];
                            last BINARYINSERTIONSORT;
                        }
                    }
                    splice @cellsorder, $low, 0, [$index, $c];
                }
            }
        }

        # the extents of cells actually used by objects
        my ($lx, $ty, $rx, $by) = (0, 0, 0, 0);

        # now find cells actually used by objects, map out the extents so we can position correctly
        for my $i (1..$total_parts) {
            my $c = $cellsorder[$i - 1];
            my $cx = $c->[1]->{index}->[0];
            my $cy = $c->[1]->{index}->[1];
            if ($i == 1) {
                $lx = $rx = $cx;
                $ty = $by = $cy;
            } else {
                $rx = $cx if $cx > $rx;
                $lx = $cx if $cx < $lx;
                $by = $cy if $cy > $by;
                $ty = $cy if $cy < $ty;
            }
        }
        # now we actually place objects into cells, positioned such that the left and bottom borders are at 0
        for (0..$#{$self->objects}) {
            my @copies = ();
            for (1..$Slic3r::duplicate) {
                my $c = shift @cellsorder;
                my $cx = $c->[1]->{index}->[0] - $lx;
                my $cy = $c->[1]->{index}->[1] - $ty;
    
                push @copies, [scale($cx * $partx), scale($cy * $party)];
            }
            push @{$self->copies}, [@copies];
        }

        # save size of area used
        $self->total_x_length(scale(($rx - $lx + 1) * $partx - $Slic3r::duplicate_distance));
        $self->total_y_length(scale(($by - $ty + 1) * $party - $Slic3r::duplicate_distance));
    } else {
        $self->total_x_length($self->objects->[0]->x_length);
        $self->total_y_length($self->objects->[0]->y_length);
        push @{$self->copies}, [[0, 0]];
    }
}

sub max_length {
    my $self = shift;
    return ($self->total_x_length > $self->total_y_length) ? $self->total_x_length : $self->total_y_length;
}

sub make_skirt {
    my $self = shift;
    return unless $Slic3r::skirts > 0;
    
    # collect points from all layers contained in skirt height
    my $skirt_height = $Slic3r::skirt_height;
    $skirt_height = $self->layer_count if $skirt_height > $self->layer_count;
    my @points = ();
    foreach my $obj_idx (0 .. $#{$self->objects}) {
        my @layers = map $self->objects->[$obj_idx]->layer($_), 0..($skirt_height-1);
        my @layer_points = (
            (map @$_, map @{$_->expolygon}, map @{$_->slices}, @layers),
            (map @$_, map @{$_->thin_walls}, @layers),
            (map @{$_->polyline}, map @{$_->support_fills->paths}, grep $_->support_fills, @layers),
        );
        push @points, map move_points($_, @layer_points), @{$self->copies->[$obj_idx]};
    }
    return if @points < 3;  # at least three points required for a convex hull
    
    # find out convex hull
    my $convex_hull = convex_hull(\@points);
    
    # draw outlines from outside to inside
    my @skirt = ();
    for (my $i = $Slic3r::skirts - 1; $i >= 0; $i--) {
        my $distance = scale ($Slic3r::skirt_distance + ($Slic3r::flow_spacing * $i));
        my $outline = offset([$convex_hull], $distance, $Slic3r::scaling_factor * 100, JT_ROUND);
        push @skirt, Slic3r::ExtrusionLoop->new(
            polygon => Slic3r::Polygon->new(@{$outline->[0]}),
            role => 'skirt',
        );
    }
    push @{$self->skirt}, @skirt;
}

sub export_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # open output gcode file
    open my $fh, ">", $file
        or die "Failed to open $file for writing\n";
    
    # write some information
    my @lt = localtime;
    printf $fh "; generated by Slic3r $Slic3r::VERSION on %02d-%02d-%02d at %02d:%02d:%02d\n\n",
        $lt[5] + 1900, $lt[4]+1, $lt[3], $lt[2], $lt[1], $lt[0];

    print $fh "; $_\n" foreach split /\R/, $Slic3r::notes;
    print $fh "\n" if $Slic3r::notes;
    
    for (qw(layer_height perimeters solid_layers fill_density nozzle_diameter filament_diameter
        extrusion_multiplier perimeter_speed infill_speed travel_speed extrusion_width_ratio scale)) {
        printf $fh "; %s = %s\n", $_, Slic3r::Config->get($_);
    }
    printf $fh "; single wall width = %.2fmm\n", $Slic3r::flow_width;
    print  $fh "\n";
    
    # write start commands to file
    printf $fh "M%s %s%d ; set bed temperature\n",
        ($Slic3r::gcode_flavor eq 'makerbot' ? '109' : '190'),
        ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $Slic3r::first_layer_bed_temperature
            if $Slic3r::first_layer_bed_temperature && $Slic3r::start_gcode !~ /M190/i;
    printf $fh "M104 %s%d ; set temperature\n",
        ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $Slic3r::first_layer_temperature
            if $Slic3r::first_layer_temperature;
    printf $fh "%s\n", Slic3r::Config->replace_options($Slic3r::start_gcode);
    printf $fh "M109 %s%d ; wait for temperature to be reached\n", 
        ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $Slic3r::first_layer_temperature
            if $Slic3r::first_layer_temperature && $Slic3r::gcode_flavor ne 'makerbot'
                && $Slic3r::start_gcode !~ /M109/i;
    print  $fh "G90 ; use absolute coordinates\n";
    print  $fh "G21 ; set units to millimeters\n";
    if ($Slic3r::gcode_flavor =~ /^(?:reprap|teacup)$/) {
        printf $fh "G92 %s0 ; reset extrusion distance\n", $Slic3r::extrusion_axis if $Slic3r::extrusion_axis;
        if ($Slic3r::gcode_flavor =~ /^(?:reprap|makerbot)$/) {
            if ($Slic3r::use_relative_e_distances) {
                print $fh "M83 ; use relative distances for extrusion\n";
            } else {
                print $fh "M82 ; use absolute distances for extrusion\n";
            }
        }
    }
    
    # calculate X,Y shift to center print around specified origin
    my @shift = (
        $Slic3r::print_center->[X] - (unscale $self->total_x_length / 2),
        $Slic3r::print_center->[Y] - (unscale $self->total_y_length / 2),
    );
    
    # set up our extruder object
    my $extruder = Slic3r::Extruder->new;
    my $min_print_speed = 60 * $Slic3r::min_print_speed;
    my $dec = $extruder->dec;
    if ($Slic3r::support_material && $Slic3r::support_material_tool > 0) {
        print $fh $extruder->set_tool(0);
    }
    print $fh $extruder->set_fan(0, 1) if $Slic3r::cooling && $Slic3r::disable_fan_first_layers;
    
    # write gcode commands layer by layer
    my @layers = ();  # [ $obj_idx, $layer ]
    for my $layer_id (0..$self->layer_count) {
        push @layers, map [ $_, $self->objects->[$_]->layers->[$layer_id] ], 0..$#{$self->objects};
    }
    foreach my $obj_layer (grep $_->[1], @layers) {
        my ($obj_idx, $layer) = @$obj_layer;
        if ($layer->id == 1) {
            printf $fh "M104 %s%d ; set temperature\n",
                ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $Slic3r::temperature
                if $Slic3r::temperature && $Slic3r::temperature != $Slic3r::first_layer_temperature;
            printf $fh "M140 %s%d ; set bed temperature\n",
                ($Slic3r::gcode_flavor eq 'mach3' ? 'P' : 'S'), $Slic3r::bed_temperature
                if $Slic3r::bed_temperature && $Slic3r::bed_temperature != $Slic3r::first_layer_bed_temperature;
        }
        
        # go to layer
        my $layer_gcode = $extruder->change_layer($layer);
        $extruder->elapsed_time(0);
        
        # extrude skirt
        $extruder->shift_x($shift[X]);
        $extruder->shift_y($shift[Y]);
        $layer_gcode .= $extruder->set_acceleration($Slic3r::perimeter_acceleration);
        if ($layer->id < $Slic3r::skirt_height) {
            $layer_gcode .= $extruder->extrude_loop($_, 'skirt') for @{$self->skirt};
        }
        
        for my $copy (@{ $self->copies->[$obj_idx] }) {
            # retract explicitely because changing the shift_[xy] properties below
            # won't always trigger the automatic retraction
            $layer_gcode .= $extruder->retract;
            
            $extruder->shift_x($shift[X] + unscale $copy->[X]);
            $extruder->shift_y($shift[Y] + unscale $copy->[Y]);
            
            # extrude perimeters
            $layer_gcode .= $extruder->extrude($_, 'perimeter') for @{ $layer->perimeters };
            
            # extrude fills
            $layer_gcode .= $extruder->set_acceleration($Slic3r::infill_acceleration);
            for my $fill (@{ $layer->fills }) {
                $layer_gcode .= $extruder->extrude_path($_, 'fill') 
                    for $fill->shortest_path($extruder->last_pos);
            }
            
            # extrude support material
            if ($layer->support_fills) {
                $layer_gcode .= $extruder->set_tool($Slic3r::support_material_tool)
                    if $Slic3r::support_material_tool > 0;
                $layer_gcode .= $extruder->extrude_path($_, 'support material') 
                    for $layer->support_fills->shortest_path($extruder->last_pos);
                $layer_gcode .= $extruder->set_tool(0)
                    if $Slic3r::support_material_tool > 0;
            }
        }
        last if !$layer_gcode;
        
        my $fan_speed = $Slic3r::fan_always_on ? $Slic3r::min_fan_speed : 0;
        my $speed_factor = 1;
        if ($Slic3r::cooling) {
            my $layer_time = $extruder->elapsed_time;
            Slic3r::debugf "Layer %d estimated printing time: %d seconds\n", $layer->id, $layer_time;
            if ($layer_time < $Slic3r::slowdown_below_layer_time) {
                $fan_speed = $Slic3r::max_fan_speed;
                $speed_factor = $layer_time / $Slic3r::slowdown_below_layer_time;
            } elsif ($layer_time < $Slic3r::fan_below_layer_time) {
                $fan_speed = $Slic3r::max_fan_speed - ($Slic3r::max_fan_speed - $Slic3r::min_fan_speed)
                    * ($layer_time - $Slic3r::slowdown_below_layer_time)
                    / ($Slic3r::fan_below_layer_time - $Slic3r::slowdown_below_layer_time); #/
            }
            Slic3r::debugf "  fan = %d%%, speed = %d%%\n", $fan_speed, $speed_factor * 100;
            
            if ($speed_factor < 1) {
                $layer_gcode =~ s/^(?=.*? [XY])(?=.*? E)(G1 .*?F)(\d+(?:\.\d+)?)/
                    my $new_speed = $2 * $speed_factor;
                    $1 . sprintf("%.${dec}f", $new_speed < $min_print_speed ? $min_print_speed : $new_speed)
                    /gexm;
            }
            $fan_speed = 0 if $layer->id < $Slic3r::disable_fan_first_layers;
        }
        $layer_gcode = $extruder->set_fan($fan_speed) . $layer_gcode;
        
        # bridge fan speed
        if (!$Slic3r::cooling || $Slic3r::bridge_fan_speed == 0 || $layer->id < $Slic3r::disable_fan_first_layers) {
            $layer_gcode =~ s/^;_BRIDGE_FAN_(?:START|END)\n//gm;
        } else {
            $layer_gcode =~ s/^;_BRIDGE_FAN_START\n/ $extruder->set_fan($Slic3r::bridge_fan_speed, 1) /gmex;
            $layer_gcode =~ s/^;_BRIDGE_FAN_END\n/ $extruder->set_fan($fan_speed, 1) /gmex;
        }
        
        print $fh $layer_gcode;
    }
    
    # save statistic data
    $self->total_extrusion_length($extruder->total_extrusion_length);
    
    # write end commands to file
    print $fh $extruder->retract;
    print $fh $extruder->set_fan(0);
    print $fh "M501 ; reset acceleration\n" if $Slic3r::acceleration;
    printf $fh "%s\n", Slic3r::Config->replace_options($Slic3r::end_gcode);
    
    printf $fh "; filament used = %.1fmm (%.1fcm3)\n",
        $self->total_extrusion_length, $self->total_extrusion_volume;
    
    # close our gcode file
    close $fh;
}

sub total_extrusion_volume {
    my $self = shift;
    return $self->total_extrusion_length * ($Slic3r::filament_diameter**2) * PI/4 / 1000;
}

1;
