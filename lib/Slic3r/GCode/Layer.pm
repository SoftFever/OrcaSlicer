package Slic3r::GCode::Layer;
use Moo;

use List::Util qw(first);
use Slic3r::Geometry qw(X Y unscale);

has 'print'                         => (is => 'ro', required => 1);
has 'gcodegen'                      => (is => 'ro', required => 1, handles => [qw(extruders)]);
has 'shift'                         => (is => 'ro', default => sub { [0,0] });

has 'spiralvase'                    => (is => 'lazy');
has 'vibration_limit'               => (is => 'lazy');
has 'arc_fitting'                   => (is => 'lazy');
has 'skirt_done'                    => (is => 'rw', default => sub { {} });  # print_z => 1
has 'brim_done'                     => (is => 'rw');
has 'second_layer_things_done'      => (is => 'rw');
has '_last_obj_copy'                => (is => 'rw');

sub _build_spiralvase {
    my $self = shift;
    
    return $self->print->config->spiral_vase
        ? Slic3r::GCode::SpiralVase->new(config => $self->print->config)
        : undef;
}

sub _build_vibration_limit {
    my $self = shift;
    
    return $self->print->config->vibration_limit
        ? Slic3r::GCode::VibrationLimit->new(config => $self->print->config)
        : undef;
}

sub _build_arc_fitting {
    my $self = shift;
    
    return $self->print->config->gcode_arcs
        ? Slic3r::GCode::ArcFitting->new(config => $self->print->config)
        : undef;
}

sub process_layer {
    my $self = shift;
    my ($layer, $object_copies) = @_;
    my $gcode = "";
    
    my $object = $layer->object;
    $self->gcodegen->config->apply_object_config($object->config);
    
    # check whether we're going to apply spiralvase logic
    if (defined $self->spiralvase) {
        $self->spiralvase->enable(
            ($layer->id > 0 || $self->print->config->brim_width == 0)
                && ($layer->id >= $self->print->config->skirt_height && $self->print->config->skirt_height != -1)
                && !defined(first { $_->config->bottom_solid_layers > $layer->id } @{$layer->regions})
                && !defined(first { @{$_->perimeters} > 1 } @{$layer->regions})
                && !defined(first { @{$_->fills} > 0 } @{$layer->regions})
        );
    }
    
    # if we're going to apply spiralvase to this layer, disable loop clipping
    $self->gcodegen->enable_loop_clipping(!defined $self->spiralvase || !$self->spiralvase->enable);
    
    if (!$self->second_layer_things_done && $layer->id == 1) {
        for my $extruder_id (sort keys %{$self->extruders}) {
            my $extruder = $self->extruders->{$extruder_id};
            $gcode .= $self->gcodegen->set_temperature($extruder->temperature, 0, $extruder->id)
                if $extruder->temperature && $extruder->temperature != $extruder->first_layer_temperature;
        }
        $gcode .= $self->gcodegen->set_bed_temperature($self->print->config->bed_temperature)
            if $self->print->config->bed_temperature && $self->print->config->bed_temperature != $self->print->config->first_layer_bed_temperature;
        $self->second_layer_things_done(1);
    }
    
    # set new layer - this will change Z and force a retraction if retract_layer_change is enabled
    $gcode .= $self->gcodegen->change_layer($layer);
    $gcode .= $self->gcodegen->placeholder_parser->process($self->print->config->layer_gcode, {
        layer_num => $self->gcodegen->layer->id,
    }) . "\n" if $self->print->config->layer_gcode;
    
    # extrude skirt
    if (((values %{$self->skirt_done}) < $self->print->config->skirt_height || $self->print->config->skirt_height == -1)
        && !$self->skirt_done->{$layer->print_z}) {
        $self->gcodegen->set_shift(@{$self->shift});
        my @extruder_ids = sort keys %{$self->extruders};
        $gcode .= $self->gcodegen->set_extruder($extruder_ids[0]);
        # skip skirt if we have a large brim
        if ($layer->id < $self->print->config->skirt_height || $self->print->config->skirt_height == -1) {
            # distribute skirt loops across all extruders
            my @skirt_loops = @{$self->print->skirt};
            for my $i (0 .. $#skirt_loops) {
                # when printing layers > 0 ignore 'min_skirt_length' and 
                # just use the 'skirts' setting; also just use the current extruder
                last if ($layer->id > 0) && ($i >= $self->print->config->skirts);
                my $extruder_id = $extruder_ids[($i/@extruder_ids) % @extruder_ids];
                $gcode .= $self->gcodegen->set_extruder($extruder_id)
                    if $layer->id == 0;
                $gcode .= $self->gcodegen->extrude_loop($skirt_loops[$i], 'skirt', $object->config->support_material_speed);
            }
        }
        $self->skirt_done->{$layer->print_z} = 1;
        $self->gcodegen->straight_once(1);
    }
    
    # extrude brim
    if (!$self->brim_done) {
        $gcode .= $self->gcodegen->set_extruder($self->print->objects->[0]->config->support_material_extruder-1);
        $self->gcodegen->set_shift(@{$self->shift});
        $gcode .= $self->gcodegen->extrude_loop($_, 'brim', $object->config->support_material_speed)
            for @{$self->print->brim};
        $self->brim_done(1);
        $self->gcodegen->straight_once(1);
    }
    
    for my $copy (@$object_copies) {
        $self->gcodegen->new_object(1) if ($self->_last_obj_copy // '') ne "$copy";
        $self->_last_obj_copy("$copy");
        
        $self->gcodegen->set_shift(map $self->shift->[$_] + unscale $copy->[$_], X,Y);
        
        # extrude support material before other things because it might use a lower Z
        # and also because we avoid travelling on other things when printing it
        if ($layer->isa('Slic3r::Layer::Support')) {
            if ($layer->support_interface_fills->count > 0) {
                $gcode .= $self->gcodegen->set_extruder($object->config->support_material_interface_extruder-1);
                $gcode .= $self->gcodegen->extrude_path($_, 'support material interface', $object->config->get_abs_value('support_material_interface_speed')) 
                    for @{$layer->support_interface_fills->chained_path_from($self->gcodegen->last_pos, 0)}; 
            }
            if ($layer->support_fills->count > 0) {
                $gcode .= $self->gcodegen->set_extruder($object->config->support_material_extruder-1);
                $gcode .= $self->gcodegen->extrude_path($_, 'support material', $object->config->get_abs_value('support_material_speed')) 
                    for @{$layer->support_fills->chained_path_from($self->gcodegen->last_pos, 0)};
            }
        }
        
        # tweak region ordering to save toolchanges
        my @region_ids = 0 .. ($self->print->regions_count-1);
        if ($self->gcodegen->multiple_extruders) {
            my $last_extruder = $self->gcodegen->extruder;
            my $best_region_id = first { $self->print->regions->[$_]->config->perimeter_extruder-1 eq $last_extruder } @region_ids;
            @region_ids = ($best_region_id, grep $_ != $best_region_id, @region_ids) if $best_region_id;
        }
        
        foreach my $region_id (@region_ids) {
            my $layerm = $layer->regions->[$region_id] or next;
            my $region = $self->print->regions->[$region_id];
            $self->gcodegen->config->apply_region_config($region->config);
            
            # group extrusions by island
            my @perimeters_by_island = map [], 0..$#{$layer->slices};   # slice idx => @perimeters
            my @infill_by_island     = map [], 0..$#{$layer->slices};   # slice idx => @fills
            
            # NOTE: we assume $layer->slices was already ordered with chained_path()!
            
            PERIMETER: foreach my $perimeter (@{$layerm->perimeters}) {
                for my $i (0 .. $#{$layer->slices}-1) {
                    if ($layer->slices->[$i]->contour->contains_point($perimeter->first_point)) {
                        push @{ $perimeters_by_island[$i] }, $perimeter;
                        next PERIMETER;
                    }
                }
                push @{ $perimeters_by_island[-1] }, $perimeter; # optimization
            }
            FILL: foreach my $fill (@{$layerm->fills}) {
                for my $i (0 .. $#{$layer->slices}-1) {
                    if ($layer->slices->[$i]->contour->contains_point($fill->first_point)) {
                        push @{ $infill_by_island[$i] }, $fill;
                        next FILL;
                    }
                }
                push @{ $infill_by_island[-1] }, $fill; # optimization
            }
            
            for my $i (0 .. $#{$layer->slices}) {
                # give priority to infill if we were already using its extruder and it wouldn't
                # be good for perimeters
                if ($self->print->config->infill_first
                    || ($self->gcodegen->multiple_extruders && $region->config->infill_extruder-1 == $self->gcodegen->extruder->id && $region->config->infill_extruder != $region->config->perimeter_extruder)) {
                    $gcode .= $self->_extrude_infill($infill_by_island[$i], $region);
                    $gcode .= $self->_extrude_perimeters($perimeters_by_island[$i], $region);
                } else {
                    $gcode .= $self->_extrude_perimeters($perimeters_by_island[$i], $region);
                    $gcode .= $self->_extrude_infill($infill_by_island[$i], $region);
                }
            }
        }
    }
    
    # apply spiral vase post-processing if this layer contains suitable geometry
    # (we must feed all the G-code into the post-processor, including the first 
    # bottom non-spiral layers otherwise it will mess with positions)
    $gcode = $self->spiralvase->process_layer($gcode)
        if defined $self->spiralvase;
    
    # apply vibration limit if enabled
    $gcode = $self->vibration_limit->process($gcode)
        if $self->print->config->vibration_limit != 0;
    
    # apply arc fitting if enabled
    $gcode = $self->arc_fitting->process($gcode)
        if $self->print->config->gcode_arcs;
    
    return $gcode;
}

sub _extrude_perimeters {
    my $self = shift;
    my ($island_perimeters, $region) = @_;
    
    return "" if !@$island_perimeters;
    
    my $gcode = "";
    $gcode .= $self->gcodegen->set_extruder($region->config->perimeter_extruder-1);
    $gcode .= $self->gcodegen->extrude($_, 'perimeter') for @$island_perimeters;
    return $gcode;
}

sub _extrude_infill {
    my $self = shift;
    my ($island_fills, $region) = @_;
    
    return "" if !@$island_fills;
    
    my $gcode = "";
    $gcode .= $self->gcodegen->set_extruder($region->config->infill_extruder-1);
    for my $fill (@$island_fills) {
        if ($fill->isa('Slic3r::ExtrusionPath::Collection')) {
            $gcode .= $self->gcodegen->extrude($_, 'fill') 
                for @{$fill->chained_path_from($self->gcodegen->last_pos, 0)};
        } else {
            $gcode .= $self->gcodegen->extrude($fill, 'fill') ;
        }
    }
    return $gcode;
}

1;
