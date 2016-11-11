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
            || *opt_key == "external_fill_link_max_length"
            || *opt_key == "fill_angle"
            || *opt_key == "fill_pattern"
            || *opt_key == "fill_link_max_length"
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

// This function analyzes slices of a region (SurfaceCollection slices).
// Each slice (instance of Surface) is analyzed, whether it is supported or whether it is the top surface.
// Initially all slices are of type S_TYPE_INTERNAL.
// Slices are compared against the top / bottom slices and regions and classified to the following groups:
// S_TYPE_TOP - Part of a region, which is not covered by any upper layer. This surface will be filled with a top solid infill.
// S_TYPE_BOTTOMBRIDGE - Part of a region, which is not fully supported, but it hangs in the air, or it hangs losely on a support or a raft.
// S_TYPE_BOTTOM - Part of a region, which is not supported by the same region, but it is supported either by another region, or by a soluble interface layer.
// S_TYPE_INTERNAL - Part of a region, which is supported by the same region type.
// If a part of a region is of S_TYPE_BOTTOM and S_TYPE_TOP, the S_TYPE_BOTTOM wins.
void PrintObject::detect_surfaces_type()
{
//    Slic3r::debugf "Detecting solid surfaces...\n";
    for (int idx_region = 0; idx_region < this->_print->regions.size(); ++ idx_region) {
        // Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
        for (int idx_layer = 0; idx_layer < int(this->layer_count()); ++ idx_layer) {
            LayerRegion *layerm = this->layers[idx_layer]->get_region(idx_region);
            layerm->slices_to_fill_surfaces_clipped();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
        }

        for (int idx_layer = 0; idx_layer < int(this->layer_count()); ++ idx_layer) {
            Layer       *layer  = this->layers[idx_layer];
            LayerRegion *layerm = layer->get_region(idx_region);
            // comparison happens against the *full* slices (considering all regions)
            // unless internal shells are requested
            Layer       *upper_layer = idx_layer + 1 < this->layer_count() ? this->get_layer(idx_layer + 1) : NULL;
            Layer       *lower_layer = idx_layer > 0 ? this->get_layer(idx_layer - 1) : NULL;
            // collapse very narrow parts (using the safety offset in the diff is not enough)
            float        offset = layerm->flow(frExternalPerimeter).scaled_width() / 10.f;

            Polygons     layerm_slices_surfaces = to_polygons(layerm->slices.surfaces);

            // find top surfaces (difference between current surfaces
            // of current layer and upper one)
            Surfaces top;
            if (upper_layer) {
                // Config value $self->config->interface_shells is true, if a support is separated from the object
                // by a soluble material (for example a PVA plastic).
                Polygons upper_slices = this->config.interface_shells.value ? 
                    to_polygons(upper_layer->get_region(idx_region)->slices.surfaces) : 
                    to_polygons(upper_layer->slices);
                surfaces_append(top,
                    offset2_ex(diff(layerm_slices_surfaces, upper_slices, true), -offset, offset),
                    stTop);
            } else {
                // if no upper layer, all surfaces of this one are solid
                // we clone surfaces because we're going to clear the slices collection
                top = layerm->slices.surfaces;
                for (Surfaces::iterator it = top.begin(); it != top.end(); ++ it)
                    it->surface_type = stTop;
            }
            
            // find bottom surfaces (difference between current surfaces
            // of current layer and lower one)
            Surfaces bottom;
            if (lower_layer) {
                // If we have soluble support material, don't bridge. The overhang will be squished against a soluble layer separating
                // the support from the print.
                SurfaceType surface_type_bottom = 
                    (this->config.support_material.value && this->config.support_material_contact_distance.value == 0) ?
                    stBottom : stBottomBridge;
                // Any surface lying on the void is a true bottom bridge (an overhang)
                surfaces_append(
                    bottom,
                    offset2_ex(
                        diff(layerm_slices_surfaces, to_polygons(lower_layer->slices), true), 
                        -offset, offset),
                    surface_type_bottom);
                // if user requested internal shells, we need to identify surfaces
                // lying on other slices not belonging to this region
                //FIXME Vojtech: config.internal_shells or config.interface_shells? Is it some legacy code?
                // Why shall multiple regions over soluble support be treated specially?
                if (this->config.interface_shells.value) {
                    // non-bridging bottom surfaces: any part of this layer lying 
                    // on something else, excluding those lying on our own region
                    surfaces_append(
                        bottom,
                        offset2_ex(
                            diff(
                                intersection(layerm_slices_surfaces, to_polygons(lower_layer->slices)), // supported
                                to_polygons(lower_layer->get_region(idx_region)->slices.surfaces), 
                                true), 
                            -offset, offset),
                        stBottom);
                }
            } else {
                // if no lower layer, all surfaces of this one are solid
                // we clone surfaces because we're going to clear the slices collection
                bottom = layerm->slices.surfaces;
                // if we have raft layers, consider bottom layer as a bridge
                // just like any other bottom surface lying on the void
                SurfaceType surface_type_bottom = 
                    (this->config.raft_layers.value > 0 && this->config.support_material_contact_distance.value > 0) ?
                    stBottomBridge : stBottom;
                for (Surfaces::iterator it = bottom.begin(); it != bottom.end(); ++ it)
                    it->surface_type = surface_type_bottom;
            }
            
            // now, if the object contained a thin membrane, we could have overlapping bottom
            // and top surfaces; let's do an intersection to discover them and consider them
            // as bottom surfaces (to allow for bridge detection)
            if (! top.empty() && ! bottom.empty()) {
//                Polygons overlapping = intersection(to_polygons(top), to_polygons(bottom));
//                Slic3r::debugf "  layer %d contains %d membrane(s)\n", $layerm->layer->id, scalar(@$overlapping)
//                    if $Slic3r::debug;
                Polygons top_polygons = to_polygons(STDMOVE(top));
                top.clear();
                surfaces_append(top,
#if 0
                    offset2_ex(diff(top_polygons, to_polygons(bottom), true), -offset, offset),
#else
                    diff_ex(top_polygons, to_polygons(bottom), false),
#endif
                    stTop);
            }
            
            // save surfaces to layer
            layerm->slices.surfaces.clear();

            // find internal surfaces (difference between top/bottom surfaces and others)
            {
                Polygons topbottom = to_polygons(top);
                polygons_append(topbottom, to_polygons(bottom));
                surfaces_append(layerm->slices.surfaces, 
#if 0
                    offset2_ex(diff(layerm_slices_surfaces, topbottom, true), -offset, offset),
#else
                    diff_ex(layerm_slices_surfaces, topbottom, false),
#endif
                    stInternal);
            }

            surfaces_append(layerm->slices.surfaces, STDMOVE(top));
            surfaces_append(layerm->slices.surfaces, STDMOVE(bottom));
            
//            Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
//                $layerm->layer->id, scalar(@bottom), scalar(@top), scalar(@internal) if $Slic3r::debug;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_slices_to_svg_debug("detect_surfaces_type-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
        } // for each layer of a region
        
        // Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
        for (int idx_layer = 0; idx_layer < int(this->layer_count()); ++ idx_layer) {
            LayerRegion *layerm = this->layers[idx_layer]->get_region(idx_region);
            layerm->slices_to_fill_surfaces_clipped();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
        } // for each layer of a region
    } // for each $self->print->region_count
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
            Layer       *layer               = this->layers[idx_layer];
            LayerRegion *layerm              = layer->get_region(idx_region);
            Flow         solid_infill_flow   = layerm->flow(frSolidInfill);
            coord_t      infill_line_spacing = solid_infill_flow.scaled_spacing(); 
            // Find a union of perimeters below / above this surface to guarantee a minimum shell thickness.
            Polygons shell;
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            ExPolygons shell_ex;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
            float min_perimeter_infill_spacing = float(infill_line_spacing) * 1.05f;
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
                SurfaceType surfaces_bottom[2] = { stBottom, stBottomBridge };
                for (int n = (int)idx_layer - layerm->region()->config.bottom_solid_layers + 1; n < (int)idx_layer + layerm->region()->config.top_solid_layers; ++ n)
                    if (n >= 0 && n < (int)this->layers.size()) {
                        Layer       &neighbor_layer = *this->layers[n];
                        LayerRegion &neighbor_region = *neighbor_layer.get_region(int(idx_region));
                        polygons_append(shell, neighbor_layer.perimeter_expolygons.expolygons);
                        if (n > int(idx_layer)) {
                            // Collect top surfaces.
                            polygons_append(shell, offset(to_expolygons(neighbor_region.slices.filter_by_type(stTop)), min_perimeter_infill_spacing));
                            polygons_append(shell, offset(to_expolygons(neighbor_region.fill_surfaces.filter_by_type(stTop)), min_perimeter_infill_spacing));
                        }
                        else if (n < int(idx_layer)) {
                            // Collect bottom and bottom bridge surfaces.
                            polygons_append(shell, offset(to_expolygons(neighbor_region.slices.filter_by_types(surfaces_bottom, 2)), min_perimeter_infill_spacing));
                            polygons_append(shell, offset(to_expolygons(neighbor_region.fill_surfaces.filter_by_types(surfaces_bottom, 2)), min_perimeter_infill_spacing));
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

            if (shell.empty())
                continue;

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

            // Trim the shells region by the internal & internal void surfaces.
            const SurfaceType surfaceTypesInternal[] = { stInternal, stInternalVoid, stInternalSolid };
            const Polygons    polygonsInternal = to_polygons(layerm->fill_surfaces.filter_by_types(surfaceTypesInternal, 2));
            shell = intersection(shell, polygonsInternal, true);
            if (shell.empty())
                continue;

            // Append the internal solids, so they will be merged with the new ones.
            polygons_append(shell, to_polygons(layerm->fill_surfaces.filter_by_type(stInternalSolid)));

            // These regions will be filled by a rectilinear full infill. Currently this type of infill
            // only fills regions, which fit at least a single line. To avoid gaps in the sparse infill,
            // make sure that this region does not contain parts narrower than the infill spacing width.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            Polygons shell_before = shell;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
#if 1
            // Intentionally inflate a bit more than how much the region has been shrunk, 
            // so there will be some overlap between this solid infill and the other infill regions (mainly the sparse infill).
            shell = offset2(shell, - 0.5f * min_perimeter_infill_spacing, 0.8f * min_perimeter_infill_spacing,
                CLIPPER_OFFSET_SCALE, ClipperLib::jtSquare);
            if (shell.empty())
                continue;
#else
            // Ensure each region is at least 3x infill line width wide, so it could be filled in.
//            float margin = float(infill_line_spacing) * 3.f;
            float margin = float(infill_line_spacing) * 1.5f;
            // we use a higher miterLimit here to handle areas with acute angles
            // in those cases, the default miterLimit would cut the corner and we'd
            // get a triangle in $too_narrow; if we grow it below then the shell
            // would have a different shape from the external surface and we'd still
            // have the same angle, so the next shell would be grown even more and so on.
            Polygons too_narrow = diff(shell, offset2(shell, -margin, margin, CLIPPER_OFFSET_SCALE, ClipperLib::jtMiter, 5.), true);
            if (! too_narrow.empty()) {
                // grow the collapsing parts and add the extra area to  the neighbor layer 
                // as well as to our original surfaces so that we support this 
                // additional area in the next shell too
                // make sure our grown surfaces don't exceed the fill area
                polygons_append(shell, intersection(offset(too_narrow, margin), polygonsInternal));
            }
#endif
            ExPolygons new_internal_solid = intersection_ex(polygonsInternal, shell, false);
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            {
                static size_t idx = 0;
                SVG svg(debug_out_path("discover_vertical_shells-regularized-%d.svg", idx ++), get_extents(shell_before));
                // Source shell.
                svg.draw(union_ex(shell_before, true));
                // Shell trimmed to the internal surfaces.
                svg.draw_outline(union_ex(shell, true), "black", "blue", scale_(0.05));
                // Regularized infill region.
                svg.draw_outline(new_internal_solid, "red", "magenta", scale_(0.05));
                svg.Close();  
            }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

            // Trim the internal & internalvoid by the shell.
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
            const SurfaceType surfaceTypesKeep[] = { stTop, stBottom, stBottomBridge };
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
            printf("Bridging " PRINTF_ZU " internal areas at layer " PRINTF_ZU "\n", to_bridge.size(), layer->id());
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
