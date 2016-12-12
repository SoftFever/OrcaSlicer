package Slic3r::Print::Object;
# extends c++ class Slic3r::PrintObject (Print.xsp)
use strict;
use warnings;

use List::Util qw(min max sum first);
use Slic3r::Flow ':roles';
use Slic3r::Geometry qw(X Y Z PI scale unscale chained_path epsilon);
use Slic3r::Geometry::Clipper qw(diff diff_ex intersection intersection_ex union union_ex 
    offset offset_ex offset2 offset2_ex intersection_ppl JT_MITER);
use Slic3r::Print::State ':steps';
use Slic3r::Surface ':types';

# If enabled, phases of prepare_infill will be written into SVG files to an "out" directory.
our $SLIC3R_DEBUG_SLICE_PROCESSING = 0;

sub region_volumes {
    my $self = shift;
    return [ map $self->get_region_volumes($_), 0..($self->region_count - 1) ];
}

sub layers {
    my $self = shift;
    return [ map $self->get_layer($_), 0..($self->layer_count - 1) ];
}

sub support_layers {
    my $self = shift;
    return [ map $self->get_support_layer($_), 0..($self->support_layer_count - 1) ];
}

# 1) Decides Z positions of the layers,
# 2) Initializes layers and their regions
# 3) Slices the object meshes
# 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
# 5) Applies size compensation (offsets the slices in XY plane)
# 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
# Resulting expolygons of layer regions are marked as Internal.
#
# this should be idempotent
sub slice {
    my $self = shift;
    
    return if $self->step_done(STEP_SLICE);
    $self->set_step_started(STEP_SLICE);
    $self->print->status_cb->(10, "Processing triangulated mesh");
    
    $self->_slice;
    
    # detect slicing errors
    my $warning_thrown = 0;
    for my $i (0 .. ($self->layer_count - 1)) {
        my $layer = $self->get_layer($i);
        next unless $layer->slicing_errors;
        if (!$warning_thrown) {
            warn "The model has overlapping or self-intersecting facets. I tried to repair it, "
                . "however you might want to check the results or repair the input file and retry.\n";
            $warning_thrown = 1;
        }
        
        # try to repair the layer surfaces by merging all contours and all holes from
        # neighbor layers
        Slic3r::debugf "Attempting to repair layer %d\n", $i;
        
        foreach my $region_id (0 .. ($layer->region_count - 1)) {
            my $layerm = $layer->region($region_id);
            
            my (@upper_surfaces, @lower_surfaces);
            for (my $j = $i+1; $j < $self->layer_count; $j++) {
                if (!$self->get_layer($j)->slicing_errors) {
                    @upper_surfaces = @{$self->get_layer($j)->region($region_id)->slices};
                    last;
                }
            }
            for (my $j = $i-1; $j >= 0; $j--) {
                if (!$self->get_layer($j)->slicing_errors) {
                    @lower_surfaces = @{$self->get_layer($j)->region($region_id)->slices};
                    last;
                }
            }
            
            my $union = union_ex([
                map $_->expolygon->contour, @upper_surfaces, @lower_surfaces,
            ]);
            my $diff = diff_ex(
                [ map @$_, @$union ],
                [ map @{$_->expolygon->holes}, @upper_surfaces, @lower_surfaces, ],
            );
            
            $layerm->slices->clear;
            $layerm->slices->append($_)
                for map Slic3r::Surface->new
                    (expolygon => $_, surface_type => S_TYPE_INTERNAL),
                    @$diff;
        }
            
        # update layer slices after repairing the single regions
        $layer->make_slices;
    }
    
    # remove empty layers from bottom
    while (@{$self->layers} && !@{$self->get_layer(0)->slices}) {
        $self->delete_layer(0);
        for (my $i = 0; $i <= $#{$self->layers}; $i++) {
            $self->get_layer($i)->set_id( $self->get_layer($i)->id-1 );
        }
    }
    
    # simplify slices if required
    if ($self->print->config->resolution) {
        $self->_simplify_slices(scale($self->print->config->resolution));
    }
    
    die "No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n"
        if !@{$self->layers};
    
    $self->set_typed_slices(0);
    $self->set_step_done(STEP_SLICE);
}

# 1) Merges typed region slices into stInternal type.
# 2) Increases an "extra perimeters" counter at region slices where needed.
# 3) Generates perimeters, gap fills and fill regions (fill regions of type stInternal).
sub make_perimeters {
    my ($self) = @_;
    
    # prerequisites
    $self->slice;
    $self->_make_perimeters;
}

sub prepare_infill {
    my ($self) = @_;
    
    # prerequisites
    $self->make_perimeters;
    
    return if $self->step_done(STEP_PREPARE_INFILL);
    $self->set_step_started(STEP_PREPARE_INFILL);
    $self->print->status_cb->(30, "Preparing infill");
    
    # This will assign a type (top/bottom/internal) to $layerm->slices.
    # Then the classifcation of $layerm->slices is transfered onto 
    # the $layerm->fill_surfaces by clipping $layerm->fill_surfaces
    # by the cummulative area of the previous $layerm->fill_surfaces.
    $self->detect_surfaces_type;
    # Mark the object to have the region slices classified (typed, which also means they are split based on whether they are supported, bridging, top layers etc.)
    $self->set_typed_slices(1);
    
    # Decide what surfaces are to be filled.
    # Here the S_TYPE_TOP / S_TYPE_BOTTOMBRIDGE / S_TYPE_BOTTOM infill is turned to just S_TYPE_INTERNAL if zero top / bottom infill layers are configured.
    # Also tiny S_TYPE_INTERNAL surfaces are turned to S_TYPE_INTERNAL_SOLID.
    $_->prepare_fill_surfaces for map @{$_->regions}, @{$self->layers};

    # this will detect bridges and reverse bridges
    # and rearrange top/bottom/internal surfaces
    # It produces enlarged overlapping bridging areas.
    #
    # 1) S_TYPE_BOTTOMBRIDGE / S_TYPE_BOTTOM infill is grown by 3mm and clipped by the total infill area. Bridges are detected. The areas may overlap.
    # 2) S_TYPE_TOP is grown by 3mm and clipped by the grown bottom areas. The areas may overlap.
    # 3) Clip the internal surfaces by the grown top/bottom surfaces.
    # 4) Merge surfaces with the same style. This will mostly get rid of the overlaps.
    #FIXME This does not likely merge surfaces, which are supported by a material with different colors, but same properties.
    $self->process_external_surfaces;

    # Add solid fills to ensure the shell vertical thickness.
    $self->discover_vertical_shells;

    # Debugging output.
    if ($SLIC3R_DEBUG_SLICE_PROCESSING) {
        for my $region_id (0 .. ($self->print->region_count-1)) {
            for (my $i = 0; $i < $self->layer_count; $i++) {
                my $layerm = $self->get_layer($i)->regions->[$region_id];
                $layerm->export_region_slices_to_svg_debug("6_discover_vertical_shells-final");
                $layerm->export_region_fill_surfaces_to_svg_debug("6_discover_vertical_shells-final");
            } # for each layer
        } # for each region
    }

    # Detect, which fill surfaces are near external layers.
    # They will be split in internal and internal-solid surfaces.
    # The purpose is to add a configurable number of solid layers to support the TOP surfaces
    # and to add a configurable number of solid layers above the BOTTOM / BOTTOMBRIDGE surfaces
    # to close these surfaces reliably.
    #FIXME Vojtech: Is this a good place to add supporting infills below sloping perimeters?
    $self->discover_horizontal_shells;

    if ($SLIC3R_DEBUG_SLICE_PROCESSING) {
        # Debugging output.
        for my $region_id (0 .. ($self->print->region_count-1)) {
            for (my $i = 0; $i < $self->layer_count; $i++) {
                my $layerm = $self->get_layer($i)->regions->[$region_id];
                $layerm->export_region_slices_to_svg_debug("7_discover_horizontal_shells-final");
                $layerm->export_region_fill_surfaces_to_svg_debug("7_discover_horizontal_shells-final");
            } # for each layer
        } # for each region
    }

    # Only active if config->infill_only_where_needed. This step trims the sparse infill,
    # so it acts as an internal support. It maintains all other infill types intact.
    # Here the internal surfaces and perimeters have to be supported by the sparse infill.
    #FIXME The surfaces are supported by a sparse infill, but the sparse infill is only as large as the area to support.
    # Likely the sparse infill will not be anchored correctly, so it will not work as intended.
    # Also one wishes the perimeters to be supported by a full infill.
    $self->clip_fill_surfaces;

    if ($SLIC3R_DEBUG_SLICE_PROCESSING) {
        # Debugging output.
        for my $region_id (0 .. ($self->print->region_count-1)) {
            for (my $i = 0; $i < $self->layer_count; $i++) {
                my $layerm = $self->get_layer($i)->regions->[$region_id];
                $layerm->export_region_slices_to_svg_debug("8_clip_surfaces-final");
                $layerm->export_region_fill_surfaces_to_svg_debug("8_clip_surfaces-final");
            } # for each layer
        } # for each region
    }
    
    # the following step needs to be done before combination because it may need
    # to remove only half of the combined infill
    $self->bridge_over_infill;

    # combine fill surfaces to honor the "infill every N layers" option
    $self->combine_infill;
    
    # Debugging output.
    if ($SLIC3R_DEBUG_SLICE_PROCESSING) {
        for my $region_id (0 .. ($self->print->region_count-1)) {
            for (my $i = 0; $i < $self->layer_count; $i++) {
                my $layerm = $self->get_layer($i)->regions->[$region_id];
                $layerm->export_region_slices_to_svg_debug("9_prepare_infill-final");
                $layerm->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
            } # for each layer
        } # for each region
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layer = $self->get_layer($i);
            $layer->export_region_slices_to_svg_debug("9_prepare_infill-final");
            $layer->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
        } # for each layer
    }

    $self->set_step_done(STEP_PREPARE_INFILL);
}

sub infill {
    my ($self) = @_;
    
    # prerequisites
    $self->prepare_infill;
    $self->_infill;
}

sub generate_support_material {
    my $self = shift;
    
    # prerequisites
    $self->slice;
    
    return if $self->step_done(STEP_SUPPORTMATERIAL);
    $self->set_step_started(STEP_SUPPORTMATERIAL);
    
    $self->clear_support_layers;
    
    if ((!$self->config->support_material && $self->config->raft_layers == 0) || scalar(@{$self->layers}) < 2) {
        $self->set_step_done(STEP_SUPPORTMATERIAL);
        return;
    }
    $self->print->status_cb->(85, "Generating support material");
    
    $self->_support_material->generate($self);
    
    $self->set_step_done(STEP_SUPPORTMATERIAL);
}

sub _support_material {
    my ($self) = @_;
    
    my $first_layer_flow = Slic3r::Flow->new_from_width(
        width               => ($self->print->config->first_layer_extrusion_width || $self->config->support_material_extrusion_width),
        role                => FLOW_ROLE_SUPPORT_MATERIAL,
        nozzle_diameter     => $self->print->config->nozzle_diameter->[ $self->config->support_material_extruder-1 ]
                                // $self->print->config->nozzle_diameter->[0],
        layer_height        => $self->config->get_abs_value('first_layer_height'),
        bridge_flow_ratio   => 0,
    );
    
    if (1) {
        # Old supports, Perl implementation.
        return Slic3r::Print::SupportMaterial->new(
            print_config        => $self->print->config,
            object_config       => $self->config,
            first_layer_flow    => $first_layer_flow,
            flow                => $self->support_material_flow,
            interface_flow      => $self->support_material_flow(FLOW_ROLE_SUPPORT_MATERIAL_INTERFACE),
        );
    } else {
        # New supports, C++ implementation.
        return Slic3r::Print::SupportMaterial2->new($self);
    }
}

# Idempotence of this method is guaranteed by the fact that we don't remove things from
# fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
sub clip_fill_surfaces {
    my $self = shift;
    return unless $self->config->infill_only_where_needed;
    
    # We only want infill under ceilings; this is almost like an
    # internal support material.
    
    # proceed top-down skipping bottom layer
    my $upper_internal = [];
    for my $layer_id (reverse 1..($self->layer_count - 1)) {
        my $layer       = $self->get_layer($layer_id);
        my $lower_layer = $self->get_layer($layer_id-1);
        
        # detect things that we need to support
        my $overhangs = [];  # Polygons
        
        # we need to support any solid surface
        push @$overhangs, map $_->p,
            grep $_->is_solid, map @{$_->fill_surfaces}, @{$layer->regions};
        
        # we also need to support perimeters when there's at least one full
        # unsupported loop
        {
            # get perimeters area as the difference between slices and fill_surfaces
            my $perimeters = diff(
                [ map @$_, @{$layer->slices} ],
                [ map $_->p, map @{$_->fill_surfaces}, @{$layer->regions} ],
            );
            
            # only consider the area that is not supported by lower perimeters
            $perimeters = intersection(
                $perimeters,
                [ map $_->p, map @{$_->fill_surfaces}, @{$lower_layer->regions} ],
                1,
            );
            
            # only consider perimeter areas that are at least one extrusion width thick
            #FIXME Offset2 eats out from both sides, while the perimeters are create outside in.
            #Should the $pw not be half of the current value?
            my $pw = min(map $_->flow(FLOW_ROLE_PERIMETER)->scaled_width, @{$layer->regions});
            $perimeters = offset2($perimeters, -$pw, +$pw);
            
            # append such thick perimeters to the areas that need support
            push @$overhangs, @$perimeters;
        }
        
        # find new internal infill
        $upper_internal = my $new_internal = intersection(
            [
                @$overhangs,
                @$upper_internal,
            ],
            [
                # our current internal fill boundaries
                map $_->p,
                    grep $_->surface_type == S_TYPE_INTERNAL || $_->surface_type == S_TYPE_INTERNALVOID,
                        map @{$_->fill_surfaces}, @{$lower_layer->regions}
            ],
        );
        
        # apply new internal infill to regions
        foreach my $layerm (@{$lower_layer->regions}) {
            my (@internal, @other) = ();
            foreach my $surface (map $_->clone, @{$layerm->fill_surfaces}) {
                if ($surface->surface_type == S_TYPE_INTERNAL || $surface->surface_type == S_TYPE_INTERNALVOID) {
                    push @internal, $surface;
                } else {
                    push @other, $surface;
                }
            }
            
            my @new = map Slic3r::Surface->new(
                expolygon       => $_,
                surface_type    => S_TYPE_INTERNAL,
            ),
                @{intersection_ex(
                    [ map $_->p, @internal ],
                    $new_internal,
                    1,
                )};
            
            push @other, map Slic3r::Surface->new(
                expolygon       => $_,
                surface_type    => S_TYPE_INTERNALVOID,
            ),
                @{diff_ex(
                    [ map $_->p, @internal ],
                    $new_internal,
                    1,
                )};
            
            # If there are voids it means that our internal infill is not adjacent to
            # perimeters. In this case it would be nice to add a loop around infill to
            # make it more robust and nicer. TODO.
            
            $layerm->fill_surfaces->clear;
            $layerm->fill_surfaces->append($_) for (@new, @other);

            if ($SLIC3R_DEBUG_SLICE_PROCESSING) {
                $layerm->export_region_fill_surfaces_to_svg_debug("6_clip_fill_surfaces");
            }
        }
    }
}

sub discover_horizontal_shells {
    my $self = shift;
    
    Slic3r::debugf "==> DISCOVERING HORIZONTAL SHELLS\n";
    
    for my $region_id (0 .. ($self->print->region_count-1)) {
        for (my $i = 0; $i < $self->layer_count; $i++) {
            my $layerm = $self->get_layer($i)->regions->[$region_id];
            
            if ($layerm->region->config->solid_infill_every_layers && $layerm->region->config->fill_density > 0
                && ($i % $layerm->region->config->solid_infill_every_layers) == 0) {
                # This is the layer to put the sparse infill in. Mark S_TYPE_INTERNAL surfaces as S_TYPE_INTERNALSOLID or S_TYPE_INTERNALBRIDGE.
                # If the sparse infill is not active, the internal surfaces are of type S_TYPE_INTERNAL.
                my $type = $layerm->region->config->fill_density == 100 ? S_TYPE_INTERNALSOLID : S_TYPE_INTERNALBRIDGE;
                $_->surface_type($type) for @{$layerm->fill_surfaces->filter_by_type(S_TYPE_INTERNAL)};
            }

            # If ensure_vertical_shell_thickness, then the rest has already been performed by discover_vertical_shells().
            next if ($layerm->region->config->ensure_vertical_shell_thickness);
            
            EXTERNAL: foreach my $type (S_TYPE_TOP, S_TYPE_BOTTOM, S_TYPE_BOTTOMBRIDGE) {
                # find slices of current type for current layer
                # use slices instead of fill_surfaces because they also include the perimeter area
                # which needs to be propagated in shells; we need to grow slices like we did for
                # fill_surfaces though.  Using both ungrown slices and grown fill_surfaces will
                # not work in some situations, as there won't be any grown region in the perimeter 
                # area (this was seen in a model where the top layer had one extra perimeter, thus
                # its fill_surfaces were thinner than the lower layer's infill), however it's the best
                # solution so far. Growing the external slices by EXTERNAL_INFILL_MARGIN will put
                # too much solid infill inside nearly-vertical slopes.
                my $solid = [
                    # Surfaces including the area of perimeters. Everything, that is visible from the top / bottom
                    # (not covered by a layer above / below).
                    # This does not contain the areas covered by perimeters!
                    (map $_->p, @{$layerm->slices->filter_by_type($type)}),
                    # Infill areas (slices without the perimeters).
                    (map $_->p, @{$layerm->fill_surfaces->filter_by_type($type)}),
                ];
                next if !@$solid;
                Slic3r::debugf "Layer %d has %s surfaces\n", $i, ($type == S_TYPE_TOP) ? 'top' : 'bottom';
                
                my $solid_layers = ($type == S_TYPE_TOP)
                    ? $layerm->region->config->top_solid_layers
                    : $layerm->region->config->bottom_solid_layers;
                NEIGHBOR: for (my $n = ($type == S_TYPE_TOP) ? $i-1 : $i+1; 
                                abs($n - $i) < $solid_layers;
                                ($type == S_TYPE_TOP) ? $n-- : $n++) {
                    
                    next if $n < 0 || $n >= $self->layer_count;
                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;
                    
                    # Reference to the lower layer of a TOP surface, or an upper layer of a BOTTOM surface.
                    my $neighbor_layerm = $self->get_layer($n)->regions->[$region_id];
                    # Reference to the neighbour fill surfaces.
                    my $neighbor_fill_surfaces = $neighbor_layerm->fill_surfaces;
                    # Clone because we will use these surfaces even after clearing the collection.
                    my @neighbor_fill_surfaces = map $_->clone, @$neighbor_fill_surfaces;
                    
                    # find intersection between neighbor and current layer's surfaces
                    # intersections have contours and holes
                    # we update $solid so that we limit the next neighbor layer to the areas that were
                    # found on this one - in other words, solid shells on one layer (for a given external surface)
                    # are always a subset of the shells found on the previous shell layer
                    # this approach allows for DWIM in hollow sloping vases, where we want bottom
                    # shells to be generated in the base but not in the walls (where there are many
                    # narrow bottom surfaces): reassigning $solid will consider the 'shadow' of the 
                    # upper perimeter as an obstacle and shell will not be propagated to more upper layers
                    #FIXME How does it work for S_TYPE_INTERNALBRIDGE? This is set for sparse infill. Likely this does not work.
                    my $new_internal_solid = $solid = intersection(
                        $solid,
                        [ map $_->p, grep { ($_->surface_type == S_TYPE_INTERNAL) || ($_->surface_type == S_TYPE_INTERNALSOLID) } @neighbor_fill_surfaces ],
                        1,
                    );
                    next EXTERNAL if !@$new_internal_solid;
                    
                    if ($layerm->region->config->fill_density == 0) {
                        # if we're printing a hollow object we discard any solid shell thinner
                        # than a perimeter width, since it's probably just crossing a sloping wall
                        # and it's not wanted in a hollow print even if it would make sense when
                        # obeying the solid shell count option strictly (DWIM!)
                        my $margin = $neighbor_layerm->flow(FLOW_ROLE_EXTERNAL_PERIMETER)->scaled_width;
                        my $regularized = offset2($new_internal_solid, -$margin, +$margin, JT_MITER, 5);
                        my $too_narrow = diff(
                            $new_internal_solid,
                            $regularized,
                            1,
                        );
                        # Trim the regularized region by the original region.
                        $new_internal_solid = $solid = intersection(
                            $new_internal_solid,
                            $regularized,
                        ) if @$too_narrow;
                    }
                    
                    # make sure the new internal solid is wide enough, as it might get collapsed
                    # when spacing is added in Fill.pm
                    if ($layerm->region->config->ensure_vertical_shell_thickness) {
                        # The possible thin sickles of top / bottom surfaces on steeply sloping surfaces touch
                        # the projections of top / bottom perimeters, therefore they will be sufficiently inflated by
                        # merging them with the projections of the top / bottom perimeters.
                    } else {
                        #FIXME Vojtech: Disable this and you will be sorry.
                        # https://github.com/prusa3d/Slic3r/issues/26 bottom
                        my $margin = 3 * $layerm->flow(FLOW_ROLE_SOLID_INFILL)->scaled_width; # require at least this size
                        # we use a higher miterLimit here to handle areas with acute angles
                        # in those cases, the default miterLimit would cut the corner and we'd
                        # get a triangle in $too_narrow; if we grow it below then the shell
                        # would have a different shape from the external surface and we'd still
                        # have the same angle, so the next shell would be grown even more and so on.
                        my $too_narrow = diff(
                            $new_internal_solid,
                            offset2($new_internal_solid, -$margin, +$margin, JT_MITER, 5),
                            1,
                        );
                        
                        if (@$too_narrow) {
                            # grow the collapsing parts and add the extra area to  the neighbor layer 
                            # as well as to our original surfaces so that we support this 
                            # additional area in the next shell too
                        
                            # make sure our grown surfaces don't exceed the fill area
                            my @grown = @{intersection(
                                offset($too_narrow, +$margin),
                                # Discard bridges as they are grown for anchoring and we can't
                                # remove such anchors. (This may happen when a bridge is being 
                                # anchored onto a wall where little space remains after the bridge
                                # is grown, and that little space is an internal solid shell so 
                                # it triggers this too_narrow logic.)
                                [ map $_->p, grep { $_->is_internal && !$_->is_bridge } @neighbor_fill_surfaces ],
                            )};
                            $new_internal_solid = $solid = [ @grown, @$new_internal_solid ];
                        }
                    }
                    
                    # internal-solid are the union of the existing internal-solid surfaces
                    # and new ones
                    my $internal_solid = union_ex([
                        ( map $_->p, grep $_->surface_type == S_TYPE_INTERNALSOLID, @neighbor_fill_surfaces ),
                        @$new_internal_solid,
                    ]);
                    
                    # subtract intersections from layer surfaces to get resulting internal surfaces
                    my $internal = diff_ex(
                        [ map $_->p, grep $_->surface_type == S_TYPE_INTERNAL, @neighbor_fill_surfaces ],
                        [ map @$_, @$internal_solid ],
                        1,
                    );
                    Slic3r::debugf "    %d internal-solid and %d internal surfaces found\n",
                        scalar(@$internal_solid), scalar(@$internal);
                    
                    # assign resulting internal surfaces to layer
                    $neighbor_fill_surfaces->clear;
                    $neighbor_fill_surfaces->append($_)
                        for map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNAL),
                            @$internal;
                    
                    # assign new internal-solid surfaces to layer
                    $neighbor_fill_surfaces->append($_)
                        for map Slic3r::Surface->new(expolygon => $_, surface_type => S_TYPE_INTERNALSOLID),
                        @$internal_solid;
                    
                    # assign top and bottom surfaces to layer
                    foreach my $s (@{Slic3r::Surface::Collection->new(grep { ($_->surface_type == S_TYPE_TOP) || $_->is_bottom } @neighbor_fill_surfaces)->group}) {
                        my $solid_surfaces = diff_ex(
                            [ map $_->p, @$s ],
                            [ map @$_, @$internal_solid, @$internal ],
                            1,
                        );
                        $neighbor_fill_surfaces->append($_)
                            for map $s->[0]->clone(expolygon => $_), @$solid_surfaces;
                    }
                }
            } # foreach my $type (S_TYPE_TOP, S_TYPE_BOTTOM, S_TYPE_BOTTOMBRIDGE)
        } # for each layer
    } # for each region

    # Debugging output.
    if ($SLIC3R_DEBUG_SLICE_PROCESSING) {
        for my $region_id (0 .. ($self->print->region_count-1)) {
            for (my $i = 0; $i < $self->layer_count; $i++) {
                my $layerm = $self->get_layer($i)->regions->[$region_id];
                $layerm->export_region_slices_to_svg_debug("5_discover_horizontal_shells");
                $layerm->export_region_fill_surfaces_to_svg_debug("5_discover_horizontal_shells");
            } # for each layer
        } # for each region
    }
}

# combine fill surfaces across layers to honor the "infill every N layers" option
# Idempotence of this method is guaranteed by the fact that we don't remove things from
# fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
sub combine_infill {
    my $self = shift;
    
    # define the type used for voids
    my %voidtype = (
        &S_TYPE_INTERNAL() => S_TYPE_INTERNALVOID,
    );
    
    # work on each region separately
    for my $region_id (0 .. ($self->print->region_count-1)) {
        my $region = $self->print->get_region($region_id);
        my $every = $region->config->infill_every_layers;
        next unless $every > 1 && $region->config->fill_density > 0;
        
        # limit the number of combined layers to the maximum height allowed by this regions' nozzle
        my $nozzle_diameter = min(
            $self->print->config->get_at('nozzle_diameter', $region->config->infill_extruder-1),
            $self->print->config->get_at('nozzle_diameter', $region->config->solid_infill_extruder-1),
        );
        
        # define the combinations
        my %combine = ();   # layer_idx => number of additional combined lower layers
        {
            my $current_height = my $layers = 0;
            for my $layer_idx (0 .. ($self->layer_count-1)) {
                my $layer = $self->get_layer($layer_idx);
                next if $layer->id == 0;  # skip first print layer (which may not be first layer in array because of raft)
                my $height = $layer->height;
                
                # check whether the combination of this layer with the lower layers' buffer
                # would exceed max layer height or max combined layer count
                if ($current_height + $height >= $nozzle_diameter + epsilon || $layers >= $every) {
                    # append combination to lower layer
                    $combine{$layer_idx-1} = $layers;
                    $current_height = $layers = 0;
                }
                
                $current_height += $height;
                $layers++;
            }
            
            # append lower layers (if any) to uppermost layer
            $combine{$self->layer_count-1} = $layers;
        }
        
        # loop through layers to which we have assigned layers to combine
        for my $layer_idx (sort keys %combine) {
            next unless $combine{$layer_idx} > 1;
            
            # get all the LayerRegion objects to be combined
            my @layerms = map $self->get_layer($_)->get_region($region_id),
                ($layer_idx - ($combine{$layer_idx}-1) .. $layer_idx);
            
            # only combine internal infill
            for my $type (S_TYPE_INTERNAL) {
                # we need to perform a multi-layer intersection, so let's split it in pairs
                
                # initialize the intersection with the candidates of the lowest layer
                my $intersection = [ map $_->expolygon, @{$layerms[0]->fill_surfaces->filter_by_type($type)} ];
                
                # start looping from the second layer and intersect the current intersection with it
                for my $layerm (@layerms[1 .. $#layerms]) {
                    $intersection = intersection_ex(
                        [ map @$_, @$intersection ],
                        [ map @{$_->expolygon}, @{$layerm->fill_surfaces->filter_by_type($type)} ],
                    );
                }
                
                my $area_threshold = $layerms[0]->infill_area_threshold;
                @$intersection = grep $_->area > $area_threshold, @$intersection;
                next if !@$intersection;
                Slic3r::debugf "  combining %d %s regions from layers %d-%d\n",
                    scalar(@$intersection),
                    ($type == S_TYPE_INTERNAL ? 'internal' : 'internal-solid'),
                    $layer_idx-($every-1), $layer_idx;
                
                # $intersection now contains the regions that can be combined across the full amount of layers
                # so let's remove those areas from all layers
                
                 my @intersection_with_clearance = map @{$_->offset(
                       $layerms[-1]->flow(FLOW_ROLE_SOLID_INFILL)->scaled_width    / 2
                     + $layerms[-1]->flow(FLOW_ROLE_PERIMETER)->scaled_width / 2
                     # Because fill areas for rectilinear and honeycomb are grown 
                     # later to overlap perimeters, we need to counteract that too.
                     + (($type == S_TYPE_INTERNALSOLID || $region->config->fill_pattern =~ /(rectilinear|grid|line|honeycomb)/)
                       ? $layerms[-1]->flow(FLOW_ROLE_SOLID_INFILL)->scaled_width
                       : 0)
                     )}, @$intersection;

                
                foreach my $layerm (@layerms) {
                    my @this_type   = @{$layerm->fill_surfaces->filter_by_type($type)};
                    my @other_types = map $_->clone, grep $_->surface_type != $type, @{$layerm->fill_surfaces};
                    
                    my @new_this_type = map Slic3r::Surface->new(expolygon => $_, surface_type => $type),
                        @{diff_ex(
                            [ map $_->p, @this_type ],
                            [ @intersection_with_clearance ],
                        )};
                    
                    # apply surfaces back with adjusted depth to the uppermost layer
                    if ($layerm->layer->id == $self->get_layer($layer_idx)->id) {
                        push @new_this_type,
                            map Slic3r::Surface->new(
                                expolygon        => $_,
                                surface_type     => $type,
                                thickness        => sum(map $_->layer->height, @layerms),
                                thickness_layers => scalar(@layerms),
                            ),
                            @$intersection;
                    } else {
                        # save void surfaces
                        push @new_this_type,
                            map Slic3r::Surface->new(expolygon => $_, surface_type => $voidtype{$type}),
                            @{intersection_ex(
                                [ map @{$_->expolygon}, @this_type ],
                                [ @intersection_with_clearance ],
                            )};
                    }
                    
                    $layerm->fill_surfaces->clear;
                    $layerm->fill_surfaces->append($_) for (@new_this_type, @other_types);
                }
            }
        }
    }
}

# Simplify the sliced model, if "resolution" configuration parameter > 0.
# The simplification is problematic, because it simplifies the slices independent from each other,
# which makes the simplified discretization visible on the object surface.
sub _simplify_slices {
    my ($self, $distance) = @_;
    
    foreach my $layer (@{$self->layers}) {
        $layer->slices->simplify($distance);
        $_->slices->simplify($distance) for @{$layer->regions};
    }
}

sub support_material_flow {
    my ($self, $role) = @_;
    
    $role //= FLOW_ROLE_SUPPORT_MATERIAL;
    my $extruder = ($role == FLOW_ROLE_SUPPORT_MATERIAL)
        ? $self->config->support_material_extruder
        : $self->config->support_material_interface_extruder;
    
    # we use a bogus layer_height because we use the same flow for all
    # support material layers
    return Slic3r::Flow->new_from_width(
        width               => $self->config->support_material_extrusion_width || $self->config->extrusion_width,
        role                => $role,
        nozzle_diameter     => $self->print->config->nozzle_diameter->[$extruder-1] // $self->print->config->nozzle_diameter->[0],
        layer_height        => $self->config->layer_height,
        bridge_flow_ratio   => 0,
    );
}

1;
