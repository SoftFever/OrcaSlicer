package Slic3r::Print;
use strict;
use warnings;

use File::Basename qw(basename fileparse);
use File::Spec;
use List::Util qw(min max first sum);
use Slic3r::ExtrusionPath ':roles';
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y Z X1 Y1 X2 Y2 MIN MAX PI scale unscale chained_path
    convex_hull);
use Slic3r::Geometry::Clipper qw(diff_ex union_ex intersection_ex intersection offset
    offset2 union union_pt_chained JT_ROUND JT_SQUARE);
use Slic3r::Print::State ':steps';

our $status_cb;

sub new {
    # TODO: port PlaceholderParser methods to C++, then its own constructor
    # can call them and no need for this new() method at all
    my ($class) = @_;
    my $self = $class->_new;
    $self->placeholder_parser->apply_env_variables;
    $self->placeholder_parser->update_timestamp;
    return $self;
}

sub set_status_cb {
    my ($class, $cb) = @_;
    $status_cb = $cb;
}

sub status_cb {
    return $status_cb // sub {};
}

# this value is not supposed to be compared with $layer->id
# since they have different semantics
sub total_layer_count {
    my $self = shift;
    return max(map $_->total_layer_count, @{$self->objects});
}

# the bounding box of objects placed in copies position
# (without taking skirt/brim/support material into account)
sub bounding_box {
    my $self = shift;
    
    my @points = ();
    foreach my $object (@{$self->objects}) {
        foreach my $copy (@{$object->_shifted_copies}) {
            push @points,
                [ $copy->[X], $copy->[Y] ],
                [ $copy->[X] + $object->size->[X], $copy->[Y] + $object->size->[Y] ];
        }
    }
    return Slic3r::Geometry::BoundingBox->new_from_points([ map Slic3r::Point->new(@$_), @points ]);
}

# the total bounding box of extrusions, including skirt/brim/support material
sub total_bounding_box {
    my ($self) = @_;
    
    # get objects bounding box
    my $bb = $self->bounding_box;
    
    # check how much we need to increase it
    my $extra = 0;
    if ($self->has_support_material) {
        $extra = &Slic3r::Print::SupportMaterial::MARGIN;
    }
    $extra = max($extra, $self->config->brim_width);
    if ($self->config->skirts > 0) {
        my $skirt_flow = $self->skirt_flow;
        $extra = max($extra, $self->config->brim_width + $self->config->skirt_distance + ($self->config->skirts * $skirt_flow->spacing));
    }
    
    if ($extra > 0) {
        $bb->offset(scale $extra);
    }
    return $bb;
}

sub size {
    my $self = shift;
    return $self->bounding_box->size;
}

sub process {
    my ($self) = @_;
    
    $_->make_perimeters for @{$self->objects};
    $_->infill for @{$self->objects};
    $_->generate_support_material for @{$self->objects};
    $self->make_skirt;
    $self->make_brim;  # must come after make_skirt
    
    # time to make some statistics
    if (0) {
        eval "use Devel::Size";
        print  "MEMORY USAGE:\n";
        printf "  meshes        = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->meshes), @{$self->objects})/1024/1024;
        printf "  layer slices  = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  region slices = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->slices), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  perimeters    = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->perimeters), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  fills         = %.1fMb\n", List::Util::sum(map Devel::Size::total_size($_->fills), map @{$_->regions}, map @{$_->layers}, @{$self->objects})/1024/1024;
        printf "  print object  = %.1fMb\n", Devel::Size::total_size($self)/1024/1024;
    }
    if (0) {
        eval "use Slic3r::Test::SectionCut";
        Slic3r::Test::SectionCut->new(print => $self)->export_svg("section_cut.svg");
    }
}

sub export_gcode {
    my $self = shift;
    my %params = @_;
    
    # prerequisites
    $self->process;
    
    # output everything to a G-code file
    my $output_file = $self->expanded_output_filepath($params{output_file});
    $self->status_cb->(90, "Exporting G-code" . ($output_file ? " to $output_file" : ""));
    $self->write_gcode($params{output_fh} || $output_file);
    
    # run post-processing scripts
    if (@{$self->config->post_process}) {
        $self->status_cb->(95, "Running post-processing scripts");
        $self->config->setenv;
        for (@{$self->config->post_process}) {
            Slic3r::debugf "  '%s' '%s'\n", $_, $output_file;
            system($_, $output_file);
        }
    }
}

sub export_svg {
    my $self = shift;
    my %params = @_;
    
    # is this needed?
    $self->init_extruders;
    
    $_->slice for @{$self->objects};
    
    my $fh = $params{output_fh};
    if (!$fh) {
        my $output_file = $self->expanded_output_filepath($params{output_file});
        $output_file =~ s/\.gcode$/.svg/i;
        Slic3r::open(\$fh, ">", $output_file) or die "Failed to open $output_file for writing\n";
        print "Exporting to $output_file..." unless $params{quiet};
    }
    
    my $print_size = $self->size;
    print $fh sprintf <<"EOF", unscale($print_size->[X]), unscale($print_size->[Y]);
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg width="%s" height="%s" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:slic3r="http://slic3r.org/namespaces/slic3r">
  <!-- 
  Generated using Slic3r $Slic3r::VERSION
  http://slic3r.org/
   -->
EOF
    
    my $print_polygon = sub {
        my ($polygon, $type) = @_;
        printf $fh qq{    <polygon slic3r:type="%s" points="%s" style="fill: %s" />\n},
            $type, (join ' ', map { join ',', map unscale $_, @$_ } @$polygon),
            ($type eq 'contour' ? 'white' : 'black');
    };
    
    my @layers = sort { $a->print_z <=> $b->print_z }
        map { @{$_->layers}, @{$_->support_layers} }
        @{$self->objects};
    
    my $layer_id = -1;
    my @previous_layer_slices = ();
    for my $layer (@layers) {
        $layer_id++;
        # TODO: remove slic3r:z for raft layers
        printf $fh qq{  <g id="layer%d" slic3r:z="%s">\n}, $layer_id, unscale($layer->slice_z);
        
        my @current_layer_slices = ();
        # sort slices so that the outermost ones come first
        my @slices = sort { $a->contour->contains_point($b->contour->[0]) ? 0 : 1 } @{$layer->slices};
        foreach my $copy (@{$layer->object->copies}) {
            foreach my $slice (@slices) {
                my $expolygon = $slice->clone;
                $expolygon->translate(@$copy);
                $print_polygon->($expolygon->contour, 'contour');
                $print_polygon->($_, 'hole') for @{$expolygon->holes};
                push @current_layer_slices, $expolygon;
            }
        }
        # generate support material
        if ($self->has_support_material && $layer->id > 0) {
            my (@supported_slices, @unsupported_slices) = ();
            foreach my $expolygon (@current_layer_slices) {
                my $intersection = intersection_ex(
                    [ map @$_, @previous_layer_slices ],
                    [ @$expolygon ],
                );
                @$intersection
                    ? push @supported_slices, $expolygon
                    : push @unsupported_slices, $expolygon;
            }
            my @supported_points = map @$_, @$_, @supported_slices;
            foreach my $expolygon (@unsupported_slices) {
                # look for the nearest point to this island among all
                # supported points
                my $contour = $expolygon->contour;
                my $support_point = $contour->first_point->nearest_point(\@supported_points)
                    or next;
                my $anchor_point = $support_point->nearest_point([ @$contour ]);
                printf $fh qq{    <line x1="%s" y1="%s" x2="%s" y2="%s" style="stroke-width: 2; stroke: white" />\n},
                    map @$_, $support_point, $anchor_point;
            }
        }
        print $fh qq{  </g>\n};
        @previous_layer_slices = @current_layer_slices;
    }
    
    print $fh "</svg>\n";
    close $fh;
    print "Done.\n" unless $params{quiet};
}

sub make_skirt {
    my $self = shift;
    
    # prerequisites
    $_->make_perimeters for @{$self->objects};
    $_->infill for @{$self->objects};
    $_->generate_support_material for @{$self->objects};
    
    return if $self->step_done(STEP_SKIRT);
    $self->set_step_started(STEP_SKIRT);
    
    # since this method must be idempotent, we clear skirt paths *before*
    # checking whether we need to generate them
    $self->skirt->clear;
    
    if ($self->config->skirts == 0
        && (!$self->config->ooze_prevention || @{$self->extruders} == 1)) {
        $self->set_step_done(STEP_SKIRT);
        return;
    }
    $self->status_cb->(88, "Generating skirt");
    
    # First off we need to decide how tall the skirt must be.
    # The skirt_height option from config is expressed in layers, but our
    # object might have different layer heights, so we need to find the print_z
    # of the highest layer involved.
    # Note that unless skirt_height == -1 (which means it's printed on all layers)
    # the actual skirt might not reach this $skirt_height_z value since the print
    # order of objects on each layer is not guaranteed and will not generally
    # include the thickest object first. It is just guaranteed that a skirt is
    # prepended to the first 'n' layers (with 'n' = skirt_height).
    # $skirt_height_z in this case is the highest possible skirt height for safety.
    my $skirt_height_z = -1;
    foreach my $object (@{$self->objects}) {
        my $skirt_height = ($self->config->skirt_height == -1)
            ? scalar(@{$object->layers})
            : min($self->config->skirt_height, scalar(@{$object->layers}));
        
        my $highest_layer = $object->get_layer($skirt_height - 1);
        $skirt_height_z = max($skirt_height_z, $highest_layer->print_z);
    }
    
    # collect points from all layers contained in skirt height
    my @points = ();
    foreach my $object (@{$self->objects}) {
        my @object_points = ();
        
        # get object layers up to $skirt_height_z
        foreach my $layer (@{$object->layers}) {
            last if $layer->print_z > $skirt_height_z;
            push @object_points, map @$_, map @$_, @{$layer->slices};
        }
        
        # get support layers up to $skirt_height_z
        foreach my $layer (@{$object->support_layers}) {
            last if $layer->print_z > $skirt_height_z;
            push @object_points, map @{$_->polyline}, @{$layer->support_fills} if $layer->support_fills;
            push @object_points, map @{$_->polyline}, @{$layer->support_interface_fills} if $layer->support_interface_fills;
        }
        
        # repeat points for each object copy
        foreach my $copy (@{$object->_shifted_copies}) {
            my @copy_points = map $_->clone, @object_points;
            $_->translate(@$copy) for @copy_points;
            push @points, @copy_points;
        }
    }
    return if @points < 3;  # at least three points required for a convex hull
    
    # find out convex hull
    my $convex_hull = convex_hull(\@points);
    
    my @extruded_length = ();  # for each extruder
    
    # skirt may be printed on several layers, having distinct layer heights,
    # but loops must be aligned so can't vary width/spacing
    # TODO: use each extruder's own flow
    my $first_layer_height = $self->skirt_first_layer_height;
    my $flow = $self->skirt_flow;
    my $spacing = $flow->spacing;
    my $mm3_per_mm = $flow->mm3_per_mm;
    
    my @extruders_e_per_mm = ();
    my $extruder_idx = 0;
    
    # draw outlines from outside to inside
    # loop while we have less skirts than required or any extruder hasn't reached the min length if any
    my $distance = scale max($self->config->skirt_distance, $self->config->brim_width);
    for (my $i = $self->config->skirts; $i > 0; $i--) {
        $distance += scale $spacing;
        my $loop = offset([$convex_hull], $distance, 1, JT_ROUND, scale(0.1))->[0];
        $self->skirt->append(Slic3r::ExtrusionLoop->new_from_paths(
            Slic3r::ExtrusionPath->new(
                polyline        => Slic3r::Polygon->new(@$loop)->split_at_first_point,
                role            => EXTR_ROLE_SKIRT,
                mm3_per_mm      => $mm3_per_mm,
                width           => $flow->width,
                height          => $first_layer_height,
            ),
        ));
        
        if ($self->config->min_skirt_length > 0) {
            $extruded_length[$extruder_idx] ||= 0;
            if (!$extruders_e_per_mm[$extruder_idx]) {
                my $config = Slic3r::Config::GCode->new;
                $config->apply_print_config($self->config);
                my $extruder = Slic3r::Extruder->new($extruder_idx, $config);
                $extruders_e_per_mm[$extruder_idx] = $extruder->e_per_mm($mm3_per_mm);
            }
            $extruded_length[$extruder_idx] += unscale $loop->length * $extruders_e_per_mm[$extruder_idx];
            $i++ if defined first { ($extruded_length[$_] // 0) < $self->config->min_skirt_length } 0 .. $#{$self->extruders};
            if ($extruded_length[$extruder_idx] >= $self->config->min_skirt_length) {
                if ($extruder_idx < $#{$self->extruders}) {
                    $extruder_idx++;
                    next;
                }
            }
        }
    }
    
    $self->skirt->reverse;
    
    $self->set_step_done(STEP_SKIRT);
}

sub make_brim {
    my $self = shift;
    
    # prerequisites
    $_->make_perimeters for @{$self->objects};
    $_->infill for @{$self->objects};
    $_->generate_support_material for @{$self->objects};
    $self->make_skirt;
    
    return if $self->step_done(STEP_BRIM);
    $self->set_step_started(STEP_BRIM);
    
    # since this method must be idempotent, we clear brim paths *before*
    # checking whether we need to generate them
    $self->brim->clear;
    
    if ($self->config->brim_width == 0) {
        $self->set_step_done(STEP_BRIM);
        return;
    }
    $self->status_cb->(88, "Generating brim");
    
    # brim is only printed on first layer and uses support material extruder
    my $first_layer_height = $self->skirt_first_layer_height;
    my $flow = $self->skirt_flow;
    my $mm3_per_mm = $flow->mm3_per_mm;
    
    my $grow_distance = $flow->scaled_width / 2;
    my @islands = (); # array of polygons
    foreach my $obj_idx (0 .. ($self->object_count - 1)) {
        my $object = $self->objects->[$obj_idx];
        my $layer0 = $object->get_layer(0);
        my @object_islands = (
            (map $_->contour, @{$layer0->slices}),
        );
        if (@{ $object->support_layers }) {
            my $support_layer0 = $object->support_layers->[0];
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_fills})
                if $support_layer0->support_fills;
            push @object_islands,
                (map @{$_->polyline->grow($grow_distance)}, @{$support_layer0->support_interface_fills})
                if $support_layer0->support_interface_fills;
        }
        foreach my $copy (@{$object->_shifted_copies}) {
            push @islands, map { $_->translate(@$copy); $_ } map $_->clone, @object_islands;
        }
    }
    
    my @loops = ();
    my $num_loops = sprintf "%.0f", $self->config->brim_width / $flow->width;
    for my $i (reverse 1 .. $num_loops) {
        # JT_SQUARE ensures no vertex is outside the given offset distance
        # -0.5 because islands are not represented by their centerlines
        # (first offset more, then step back - reverse order than the one used for 
        # perimeters because here we're offsetting outwards)
        push @loops, @{offset2(\@islands, ($i + 0.5) * $flow->scaled_spacing, -1.0 * $flow->scaled_spacing, 100000, JT_SQUARE)};
    }
    
    $self->brim->append(map Slic3r::ExtrusionLoop->new_from_paths(
        Slic3r::ExtrusionPath->new(
            polyline        => Slic3r::Polygon->new(@$_)->split_at_first_point,
            role            => EXTR_ROLE_SKIRT,
            mm3_per_mm      => $mm3_per_mm,
            width           => $flow->width,
            height          => $first_layer_height,
        ),
    ), reverse @{union_pt_chained(\@loops)});
    
    $self->set_step_done(STEP_BRIM);
}

sub skirt_first_layer_height {
    my ($self) = @_;
    return $self->objects->[0]->config->get_abs_value('first_layer_height');
}

sub skirt_flow {
    my ($self) = @_;
    
    return Slic3r::Flow->new_from_width(
        width               => ($self->config->first_layer_extrusion_width || $self->regions->[0]->config->perimeter_extrusion_width),
        role                => FLOW_ROLE_PERIMETER,
        nozzle_diameter     => $self->config->get_at('nozzle_diameter', $self->objects->[0]->config->support_material_extruder-1),
        layer_height        => $self->skirt_first_layer_height,
        bridge_flow_ratio   => 0,
    );
}

sub write_gcode {
    my $self = shift;
    my ($file) = @_;
    
    # open output gcode file if we weren't supplied a file-handle
    my $fh;
    if (ref $file eq 'IO::Scalar') {
        $fh = $file;
    } else {
        Slic3r::open(\$fh, ">", $file)
            or die "Failed to open $file for writing\n";
        
        # enable UTF-8 output since user might have entered Unicode characters in fields like notes
        binmode $fh, ':utf8';
    }
    
    
    # write some information
    my @lt = localtime;
    printf $fh "; generated by Slic3r $Slic3r::VERSION on %04d-%02d-%02d at %02d:%02d:%02d\n\n",
        $lt[5] + 1900, $lt[4]+1, $lt[3], $lt[2], $lt[1], $lt[0];

    print $fh "; $_\n" foreach split /\R/, $self->config->notes;
    print $fh "\n" if $self->config->notes;
    
    my $first_object = $self->objects->[0];
    my $layer_height = $first_object->config->layer_height;
    for my $region_id (0..$#{$self->regions}) {
        printf $fh "; external perimeters extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_EXTERNAL_PERIMETER, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; perimeters extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_PERIMETER, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_INFILL, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; solid infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_SOLID_INFILL, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; top infill extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_TOP_SOLID_INFILL, $layer_height, 0, 0, -1, $first_object)->width;
        printf $fh "; support material extrusion width = %.2fmm\n",
            $self->objects->[0]->support_material_flow->width
            if $self->has_support_material;
        printf $fh "; first layer extrusion width = %.2fmm\n",
            $self->regions->[$region_id]->flow(FLOW_ROLE_PERIMETER, $layer_height, 0, 1, -1, $self->objects->[0])->width
            if $self->regions->[$region_id]->config->first_layer_extrusion_width;
        print  $fh "\n";
    }
    
    # prepare the helper object for replacing placeholders in custom G-code and output filename
    $self->placeholder_parser->update_timestamp;
    
    # estimate the total number of layer changes
    # TODO: only do this when M73 is enabled
    my $layer_count;
    if ($self->config->complete_objects) {
        $layer_count = sum(map { $_->total_layer_count * @{$_->copies} } @{$self->objects});
    } else {
        # if sequential printing is not enable, all copies of the same object share the same layer change command(s)
        $layer_count = sum(map { $_->total_layer_count } @{$self->objects});
    }
    
    # set up our helper object
    my $gcodegen = Slic3r::GCode->new(
        placeholder_parser  => $self->placeholder_parser,
        layer_count         => $layer_count,
        enable_cooling_markers => 1,
    );
    $gcodegen->apply_print_config($self->config);
    $gcodegen->set_extruders($self->extruders);
    
    print $fh $gcodegen->writer->set_fan(0, 1) if $self->config->cooling && $self->config->disable_fan_first_layers;
    
    # set bed temperature
    if ((my $temp = $self->config->first_layer_bed_temperature) && $self->config->start_gcode !~ /M(?:190|140)/i) {
        printf $fh $gcodegen->writer->set_bed_temperature($temp, 1);
    }
    
    # set extruder(s) temperature before and after start G-code
    my $print_first_layer_temperature = sub {
        my ($wait) = @_;
        
        return if $self->config->start_gcode =~ /M(?:109|104)/i;
        for my $t (@{$self->extruders}) {
            my $temp = $self->config->get_at('first_layer_temperature', $t);
            $temp += $self->config->standby_temperature_delta if $self->config->ooze_prevention;
            printf $fh $gcodegen->writer->set_temperature($temp, $wait, $t) if $temp > 0;
        }
    };
    $print_first_layer_temperature->(0);
    printf $fh "%s\n", $gcodegen->placeholder_parser->process($self->config->start_gcode);
    $print_first_layer_temperature->(1);
    
    # set other general things
    print $fh $gcodegen->preamble;
    
    # initialize a motion planner for object-to-object travel moves
    if ($self->config->avoid_crossing_perimeters) {
        my $distance_from_objects = 1;
        # compute the offsetted convex hull for each object and repeat it for each copy.
        my @islands = ();
        foreach my $obj_idx (0 .. ($self->object_count - 1)) {
            my $convex_hull = convex_hull([
                map @{$_->contour}, map @{$_->slices}, @{$self->objects->[$obj_idx]->layers},
            ]);
            # discard layers only containing thin walls (offset would fail on an empty polygon)
            if (@$convex_hull) {
                my $expolygon = Slic3r::ExPolygon->new($convex_hull);
                my @island = @{$expolygon->offset_ex(scale $distance_from_objects, 1, JT_SQUARE)};
                foreach my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                    push @islands, map { my $c = $_->clone; $c->translate(@$copy); $c } @island;
                }
            }
        }
        $gcodegen->init_external_mp(union_ex([ map @$_, @islands ]));
    }
    
    # calculate wiping points if needed
    if ($self->config->ooze_prevention) {
        my @skirt_points = map @$_, map @$_, @{$self->skirt};
        if (@skirt_points) {
            my $outer_skirt = convex_hull(\@skirt_points);
            my @skirts = ();
            foreach my $extruder_id (@{$self->extruders}) {
                push @skirts, my $s = $outer_skirt->clone;
                $s->translate(map scale($_), @{$self->config->get_at('extruder_offset', $extruder_id)});
            }
            my $convex_hull = convex_hull([ map @$_, @skirts ]);
            
            my $oozeprev = Slic3r::GCode::OozePrevention->new(
                standby_points => [ map $_->clone, map @$_, map $_->subdivide(scale 10), @{offset([$convex_hull], scale 3)} ],
            );
            $gcodegen->ooze_prevention($oozeprev);
        }
    }
    
    # prepare the layer processor
    my $layer_gcode = Slic3r::GCode::Layer->new(
        print       => $self,
        gcodegen    => $gcodegen,
    );
    
    # set initial extruder only after custom start G-code
    print $fh $gcodegen->set_extruder($self->extruders->[0]);
    
    # do all objects for each layer
    if ($self->config->complete_objects) {
        # print objects from the smallest to the tallest to avoid collisions
        # when moving onto next object starting point
        my @obj_idx = sort { $self->objects->[$a]->size->[Z] <=> $self->objects->[$b]->size->[Z] } 0..($self->object_count - 1);
        
        my $finished_objects = 0;
        for my $obj_idx (@obj_idx) {
            my $object = $self->objects->[$obj_idx];
            for my $copy (@{ $self->objects->[$obj_idx]->_shifted_copies }) {
                # move to the origin position for the copy we're going to print.
                # this happens before Z goes down to layer 0 again, so that 
                # no collision happens hopefully.
                if ($finished_objects > 0) {
                    $gcodegen->set_origin(Slic3r::Pointf->new(map unscale $copy->[$_], X,Y));
                    print $fh $gcodegen->retract;
                    print $fh $gcodegen->travel_to(
                        $object->_copies_shift->negative,
                        undef,
                        'move to origin position for next object',
                    );
                }
                
                my $buffer = Slic3r::GCode::CoolingBuffer->new(
                    config      => $self->config,
                    gcodegen    => $gcodegen,
                );
                
                my @layers = sort { $a->print_z <=> $b->print_z } @{$object->layers}, @{$object->support_layers};
                for my $layer (@layers) {
                    # if we are printing the bottom layer of an object, and we have already finished
                    # another one, set first layer temperatures. this happens before the Z move
                    # is triggered, so machine has more time to reach such temperatures
                    if ($layer->id == 0 && $finished_objects > 0) {
                        printf $fh $gcodegen->writer->set_bed_temperature($self->config->first_layer_bed_temperature),
                            if $self->config->first_layer_bed_temperature;
                        $print_first_layer_temperature->(0);
                    }
                    print $fh $buffer->append(
                        $layer_gcode->process_layer($layer, [$copy]),
                        $layer->object->ptr,
                        $layer->id,
                        $layer->print_z,
                    );
                }
                print $fh $buffer->flush;
                $finished_objects++;
            }
        }
    } else {
        # order objects using a nearest neighbor search
        my @obj_idx = @{chained_path([ map Slic3r::Point->new(@{$_->_shifted_copies->[0]}), @{$self->objects} ])};
        
        # sort layers by Z
        my %layers = ();  # print_z => [ [layers], [layers], [layers] ]  by obj_idx
        foreach my $obj_idx (0 .. ($self->object_count - 1)) {
            my $object = $self->objects->[$obj_idx];
            foreach my $layer (@{$object->layers}, @{$object->support_layers}) {
                $layers{ $layer->print_z } ||= [];
                $layers{ $layer->print_z }[$obj_idx] ||= [];
                push @{$layers{ $layer->print_z }[$obj_idx]}, $layer;
            }
        }
        
        my $buffer = Slic3r::GCode::CoolingBuffer->new(
            config      => $self->config,
            gcodegen    => $gcodegen,
        );
        foreach my $print_z (sort { $a <=> $b } keys %layers) {
            foreach my $obj_idx (@obj_idx) {
                foreach my $layer (@{ $layers{$print_z}[$obj_idx] // [] }) {
                    print $fh $buffer->append(
                        $layer_gcode->process_layer($layer, $layer->object->_shifted_copies),
                        $layer->object->ptr . ref($layer),  # differentiate $obj_id between normal layers and support layers
                        $layer->id,
                        $layer->print_z,
                    );
                }
            }
        }
        print $fh $buffer->flush;
    }
    
    # write end commands to file
    print $fh $gcodegen->retract;
    print $fh $gcodegen->writer->set_fan(0);
    printf $fh "%s\n", $gcodegen->placeholder_parser->process($self->config->end_gcode);
    print $fh $gcodegen->writer->update_progress($gcodegen->layer_count, $gcodegen->layer_count, 1);  # 100%
    
    $self->total_used_filament(0);
    $self->total_extruded_volume(0);
    foreach my $extruder (@{$gcodegen->writer->extruders}) {
        my $used_filament = $extruder->used_filament;
        my $extruded_volume = $extruder->extruded_volume;
        
        printf $fh "; filament used = %.1fmm (%.1fcm3)\n",
            $used_filament, $extruded_volume/1000;
        
        $self->total_used_filament($self->total_used_filament + $used_filament);
        $self->total_extruded_volume($self->total_extruded_volume + $extruded_volume);
    }
    
    # append full config
    print $fh "\n";
    foreach my $config ($self->config, $self->default_object_config, $self->default_region_config) {
        foreach my $opt_key (sort @{$config->get_keys}) {
            next if $Slic3r::Config::Options->{$opt_key}{shortcut};
            printf $fh "; %s = %s\n", $opt_key, $config->serialize($opt_key);
        }
    }
    
    # close our gcode file
    close $fh;
}

# this method will return the supplied input file path after expanding its
# format variables with their values
sub expanded_output_filepath {
    my $self = shift;
    my ($path) = @_;
    
    return undef if !@{$self->objects};
    my $input_file = first { defined $_ } map $_->model_object->input_file, @{$self->objects};
    return undef if !defined $input_file;
    
    my $filename = my $filename_base = basename($input_file);
    $filename_base =~ s/\.[^.]+$//;  # without suffix
    my $extra = {
        input_filename      => $filename,
        input_filename_base => $filename_base,
    };
    
    if ($path && -d $path) {
        # if output path is an existing directory, we take that and append
        # the specified filename format
        $path = File::Spec->join($path, $self->config->output_filename_format);
    } elsif (!$path) {
        # if no explicit output file was defined, we take the input
        # file directory and append the specified filename format
        $path = (fileparse($input_file))[1] . $self->config->output_filename_format;
    } else {
        # path is a full path to a file so we use it as it is
    }
    
    # make sure we use an up-to-date timestamp
    $self->placeholder_parser->update_timestamp;
    return $self->placeholder_parser->process($path, $extra);
}

# This method assigns extruders to the volumes having a material
# but not having extruders set in the material config.
sub auto_assign_extruders {
    my ($self, $model_object) = @_;
    
    # only assign extruders if object has more than one volume
    return if @{$model_object->volumes} == 1;
    
    my $extruders = scalar @{ $self->config->nozzle_diameter };
    foreach my $i (0..$#{$model_object->volumes}) {
        my $volume = $model_object->volumes->[$i];
        if ($volume->material_id ne '') {
            my $material = $model_object->model->get_material($volume->material_id);
            my $config = $material->config;
            my $extruder_id = $i + 1;
            $config->set_ifndef('extruder', $extruder_id);
        }
    }
}

1;
