#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "SVG.hpp"

namespace Slic3r {

PrintObject::PrintObject(Print* print, ModelObject* model_object, const BoundingBoxf3 &modobj_bbox)
:   typed_slices(false),
    _print(print),
    _model_object(model_object)
{
    // Compute the translation to be applied to our meshes so that we work with smaller coordinates
    {
        // Translate meshes so that our toolpath generation algorithms work with smaller
        // XY coordinates; this translation is an optimization and not strictly required.
        // A cloned mesh will be aligned to 0 before slicing in _slice_region() since we
        // don't assume it's already aligned and we don't alter the original position in model.
        // We store the XY translation so that we can place copies correctly in the output G-code
        // (copies are expressed in G-code coordinates and this translation is not publicly exposed).
        this->_copies_shift = Point(
            scale_(modobj_bbox.min.x), scale_(modobj_bbox.min.y));

        // Scale the object size and store it
        Pointf3 size = modobj_bbox.size();
        this->size = Point3(scale_(size.x), scale_(size.y), scale_(size.z));
    }
    
    this->reload_model_instances();
    this->layer_height_ranges = model_object->layer_height_ranges;
}

bool
PrintObject::add_copy(const Pointf &point)
{
    Points points = this->_copies;
    points.push_back(Point::new_scale(point.x, point.y));
    return this->set_copies(points);
}

bool
PrintObject::delete_last_copy()
{
    Points points = this->_copies;
    points.pop_back();
    return this->set_copies(points);
}

bool
PrintObject::delete_all_copies()
{
    Points points;
    return this->set_copies(points);
}

bool
PrintObject::set_copies(const Points &points)
{
    this->_copies = points;
    
    // order copies with a nearest neighbor search and translate them by _copies_shift
    this->_shifted_copies.clear();
    this->_shifted_copies.reserve(points.size());
    
    // order copies with a nearest-neighbor search
    std::vector<Points::size_type> ordered_copies;
    Slic3r::Geometry::chained_path(points, ordered_copies);
    
    for (std::vector<Points::size_type>::const_iterator it = ordered_copies.begin(); it != ordered_copies.end(); ++it) {
        Point copy = points[*it];
        copy.translate(this->_copies_shift);
        this->_shifted_copies.push_back(copy);
    }
    
    bool invalidated = false;
    if (this->_print->invalidate_step(psSkirt)) invalidated = true;
    if (this->_print->invalidate_step(psBrim)) invalidated = true;
    return invalidated;
}

bool
PrintObject::reload_model_instances()
{
    Points copies;
    for (ModelInstancePtrs::const_iterator i = this->_model_object->instances.begin(); i != this->_model_object->instances.end(); ++i) {
        copies.push_back(Point::new_scale((*i)->offset.x, (*i)->offset.y));
    }
    return this->set_copies(copies);
}

BoundingBox
PrintObject::bounding_box() const
{
    // since the object is aligned to origin, bounding box coincides with size
    Points pp;
    pp.push_back(Point(0,0));
    pp.push_back(this->size);
    return BoundingBox(pp);
}

void
PrintObject::add_region_volume(int region_id, int volume_id)
{
    region_volumes[region_id].push_back(volume_id);
}

/*  This is the *total* layer count (including support layers)
    this value is not supposed to be compared with Layer::id
    since they have different semantics */
size_t
PrintObject::total_layer_count() const
{
    return this->layer_count() + this->support_layer_count();
}

size_t
PrintObject::layer_count() const
{
    return this->layers.size();
}

void
PrintObject::clear_layers()
{
    for (int i = this->layers.size()-1; i >= 0; --i)
        this->delete_layer(i);
}

Layer*
PrintObject::add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    Layer* layer = new Layer(id, this, height, print_z, slice_z);
    layers.push_back(layer);
    return layer;
}

void
PrintObject::delete_layer(int idx)
{
    LayerPtrs::iterator i = this->layers.begin() + idx;
    delete *i;
    this->layers.erase(i);
}

size_t
PrintObject::support_layer_count() const
{
    return this->support_layers.size();
}

void
PrintObject::clear_support_layers()
{
    for (int i = this->support_layers.size()-1; i >= 0; --i)
        this->delete_support_layer(i);
}

SupportLayer*
PrintObject::get_support_layer(int idx)
{
    return this->support_layers.at(idx);
}

SupportLayer*
PrintObject::add_support_layer(int id, coordf_t height, coordf_t print_z)
{
    SupportLayer* layer = new SupportLayer(id, this, height, print_z, -1);
    support_layers.push_back(layer);
    return layer;
}

void
PrintObject::delete_support_layer(int idx)
{
    SupportLayerPtrs::iterator i = this->support_layers.begin() + idx;
    delete *i;
    this->support_layers.erase(i);
}

bool
PrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    std::set<PrintObjectStep> steps;
    
    // this method only accepts PrintObjectConfig and PrintRegionConfig option keys
    for (std::vector<t_config_option_key>::const_iterator opt_key = opt_keys.begin(); opt_key != opt_keys.end(); ++opt_key) {
        if (*opt_key == "perimeters"
            || *opt_key == "extra_perimeters"
            || *opt_key == "gap_fill_speed"
            || *opt_key == "overhangs"
            || *opt_key == "first_layer_extrusion_width"
            || *opt_key == "perimeter_extrusion_width"
            || *opt_key == "infill_overlap"
            || *opt_key == "thin_walls"
            || *opt_key == "external_perimeters_first") {
            steps.insert(posPerimeters);
        } else if (*opt_key == "layer_height"
            || *opt_key == "first_layer_height"
            || *opt_key == "xy_size_compensation"
            || *opt_key == "raft_layers") {
            steps.insert(posSlice);
        } else if (*opt_key == "support_material"
            || *opt_key == "support_material_angle"
            || *opt_key == "support_material_extruder"
            || *opt_key == "support_material_extrusion_width"
            || *opt_key == "support_material_interface_layers"
            || *opt_key == "support_material_interface_extruder"
            || *opt_key == "support_material_interface_spacing"
            || *opt_key == "support_material_interface_speed"
            || *opt_key == "support_material_buildplate_only"
            || *opt_key == "support_material_pattern"
            || *opt_key == "support_material_spacing"
            || *opt_key == "support_material_threshold"
            || *opt_key == "support_material_with_sheath"
            || *opt_key == "dont_support_bridges"
            || *opt_key == "first_layer_extrusion_width") {
            steps.insert(posSupportMaterial);
        } else if (*opt_key == "interface_shells"
            || *opt_key == "infill_only_where_needed"
            || *opt_key == "infill_every_layers"
            || *opt_key == "solid_infill_every_layers"
            || *opt_key == "bottom_solid_layers"
            || *opt_key == "top_solid_layers"
            || *opt_key == "solid_infill_below_area"
            || *opt_key == "infill_extruder"
            || *opt_key == "solid_infill_extruder"
            || *opt_key == "infill_extrusion_width"
            || *opt_key == "ensure_vertical_shell_thickness") {
            steps.insert(posPrepareInfill);
        } else if (*opt_key == "external_fill_pattern"
            || *opt_key == "fill_angle"
            || *opt_key == "fill_pattern"
            || *opt_key == "top_infill_extrusion_width"
            || *opt_key == "first_layer_extrusion_width") {
            steps.insert(posInfill);
        } else if (*opt_key == "fill_density"
            || *opt_key == "solid_infill_extrusion_width") {
            steps.insert(posPerimeters);
            steps.insert(posPrepareInfill);
        } else if (*opt_key == "external_perimeter_extrusion_width"
            || *opt_key == "perimeter_extruder") {
            steps.insert(posPerimeters);
            steps.insert(posSupportMaterial);
        } else if (*opt_key == "bridge_flow_ratio") {
            steps.insert(posPerimeters);
            steps.insert(posInfill);
        } else if (*opt_key == "seam_position"
            || *opt_key == "seam_preferred_direction"
            || *opt_key == "seam_preferred_direction_jitter"
            || *opt_key == "support_material_speed"
            || *opt_key == "bridge_speed"
            || *opt_key == "external_perimeter_speed"
            || *opt_key == "infill_speed"
            || *opt_key == "perimeter_speed"
            || *opt_key == "small_perimeter_speed"
            || *opt_key == "solid_infill_speed"
            || *opt_key == "top_solid_infill_speed") {
            // these options only affect G-code export, so nothing to invalidate
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            return this->invalidate_all_steps();
        }
    }
    
    bool invalidated = false;
    for (std::set<PrintObjectStep>::const_iterator step = steps.begin(); step != steps.end(); ++step) {
        if (this->invalidate_step(*step)) invalidated = true;
    }
    
    return invalidated;
}

bool
PrintObject::invalidate_step(PrintObjectStep step)
{
    bool invalidated = this->state.invalidate(step);
    
    // propagate to dependent steps
    if (step == posPerimeters) {
        this->invalidate_step(posPrepareInfill);
        this->_print->invalidate_step(psSkirt);
        this->_print->invalidate_step(psBrim);
    } else if (step == posPrepareInfill) {
        this->invalidate_step(posInfill);
    } else if (step == posInfill) {
        this->_print->invalidate_step(psSkirt);
        this->_print->invalidate_step(psBrim);
    } else if (step == posSlice) {
        this->invalidate_step(posPerimeters);
        this->invalidate_step(posSupportMaterial);
    } else if (step == posSupportMaterial) {
        this->_print->invalidate_step(psSkirt);
        this->_print->invalidate_step(psBrim);
    }
    
    return invalidated;
}

bool
PrintObject::invalidate_all_steps()
{
    // make a copy because when invalidating steps the iterators are not working anymore
    std::set<PrintObjectStep> steps = this->state.started;
    
    bool invalidated = false;
    for (std::set<PrintObjectStep>::const_iterator step = steps.begin(); step != steps.end(); ++step) {
        if (this->invalidate_step(*step)) invalidated = true;
    }
    return invalidated;
}

bool
PrintObject::has_support_material() const
{
    return this->config.support_material
        || this->config.raft_layers > 0
        || this->config.support_material_enforce_layers > 0;
}

void
PrintObject::process_external_surfaces()
{
    FOREACH_REGION(this->_print, region) {
        size_t region_id = region - this->_print->regions.begin();
        
        FOREACH_LAYER(this, layer_it) {
            const Layer* lower_layer = (layer_it == this->layers.begin())
                ? NULL
                : *(layer_it-1);
            
            (*layer_it)->get_region(region_id)->process_external_surfaces(lower_layer);
        }
    }
}

void
PrintObject::discover_vertical_shells()
{
    for (size_t idx_region = 0; idx_region < this->_print->regions.size(); ++ idx_region) {
        if (! this->_print->regions[idx_region]->config.ensure_vertical_shell_thickness.value)
            continue;
        for (size_t idx_layer = 0; idx_layer < this->layers.size(); ++ idx_layer) {
            Layer* layer = this->layers[idx_layer];
            LayerRegion* layerm = layer->get_region(idx_region);
            // Find a union of perimeters below / above this surface to guarantee a minimum shell thickness.
            Polygons shell;
            ExPolygons shell_ex;
            if (1)
            {
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                {
                    static size_t idx = 0;
                    SVG svg_cummulative(debug_out_path("discover_vertical_shells-perimeters-before-union-run%d.svg", idx), this->bounding_box());
                    for (int n = (int)idx_layer - layerm->region()->config.bottom_solid_layers + 1; n < (int)idx_layer + layerm->region()->config.top_solid_layers; ++ n) {
                        if (n < 0 || n >= (int)this->layers.size())
                            continue;
                        ExPolygons &expolys = this->layers[n]->perimeter_expolygons;
                        for (size_t i = 0; i < expolys.size(); ++ i) {
                            SVG svg(debug_out_path("discover_vertical_shells-perimeters-before-union-run%d-layer%d-expoly%d.svg", idx, n, i), get_extents(expolys[i]));
                            svg.draw(expolys[i]);
                            svg.draw_outline(expolys[i].contour, "black", scale_(0.05));
                            svg.draw_outline(expolys[i].holes, "blue", scale_(0.05));
                            svg.Close();

                            svg_cummulative.draw(expolys[i]);
                            svg_cummulative.draw_outline(expolys[i].contour, "black", scale_(0.05));
                            svg_cummulative.draw_outline(expolys[i].holes, "blue", scale_(0.05));
                        }
                    }
                    ++ idx;
                }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                for (int n = (int)idx_layer - layerm->region()->config.bottom_solid_layers + 1; n < (int)idx_layer + layerm->region()->config.top_solid_layers; ++ n) {
                    if (n < 0 || n >= (int)this->layers.size())
                        continue;
                    ExPolygons &expolys = this->layers[n]->perimeter_expolygons;
                    for (size_t i = 0; i < expolys.size(); ++ i) {
                        shell.push_back(expolys[i].contour);
                        shell.insert(shell.end(), expolys[i].holes.begin(), expolys[i].holes.end());
                    }
                }
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                {
                    static size_t idx = 0;
                    SVG svg(debug_out_path("discover_vertical_shells-perimeters-before-union-%d.svg", idx ++), get_extents(shell));
                    svg.draw(shell);
                    svg.draw_outline(shell, "black", scale_(0.05));
                    svg.Close(); 
                }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                shell = union_(shell, true);
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                shell_ex = union_ex(shell, true);
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
            }

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            {
                static size_t idx = 0;
                SVG svg(debug_out_path("discover_vertical_shells-perimeters-after-union-%d.svg", idx ++), get_extents(shell));
                svg.draw(shell_ex);
                svg.draw_outline(shell_ex, "black", "blue", scale_(0.05));
                svg.Close();  
            }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            {
                static size_t idx = 0;
                SVG svg(debug_out_path("discover_vertical_shells-internal-wshell-%d.svg", idx ++), get_extents(shell));
                svg.draw(layerm->fill_surfaces.filter_by_type(stInternal), "yellow", 0.5);
                svg.draw_outline(layerm->fill_surfaces.filter_by_type(stInternal), "black", "blue", scale_(0.05));
                svg.draw(shell_ex, "blue", 0.5);
                svg.draw_outline(shell_ex, "black", "blue", scale_(0.05));
                svg.Close();
            } 
            {
                static size_t idx = 0;
                SVG svg(debug_out_path("discover_vertical_shells-internalvoid-wshell-%d.svg", idx ++), get_extents(shell));
                svg.draw(layerm->fill_surfaces.filter_by_type(stInternalVoid), "yellow", 0.5);
                svg.draw_outline(layerm->fill_surfaces.filter_by_type(stInternalVoid), "black", "blue", scale_(0.05));
                svg.draw(shell_ex, "blue", 0.5);
                svg.draw_outline(shell_ex, "black", "blue", scale_(0.05));
                svg.Close();
            } 
            {
                static size_t idx = 0;
                SVG svg(debug_out_path("discover_vertical_shells-internalvoid-wshell-%d.svg", idx ++), get_extents(shell));
                svg.draw(layerm->fill_surfaces.filter_by_type(stInternalVoid), "yellow", 0.5);
                svg.draw_outline(layerm->fill_surfaces.filter_by_type(stInternalVoid), "black", "blue", scale_(0.05));
                svg.draw(shell_ex, "blue", 0.5);
                svg.draw_outline(shell_ex, "black", "blue", scale_(0.05)); 
                svg.Close();
            } 
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

            // Trim the internal & internalvoid by the $shell.
            Slic3r::ExPolygons new_internal = diff_ex(
                to_polygons(layerm->fill_surfaces.filter_by_type(stInternal)),
                shell,
                false
            );
            Slic3r::ExPolygons new_internal_void = diff_ex(
                to_polygons(layerm->fill_surfaces.filter_by_type(stInternalVoid)),
                shell,
                false
            );
            // Add shells tstInternalVoido internal & internalvoid.
            const SurfaceType surfaceTypesInternal[] = { stInternal, stInternalVoid };
            Slic3r::ExPolygons new_internal_solid = intersection_ex(
                to_polygons(layerm->fill_surfaces.filter_by_types(surfaceTypesInternal, 2)),
                shell,
                true
            );

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            {
                static size_t idx = 0;
                SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal-%d.svg", idx), get_extents(shell), new_internal, "black", "blue", scale_(0.05));
                SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_void-%d.svg", idx), get_extents(shell), new_internal_void, "black", "blue", scale_(0.05));
                SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_solid-%d.svg", idx), get_extents(shell), new_internal_solid, "black", "blue", scale_(0.05));
                ++ idx;
            }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

            // Assign resulting internal surfaces to layer.
            const SurfaceType surfaceTypesKeep[] = { stTop, stBottom, stBottomBridge, stInternalSolid };
            layerm->fill_surfaces.keep_types(surfaceTypesKeep, sizeof(surfaceTypesKeep)/sizeof(SurfaceType));
            layerm->fill_surfaces.append(stInternal     , new_internal);
            layerm->fill_surfaces.append(stInternalVoid , new_internal_void);
            layerm->fill_surfaces.append(stInternalSolid, new_internal_solid);

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_slices_to_svg_debug("4_discover_vertical_shells");
            layerm->export_region_fill_surfaces_to_svg_debug("4_discover_vertical_shells");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
        } // for each layer
    } // for each region
}

/* This method applies bridge flow to the first internal solid layer above
   sparse infill */
void
PrintObject::bridge_over_infill()
{
    FOREACH_REGION(this->_print, region) {
        size_t region_id = region - this->_print->regions.begin();
        
        // skip bridging in case there are no voids
        if ((*region)->config.fill_density.value == 100) continue;
        
        // get bridge flow
        Flow bridge_flow = (*region)->flow(
            frSolidInfill,
            -1,     // layer height, not relevant for bridge flow
            true,   // bridge
            false,  // first layer
            -1,     // custom width, not relevant for bridge flow
            *this
        );
        
        FOREACH_LAYER(this, layer_it) {
            // skip first layer
            if (layer_it == this->layers.begin()) continue;
            
            Layer* layer        = *layer_it;
            LayerRegion* layerm = layer->get_region(region_id);
            
            // extract the stInternalSolid surfaces that might be transformed into bridges
            Polygons internal_solid;
            layerm->fill_surfaces.filter_by_type(stInternalSolid, &internal_solid);
            
            // check whether the lower area is deep enough for absorbing the extra flow
            // (for obvious physical reasons but also for preventing the bridge extrudates
            // from overflowing in 3D preview)
            ExPolygons to_bridge;
            {
                Polygons to_bridge_pp = internal_solid;
                
                // iterate through lower layers spanned by bridge_flow
                double bottom_z = layer->print_z - bridge_flow.height;
                for (int i = (layer_it - this->layers.begin()) - 1; i >= 0; --i) {
                    const Layer* lower_layer = this->layers[i];
                    
                    // stop iterating if layer is lower than bottom_z
                    if (lower_layer->print_z < bottom_z) break;
                    
                    // iterate through regions and collect internal surfaces
                    Polygons lower_internal;
                    FOREACH_LAYERREGION(lower_layer, lower_layerm_it)
                        (*lower_layerm_it)->fill_surfaces.filter_by_type(stInternal, &lower_internal);
                    
                    // intersect such lower internal surfaces with the candidate solid surfaces
                    to_bridge_pp = intersection(to_bridge_pp, lower_internal);
                }
                
                // there's no point in bridging too thin/short regions
                //FIXME Vojtech: The offset2 function is not a geometric offset, 
                // therefore it may create 1) gaps, and 2) sharp corners, which are outside the original contour.
                // The gaps will be filled by a separate region, which makes the infill less stable and it takes longer.
                {
                    double min_width = bridge_flow.scaled_width() * 3;
                    to_bridge_pp = offset2(to_bridge_pp, -min_width, +min_width);
                }
                
                if (to_bridge_pp.empty()) continue;
                
                // convert into ExPolygons
                to_bridge = union_ex(to_bridge_pp);
            }
            
            #ifdef SLIC3R_DEBUG
            printf("Bridging %zu internal areas at layer %zu\n", to_bridge.size(), layer->id());
            #endif
            
            // compute the remaning internal solid surfaces as difference
            ExPolygons not_to_bridge = diff_ex(internal_solid, to_bridge, true);
            to_bridge = intersection_ex(to_polygons(to_bridge), internal_solid, true);
            
            // build the new collection of fill_surfaces
            {
                layerm->fill_surfaces.remove_type(stInternalSolid);

                for (ExPolygons::const_iterator ex = to_bridge.begin(); ex != to_bridge.end(); ++ex)
                    layerm->fill_surfaces.surfaces.push_back(Surface(stInternalBridge, *ex));
                
                for (ExPolygons::const_iterator ex = not_to_bridge.begin(); ex != not_to_bridge.end(); ++ex)
                    layerm->fill_surfaces.surfaces.push_back(Surface(stInternalSolid, *ex));
            }
            
            /*
            # exclude infill from the layers below if needed
            # see discussion at https://github.com/alexrj/Slic3r/issues/240
            # Update: do not exclude any infill. Sparse infill is able to absorb the excess material.
            if (0) {
                my $excess = $layerm->extruders->{infill}->bridge_flow->width - $layerm->height;
                for (my $i = $layer_id-1; $excess >= $self->get_layer($i)->height; $i--) {
                    Slic3r::debugf "  skipping infill below those areas at layer %d\n", $i;
                    foreach my $lower_layerm (@{$self->get_layer($i)->regions}) {
                        my @new_surfaces = ();
                        # subtract the area from all types of surfaces
                        foreach my $group (@{$lower_layerm->fill_surfaces->group}) {
                            push @new_surfaces, map $group->[0]->clone(expolygon => $_),
                                @{diff_ex(
                                    [ map $_->p, @$group ],
                                    [ map @$_, @$to_bridge ],
                                )};
                            push @new_surfaces, map Slic3r::Surface->new(
                                expolygon       => $_,
                                surface_type    => S_TYPE_INTERNALVOID,
                            ), @{intersection_ex(
                                [ map $_->p, @$group ],
                                [ map @$_, @$to_bridge ],
                            )};
                        }
                        $lower_layerm->fill_surfaces->clear;
                        $lower_layerm->fill_surfaces->append($_) for @new_surfaces;
                    }
                    
                    $excess -= $self->get_layer($i)->height;
                }
            }
            */

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_slices_to_svg_debug("7_bridge_over_infill");
            layerm->export_region_fill_surfaces_to_svg_debug("7_bridge_over_infill");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
        }
    }
}

}
