#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "ElephantFootCompensation.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "SupportMaterial.hpp"
#include "Surface.hpp"
#include "Slicing.hpp"
#include "Utils.hpp"

#include <utility>
#include <boost/log/trivial.hpp>
#include <float.h>

#include <tbb/parallel_for.h>
#include <tbb/atomic.h>

#include <Shiny/Shiny.h>

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
#define SLIC3R_DEBUG
#endif

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #define DEBUG
    #define _DEBUG
    #include "SVG.hpp"
    #undef assert 
    #include <cassert>
#endif

namespace Slic3r {

// Constructor is called from the main thread, therefore all Model / ModelObject / ModelIntance data are valid.
PrintObject::PrintObject(Print* print, ModelObject* model_object, const Transform3d& trafo, PrintInstances&& instances) :
    PrintObjectBaseWithState(print, model_object),
    m_trafo(trafo)
{
    // Compute centering offet to be applied to our meshes so that we work with smaller coordinates
    // requiring less bits to represent Clipper coordinates.

	// Snug bounding box of a rotated and scaled object by the 1st instantion, without the instance translation applied.
	// All the instances share the transformation matrix with the exception of translation in XY and rotation by Z,
	// therefore a bounding box from 1st instance of a ModelObject is good enough for calculating the object center,
	// snug height and an approximate bounding box in XY.
    BoundingBoxf3  bbox        = model_object->raw_bounding_box();
    Vec3d 		   bbox_center = bbox.center();
	// We may need to rotate the bbox / bbox_center from the original instance to the current instance.
	double z_diff = Geometry::rotation_diff_z(model_object->instances.front()->get_rotation(), instances.front().model_instance->get_rotation());
	if (std::abs(z_diff) > EPSILON) {
		auto z_rot  = Eigen::AngleAxisd(z_diff, Vec3d::UnitZ());
		bbox 		= bbox.transformed(Transform3d(z_rot));
		bbox_center = (z_rot * bbox_center).eval();
	}

    // Center of the transformed mesh (without translation).
    m_center_offset = Point::new_scale(bbox_center.x(), bbox_center.y());
    // Size of the transformed mesh. This bounding may not be snug in XY plane, but it is snug in Z.
    m_size = (bbox.size() * (1. / SCALING_FACTOR)).cast<coord_t>();

    this->set_instances(std::move(instances));
}

PrintBase::ApplyStatus PrintObject::set_instances(PrintInstances &&instances)
{
    for (PrintInstance &i : instances)
    	// Add the center offset, which will be subtracted from the mesh when slicing.
    	i.shift += m_center_offset;
    // Invalidate and set copies.
    PrintBase::ApplyStatus status = PrintBase::APPLY_STATUS_UNCHANGED;
    bool equal_length = instances.size() == m_instances.size();
    bool equal = equal_length && std::equal(instances.begin(), instances.end(), m_instances.begin(), 
    	[](const PrintInstance& lhs, const PrintInstance& rhs) { return lhs.model_instance == rhs.model_instance && lhs.shift == rhs.shift; });
    if (! equal) {
        status = PrintBase::APPLY_STATUS_CHANGED;
        if (m_print->invalidate_steps({ psSkirt, psBrim, psGCodeExport }) ||
            (! equal_length && m_print->invalidate_step(psWipeTower)))
            status = PrintBase::APPLY_STATUS_INVALIDATED;
        m_instances = std::move(instances);
	    for (PrintInstance &i : m_instances)
	    	i.print_object = this;
    }
    return status;
}

// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
void PrintObject::slice()
{
    if (! this->set_started(posSlice))
        return;
    m_print->set_status(10, L("Processing triangulated mesh"));
    std::vector<coordf_t> layer_height_profile;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile);
    m_print->throw_if_canceled();
    this->_slice(layer_height_profile);
    m_print->throw_if_canceled();
    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = this->_fix_slicing_errors();
    m_print->throw_if_canceled();
    if (! warning.empty())
        BOOST_LOG_TRIVIAL(info) << warning;
    // Simplify slices if required.
    if (m_print->config().resolution)
        this->simplify_slices(scale_(this->print()->config().resolution));
    // Update bounding boxes
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer &layer = *m_layers[layer_idx];
                layer.lslices_bboxes.clear();
                layer.lslices_bboxes.reserve(layer.lslices.size());
                for (const ExPolygon &expoly : layer.lslices)
                	layer.lslices_bboxes.emplace_back(get_extents(expoly));
            }
        });
    if (m_layers.empty())
        throw std::runtime_error("No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n");    
    this->set_done(posSlice);
}

// 1) Merges typed region slices into stInternal type.
// 2) Increases an "extra perimeters" counter at region slices where needed.
// 3) Generates perimeters, gap fills and fill regions (fill regions of type stInternal).
void PrintObject::make_perimeters()
{
    // prerequisites
    this->slice();

    if (! this->set_started(posPerimeters))
        return;

    m_print->set_status(20, L("Generating perimeters"));
    BOOST_LOG_TRIVIAL(info) << "Generating perimeters..." << log_memory_info();
    
    // merge slices if they were split into types
    if (m_typed_slices) {
        for (Layer *layer : m_layers) {
            layer->merge_slices();
            m_print->throw_if_canceled();
        }
        m_typed_slices = false;
    }
    
    // compare each layer to the one below, and mark those slices needing
    // one additional inner perimeter, like the top of domed objects-
    
    // this algorithm makes sure that at least one perimeter is overlapping
    // but we don't generate any extra perimeter if fill density is zero, as they would be floating
    // inside the object - infill_only_where_needed should be the method of choice for printing
    // hollow objects
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        const PrintRegion &region = *m_print->regions()[region_id];
        if (! region.config().extra_perimeters || region.config().perimeters == 0 || region.config().fill_density == 0 || this->layer_count() < 2)
            continue;

        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size() - 1),
            [this, &region, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    LayerRegion &layerm                     = *m_layers[layer_idx]->m_regions[region_id];
                    const LayerRegion &upper_layerm         = *m_layers[layer_idx+1]->m_regions[region_id];
                    const Polygons upper_layerm_polygons    = upper_layerm.slices;
                    // Filter upper layer polygons in intersection_ppl by their bounding boxes?
                    // my $upper_layerm_poly_bboxes= [ map $_->bounding_box, @{$upper_layerm_polygons} ];
                    const double total_loop_length      = total_length(upper_layerm_polygons);
                    const coord_t perimeter_spacing     = layerm.flow(frPerimeter).scaled_spacing();
                    const Flow ext_perimeter_flow       = layerm.flow(frExternalPerimeter);
                    const coord_t ext_perimeter_width   = ext_perimeter_flow.scaled_width();
                    const coord_t ext_perimeter_spacing = ext_perimeter_flow.scaled_spacing();

                    for (Surface &slice : layerm.slices.surfaces) {
                        for (;;) {
                            // compute the total thickness of perimeters
                            const coord_t perimeters_thickness = ext_perimeter_width/2 + ext_perimeter_spacing/2
                                + (region.config().perimeters-1 + slice.extra_perimeters) * perimeter_spacing;
                            // define a critical area where we don't want the upper slice to fall into
                            // (it should either lay over our perimeters or outside this area)
                            const coord_t critical_area_depth = coord_t(perimeter_spacing * 1.5);
                            const Polygons critical_area = diff(
                                offset(slice.expolygon, float(- perimeters_thickness)),
                                offset(slice.expolygon, float(- perimeters_thickness - critical_area_depth))
                            );
                            // check whether a portion of the upper slices falls inside the critical area
                            const Polylines intersection = intersection_pl(to_polylines(upper_layerm_polygons), critical_area);
                            // only add an additional loop if at least 30% of the slice loop would benefit from it
                            if (total_length(intersection) <=  total_loop_length*0.3)
                                break;
                            /*
                            if (0) {
                                require "Slic3r/SVG.pm";
                                Slic3r::SVG::output(
                                    "extra.svg",
                                    no_arrows   => 1,
                                    expolygons  => union_ex($critical_area),
                                    polylines   => [ map $_->split_at_first_point, map $_->p, @{$upper_layerm->slices} ],
                                );
                            }
                            */
                            ++ slice.extra_perimeters;
                        }
                        #ifdef DEBUG
                            if (slice.extra_perimeters > 0)
                                printf("  adding %d more perimeter(s) at layer %zu\n", slice.extra_perimeters, layer_idx);
                        #endif
                    }
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - end";
    }

    BOOST_LOG_TRIVIAL(debug) << "Generating perimeters in parallel - start";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                m_layers[layer_idx]->make_perimeters();
            }
        }
    );
    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Generating perimeters in parallel - end";

    this->set_done(posPerimeters);
}

void PrintObject::prepare_infill()
{
    if (! this->set_started(posPrepareInfill))
        return;

    m_print->set_status(30, L("Preparing infill"));

    // This will assign a type (top/bottom/internal) to $layerm->slices.
    // Then the classifcation of $layerm->slices is transfered onto 
    // the $layerm->fill_surfaces by clipping $layerm->fill_surfaces
    // by the cummulative area of the previous $layerm->fill_surfaces.
    this->detect_surfaces_type();
    m_print->throw_if_canceled();
    
    // Decide what surfaces are to be filled.
    // Here the stTop / stBottomBridge / stBottom infill is turned to just stInternal if zero top / bottom infill layers are configured.
    // Also tiny stInternal surfaces are turned to stInternalSolid.
    BOOST_LOG_TRIVIAL(info) << "Preparing fill surfaces..." << log_memory_info();
    for (auto *layer : m_layers)
        for (auto *region : layer->m_regions) {
            region->prepare_fill_surfaces();
            m_print->throw_if_canceled();
        }

    // this will detect bridges and reverse bridges
    // and rearrange top/bottom/internal surfaces
    // It produces enlarged overlapping bridging areas.
    //
    // 1) stBottomBridge / stBottom infill is grown by 3mm and clipped by the total infill area. Bridges are detected. The areas may overlap.
    // 2) stTop is grown by 3mm and clipped by the grown bottom areas. The areas may overlap.
    // 3) Clip the internal surfaces by the grown top/bottom surfaces.
    // 4) Merge surfaces with the same style. This will mostly get rid of the overlaps.
    //FIXME This does not likely merge surfaces, which are supported by a material with different colors, but same properties.
    this->process_external_surfaces();
    m_print->throw_if_canceled();

    // Add solid fills to ensure the shell vertical thickness.
    this->discover_vertical_shells();
    m_print->throw_if_canceled();

    // Debugging output.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("6_discover_vertical_shells-final");
            layerm->export_region_fill_surfaces_to_svg_debug("6_discover_vertical_shells-final");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Detect, which fill surfaces are near external layers.
    // They will be split in internal and internal-solid surfaces.
    // The purpose is to add a configurable number of solid layers to support the TOP surfaces
    // and to add a configurable number of solid layers above the BOTTOM / BOTTOMBRIDGE surfaces
    // to close these surfaces reliably.
    //FIXME Vojtech: Is this a good place to add supporting infills below sloping perimeters?
    this->discover_horizontal_shells();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("7_discover_horizontal_shells-final");
            layerm->export_region_fill_surfaces_to_svg_debug("7_discover_horizontal_shells-final");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    // Only active if config->infill_only_where_needed. This step trims the sparse infill,
    // so it acts as an internal support. It maintains all other infill types intact.
    // Here the internal surfaces and perimeters have to be supported by the sparse infill.
    //FIXME The surfaces are supported by a sparse infill, but the sparse infill is only as large as the area to support.
    // Likely the sparse infill will not be anchored correctly, so it will not work as intended.
    // Also one wishes the perimeters to be supported by a full infill.
    this->clip_fill_surfaces();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("8_clip_surfaces-final");
            layerm->export_region_fill_surfaces_to_svg_debug("8_clip_surfaces-final");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
    
    // the following step needs to be done before combination because it may need
    // to remove only half of the combined infill
    this->bridge_over_infill();
    m_print->throw_if_canceled();

    // combine fill surfaces to honor the "infill every N layers" option
    this->combine_infill();
    m_print->throw_if_canceled();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("9_prepare_infill-final");
            layerm->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
        } // for each layer
    } // for each region
    for (const Layer *layer : m_layers) {
        layer->export_region_slices_to_svg_debug("9_prepare_infill-final");
        layer->export_region_fill_surfaces_to_svg_debug("9_prepare_infill-final");
    } // for each layer
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

    this->set_done(posPrepareInfill);
}

void PrintObject::infill()
{
    // prerequisites
    this->prepare_infill();

    if (this->set_started(posInfill)) {
        BOOST_LOG_TRIVIAL(debug) << "Filling layers in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->make_fills();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Filling layers in parallel - end";
        /*  we could free memory now, but this would make this step not idempotent
        ### $_->fill_surfaces->clear for map @{$_->regions}, @{$object->layers};
        */
        this->set_done(posInfill);
    }
}

void PrintObject::ironing()
{
    if (this->set_started(posIroning)) {
        BOOST_LOG_TRIVIAL(debug) << "Ironing in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(1, m_layers.size()),
            [this](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->make_ironing();
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Ironing in parallel - end";
        this->set_done(posIroning);
    }
}

void PrintObject::generate_support_material()
{
    if (this->set_started(posSupportMaterial)) {
        this->clear_support_layers();
        if ((m_config.support_material || m_config.raft_layers > 0) && m_layers.size() > 1) {
            m_print->set_status(85, L("Generating support material"));    
            this->_generate_support_material();
            m_print->throw_if_canceled();
        } else {
#if 0
            // Printing without supports. Empty layer means some objects or object parts are levitating,
            // therefore they cannot be printed without supports.
            for (const Layer *layer : m_layers)
                if (layer->empty())
                    throw std::runtime_error("Levitating objects cannot be printed without supports.");
#endif
        }
        this->set_done(posSupportMaterial);
    }
}

void PrintObject::clear_layers()
{
    for (Layer *l : m_layers)
        delete l;
    m_layers.clear();
}

Layer* PrintObject::add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    m_layers.emplace_back(new Layer(id, this, height, print_z, slice_z));
    return m_layers.back();
}

void PrintObject::clear_support_layers()
{
    for (Layer *l : m_support_layers)
        delete l;
    m_support_layers.clear();
}

SupportLayer* PrintObject::add_support_layer(int id, coordf_t height, coordf_t print_z)
{
    m_support_layers.emplace_back(new SupportLayer(id, this, height, print_z, -1));
    return m_support_layers.back();
}

SupportLayerPtrs::const_iterator PrintObject::insert_support_layer(SupportLayerPtrs::const_iterator pos, size_t id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    return m_support_layers.insert(pos, new SupportLayer(id, this, height, print_z, slice_z));
}

// Called by Print::apply().
// This method only accepts PrintObjectConfig and PrintRegionConfig option keys.
bool PrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<PrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (   opt_key == "perimeters"
            || opt_key == "extra_perimeters"
            || opt_key == "gap_fill_speed"
            || opt_key == "overhangs"
            || opt_key == "first_layer_extrusion_width"
            || opt_key == "perimeter_extrusion_width"
            || opt_key == "infill_overlap"
            || opt_key == "thin_walls"
            || opt_key == "external_perimeters_first") {
            steps.emplace_back(posPerimeters);
        } else if (
               opt_key == "layer_height"
            || opt_key == "first_layer_height"
            || opt_key == "raft_layers"
            || opt_key == "slice_closing_radius") {
            steps.emplace_back(posSlice);
		} else if (
               opt_key == "clip_multipart_objects"
            || opt_key == "elefant_foot_compensation"
            || opt_key == "support_material_contact_distance" 
            || opt_key == "xy_size_compensation") {
            steps.emplace_back(posSlice);
        } else if (opt_key == "support_material") {
            steps.emplace_back(posSupportMaterial);
            if (m_config.support_material_contact_distance == 0.) {
            	// Enabling / disabling supports while soluble support interface is enabled.
            	// This changes the bridging logic (bridging enabled without supports, disabled with supports).
            	// Reset everything.
            	// See GH #1482 for details.
	            steps.emplace_back(posSlice);
	        }
        } else if (
        	   opt_key == "support_material_auto"
            || opt_key == "support_material_angle"
            || opt_key == "support_material_buildplate_only"
            || opt_key == "support_material_enforce_layers"
            || opt_key == "support_material_extruder"
            || opt_key == "support_material_extrusion_width"
            || opt_key == "support_material_interface_layers"
            || opt_key == "support_material_interface_contact_loops"
            || opt_key == "support_material_interface_extruder"
            || opt_key == "support_material_interface_spacing"
            || opt_key == "support_material_pattern"
            || opt_key == "support_material_xy_spacing"
            || opt_key == "support_material_spacing"
            || opt_key == "support_material_synchronize_layers"
            || opt_key == "support_material_threshold"
            || opt_key == "support_material_with_sheath"
            || opt_key == "dont_support_bridges"
            || opt_key == "first_layer_extrusion_width") {
            steps.emplace_back(posSupportMaterial);
        } else if (
               opt_key == "interface_shells"
            || opt_key == "infill_only_where_needed"
            || opt_key == "infill_every_layers"
            || opt_key == "solid_infill_every_layers"
            || opt_key == "bottom_solid_layers"
            || opt_key == "bottom_solid_min_thickness"
            || opt_key == "top_solid_layers"
            || opt_key == "top_solid_min_thickness"
            || opt_key == "solid_infill_below_area"
            || opt_key == "infill_extruder"
            || opt_key == "solid_infill_extruder"
            || opt_key == "infill_extrusion_width"
            || opt_key == "ensure_vertical_shell_thickness"
            || opt_key == "bridge_angle") {
            steps.emplace_back(posPrepareInfill);
        } else if (
               opt_key == "top_fill_pattern"
            || opt_key == "bottom_fill_pattern"
            || opt_key == "external_fill_link_max_length"
            || opt_key == "fill_angle"
            || opt_key == "fill_pattern"
            || opt_key == "fill_link_max_length"
            || opt_key == "top_infill_extrusion_width"
            || opt_key == "first_layer_extrusion_width") {
            steps.emplace_back(posInfill);
        } else if (
               opt_key == "fill_density"
            || opt_key == "solid_infill_extrusion_width") {
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (
               opt_key == "external_perimeter_extrusion_width"
            || opt_key == "perimeter_extruder") {
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posSupportMaterial);
        } else if (opt_key == "bridge_flow_ratio") {
            if (m_config.support_material_contact_distance > 0.) {
            	// Only invalidate due to bridging if bridging is enabled.
            	// If later "support_material_contact_distance" is modified, the complete PrintObject is invalidated anyway.
            	steps.emplace_back(posPerimeters);
            	steps.emplace_back(posInfill);
	            steps.emplace_back(posSupportMaterial);
	        }
        } else if (
               opt_key == "seam_position"
            || opt_key == "seam_preferred_direction"
            || opt_key == "seam_preferred_direction_jitter"
            || opt_key == "support_material_speed"
            || opt_key == "support_material_interface_speed"
            || opt_key == "bridge_speed"
            || opt_key == "external_perimeter_speed"
            || opt_key == "infill_speed"
            || opt_key == "perimeter_speed"
            || opt_key == "small_perimeter_speed"
            || opt_key == "solid_infill_speed"
            || opt_key == "top_solid_infill_speed") {
            invalidated |= m_print->invalidate_step(psGCodeExport);
        } else if (
               opt_key == "wipe_into_infill"
            || opt_key == "wipe_into_objects") {
            invalidated |= m_print->invalidate_step(psWipeTower);
            invalidated |= m_print->invalidate_step(psGCodeExport);
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            this->invalidate_all_steps();
            invalidated = true;
        }
    }

    sort_remove_duplicates(steps);
    for (PrintObjectStep step : steps)
        invalidated |= this->invalidate_step(step);
    return invalidated;
}

bool PrintObject::invalidate_step(PrintObjectStep step)
{
	bool invalidated = Inherited::invalidate_step(step);
    
    // propagate to dependent steps
    if (step == posPerimeters) {
		invalidated |= this->invalidate_steps({ posPrepareInfill, posInfill });
        invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
    } else if (step == posPrepareInfill) {
        invalidated |= this->invalidate_step(posInfill);
    } else if (step == posInfill) {
        invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
    } else if (step == posSlice) {
		invalidated |= this->invalidate_steps({ posPerimeters, posPrepareInfill, posInfill, posSupportMaterial });
		invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
        this->m_slicing_params.valid = false;
    } else if (step == posSupportMaterial) {
        invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
        this->m_slicing_params.valid = false;
    }

    // Wipe tower depends on the ordering of extruders, which in turn depends on everything.
    // It also decides about what the wipe_into_infill / wipe_into_object features will do,
    // and that too depends on many of the settings.
    invalidated |= m_print->invalidate_step(psWipeTower);
    // Invalidate G-code export in any case.
    invalidated |= m_print->invalidate_step(psGCodeExport);
    return invalidated;
}

bool PrintObject::invalidate_all_steps()
{
	// First call the "invalidate" functions, which may cancel background processing.
    bool result = Inherited::invalidate_all_steps() | m_print->invalidate_all_steps();
	// Then reset some of the depending values.
	this->m_slicing_params.valid = false;
	this->region_volumes.clear();
	return result;
}

bool PrintObject::has_support_material() const
{
    return m_config.support_material
        || m_config.raft_layers > 0
        || m_config.support_material_enforce_layers > 0;
}

static const PrintRegion* first_printing_region(const PrintObject &print_object)
{
    for (size_t idx_region = 0; idx_region < print_object.region_volumes.size(); ++ idx_region)
    	if (!print_object.region_volumes.empty())
    		return print_object.print()->regions()[idx_region];
    return nullptr;
}

// This function analyzes slices of a region (SurfaceCollection slices).
// Each region slice (instance of Surface) is analyzed, whether it is supported or whether it is the top surface.
// Initially all slices are of type stInternal.
// Slices are compared against the top / bottom slices and regions and classified to the following groups:
// stTop          - Part of a region, which is not covered by any upper layer. This surface will be filled with a top solid infill.
// stBottomBridge - Part of a region, which is not fully supported, but it hangs in the air, or it hangs losely on a support or a raft.
// stBottom       - Part of a region, which is not supported by the same region, but it is supported either by another region, or by a soluble interface layer.
// stInternal     - Part of a region, which is supported by the same region type.
// If a part of a region is of stBottom and stTop, the stBottom wins.
void PrintObject::detect_surfaces_type()
{
    BOOST_LOG_TRIVIAL(info) << "Detecting solid surfaces..." << log_memory_info();

    // Interface shells: the intersecting parts are treated as self standing objects supporting each other.
    // Each of the objects will have a full number of top / bottom layers, even if these top / bottom layers
    // are completely hidden inside a collective body of intersecting parts.
    // This is useful if one of the parts is to be dissolved, or if it is transparent and the internal shells
    // should be visible.
    bool spiral_vase      = this->print()->config().spiral_vase.value;
    bool interface_shells = ! spiral_vase && m_config.interface_shells.value;
    size_t num_layers     = spiral_vase ? first_printing_region(*this)->config().bottom_solid_layers : m_layers.size();

    for (size_t idx_region = 0; idx_region < this->region_volumes.size(); ++ idx_region) {
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << idx_region << " in parallel - start";
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        for (Layer *layer : m_layers)
            layer->m_regions[idx_region]->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

        // If interface shells are allowed, the region->surfaces cannot be overwritten as they may be used by other threads.
        // Cache the result of the following parallel_loop.
        std::vector<Surfaces> surfaces_new;
        if (interface_shells)
            surfaces_new.assign(num_layers, Surfaces());

        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, 
            	spiral_vase ?
            		// In spiral vase mode, reserve the last layer for the top surface if more than 1 layer is planned for the vase bottom.
            		((num_layers > 1) ? num_layers - 1 : num_layers) :
            		// In non-spiral vase mode, go over all layers.
            		m_layers.size()),
            [this, idx_region, interface_shells, &surfaces_new](const tbb::blocked_range<size_t>& range) {
                // If we have raft layers, consider bottom layer as a bridge just like any other bottom surface lying on the void.
                SurfaceType surface_type_bottom_1st =
                    (m_config.raft_layers.value > 0 && m_config.support_material_contact_distance.value > 0) ?
                    stBottomBridge : stBottom;
                // If we have soluble support material, don't bridge. The overhang will be squished against a soluble layer separating
                // the support from the print.
                SurfaceType surface_type_bottom_other =
                    (m_config.support_material.value && m_config.support_material_contact_distance.value == 0) ?
                    stBottom : stBottomBridge;
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Detecting solid surfaces for region " << idx_region << " and layer " << layer->print_z;
                    Layer       *layer  = m_layers[idx_layer];
                    LayerRegion *layerm = layer->m_regions[idx_region];
                    // comparison happens against the *full* slices (considering all regions)
                    // unless internal shells are requested
                    Layer       *upper_layer = (idx_layer + 1 < this->layer_count()) ? m_layers[idx_layer + 1] : nullptr;
                    Layer       *lower_layer = (idx_layer > 0) ? m_layers[idx_layer - 1] : nullptr;
                    // collapse very narrow parts (using the safety offset in the diff is not enough)
                    float        offset = layerm->flow(frExternalPerimeter).scaled_width() / 10.f;

                    Polygons     layerm_slices_surfaces = to_polygons(layerm->slices.surfaces);

                    // find top surfaces (difference between current surfaces
                    // of current layer and upper one)
                    Surfaces top;
                    if (upper_layer) {
                        Polygons upper_slices = interface_shells ? 
                            to_polygons(upper_layer->m_regions[idx_region]->slices.surfaces) : 
                            to_polygons(upper_layer->lslices);
                        surfaces_append(top,
                            //FIXME implement offset2_ex working over ExPolygons, that should be a bit more efficient than calling offset_ex twice.
                            offset_ex(offset_ex(diff_ex(layerm_slices_surfaces, upper_slices, true), -offset), offset),
                            stTop);
                    } else {
                        // if no upper layer, all surfaces of this one are solid
                        // we clone surfaces because we're going to clear the slices collection
                        top = layerm->slices.surfaces;
                        for (Surface &surface : top)
                            surface.surface_type = stTop;
                    }
                    
                    // Find bottom surfaces (difference between current surfaces of current layer and lower one).
                    Surfaces bottom;
                    if (lower_layer) {
#if 0
                        //FIXME Why is this branch failing t\multi.t ?
                        Polygons lower_slices = interface_shells ? 
                            to_polygons(lower_layer->get_region(idx_region)->slices.surfaces) : 
                            to_polygons(lower_layer->slices);
                        surfaces_append(bottom,
                            offset2_ex(diff(layerm_slices_surfaces, lower_slices, true), -offset, offset),
                            surface_type_bottom_other);
#else
                        // Any surface lying on the void is a true bottom bridge (an overhang)
                        surfaces_append(
                            bottom,
                            offset2_ex(
                                diff(layerm_slices_surfaces, to_polygons(lower_layer->lslices), true), 
                                -offset, offset),
                            surface_type_bottom_other);
                        // if user requested internal shells, we need to identify surfaces
                        // lying on other slices not belonging to this region
                        if (interface_shells) {
                            // non-bridging bottom surfaces: any part of this layer lying 
                            // on something else, excluding those lying on our own region
                            surfaces_append(
                                bottom,
                                offset2_ex(
                                    diff(
                                        intersection(layerm_slices_surfaces, to_polygons(lower_layer->lslices)), // supported
                                        to_polygons(lower_layer->m_regions[idx_region]->slices.surfaces), 
                                        true), 
                                    -offset, offset),
                                stBottom);
                        }
#endif
                    } else {
                        // if no lower layer, all surfaces of this one are solid
                        // we clone surfaces because we're going to clear the slices collection
                        bottom = layerm->slices.surfaces;
                        for (Surface &surface : bottom)
                            surface.surface_type = surface_type_bottom_1st;
                    }
                    
                    // now, if the object contained a thin membrane, we could have overlapping bottom
                    // and top surfaces; let's do an intersection to discover them and consider them
                    // as bottom surfaces (to allow for bridge detection)
                    if (! top.empty() && ! bottom.empty()) {
        //                Polygons overlapping = intersection(to_polygons(top), to_polygons(bottom));
        //                Slic3r::debugf "  layer %d contains %d membrane(s)\n", $layerm->layer->id, scalar(@$overlapping)
        //                    if $Slic3r::debug;
                        Polygons top_polygons = to_polygons(std::move(top));
                        top.clear();
                        surfaces_append(top,
                            diff_ex(top_polygons, to_polygons(bottom), false),
                            stTop);
                    }

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        static int iRun = 0;
                        std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(top),                           SVG::ExPolygonAttributes("green")));
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(bottom),                        SVG::ExPolygonAttributes("brown")));
                        expolygons_with_attributes.emplace_back(std::make_pair(to_expolygons(layerm->slices.surfaces),  SVG::ExPolygonAttributes("black")));
                        SVG::export_expolygons(debug_out_path("1_detect_surfaces_type_%d_region%d-layer_%f.svg", iRun ++, idx_region, layer->print_z).c_str(), expolygons_with_attributes);
                    }
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    
                    // save surfaces to layer
                    Surfaces &surfaces_out = interface_shells ? surfaces_new[idx_layer] : layerm->slices.surfaces;
                    surfaces_out.clear();

                    // find internal surfaces (difference between top/bottom surfaces and others)
                    {
                        Polygons topbottom = to_polygons(top);
                        polygons_append(topbottom, to_polygons(bottom));
                        surfaces_append(surfaces_out,
                            diff_ex(layerm_slices_surfaces, topbottom, false),
                            stInternal);
                    }

                    surfaces_append(surfaces_out, std::move(top));
                    surfaces_append(surfaces_out, std::move(bottom));
                    
        //            Slic3r::debugf "  layer %d has %d bottom, %d top and %d internal surfaces\n",
        //                $layerm->layer->id, scalar(@bottom), scalar(@top), scalar(@internal) if $Slic3r::debug;

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_slices_to_svg_debug("detect_surfaces_type-final");
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                }
            }
        ); // for each layer of a region
        m_print->throw_if_canceled();

        if (interface_shells) {
            // Move surfaces_new to layerm->slices.surfaces
            for (size_t idx_layer = 0; idx_layer < num_layers; ++ idx_layer)
                m_layers[idx_layer]->m_regions[idx_region]->slices.surfaces = std::move(surfaces_new[idx_layer]);
        }

        if (spiral_vase) {
        	if (num_layers > 1)
	        	// Turn the last bottom layer infill to a top infill, so it will be extruded with a proper pattern.
	        	m_layers[num_layers - 1]->m_regions[idx_region]->slices.set_type(stTop);
	        for (size_t i = num_layers; i < m_layers.size(); ++ i)
	        	m_layers[i]->m_regions[idx_region]->slices.set_type(stInternal);
        }

        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << idx_region << " - clipping in parallel - start";
        // Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, idx_region, interface_shells](const tbb::blocked_range<size_t>& range) {
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    LayerRegion *layerm = m_layers[idx_layer]->m_regions[idx_region];
                    layerm->slices_to_fill_surfaces_clipped();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                } // for each layer of a region
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << idx_region << " - clipping in parallel - end";
    } // for each this->print->region_count

    // Mark the object to have the region slices classified (typed, which also means they are split based on whether they are supported, bridging, top layers etc.)
    m_typed_slices = true;
}

void PrintObject::process_external_surfaces()
{
    BOOST_LOG_TRIVIAL(info) << "Processing external surfaces..." << log_memory_info();

    // Cached surfaces covered by some extrusion, defining regions, over which the from the surfaces one layer higher are allowed to expand.
    std::vector<Polygons> surfaces_covered;
    // Is there any printing region, that has zero infill? If so, then we don't want the expansion to be performed over the complete voids, but only
    // over voids, which are supported by the layer below.
    bool 				  has_voids = false;
	for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id)
		if (! this->region_volumes.empty() && this->print()->regions()[region_id]->config().fill_density == 0) {
			has_voids = true;
			break;
		}
	if (has_voids && m_layers.size() > 1) {
	    // All but stInternal fill surfaces will get expanded and possibly trimmed.
	    std::vector<unsigned char> layer_expansions_and_voids(m_layers.size(), false);
	    for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
	    	const Layer *layer = m_layers[layer_idx];
	    	bool expansions = false;
	    	bool voids      = false;
	    	for (const LayerRegion *layerm : layer->regions()) {
	    		for (const Surface &surface : layerm->fill_surfaces.surfaces) {
	    			if (surface.surface_type == stInternal)
	    				voids = true;
	    			else
	    				expansions = true;
	    			if (voids && expansions) {
	    				layer_expansions_and_voids[layer_idx] = true;
	    				goto end;
	    			}
	    		}
	    	}
		end:;
		}
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - start";
	    surfaces_covered.resize(m_layers.size() - 1, Polygons());
    	auto unsupported_width = - float(scale_(0.3 * EXTERNAL_INFILL_MARGIN));
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size() - 1),
	        [this, &surfaces_covered, &layer_expansions_and_voids, unsupported_width](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
	            	if (layer_expansions_and_voids[layer_idx + 1]) {
		                m_print->throw_if_canceled();
		                Polygons voids;
		                for (const LayerRegion *layerm : m_layers[layer_idx]->regions()) {
		                	if (layerm->region()->config().fill_density.value == 0.)
		                		for (const Surface &surface : layerm->fill_surfaces.surfaces)
		                			// Shrink the holes, let the layer above expand slightly inside the unsupported areas.
		                			polygons_append(voids, offset(surface.expolygon, unsupported_width));
		                }
		                surfaces_covered[layer_idx] = diff(to_polygons(this->m_layers[layer_idx]->lslices), voids);
	            	}
	        }
	    );
	    m_print->throw_if_canceled();
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - end";
	}

	for (size_t region_id = 0; region_id < this->region_volumes.size(); ++region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, &surfaces_covered, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Processing external surface, layer" << m_layers[layer_idx]->print_z;
                    m_layers[layer_idx]->get_region((int)region_id)->process_external_surfaces(
                    	(layer_idx == 0) ? nullptr : m_layers[layer_idx - 1],
                    	(layer_idx == 0 || surfaces_covered.empty() || surfaces_covered[layer_idx - 1].empty()) ? nullptr : &surfaces_covered[layer_idx - 1]);
                }
            }
        );
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - end";
    }
}

void PrintObject::discover_vertical_shells()
{
    PROFILE_FUNC();

    BOOST_LOG_TRIVIAL(info) << "Discovering vertical shells..." << log_memory_info();

    struct DiscoverVerticalShellsCacheEntry
    {
        // Collected polygons, offsetted
        Polygons    top_surfaces;
        Polygons    bottom_surfaces;
        Polygons    holes;
    };
    bool     spiral_vase      = this->print()->config().spiral_vase.value;
    size_t   num_layers       = spiral_vase ? first_printing_region(*this)->config().bottom_solid_layers : m_layers.size();
    coordf_t min_layer_height = this->slicing_parameters().min_layer_height;
    // Does this region possibly produce more than 1 top or bottom layer?
    auto has_extra_layers_fn = [min_layer_height](const PrintRegionConfig &config) {
	    auto num_extra_layers = [min_layer_height](int num_solid_layers, coordf_t min_shell_thickness) {
	    	if (num_solid_layers == 0)
	    		return 0;
	    	int n = num_solid_layers - 1;
	    	int n2 = int(ceil(min_shell_thickness / min_layer_height));
	    	return std::max(n, n2 - 1);
	    };
    	return num_extra_layers(config.top_solid_layers, config.top_solid_min_thickness) +
	    	   num_extra_layers(config.bottom_solid_layers, config.bottom_solid_min_thickness) > 0;
    };
    std::vector<DiscoverVerticalShellsCacheEntry> cache_top_botom_regions(num_layers, DiscoverVerticalShellsCacheEntry());
    bool top_bottom_surfaces_all_regions = this->region_volumes.size() > 1 && ! m_config.interface_shells.value;
    if (top_bottom_surfaces_all_regions) {
        // This is a multi-material print and interface_shells are disabled, meaning that the vertical shell thickness
        // is calculated over all materials.
        // Is the "ensure vertical wall thickness" applicable to any region?
        bool has_extra_layers = false;
        for (size_t idx_region = 0; idx_region < this->region_volumes.size(); ++idx_region) {
            const PrintRegionConfig &config = m_print->get_region(idx_region)->config();
            if (config.ensure_vertical_shell_thickness.value && has_extra_layers_fn(config)) {
                has_extra_layers = true;
                break;
            }
        }
        if (! has_extra_layers)
            // The "ensure vertical wall thickness" feature is not applicable to any of the regions. Quit.
            return;
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - start : cache top / bottom";
        //FIXME Improve the heuristics for a grain size.
        size_t grain_size = std::max(num_layers / 16, size_t(1));
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                const SurfaceType surfaces_bottom[2] = { stBottom, stBottomBridge };
                const size_t num_regions = this->region_volumes.size();
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    const Layer                      &layer = *m_layers[idx_layer];
                    DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[idx_layer];
                    // Simulate single set of perimeters over all merged regions.
                    float                             perimeter_offset = 0.f;
                    float                             perimeter_min_spacing = FLT_MAX;
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    static size_t debug_idx = 0;
                    ++ debug_idx;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    for (size_t idx_region = 0; idx_region < num_regions; ++ idx_region) {
                        LayerRegion &layerm                       = *layer.m_regions[idx_region];
                        float        min_perimeter_infill_spacing = float(layerm.flow(frSolidInfill).scaled_spacing()) * 1.05f;
                        // Top surfaces.
                        append(cache.top_surfaces, offset(to_expolygons(layerm.slices.filter_by_type(stTop)), min_perimeter_infill_spacing));
                        append(cache.top_surfaces, offset(to_expolygons(layerm.fill_surfaces.filter_by_type(stTop)), min_perimeter_infill_spacing));
                        // Bottom surfaces.
                        append(cache.bottom_surfaces, offset(to_expolygons(layerm.slices.filter_by_types(surfaces_bottom, 2)), min_perimeter_infill_spacing));
                        append(cache.bottom_surfaces, offset(to_expolygons(layerm.fill_surfaces.filter_by_types(surfaces_bottom, 2)), min_perimeter_infill_spacing));
                        // Calculate the maximum perimeter offset as if the slice was extruded with a single extruder only.
                        // First find the maxium number of perimeters per region slice.
                        unsigned int perimeters = 0;
                        for (Surface &s : layerm.slices.surfaces)
                            perimeters = std::max<unsigned int>(perimeters, s.extra_perimeters);
                        perimeters += layerm.region()->config().perimeters.value;
                        // Then calculate the infill offset.
                        if (perimeters > 0) {
                            Flow extflow = layerm.flow(frExternalPerimeter);
                            Flow flow    = layerm.flow(frPerimeter);
                            perimeter_offset = std::max(perimeter_offset,
                                0.5f * float(extflow.scaled_width() + extflow.scaled_spacing()) + (float(perimeters) - 1.f) * flow.scaled_spacing());
                            perimeter_min_spacing = std::min(perimeter_min_spacing, float(std::min(extflow.scaled_spacing(), flow.scaled_spacing())));
                        }
                        polygons_append(cache.holes, to_polygons(layerm.fill_expolygons));
                    }
                    // Save some computing time by reducing the number of polygons.
                    cache.top_surfaces    = union_(cache.top_surfaces,    false);
                    cache.bottom_surfaces = union_(cache.bottom_surfaces, false);
                    // For a multi-material print, simulate perimeter / infill split as if only a single extruder has been used for the whole print.
                    if (perimeter_offset > 0.) {
                        // The layer.lslices are forced to merge by expanding them first.
                        polygons_append(cache.holes, offset(offset_ex(layer.lslices, 0.3f * perimeter_min_spacing), - perimeter_offset - 0.3f * perimeter_min_spacing));
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                        {
                            Slic3r::SVG svg(debug_out_path("discover_vertical_shells-extra-holes-%d.svg", debug_idx), get_extents(layer.lslices));
                            svg.draw(layer.lslices, "blue");
                            svg.draw(union_ex(cache.holes), "red");
                            svg.draw_outline(union_ex(cache.holes), "black", "blue", scale_(0.05));
                            svg.Close(); 
                        }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    }
                    cache.holes = union_(cache.holes, false);
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - end : cache top / bottom";
    }

    for (size_t idx_region = 0; idx_region < this->region_volumes.size(); ++ idx_region) {
        PROFILE_BLOCK(discover_vertical_shells_region);

        const PrintRegion &region = *m_print->get_region(idx_region);
        if (! region.config().ensure_vertical_shell_thickness.value)
            // This region will be handled by discover_horizontal_shells().
            continue;
        if (! has_extra_layers_fn(region.config()))
            // Zero or 1 layer, there is no additional vertical wall thickness enforced.
            continue;

        //FIXME Improve the heuristics for a grain size.
        size_t grain_size = std::max(num_layers / 16, size_t(1));

        if (! top_bottom_surfaces_all_regions) {
            // This is either a single material print, or a multi-material print and interface_shells are enabled, meaning that the vertical shell thickness
            // is calculated over a single material.
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << idx_region << " in parallel - start : cache top / bottom";
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, num_layers, grain_size),
                [this, idx_region, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                    const SurfaceType surfaces_bottom[2] = { stBottom, stBottomBridge };
                    for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                        m_print->throw_if_canceled();
                        Layer       &layer                        = *m_layers[idx_layer];
                        LayerRegion &layerm                       = *layer.m_regions[idx_region];
                        float        min_perimeter_infill_spacing = float(layerm.flow(frSolidInfill).scaled_spacing()) * 1.05f;
                        // Top surfaces.
                        auto &cache = cache_top_botom_regions[idx_layer];
                        cache.top_surfaces = offset(to_expolygons(layerm.slices.filter_by_type(stTop)), min_perimeter_infill_spacing);
                        append(cache.top_surfaces, offset(to_expolygons(layerm.fill_surfaces.filter_by_type(stTop)), min_perimeter_infill_spacing));
                        // Bottom surfaces.
                        cache.bottom_surfaces = offset(to_expolygons(layerm.slices.filter_by_types(surfaces_bottom, 2)), min_perimeter_infill_spacing);
                        append(cache.bottom_surfaces, offset(to_expolygons(layerm.fill_surfaces.filter_by_types(surfaces_bottom, 2)), min_perimeter_infill_spacing));
                        // Holes over all regions. Only collect them once, they are valid for all idx_region iterations.
                        if (cache.holes.empty()) {
                            for (size_t idx_region = 0; idx_region < layer.regions().size(); ++ idx_region)
                                polygons_append(cache.holes, to_polygons(layer.regions()[idx_region]->fill_expolygons));
                        }
                    }
                });
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << idx_region << " in parallel - end : cache top / bottom";
        }

        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << idx_region << " in parallel - start : ensure vertical wall thickness";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, idx_region, &cache_top_botom_regions]
            (const tbb::blocked_range<size_t>& range) {
                // printf("discover_vertical_shells from %d to %d\n", range.begin(), range.end());
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    PROFILE_BLOCK(discover_vertical_shells_region_layer);
                    m_print->throw_if_canceled();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        			static size_t debug_idx = 0;
        			++ debug_idx;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    Layer       	        *layer          = m_layers[idx_layer];
                    LayerRegion 	        *layerm         = layer->m_regions[idx_region];
                    const PrintRegionConfig &region_config  = layerm->region()->config();

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_slices_to_svg_debug("4_discover_vertical_shells-initial");
                    layerm->export_region_fill_surfaces_to_svg_debug("4_discover_vertical_shells-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    Flow         solid_infill_flow   = layerm->flow(frSolidInfill);
                    coord_t      infill_line_spacing = solid_infill_flow.scaled_spacing(); 
                    // Find a union of perimeters below / above this surface to guarantee a minimum shell thickness.
                    Polygons shell;
                    Polygons holes;
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    ExPolygons shell_ex;
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    float min_perimeter_infill_spacing = float(infill_line_spacing) * 1.05f;
                    {
                        PROFILE_BLOCK(discover_vertical_shells_region_layer_collect);
#if 0
// #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                        {
        					Slic3r::SVG svg_cummulative(debug_out_path("discover_vertical_shells-perimeters-before-union-run%d.svg", debug_idx), this->bounding_box());
                            for (int n = (int)idx_layer - n_extra_bottom_layers; n <= (int)idx_layer + n_extra_top_layers; ++ n) {
                                if (n < 0 || n >= (int)m_layers.size())
                                    continue;
                                ExPolygons &expolys = m_layers[n]->perimeter_expolygons;
                                for (size_t i = 0; i < expolys.size(); ++ i) {
        							Slic3r::SVG svg(debug_out_path("discover_vertical_shells-perimeters-before-union-run%d-layer%d-expoly%d.svg", debug_idx, n, i), get_extents(expolys[i]));
                                    svg.draw(expolys[i]);
                                    svg.draw_outline(expolys[i].contour, "black", scale_(0.05));
                                    svg.draw_outline(expolys[i].holes, "blue", scale_(0.05));
                                    svg.Close();

                                    svg_cummulative.draw(expolys[i]);
                                    svg_cummulative.draw_outline(expolys[i].contour, "black", scale_(0.05));
                                    svg_cummulative.draw_outline(expolys[i].holes, "blue", scale_(0.05));
                                }
                            }
                        }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
			        	polygons_append(holes, cache_top_botom_regions[idx_layer].holes);
			        	if (int n_top_layers = region_config.top_solid_layers.value; n_top_layers > 0) {
                            // Gather top regions projected to this layer.
                            coordf_t print_z = layer->print_z;
	                        for (int i = int(idx_layer) + 1; 
	                        	i < int(cache_top_botom_regions.size()) && 
	                        		(i < int(idx_layer) + n_top_layers ||
	                        		 m_layers[i]->print_z - print_z < region_config.top_solid_min_thickness - EPSILON);
	                        	++ i) {
	                            const DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[i];
								if (! holes.empty())
									holes = intersection(holes, cache.holes);
								if (! cache.top_surfaces.empty()) {
		                            polygons_append(shell, cache.top_surfaces);
		                            // Running the union_ using the Clipper library piece by piece is cheaper 
		                            // than running the union_ all at once.
	                               shell = union_(shell, false);
	                           }
	                        }
	                    }
	                    if (int n_bottom_layers = region_config.bottom_solid_layers.value; n_bottom_layers > 0) {
                            // Gather bottom regions projected to this layer.
                            coordf_t bottom_z = layer->bottom_z();
	                        for (int i = int(idx_layer) - 1;
	                        	i >= 0 &&
	                        		(i > int(idx_layer) - n_bottom_layers ||
	                        		 bottom_z - m_layers[i]->bottom_z() < region_config.bottom_solid_min_thickness - EPSILON);
	                        	-- i) {
	                            const DiscoverVerticalShellsCacheEntry &cache = cache_top_botom_regions[i];
								if (! holes.empty())
									holes = intersection(holes, cache.holes);
								if (! cache.bottom_surfaces.empty()) {
		                            polygons_append(shell, cache.bottom_surfaces);
		                            // Running the union_ using the Clipper library piece by piece is cheaper 
		                            // than running the union_ all at once.
		                            shell = union_(shell, false);
		                        }
	                        }
	                    }
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                        {
        					Slic3r::SVG svg(debug_out_path("discover_vertical_shells-perimeters-before-union-%d.svg", debug_idx), get_extents(shell));
                            svg.draw(shell);
                            svg.draw_outline(shell, "black", scale_(0.05));
                            svg.Close(); 
                        }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
#if 0
                        {
                            PROFILE_BLOCK(discover_vertical_shells_region_layer_shell_);
        //                    shell = union_(shell, true);
                            shell = union_(shell, false); 
                        }
#endif
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                        shell_ex = union_ex(shell, true);
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    }

                    //if (shell.empty())
                    //    continue;

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-perimeters-after-union-%d.svg", debug_idx), get_extents(shell));
                        svg.draw(shell_ex);
                        svg.draw_outline(shell_ex, "black", "blue", scale_(0.05));
                        svg.Close();  
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-internal-wshell-%d.svg", debug_idx), get_extents(shell));
                        svg.draw(layerm->fill_surfaces.filter_by_type(stInternal), "yellow", 0.5);
                        svg.draw_outline(layerm->fill_surfaces.filter_by_type(stInternal), "black", "blue", scale_(0.05));
                        svg.draw(shell_ex, "blue", 0.5);
                        svg.draw_outline(shell_ex, "black", "blue", scale_(0.05));
                        svg.Close();
                    } 
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-internalvoid-wshell-%d.svg", debug_idx), get_extents(shell));
                        svg.draw(layerm->fill_surfaces.filter_by_type(stInternalVoid), "yellow", 0.5);
                        svg.draw_outline(layerm->fill_surfaces.filter_by_type(stInternalVoid), "black", "blue", scale_(0.05));
                        svg.draw(shell_ex, "blue", 0.5);
                        svg.draw_outline(shell_ex, "black", "blue", scale_(0.05));
                        svg.Close();
                    } 
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-internalvoid-wshell-%d.svg", debug_idx), get_extents(shell));
                        svg.draw(layerm->fill_surfaces.filter_by_type(stInternalVoid), "yellow", 0.5);
                        svg.draw_outline(layerm->fill_surfaces.filter_by_type(stInternalVoid), "black", "blue", scale_(0.05));
                        svg.draw(shell_ex, "blue", 0.5);
                        svg.draw_outline(shell_ex, "black", "blue", scale_(0.05)); 
                        svg.Close();
                    } 
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // Trim the shells region by the internal & internal void surfaces.
                    const SurfaceType surfaceTypesInternal[] = { stInternal, stInternalVoid, stInternalSolid };
                    const Polygons    polygonsInternal = to_polygons(layerm->fill_surfaces.filter_by_types(surfaceTypesInternal, 3));
                    shell = intersection(shell, polygonsInternal, true);
                    polygons_append(shell, diff(polygonsInternal, holes));
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
                    shell = offset(offset_ex(union_ex(shell), - 0.5f * min_perimeter_infill_spacing), 0.8f * min_perimeter_infill_spacing, ClipperLib::jtSquare);
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
                    Polygons too_narrow = diff(shell, offset2(shell, -margin, margin, ClipperLib::jtMiter, 5.), true);
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
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-regularized-%d.svg", debug_idx), get_extents(shell_before));
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
                        SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal-%d.svg", debug_idx), get_extents(shell), new_internal, "black", "blue", scale_(0.05));
        				SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_void-%d.svg", debug_idx), get_extents(shell), new_internal_void, "black", "blue", scale_(0.05));
        				SVG::export_expolygons(debug_out_path("discover_vertical_shells-new_internal_solid-%d.svg", debug_idx), get_extents(shell), new_internal_solid, "black", "blue", scale_(0.05));
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // Assign resulting internal surfaces to layer.
                    const SurfaceType surfaceTypesKeep[] = { stTop, stBottom, stBottomBridge };
                    layerm->fill_surfaces.keep_types(surfaceTypesKeep, sizeof(surfaceTypesKeep)/sizeof(SurfaceType));
                    layerm->fill_surfaces.append(new_internal,       stInternal);
                    layerm->fill_surfaces.append(new_internal_void,  stInternalVoid);
                    layerm->fill_surfaces.append(new_internal_solid, stInternalSolid);
                } // for each layer
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << idx_region << " in parallel - end";

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
		for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++idx_layer) {
			LayerRegion *layerm = m_layers[idx_layer]->get_region(idx_region);
			layerm->export_region_slices_to_svg_debug("4_discover_vertical_shells-final");
			layerm->export_region_fill_surfaces_to_svg_debug("4_discover_vertical_shells-final");
		}
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
    } // for each region

    // Write the profiler measurements to file
//    PROFILE_UPDATE();
//    PROFILE_OUTPUT(debug_out_path("discover_vertical_shells-profile.txt").c_str());
}

/* This method applies bridge flow to the first internal solid layer above
   sparse infill */
void PrintObject::bridge_over_infill()
{
    BOOST_LOG_TRIVIAL(info) << "Bridge over infill..." << log_memory_info();

    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        const PrintRegion &region = *m_print->regions()[region_id];
        
        // skip bridging in case there are no voids
        if (region.config().fill_density.value == 100) continue;
        
        // get bridge flow
        Flow bridge_flow = region.flow(
            frSolidInfill,
            -1,     // layer height, not relevant for bridge flow
            true,   // bridge
            false,  // first layer
            -1,     // custom width, not relevant for bridge flow
            *this
        );
        
		for (LayerPtrs::iterator layer_it = m_layers.begin(); layer_it != m_layers.end(); ++ layer_it) {
            // skip first layer
			if (layer_it == m_layers.begin())
                continue;
            
            Layer* layer        = *layer_it;
            LayerRegion* layerm = layer->m_regions[region_id];
            
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
                for (int i = int(layer_it - m_layers.begin()) - 1; i >= 0; --i) {
                    const Layer* lower_layer = m_layers[i];
                    
                    // stop iterating if layer is lower than bottom_z
                    if (lower_layer->print_z < bottom_z) break;
                    
                    // iterate through regions and collect internal surfaces
                    Polygons lower_internal;
                    for (LayerRegion *lower_layerm : lower_layer->m_regions)
                        lower_layerm->fill_surfaces.filter_by_type(stInternal, &lower_internal);
                    
                    // intersect such lower internal surfaces with the candidate solid surfaces
                    to_bridge_pp = intersection(to_bridge_pp, lower_internal);
                }
                
                // there's no point in bridging too thin/short regions
                //FIXME Vojtech: The offset2 function is not a geometric offset, 
                // therefore it may create 1) gaps, and 2) sharp corners, which are outside the original contour.
                // The gaps will be filled by a separate region, which makes the infill less stable and it takes longer.
                {
                    float min_width = float(bridge_flow.scaled_width()) * 3.f;
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
            ExPolygons not_to_bridge = diff_ex(internal_solid, to_polygons(to_bridge), true);
            to_bridge = intersection_ex(to_polygons(to_bridge), internal_solid, true);
            // build the new collection of fill_surfaces
            layerm->fill_surfaces.remove_type(stInternalSolid);
            for (ExPolygon &ex : to_bridge)
                layerm->fill_surfaces.surfaces.push_back(Surface(stInternalBridge, ex));
            for (ExPolygon &ex : not_to_bridge)
                layerm->fill_surfaces.surfaces.push_back(Surface(stInternalSolid, ex));            
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
                                surface_type    => stInternalVoid,
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
            m_print->throw_if_canceled();
        }
    }
}

static void clamp_exturder_to_default(ConfigOptionInt &opt, size_t num_extruders)
{
    if (opt.value > (int)num_extruders)
        // assign the default extruder
        opt.value = 1;
}

PrintObjectConfig PrintObject::object_config_from_model_object(const PrintObjectConfig &default_object_config, const ModelObject &object, size_t num_extruders)
{
    PrintObjectConfig config = default_object_config;
    normalize_and_apply_config(config, object.config);
    // Clamp invalid extruders to the default extruder (with index 1).
    clamp_exturder_to_default(config.support_material_extruder,           num_extruders);
    clamp_exturder_to_default(config.support_material_interface_extruder, num_extruders);
    return config;
}

PrintRegionConfig PrintObject::region_config_from_model_volume(const PrintRegionConfig &default_region_config, const DynamicPrintConfig *layer_range_config, const ModelVolume &volume, size_t num_extruders)
{
    PrintRegionConfig config = default_region_config;
    normalize_and_apply_config(config, volume.get_object()->config);
    if (layer_range_config != nullptr)
    	normalize_and_apply_config(config, *layer_range_config);
    normalize_and_apply_config(config, volume.config);
    if (! volume.material_id().empty())
        normalize_and_apply_config(config, volume.material()->config);
    // Clamp invalid extruders to the default extruder (with index 1).
    clamp_exturder_to_default(config.infill_extruder,       num_extruders);
    clamp_exturder_to_default(config.perimeter_extruder,    num_extruders);
    clamp_exturder_to_default(config.solid_infill_extruder, num_extruders);
    return config;
}

void PrintObject::update_slicing_parameters()
{
    if (! m_slicing_params.valid)
        m_slicing_params = SlicingParameters::create_from_config(
            this->print()->config(), m_config, unscale<double>(this->height()), this->object_extruders());
}

SlicingParameters PrintObject::slicing_parameters(const DynamicPrintConfig& full_config, const ModelObject& model_object, float object_max_z)
{
	PrintConfig         print_config;
	PrintObjectConfig   object_config;
	PrintRegionConfig   default_region_config;
	print_config.apply(full_config, true);
	object_config.apply(full_config, true);
	default_region_config.apply(full_config, true);
	size_t              num_extruders = print_config.nozzle_diameter.size();
	object_config = object_config_from_model_object(object_config, model_object, num_extruders);

	std::vector<unsigned int> object_extruders;
	for (const ModelVolume* model_volume : model_object.volumes)
		if (model_volume->is_model_part()) {
			PrintRegion::collect_object_printing_extruders(
				print_config,
				region_config_from_model_volume(default_region_config, nullptr, *model_volume, num_extruders),
				object_extruders);
			for (const std::pair<const t_layer_height_range, DynamicPrintConfig> &range_and_config : model_object.layer_config_ranges)
				if (range_and_config.second.has("perimeter_extruder") ||
					range_and_config.second.has("infill_extruder") ||
					range_and_config.second.has("solid_infill_extruder"))
					PrintRegion::collect_object_printing_extruders(
						print_config,
						region_config_from_model_volume(default_region_config, &range_and_config.second, *model_volume, num_extruders),
						object_extruders);
		}
    sort_remove_duplicates(object_extruders);

    if (object_max_z <= 0.f)
        object_max_z = (float)model_object.raw_bounding_box().size().z();
    return SlicingParameters::create_from_config(print_config, object_config, object_max_z, object_extruders);
}

// returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
std::vector<unsigned int> PrintObject::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(this->region_volumes.size() * 3);    
    for (size_t idx_region = 0; idx_region < this->region_volumes.size(); ++ idx_region)
        if (! this->region_volumes[idx_region].empty())
            m_print->get_region(idx_region)->collect_object_printing_extruders(extruders);
    sort_remove_duplicates(extruders);
    return extruders;
}

bool PrintObject::update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile)
{
    bool updated = false;

    if (layer_height_profile.empty()) {
        // use the constructor because the assignement is crashing on ASAN OsX
        layer_height_profile = std::vector<coordf_t>(model_object.layer_height_profile);
//        layer_height_profile = model_object.layer_height_profile;
        updated = true;
    }

    // Verify the layer_height_profile.
    if (! layer_height_profile.empty() && 
            // Must not be of even length.
            ((layer_height_profile.size() & 1) != 0 || 
            // Last entry must be at the top of the object.
             std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_parameters.object_print_z_height()) > 1e-3))
        layer_height_profile.clear();

    if (layer_height_profile.empty()) {
        //layer_height_profile = layer_height_profile_adaptive(slicing_parameters, model_object.layer_config_ranges, model_object.volumes);
        layer_height_profile = layer_height_profile_from_ranges(slicing_parameters, model_object.layer_config_ranges);
        updated = true;
    }
    return updated;
}

// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
void PrintObject::_slice(const std::vector<coordf_t> &layer_height_profile)
{
    BOOST_LOG_TRIVIAL(info) << "Slicing objects..." << log_memory_info();

    m_typed_slices = false;

#ifdef SLIC3R_PROFILE
    // Disable parallelization so the Shiny profiler works
    static tbb::task_scheduler_init *tbb_init = nullptr;
    tbb_init = new tbb::task_scheduler_init(1);
#endif

    // 1) Initialize layers and their slice heights.
    std::vector<float> slice_zs;
    {
        this->clear_layers();
        // Object layers (pairs of bottom/top Z coordinate), without the raft.
        std::vector<coordf_t> object_layers = generate_object_layers(m_slicing_params, layer_height_profile);
        // Reserve object layers for the raft. Last layer of the raft is the contact layer.
        int id = int(m_slicing_params.raft_layers());
        slice_zs.reserve(object_layers.size());
        Layer *prev = nullptr;
        for (size_t i_layer = 0; i_layer < object_layers.size(); i_layer += 2) {
            coordf_t lo = object_layers[i_layer];
            coordf_t hi = object_layers[i_layer + 1];
            coordf_t slice_z = 0.5 * (lo + hi);
            Layer *layer = this->add_layer(id ++, hi - lo, hi + m_slicing_params.object_print_z_min, slice_z);
            slice_zs.push_back(float(slice_z));
            if (prev != nullptr) {
                prev->upper_layer = layer;
                layer->lower_layer = prev;
            }
            // Make sure all layers contain layer region objects for all regions.
            for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id)
                layer->add_region(this->print()->regions()[region_id]);
            prev = layer;
        }
    }

    // Count model parts and modifier meshes, check whether the model parts are of the same region.
    int              all_volumes_single_region = -2; // not set yet
    bool 			 has_z_ranges  = false;
	size_t           num_volumes   = 0;
    size_t           num_modifiers = 0;
    for (int region_id = 0; region_id < (int)this->region_volumes.size(); ++ region_id) {
		int last_volume_id = -1;
        for (const std::pair<t_layer_height_range, int> &volume_and_range : this->region_volumes[region_id]) {
			const int		   volume_id    = volume_and_range.second;
			const ModelVolume *model_volume = this->model_object()->volumes[volume_id];
            if (model_volume->is_model_part()) {
				if (last_volume_id == volume_id) {
					has_z_ranges = true;
				} else {
					last_volume_id = volume_id;
					if (all_volumes_single_region == -2)
						// first model volume met
						all_volumes_single_region = region_id;
					else if (all_volumes_single_region != region_id)
						// multiple volumes met and they are not equal
						all_volumes_single_region = -1;
					++ num_volumes;
				}
            } else if (model_volume->is_modifier())
                ++ num_modifiers;
        }
    }
    assert(num_volumes > 0);
    
    // Slice all non-modifier volumes.
    bool clipped  = false;
    bool upscaled = false;
    auto slicing_mode = this->print()->config().spiral_vase ? SlicingMode::PositiveLargestContour : SlicingMode::Regular;
    if (! has_z_ranges && (! m_config.clip_multipart_objects.value || all_volumes_single_region >= 0)) {
        // Cheap path: Slice regions without mutual clipping.
        // The cheap path is possible if no clipping is allowed or if slicing volumes of just a single region.
        for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
            BOOST_LOG_TRIVIAL(debug) << "Slicing objects - region " << region_id;
            // slicing in parallel
            std::vector<ExPolygons> expolygons_by_layer = this->slice_region(region_id, slice_zs, slicing_mode);
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Slicing objects - append slices " << region_id << " start";
            for (size_t layer_id = 0; layer_id < expolygons_by_layer.size(); ++ layer_id)
                m_layers[layer_id]->regions()[region_id]->slices.append(std::move(expolygons_by_layer[layer_id]), stInternal);
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Slicing objects - append slices " << region_id << " end";
        }
    } else {
        // Expensive path: Slice one volume after the other in the order they are presented at the user interface,
        // clip the last volumes with the first.
        // First slice the volumes.
        struct SlicedVolume {
            SlicedVolume(int volume_id, int region_id, std::vector<ExPolygons> &&expolygons_by_layer) : 
                volume_id(volume_id), region_id(region_id), expolygons_by_layer(std::move(expolygons_by_layer)) {}
            int                     volume_id;
            int                     region_id;
            std::vector<ExPolygons> expolygons_by_layer;
        };
        std::vector<SlicedVolume> sliced_volumes;
        sliced_volumes.reserve(num_volumes);
		for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
			const std::vector<std::pair<t_layer_height_range, int>> &volumes_and_ranges = this->region_volumes[region_id];
			for (size_t i = 0; i < volumes_and_ranges.size(); ) {
				int 			   volume_id    = volumes_and_ranges[i].second;
				const ModelVolume *model_volume = this->model_object()->volumes[volume_id];
				if (model_volume->is_model_part()) {
					BOOST_LOG_TRIVIAL(debug) << "Slicing objects - volume " << volume_id;
					// Find the ranges of this volume. Ranges in volumes_and_ranges must not overlap for a single volume.
					std::vector<t_layer_height_range> ranges;
					ranges.emplace_back(volumes_and_ranges[i].first);
					size_t j = i + 1;
					for (; j < volumes_and_ranges.size() && volume_id == volumes_and_ranges[j].second; ++ j)
						if (! ranges.empty() && std::abs(ranges.back().second - volumes_and_ranges[j].first.first) < EPSILON)
							ranges.back().second = volumes_and_ranges[j].first.second;
						else
							ranges.emplace_back(volumes_and_ranges[j].first);
                    // slicing in parallel
					sliced_volumes.emplace_back(volume_id, (int)region_id, this->slice_volume(slice_zs, ranges, slicing_mode, *model_volume));
					i = j;
				} else
					++ i;
			}
		}
        // Second clip the volumes in the order they are presented at the user interface.
        BOOST_LOG_TRIVIAL(debug) << "Slicing objects - parallel clipping - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, slice_zs.size()),
            [this, &sliced_volumes, num_modifiers](const tbb::blocked_range<size_t>& range) {
                float delta   = float(scale_(m_config.xy_size_compensation.value));
                // Only upscale together with clipping if there are no modifiers, as the modifiers shall be applied before upscaling
                // (upscaling may grow the object outside of the modifier mesh).
                bool  upscale = delta > 0 && num_modifiers == 0;
                for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
                    m_print->throw_if_canceled();
                    // Trim volumes in a single layer, one by the other, possibly apply upscaling.
                    {
                        Polygons processed;
                        for (SlicedVolume &sliced_volume : sliced_volumes) 
                        	if (! sliced_volume.expolygons_by_layer.empty()) {
	                            ExPolygons slices = std::move(sliced_volume.expolygons_by_layer[layer_id]);
	                            if (upscale)
	                                slices = offset_ex(std::move(slices), delta);
	                            if (! processed.empty())
	                                // Trim by the slices of already processed regions.
	                                slices = diff_ex(to_polygons(std::move(slices)), processed);
	                            if (size_t(&sliced_volume - &sliced_volumes.front()) + 1 < sliced_volumes.size())
	                                // Collect the already processed regions to trim the to be processed regions.
	                                polygons_append(processed, slices);
	                            sliced_volume.expolygons_by_layer[layer_id] = std::move(slices);
	                        }
                    }
                    // Collect and union volumes of a single region.
                    for (int region_id = 0; region_id < (int)this->region_volumes.size(); ++ region_id) {
                        ExPolygons expolygons;
                        size_t     num_volumes = 0;
                        for (SlicedVolume &sliced_volume : sliced_volumes)
                            if (sliced_volume.region_id == region_id && ! sliced_volume.expolygons_by_layer.empty() && ! sliced_volume.expolygons_by_layer[layer_id].empty()) {
                                ++ num_volumes;
                                append(expolygons, std::move(sliced_volume.expolygons_by_layer[layer_id]));
                            }
                        if (num_volumes > 1)
                            // Merge the islands using a positive / negative offset.
                            expolygons = offset_ex(offset_ex(expolygons, float(scale_(EPSILON))), -float(scale_(EPSILON)));
                        m_layers[layer_id]->regions()[region_id]->slices.append(std::move(expolygons), stInternal);
                    }
                }
            });
        BOOST_LOG_TRIVIAL(debug) << "Slicing objects - parallel clipping - end";
        clipped  = true;
        upscaled = m_config.xy_size_compensation.value > 0 && num_modifiers == 0;
    }

    // Slice all modifier volumes.
    if (this->region_volumes.size() > 1) {
        for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
            BOOST_LOG_TRIVIAL(debug) << "Slicing modifier volumes - region " << region_id;
            // slicing in parallel
            std::vector<ExPolygons> expolygons_by_layer = this->slice_modifiers(region_id, slice_zs);
            m_print->throw_if_canceled();
            if (expolygons_by_layer.empty())
                continue;
            // loop through the other regions and 'steal' the slices belonging to this one
            BOOST_LOG_TRIVIAL(debug) << "Slicing modifier volumes - stealing " << region_id << " start";
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, m_layers.size()),
				[this, &expolygons_by_layer, region_id](const tbb::blocked_range<size_t>& range) {
                    for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
                        for (size_t other_region_id = 0; other_region_id < this->region_volumes.size(); ++ other_region_id) {
                            if (region_id == other_region_id)
                                continue;
                            Layer       *layer = m_layers[layer_id];
                            LayerRegion *layerm = layer->m_regions[region_id];
                            LayerRegion *other_layerm = layer->m_regions[other_region_id];
                            if (layerm == nullptr || other_layerm == nullptr || other_layerm->slices.empty() || expolygons_by_layer[layer_id].empty())
                                continue;
                            Polygons other_slices = to_polygons(other_layerm->slices);
                            ExPolygons my_parts = intersection_ex(other_slices, to_polygons(expolygons_by_layer[layer_id]));
                            if (my_parts.empty())
                                continue;
                            // Remove such parts from original region.
                            other_layerm->slices.set(diff_ex(other_slices, to_polygons(my_parts)), stInternal);
                            // Append new parts to our region.
                            layerm->slices.append(std::move(my_parts), stInternal);
                        }
                    }
                });
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Slicing modifier volumes - stealing " << region_id << " end";
        }
    }
    
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - removing top empty layers";
    while (! m_layers.empty()) {
        const Layer *layer = m_layers.back();
        if (! layer->empty())
            goto end;
        delete layer;
        m_layers.pop_back();
		if (! m_layers.empty())
			m_layers.back()->upper_layer = nullptr;
    }
    m_print->throw_if_canceled();
end:
    ;

    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - make_slices in parallel - begin";
    {
        // Compensation value, scaled.
        const float xy_compensation_scaled 	 			= float(scale_(m_config.xy_size_compensation.value));
        const float elephant_foot_compensation_scaled 	= (m_config.raft_layers == 0) ? 
        	// Only enable Elephant foot compensation if printing directly on the print bed.
            float(scale_(m_config.elefant_foot_compensation.value)) :
        	0.f;
        // Uncompensated slices for the first layer in case the Elephant foot compensation is applied.
	    ExPolygons  lslices_1st_layer;
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size()),
			[this, upscaled, clipped, xy_compensation_scaled, elephant_foot_compensation_scaled, &lslices_1st_layer]
				(const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
	                m_print->throw_if_canceled();
	                Layer *layer = m_layers[layer_id];
	                // Apply size compensation and perform clipping of multi-part objects.
	                float elfoot = (layer_id == 0) ? elephant_foot_compensation_scaled : 0.f;
	                if (layer->m_regions.size() == 1) {
	                	assert(! upscaled);
	                	assert(! clipped);
	                    // Optimized version for a single region layer.
	                    // Single region, growing or shrinking.
	                    LayerRegion *layerm = layer->m_regions.front();
	                    if (elfoot > 0) {
		                    // Apply the elephant foot compensation and store the 1st layer slices without the Elephant foot compensation applied.
		                    lslices_1st_layer = to_expolygons(std::move(layerm->slices.surfaces));
		                    float delta = xy_compensation_scaled;
	                        if (delta > elfoot) {
	                            delta -= elfoot;
	                            elfoot = 0.f;
	                        } else if (delta > 0)
	                            elfoot -= delta;
							layerm->slices.set(
								union_ex(
									Slic3r::elephant_foot_compensation(
										(delta == 0.f) ? lslices_1st_layer : offset_ex(lslices_1st_layer, delta), 
	                            		layerm->flow(frExternalPerimeter), unscale<double>(elfoot))),
								stInternal);
							if (xy_compensation_scaled != 0.f)
								lslices_1st_layer = offset_ex(std::move(lslices_1st_layer), xy_compensation_scaled);
	                    } else if (xy_compensation_scaled != 0.f) {
	                        // Apply the XY compensation.
	                        layerm->slices.set(
                                offset_ex(to_expolygons(std::move(layerm->slices.surfaces)), xy_compensation_scaled),
	                            stInternal);
	                    }
	                } else {
	                    bool upscale   = ! upscaled && xy_compensation_scaled > 0.f;
	                    bool clip      = ! clipped && m_config.clip_multipart_objects.value;
	                    if (upscale || clip) {
	                        // Multiple regions, growing or just clipping one region by the other.
	                        // When clipping the regions, priority is given to the first regions.
	                        Polygons processed;
	            			for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id) {
	                            LayerRegion *layerm = layer->m_regions[region_id];
	            				ExPolygons slices = to_expolygons(std::move(layerm->slices.surfaces));
	            				if (upscale)
	            					slices = offset_ex(std::move(slices), xy_compensation_scaled);
	                            if (region_id > 0 && clip)
	                                // Trim by the slices of already processed regions.
	                                slices = diff_ex(to_polygons(std::move(slices)), processed);
	                            if (clip && (region_id + 1 < layer->m_regions.size()))
	                                // Collect the already processed regions to trim the to be processed regions.
	                                polygons_append(processed, slices);
	                            layerm->slices.set(std::move(slices), stInternal);
	                        }
	                    }
	                    if (xy_compensation_scaled < 0.f || elfoot > 0.f) {
	                        // Apply the negative XY compensation.
	                        Polygons trimming;
	                        static const float eps = float(scale_(m_config.slice_closing_radius.value) * 1.5);
	                        if (elfoot > 0.f) {
	                        	lslices_1st_layer = offset_ex(layer->merged(eps), std::min(xy_compensation_scaled, 0.f) - eps);
								trimming = to_polygons(Slic3r::elephant_foot_compensation(lslices_1st_layer,
									layer->m_regions.front()->flow(frExternalPerimeter), unscale<double>(elfoot)));
	                        } else
		                        trimming = offset(layer->merged(float(SCALED_EPSILON)), xy_compensation_scaled - float(SCALED_EPSILON));
	                        for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id)
	                            layer->m_regions[region_id]->trim_surfaces(trimming);
	                    }
	                }
	                // Merge all regions' slices to get islands, chain them by a shortest path.
	                layer->make_slices();
	            }
	        });
	    if (elephant_foot_compensation_scaled > 0.f) {
	    	// The Elephant foot has been compensated, therefore the 1st layer's lslices are shrank with the Elephant foot compensation value.
	    	// Store the uncompensated value there.
	    	assert(! m_layers.empty());
	    	assert(m_layers.front()->id() == 0);
			m_layers.front()->lslices = std::move(lslices_1st_layer);
		}
	}

    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - make_slices in parallel - end";
}

// To be used only if there are no layer span specific configurations applied, which would lead to z ranges being generated for this region.
std::vector<ExPolygons> PrintObject::slice_region(size_t region_id, const std::vector<float> &z, SlicingMode mode) const
{
	std::vector<const ModelVolume*> volumes;
    if (region_id < this->region_volumes.size()) {
		for (const std::pair<t_layer_height_range, int> &volume_and_range : this->region_volumes[region_id]) {
			const ModelVolume *volume = this->model_object()->volumes[volume_and_range.second];
			if (volume->is_model_part())
				volumes.emplace_back(volume);
		}
    }
	return this->slice_volumes(z, mode, volumes);
}

// Z ranges are not applicable to modifier meshes, therefore a sinle volume will be found in volume_and_range at most once.
std::vector<ExPolygons> PrintObject::slice_modifiers(size_t region_id, const std::vector<float> &slice_zs) const
{
	std::vector<ExPolygons> out;
    if (region_id < this->region_volumes.size())
    {
		std::vector<std::vector<t_layer_height_range>> volume_ranges;
		const std::vector<std::pair<t_layer_height_range, int>> &volumes_and_ranges = this->region_volumes[region_id];
		volume_ranges.reserve(volumes_and_ranges.size());
		for (size_t i = 0; i < volumes_and_ranges.size(); ) {
			int 			   volume_id    = volumes_and_ranges[i].second;
			const ModelVolume *model_volume = this->model_object()->volumes[volume_id];
			if (model_volume->is_modifier()) {
				std::vector<t_layer_height_range> ranges;
				ranges.emplace_back(volumes_and_ranges[i].first);
				size_t j = i + 1;
				for (; j < volumes_and_ranges.size() && volume_id == volumes_and_ranges[j].second; ++ j) {
					if (! ranges.empty() && std::abs(ranges.back().second - volumes_and_ranges[j].first.first) < EPSILON)
						ranges.back().second = volumes_and_ranges[j].first.second;
					else
						ranges.emplace_back(volumes_and_ranges[j].first);
				}
				volume_ranges.emplace_back(std::move(ranges));
				i = j;
			} else
				++ i;
		}

		if (! volume_ranges.empty()) 
		{
			bool equal_ranges = true;
			for (size_t i = 1; i < volume_ranges.size(); ++ i) {
				assert(! volume_ranges[i].empty());
				if (volume_ranges.front() != volume_ranges[i]) {
					equal_ranges = false;
					break;
				}
			}

			if (equal_ranges && volume_ranges.front().size() == 1 && volume_ranges.front().front() == t_layer_height_range(0, DBL_MAX)) {
				// No modifier in this region was split to layer spans.
				std::vector<const ModelVolume*> volumes;
				for (const std::pair<t_layer_height_range, int> &volume_and_range : this->region_volumes[region_id]) {
					const ModelVolume *volume = this->model_object()->volumes[volume_and_range.second];
					if (volume->is_modifier())
						volumes.emplace_back(volume);
				}
				out = this->slice_volumes(slice_zs, SlicingMode::Regular, volumes);
			} else {
				// Some modifier in this region was split to layer spans.
				std::vector<char> merge;
				for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
					const std::vector<std::pair<t_layer_height_range, int>> &volumes_and_ranges = this->region_volumes[region_id];
					for (size_t i = 0; i < volumes_and_ranges.size(); ) {
						int 			   volume_id    = volumes_and_ranges[i].second;
						const ModelVolume *model_volume = this->model_object()->volumes[volume_id];
						if (model_volume->is_modifier()) {
							BOOST_LOG_TRIVIAL(debug) << "Slicing modifiers - volume " << volume_id;
							// Find the ranges of this volume. Ranges in volumes_and_ranges must not overlap for a single volume.
							std::vector<t_layer_height_range> ranges;
							ranges.emplace_back(volumes_and_ranges[i].first);
							size_t j = i + 1;
							for (; j < volumes_and_ranges.size() && volume_id == volumes_and_ranges[j].second; ++ j)
								ranges.emplace_back(volumes_and_ranges[j].first);
			                // slicing in parallel
			                std::vector<ExPolygons> this_slices = this->slice_volume(slice_zs, ranges, SlicingMode::Regular, *model_volume);
			                if (out.empty()) {
			                	out = std::move(this_slices);
			                	merge.assign(out.size(), false);
			                } else {
			                	for (size_t i = 0; i < out.size(); ++ i)
                                    if (! this_slices[i].empty()) {
			                			if (! out[i].empty()) {
			                				append(out[i], this_slices[i]);
			                				merge[i] = true;
			                			} else
			                				out[i] = std::move(this_slices[i]);
                                    }
			                }
							i = j;
						} else
							++ i;
					}
				}
				for (size_t i = 0; i < merge.size(); ++ i)
					if (merge[i])
						out[i] = union_ex(out[i]);
			}
		}
	}

	return out;
}

std::vector<ExPolygons> PrintObject::slice_support_volumes(const ModelVolumeType &model_volume_type) const
{
    std::vector<const ModelVolume*> volumes;
    for (const ModelVolume *volume : this->model_object()->volumes)
        if (volume->type() == model_volume_type)
            volumes.emplace_back(volume);
    std::vector<float> zs;
    zs.reserve(this->layers().size());
    for (const Layer *l : this->layers())
        zs.emplace_back((float)l->slice_z);
    return this->slice_volumes(zs, SlicingMode::Regular, volumes);
}

std::vector<ExPolygons> PrintObject::slice_volumes(const std::vector<float> &z, SlicingMode mode, const std::vector<const ModelVolume*> &volumes) const
{
    std::vector<ExPolygons> layers;
    if (! volumes.empty()) {
        // Compose mesh.
        //FIXME better to perform slicing over each volume separately and then to use a Boolean operation to merge them.
		TriangleMesh mesh(volumes.front()->mesh());
        mesh.transform(volumes.front()->get_matrix(), true);
		assert(mesh.repaired);
		if (volumes.size() == 1 && mesh.repaired) {
			//FIXME The admesh repair function may break the face connectivity, rather refresh it here as the slicing code relies on it.
			stl_check_facets_exact(&mesh.stl);
		}
        for (size_t idx_volume = 1; idx_volume < volumes.size(); ++ idx_volume) {
            const ModelVolume &model_volume = *volumes[idx_volume];
            TriangleMesh vol_mesh(model_volume.mesh());
            vol_mesh.transform(model_volume.get_matrix(), true);
            mesh.merge(vol_mesh);
        }
        if (mesh.stl.stats.number_of_facets > 0) {
            mesh.transform(m_trafo, true);
            // apply XY shift
            mesh.translate(- unscale<float>(m_center_offset.x()), - unscale<float>(m_center_offset.y()), 0);
            // perform actual slicing
            const Print *print = this->print();
            auto callback = TriangleMeshSlicer::throw_on_cancel_callback_type([print](){print->throw_if_canceled();});
            // TriangleMeshSlicer needs shared vertices, also this calls the repair() function.
            mesh.require_shared_vertices();
            TriangleMeshSlicer mslicer;
            mslicer.init(&mesh, callback);
			mslicer.slice(z, mode, float(m_config.slice_closing_radius.value), &layers, callback);
            m_print->throw_if_canceled();
        }
    }
    return layers;
}

std::vector<ExPolygons> PrintObject::slice_volume(const std::vector<float> &z, SlicingMode mode, const ModelVolume &volume) const
{
    std::vector<ExPolygons> layers;
    if (! z.empty()) {
	    // Compose mesh.
	    //FIXME better to split the mesh into separate shells, perform slicing over each shell separately and then to use a Boolean operation to merge them.
	    TriangleMesh mesh(volume.mesh());
	    mesh.transform(volume.get_matrix(), true);
		if (mesh.repaired) {
			//FIXME The admesh repair function may break the face connectivity, rather refresh it here as the slicing code relies on it.
			stl_check_facets_exact(&mesh.stl);
		}
	    if (mesh.stl.stats.number_of_facets > 0) {
	        mesh.transform(m_trafo, true);
	        // apply XY shift
	        mesh.translate(- unscale<float>(m_center_offset.x()), - unscale<float>(m_center_offset.y()), 0);
	        // perform actual slicing
	        TriangleMeshSlicer mslicer;
	        const Print *print = this->print();
	        auto callback = TriangleMeshSlicer::throw_on_cancel_callback_type([print](){print->throw_if_canceled();});
	        // TriangleMeshSlicer needs the shared vertices.
	        mesh.require_shared_vertices();
	        mslicer.init(&mesh, callback);
	        mslicer.slice(z, mode, float(m_config.slice_closing_radius.value), &layers, callback);
	        m_print->throw_if_canceled();
	    }
	}
    return layers;
}

// Filter the zs not inside the ranges. The ranges are closed at the botton and open at the top, they are sorted lexicographically and non overlapping.
std::vector<ExPolygons> PrintObject::slice_volume(const std::vector<float> &z, const std::vector<t_layer_height_range> &ranges, SlicingMode mode, const ModelVolume &volume) const
{
	std::vector<ExPolygons> out;
	if (! z.empty() && ! ranges.empty()) {
		if (ranges.size() == 1 && z.front() >= ranges.front().first && z.back() < ranges.front().second) {
			// All layers fit into a single range.
			out = this->slice_volume(z, mode, volume);
		} else {
			std::vector<float> 					   z_filtered;
			std::vector<std::pair<size_t, size_t>> n_filtered;
			z_filtered.reserve(z.size());
			n_filtered.reserve(2 * ranges.size());
			size_t i = 0;
			for (const t_layer_height_range &range : ranges) {
				for (; i < z.size() && z[i] < range.first; ++ i) ;
				size_t first = i;
				for (; i < z.size() && z[i] < range.second; ++ i)
					z_filtered.emplace_back(z[i]);
				if (i > first)
					n_filtered.emplace_back(std::make_pair(first, i));
			}
			if (! n_filtered.empty()) {
				std::vector<ExPolygons> layers = this->slice_volume(z_filtered, mode, volume);
				out.assign(z.size(), ExPolygons());
				i = 0;
				for (const std::pair<size_t, size_t> &span : n_filtered)
					for (size_t j = span.first; j < span.second; ++ j)
						out[j] = std::move(layers[i ++]);
			}
		}
	}
	return out;
}

std::string PrintObject::_fix_slicing_errors()
{
    // Collect layers with slicing errors.
    // These layers will be fixed in parallel.
    std::vector<size_t> buggy_layers;
    buggy_layers.reserve(m_layers.size());
    for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++ idx_layer)
        if (m_layers[idx_layer]->slicing_errors)
            buggy_layers.push_back(idx_layer);

    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - begin";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [this, &buggy_layers](const tbb::blocked_range<size_t>& range) {
            for (size_t buggy_layer_idx = range.begin(); buggy_layer_idx < range.end(); ++ buggy_layer_idx) {
                m_print->throw_if_canceled();
                size_t idx_layer = buggy_layers[buggy_layer_idx];
                Layer *layer     = m_layers[idx_layer];
                assert(layer->slicing_errors);
                // Try to repair the layer surfaces by merging all contours and all holes from neighbor layers.
                // BOOST_LOG_TRIVIAL(trace) << "Attempting to repair layer" << idx_layer;
                for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id) {
                    LayerRegion *layerm = layer->m_regions[region_id];
                    // Find the first valid layer below / above the current layer.
                    const Surfaces *upper_surfaces = nullptr;
                    const Surfaces *lower_surfaces = nullptr;
                    for (size_t j = idx_layer + 1; j < m_layers.size(); ++ j)
                        if (! m_layers[j]->slicing_errors) {
                            upper_surfaces = &m_layers[j]->regions()[region_id]->slices.surfaces;
                            break;
                        }
                    for (int j = int(idx_layer) - 1; j >= 0; -- j)
                        if (! m_layers[j]->slicing_errors) {
                            lower_surfaces = &m_layers[j]->regions()[region_id]->slices.surfaces;
                            break;
                        }
                    // Collect outer contours and holes from the valid layers above & below.
                    Polygons outer;
                    outer.reserve(
                        ((upper_surfaces == nullptr) ? 0 : upper_surfaces->size()) + 
                        ((lower_surfaces == nullptr) ? 0 : lower_surfaces->size()));
                    size_t num_holes = 0;
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    Polygons holes;
                    holes.reserve(num_holes);
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    layerm->slices.set(diff_ex(union_(outer), holes, false), stInternal);
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - end";

    // remove empty layers from bottom
    while (! m_layers.empty() && (m_layers.front()->lslices.empty() || m_layers.front()->empty())) {
        delete m_layers.front();
        m_layers.erase(m_layers.begin());
        m_layers.front()->lower_layer = nullptr;
        for (size_t i = 0; i < m_layers.size(); ++ i)
            m_layers[i]->set_id(m_layers[i]->id() - 1);
    }

    return buggy_layers.empty() ? "" :
        "The model has overlapping or self-intersecting facets. I tried to repair it, "
        "however you might want to check the results or repair the input file and retry.\n";
}

// Simplify the sliced model, if "resolution" configuration parameter > 0.
// The simplification is problematic, because it simplifies the slices independent from each other,
// which makes the simplified discretization visible on the object surface.
void PrintObject::simplify_slices(double distance)
{
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - siplifying slices in parallel - begin";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this, distance](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer *layer = m_layers[layer_idx];
                for (size_t region_idx = 0; region_idx < layer->m_regions.size(); ++ region_idx)
                    layer->m_regions[region_idx]->slices.simplify(distance);
				{
					ExPolygons simplified;
					for (const ExPolygon &expoly : layer->lslices)
						expoly.simplify(distance, &simplified);
					layer->lslices = std::move(simplified);
				}
            }
        });
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - siplifying slices in parallel - end";
}

// Only active if config->infill_only_where_needed. This step trims the sparse infill,
// so it acts as an internal support. It maintains all other infill types intact.
// Here the internal surfaces and perimeters have to be supported by the sparse infill.
//FIXME The surfaces are supported by a sparse infill, but the sparse infill is only as large as the area to support.
// Likely the sparse infill will not be anchored correctly, so it will not work as intended.
// Also one wishes the perimeters to be supported by a full infill.
// Idempotence of this method is guaranteed by the fact that we don't remove things from
// fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
void PrintObject::clip_fill_surfaces()
{
    if (! m_config.infill_only_where_needed.value ||
        ! std::any_of(this->print()->regions().begin(), this->print()->regions().end(), 
            [](const PrintRegion *region) { return region->config().fill_density > 0; }))
        return;

    // We only want infill under ceilings; this is almost like an
    // internal support material.
    // Proceed top-down, skipping the bottom layer.
    Polygons upper_internal;
    for (int layer_id = int(m_layers.size()) - 1; layer_id > 0; -- layer_id) {
        Layer *layer       = m_layers[layer_id];
        Layer *lower_layer = m_layers[layer_id - 1];
        // Detect things that we need to support.
        // Cummulative slices.
        Polygons slices;
        polygons_append(slices, layer->lslices);
        // Cummulative fill surfaces.
        Polygons fill_surfaces;
        // Solid surfaces to be supported.
        Polygons overhangs;
        for (const LayerRegion *layerm : layer->m_regions)
            for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                Polygons polygons = to_polygons(surface.expolygon);
                if (surface.is_solid())
                    polygons_append(overhangs, polygons);
                polygons_append(fill_surfaces, std::move(polygons));
            }
        Polygons lower_layer_fill_surfaces;
        Polygons lower_layer_internal_surfaces;
        for (const LayerRegion *layerm : lower_layer->m_regions)
            for (const Surface &surface : layerm->fill_surfaces.surfaces) {
                Polygons polygons = to_polygons(surface.expolygon);
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(lower_layer_internal_surfaces, polygons);
                polygons_append(lower_layer_fill_surfaces, std::move(polygons));
            }
        // We also need to support perimeters when there's at least one full unsupported loop
        {
            // Get perimeters area as the difference between slices and fill_surfaces
            // Only consider the area that is not supported by lower perimeters
            Polygons perimeters = intersection(diff(slices, fill_surfaces), lower_layer_fill_surfaces);
            // Only consider perimeter areas that are at least one extrusion width thick.
            //FIXME Offset2 eats out from both sides, while the perimeters are create outside in.
            //Should the pw not be half of the current value?
            float pw = FLT_MAX;
            for (const LayerRegion *layerm : layer->m_regions)
                pw = std::min(pw, (float)layerm->flow(frPerimeter).scaled_width());
            // Append such thick perimeters to the areas that need support
            polygons_append(overhangs, offset2(perimeters, -pw, +pw));
        }
        // Find new internal infill.
        polygons_append(overhangs, std::move(upper_internal));
        upper_internal = intersection(overhangs, lower_layer_internal_surfaces);
        // Apply new internal infill to regions.
        for (LayerRegion *layerm : lower_layer->m_regions) {
            if (layerm->region()->config().fill_density.value == 0)
                continue;
            SurfaceType internal_surface_types[] = { stInternal, stInternalVoid };
            Polygons internal;
            for (Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(internal, std::move(surface.expolygon));
            layerm->fill_surfaces.remove_types(internal_surface_types, 2);
            layerm->fill_surfaces.append(intersection_ex(internal, upper_internal, true), stInternal);
            layerm->fill_surfaces.append(diff_ex        (internal, upper_internal, true), stInternalVoid);
            // If there are voids it means that our internal infill is not adjacent to
            // perimeters. In this case it would be nice to add a loop around infill to
            // make it more robust and nicer. TODO.
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
            layerm->export_region_fill_surfaces_to_svg_debug("6_clip_fill_surfaces");
#endif
        }
        m_print->throw_if_canceled();
    }
}

void PrintObject::discover_horizontal_shells()
{
    BOOST_LOG_TRIVIAL(trace) << "discover_horizontal_shells()";
    
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        for (size_t i = 0; i < m_layers.size(); ++ i) {
            m_print->throw_if_canceled();
            Layer 					*layer  = m_layers[i];
            LayerRegion             *layerm = layer->regions()[region_id];
            const PrintRegionConfig &region_config = layerm->region()->config();
            if (region_config.solid_infill_every_layers.value > 0 && region_config.fill_density.value > 0 &&
                (i % region_config.solid_infill_every_layers) == 0) {
                // Insert a solid internal layer. Mark stInternal surfaces as stInternalSolid or stInternalBridge.
                SurfaceType type = (region_config.fill_density == 100) ? stInternalSolid : stInternalBridge;
                for (Surface &surface : layerm->fill_surfaces.surfaces)
                    if (surface.surface_type == stInternal)
                        surface.surface_type = type;
            }

            // If ensure_vertical_shell_thickness, then the rest has already been performed by discover_vertical_shells().
            if (region_config.ensure_vertical_shell_thickness.value)
                continue;
            
            coordf_t print_z  = layer->print_z;
            coordf_t bottom_z = layer->bottom_z();
            for (size_t idx_surface_type = 0; idx_surface_type < 3; ++ idx_surface_type) {
                m_print->throw_if_canceled();
                SurfaceType type = (idx_surface_type == 0) ? stTop : (idx_surface_type == 1) ? stBottom : stBottomBridge;
                int num_solid_layers = (type == stTop) ? region_config.top_solid_layers.value : region_config.bottom_solid_layers.value;
                if (num_solid_layers == 0)
                	continue;
                // Find slices of current type for current layer.
                // Use slices instead of fill_surfaces, because they also include the perimeter area,
                // which needs to be propagated in shells; we need to grow slices like we did for
                // fill_surfaces though. Using both ungrown slices and grown fill_surfaces will
                // not work in some situations, as there won't be any grown region in the perimeter 
                // area (this was seen in a model where the top layer had one extra perimeter, thus
                // its fill_surfaces were thinner than the lower layer's infill), however it's the best
                // solution so far. Growing the external slices by EXTERNAL_INFILL_MARGIN will put
                // too much solid infill inside nearly-vertical slopes.

                // Surfaces including the area of perimeters. Everything, that is visible from the top / bottom
                // (not covered by a layer above / below).
                // This does not contain the areas covered by perimeters!
                Polygons solid;
                for (const Surface &surface : layerm->slices.surfaces)
                    if (surface.surface_type == type)
                        polygons_append(solid, to_polygons(surface.expolygon));
                // Infill areas (slices without the perimeters).
                for (const Surface &surface : layerm->fill_surfaces.surfaces)
                    if (surface.surface_type == type)
                        polygons_append(solid, to_polygons(surface.expolygon));
                if (solid.empty())
                    continue;
//                Slic3r::debugf "Layer %d has %s surfaces\n", $i, ($type == stTop) ? 'top' : 'bottom';
                
                // Scatter top / bottom regions to other layers. Scattering process is inherently serial, it is difficult to parallelize without locking.
                for (int n = (type == stTop) ? int(i) - 1 : int(i) + 1;
                	(type == stTop) ?
                		(n >= 0                   && (int(i) - n < num_solid_layers || 
                								 	  print_z - m_layers[n]->print_z < region_config.top_solid_min_thickness.value - EPSILON)) :
                		(n < int(m_layers.size()) && (n - int(i) < num_solid_layers ||
                									  m_layers[n]->bottom_z() - bottom_z < region_config.bottom_solid_min_thickness.value - EPSILON));
                	(type == stTop) ? -- n : ++ n)
                {
//                    Slic3r::debugf "  looking for neighbors on layer %d...\n", $n;                  
                    // Reference to the lower layer of a TOP surface, or an upper layer of a BOTTOM surface.
                    LayerRegion *neighbor_layerm = m_layers[n]->regions()[region_id];
                    
                    // find intersection between neighbor and current layer's surfaces
                    // intersections have contours and holes
                    // we update $solid so that we limit the next neighbor layer to the areas that were
                    // found on this one - in other words, solid shells on one layer (for a given external surface)
                    // are always a subset of the shells found on the previous shell layer
                    // this approach allows for DWIM in hollow sloping vases, where we want bottom
                    // shells to be generated in the base but not in the walls (where there are many
                    // narrow bottom surfaces): reassigning $solid will consider the 'shadow' of the 
                    // upper perimeter as an obstacle and shell will not be propagated to more upper layers
                    //FIXME How does it work for stInternalBRIDGE? This is set for sparse infill. Likely this does not work.
                    Polygons new_internal_solid;
                    {
                        Polygons internal;
                        for (const Surface &surface : neighbor_layerm->fill_surfaces.surfaces)
                            if (surface.surface_type == stInternal || surface.surface_type == stInternalSolid)
                                polygons_append(internal, to_polygons(surface.expolygon));
                        new_internal_solid = intersection(solid, internal, true);
                    }
                    if (new_internal_solid.empty()) {
                        // No internal solid needed on this layer. In order to decide whether to continue
                        // searching on the next neighbor (thus enforcing the configured number of solid
                        // layers, use different strategies according to configured infill density:
                        if (region_config.fill_density.value == 0) {
                            // If user expects the object to be void (for example a hollow sloping vase),
                            // don't continue the search. In this case, we only generate the external solid
                            // shell if the object would otherwise show a hole (gap between perimeters of 
                            // the two layers), and internal solid shells are a subset of the shells found 
                            // on each previous layer.
                            goto EXTERNAL;
                        } else {
                            // If we have internal infill, we can generate internal solid shells freely.
                            continue;
                        }
                    }
                    
                    if (region_config.fill_density.value == 0) {
                        // if we're printing a hollow object we discard any solid shell thinner
                        // than a perimeter width, since it's probably just crossing a sloping wall
                        // and it's not wanted in a hollow print even if it would make sense when
                        // obeying the solid shell count option strictly (DWIM!)
                        float margin = float(neighbor_layerm->flow(frExternalPerimeter).scaled_width());
                        Polygons too_narrow = diff(
                            new_internal_solid, 
                            offset2(new_internal_solid, -margin, +margin, jtMiter, 5), 
                            true);
                        // Trim the regularized region by the original region.
                        if (! too_narrow.empty())
                            new_internal_solid = solid = diff(new_internal_solid, too_narrow);
                    }

                    // make sure the new internal solid is wide enough, as it might get collapsed
                    // when spacing is added in Fill.pm
                    {
                        //FIXME Vojtech: Disable this and you will be sorry.
                        // https://github.com/prusa3d/PrusaSlicer/issues/26 bottom
                        float margin = 3.f * layerm->flow(frSolidInfill).scaled_width(); // require at least this size
                        // we use a higher miterLimit here to handle areas with acute angles
                        // in those cases, the default miterLimit would cut the corner and we'd
                        // get a triangle in $too_narrow; if we grow it below then the shell
                        // would have a different shape from the external surface and we'd still
                        // have the same angle, so the next shell would be grown even more and so on.
                        Polygons too_narrow = diff(
                            new_internal_solid,
                            offset2(new_internal_solid, -margin, +margin, ClipperLib::jtMiter, 5),
                            true);
                        if (! too_narrow.empty()) {
                            // grow the collapsing parts and add the extra area to  the neighbor layer 
                            // as well as to our original surfaces so that we support this 
                            // additional area in the next shell too
                            // make sure our grown surfaces don't exceed the fill area
                            Polygons internal;
                            for (const Surface &surface : neighbor_layerm->fill_surfaces.surfaces)
                                if (surface.is_internal() && !surface.is_bridge())
                                    polygons_append(internal, to_polygons(surface.expolygon));
                            polygons_append(new_internal_solid, 
                                intersection(
                                    offset(too_narrow, +margin),
                                    // Discard bridges as they are grown for anchoring and we can't
                                    // remove such anchors. (This may happen when a bridge is being 
                                    // anchored onto a wall where little space remains after the bridge
                                    // is grown, and that little space is an internal solid shell so 
                                    // it triggers this too_narrow logic.)
                                    internal));
                            // see https://github.com/prusa3d/PrusaSlicer/pull/3426
                            // solid = new_internal_solid;
                        }
                    }
                    
                    // internal-solid are the union of the existing internal-solid surfaces
                    // and new ones
                    SurfaceCollection backup = std::move(neighbor_layerm->fill_surfaces);
                    polygons_append(new_internal_solid, to_polygons(backup.filter_by_type(stInternalSolid)));
                    ExPolygons internal_solid = union_ex(new_internal_solid, false);
                    // assign new internal-solid surfaces to layer
                    neighbor_layerm->fill_surfaces.set(internal_solid, stInternalSolid);
                    // subtract intersections from layer surfaces to get resulting internal surfaces
                    Polygons polygons_internal = to_polygons(std::move(internal_solid));
                    ExPolygons internal = diff_ex(
                        to_polygons(backup.filter_by_type(stInternal)),
                        polygons_internal,
                        true);
                    // assign resulting internal surfaces to layer
                    neighbor_layerm->fill_surfaces.append(internal, stInternal);
                    polygons_append(polygons_internal, to_polygons(std::move(internal)));
                    // assign top and bottom surfaces to layer
                    SurfaceType surface_types_solid[] = { stTop, stBottom, stBottomBridge };
                    backup.keep_types(surface_types_solid, 3);
                    std::vector<SurfacesPtr> top_bottom_groups;
                    backup.group(&top_bottom_groups);
                    for (SurfacesPtr &group : top_bottom_groups)
                        neighbor_layerm->fill_surfaces.append(
                            diff_ex(to_polygons(group), polygons_internal),
                            // Use an existing surface as a template, it carries the bridge angle etc.
                            *group.front());
                }
		EXTERNAL:;
            } // foreach type (stTop, stBottom, stBottomBridge)
        } // for each layer
    } // for each region

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        for (const Layer *layer : m_layers) {
            const LayerRegion *layerm = layer->m_regions[region_id];
            layerm->export_region_slices_to_svg_debug("5_discover_horizontal_shells");
            layerm->export_region_fill_surfaces_to_svg_debug("5_discover_horizontal_shells");
        } // for each layer
    } // for each region
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

// combine fill surfaces across layers to honor the "infill every N layers" option
// Idempotence of this method is guaranteed by the fact that we don't remove things from
// fill_surfaces but we only turn them into VOID surfaces, thus preserving the boundaries.
void PrintObject::combine_infill()
{
    // Work on each region separately.
    for (size_t region_id = 0; region_id < this->region_volumes.size(); ++ region_id) {
        const PrintRegion *region = this->print()->regions()[region_id];
        const size_t every = region->config().infill_every_layers.value;
        if (every < 2 || region->config().fill_density == 0.)
            continue;
        // Limit the number of combined layers to the maximum height allowed by this regions' nozzle.
        //FIXME limit the layer height to max_layer_height
        double nozzle_diameter = std::min(
            this->print()->config().nozzle_diameter.get_at(region->config().infill_extruder.value - 1),
            this->print()->config().nozzle_diameter.get_at(region->config().solid_infill_extruder.value - 1));
        // define the combinations
        std::vector<size_t> combine(m_layers.size(), 0);
        {
            double current_height = 0.;
            size_t num_layers = 0;
            for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
                m_print->throw_if_canceled();
                const Layer *layer = m_layers[layer_idx];
                if (layer->id() == 0)
                    // Skip first print layer (which may not be first layer in array because of raft).
                    continue;
                // Check whether the combination of this layer with the lower layers' buffer
                // would exceed max layer height or max combined layer count.
                if (current_height + layer->height >= nozzle_diameter + EPSILON || num_layers >= every) {
                    // Append combination to lower layer.
                    combine[layer_idx - 1] = num_layers;
                    current_height = 0.;
                    num_layers = 0;
                }
                current_height += layer->height;
                ++ num_layers;
            }
            
            // Append lower layers (if any) to uppermost layer.
            combine[m_layers.size() - 1] = num_layers;
        }
        
        // loop through layers to which we have assigned layers to combine
        for (size_t layer_idx = 0; layer_idx < m_layers.size(); ++ layer_idx) {
            m_print->throw_if_canceled();
            size_t num_layers = combine[layer_idx];
			if (num_layers <= 1)
                continue;
            // Get all the LayerRegion objects to be combined.
            std::vector<LayerRegion*> layerms;
            layerms.reserve(num_layers);
			for (size_t i = layer_idx + 1 - num_layers; i <= layer_idx; ++ i)
                layerms.emplace_back(m_layers[i]->regions()[region_id]);
            // We need to perform a multi-layer intersection, so let's split it in pairs.
            // Initialize the intersection with the candidates of the lowest layer.
            ExPolygons intersection = to_expolygons(layerms.front()->fill_surfaces.filter_by_type(stInternal));
            // Start looping from the second layer and intersect the current intersection with it.
            for (size_t i = 1; i < layerms.size(); ++ i)
                intersection = intersection_ex(
                    to_polygons(intersection),
                    to_polygons(layerms[i]->fill_surfaces.filter_by_type(stInternal)),
                    false);
            double area_threshold = layerms.front()->infill_area_threshold();
            if (! intersection.empty() && area_threshold > 0.)
                intersection.erase(std::remove_if(intersection.begin(), intersection.end(), 
                    [area_threshold](const ExPolygon &expoly) { return expoly.area() <= area_threshold; }), 
                    intersection.end());
            if (intersection.empty())
                continue;
//            Slic3r::debugf "  combining %d %s regions from layers %d-%d\n",
//                scalar(@$intersection),
//                ($type == stInternal ? 'internal' : 'internal-solid'),
//                $layer_idx-($every-1), $layer_idx;
            // intersection now contains the regions that can be combined across the full amount of layers,
            // so let's remove those areas from all layers.
            Polygons intersection_with_clearance;
            intersection_with_clearance.reserve(intersection.size());
            float clearance_offset = 
                0.5f * layerms.back()->flow(frPerimeter).scaled_width() +
             // Because fill areas for rectilinear and honeycomb are grown 
             // later to overlap perimeters, we need to counteract that too.
                ((region->config().fill_pattern == ipRectilinear   ||
                  region->config().fill_pattern == ipMonotonous    ||
                  region->config().fill_pattern == ipGrid          ||
                  region->config().fill_pattern == ipLine          ||
                  region->config().fill_pattern == ipHoneycomb) ? 1.5f : 0.5f) * 
                    layerms.back()->flow(frSolidInfill).scaled_width();
            for (ExPolygon &expoly : intersection)
                polygons_append(intersection_with_clearance, offset(expoly, clearance_offset));
            for (LayerRegion *layerm : layerms) {
                Polygons internal = to_polygons(layerm->fill_surfaces.filter_by_type(stInternal));
                layerm->fill_surfaces.remove_type(stInternal);
                layerm->fill_surfaces.append(diff_ex(internal, intersection_with_clearance, false), stInternal);
                if (layerm == layerms.back()) {
                    // Apply surfaces back with adjusted depth to the uppermost layer.
                    Surface templ(stInternal, ExPolygon());
                    templ.thickness = 0.;
                    for (LayerRegion *layerm2 : layerms)
                        templ.thickness += layerm2->layer()->height;
                    templ.thickness_layers = (unsigned short)layerms.size();
                    layerm->fill_surfaces.append(intersection, templ);
                } else {
                    // Save void surfaces.
                    layerm->fill_surfaces.append(
                        intersection_ex(internal, intersection_with_clearance, false),
                        stInternalVoid);
                }
            }
        }
    }
}

void PrintObject::_generate_support_material()
{
    PrintObjectSupportMaterial support_material(this, m_slicing_params);
    support_material.generate(*this);
}


void PrintObject::project_and_append_custom_supports(
        FacetSupportType type, std::vector<ExPolygons>& expolys) const
{
    for (const ModelVolume* mv : this->model_object()->volumes) {
        const indexed_triangle_set custom_facets = mv->m_supported_facets.get_facets(*mv, type);
        if (custom_facets.indices.empty())
            continue;

        const TriangleMesh& mesh = mv->mesh();
        const Transform3f& tr1 = mv->get_matrix().cast<float>();
        const Transform3f& tr2 = this->trafo().cast<float>();
        const Transform3f  tr  = tr2 * tr1;


        // The projection will be at most a pentagon. Let's minimize heap
        // reallocations by saving in in the following struct.
        // Points are used so that scaling can be done in parallel
        // and they can be moved from to create an ExPolygon later.
        struct LightPolygon {
            LightPolygon() { pts.reserve(5); }
            Points pts;

            void add(const Vec2f& pt) {
                pts.emplace_back(scale_(pt.x()), scale_(pt.y()));
                assert(pts.size() <= 5);
            }
        };

        // Structure to collect projected polygons. One element for each triangle.
        // Saves vector of polygons and layer_id of the first one.
        struct TriangleProjections {
            size_t first_layer_id;
            std::vector<LightPolygon> polygons;
        };

        // Vector to collect resulting projections from each triangle.
        std::vector<TriangleProjections> projections_of_triangles(custom_facets.indices.size());

        // Iterate over all triangles.
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, custom_facets.indices.size()),
            [&](const tbb::blocked_range<size_t>& range) {
            for (size_t idx = range.begin(); idx < range.end(); ++ idx) {

            std::array<Vec3f, 3> facet;

            // Transform the triangle into worlds coords.
            for (int i=0; i<3; ++i)
                facet[i] = tr * custom_facets.vertices[custom_facets.indices[idx](i)];

            // Ignore triangles with upward-pointing normal.
            if ((facet[1]-facet[0]).cross(facet[2]-facet[0]).z() > 0.)
                continue;

            // Sort the three vertices according to z-coordinate.
            std::sort(facet.begin(), facet.end(),
                      [](const Vec3f& pt1, const Vec3f&pt2) {
                          return pt1.z() < pt2.z();
                      });

            std::array<Vec2f, 3> trianglef;
            for (int i=0; i<3; ++i) {
                trianglef[i] = Vec2f(facet[i].x(), facet[i].y());
                trianglef[i] += Vec2f(unscale<float>(this->center_offset().x()),
                                      unscale<float>(this->center_offset().y()));
            }

            // Find lowest slice not below the triangle.
            auto it = std::lower_bound(layers().begin(), layers().end(), facet[0].z()+EPSILON,
                          [](const Layer* l1, float z) {
                               return l1->slice_z < z;
                          });

            // Count how many projections will be generated for this triangle
            // and allocate respective amount in projections_of_triangles.
            projections_of_triangles[idx].first_layer_id = it-layers().begin();
            size_t last_layer_id = projections_of_triangles[idx].first_layer_id;
            // The cast in the condition below is important. The comparison must
            // be an exact opposite of the one lower in the code where
            // the polygons are appended. And that one is on floats.
            while (last_layer_id + 1 < layers().size()
                && float(layers()[last_layer_id]->slice_z) <= facet[2].z())
                ++last_layer_id;
            projections_of_triangles[idx].polygons.resize(
                last_layer_id - projections_of_triangles[idx].first_layer_id + 1);

            // Calculate how to move points on triangle sides per unit z increment.
            Vec2f ta(trianglef[1] - trianglef[0]);
            Vec2f tb(trianglef[2] - trianglef[0]);
            ta *= 1./(facet[1].z() - facet[0].z());
            tb *= 1./(facet[2].z() - facet[0].z());

            // Projection on current slice will be build directly in place.
            LightPolygon* proj = &projections_of_triangles[idx].polygons[0];
            proj->add(trianglef[0]);

            bool passed_first = false;
            bool stop = false;

            // Project a sub-polygon on all slices intersecting the triangle.
            while (it != layers().end()) {
                const float z = (*it)->slice_z;

                // Projections of triangle sides intersections with slices.
                // a moves along one side, b tracks the other.
                Vec2f a;
                Vec2f b;

                // If the middle vertex was already passed, append the vertex
                // and use ta for tracking the remaining side.
                if (z > facet[1].z() && ! passed_first) {
                    proj->add(trianglef[1]);
                    ta = trianglef[2]-trianglef[1];
                    ta *= 1./(facet[2].z() - facet[1].z());
                    passed_first = true;
                }

                // This slice is above the triangle already.
                if (z > facet[2].z() || it+1 == layers().end()) {
                    proj->add(trianglef[2]);
                    stop = true;
                }
                else {
                    // Move a, b along the side it currently tracks to get
                    // projected intersection with current slice.
                    a = passed_first ? (trianglef[1]+ta*(z-facet[1].z()))
                                     : (trianglef[0]+ta*(z-facet[0].z()));
                    b = trianglef[0]+tb*(z-facet[0].z());
                    proj->add(a);
                    proj->add(b);
                }

               if (stop)
                    break;

                // Advance to the next layer.
                ++it;
                ++proj;
                assert(proj <= &projections_of_triangles[idx].polygons.back() );

                // a, b are first two points of the polygon for the next layer.
                proj->add(b);
                proj->add(a);
            }
        }
        }); // end of parallel_for

        // Make sure that the output vector can be used.
        expolys.resize(layers().size());

        // Now append the collected polygons to respective layers.
        for (auto& trg : projections_of_triangles) {
            int layer_id = trg.first_layer_id;

            for (const LightPolygon& poly : trg.polygons) {
                expolys[layer_id].emplace_back(std::move(poly.pts));
                ++layer_id;
            }
        }

    } // loop over ModelVolumes
}



const Layer* PrintObject::get_layer_at_printz(coordf_t print_z) const {
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [print_z](const Layer *layer) { return layer->print_z < print_z; });
    return (it == m_layers.end() || (*it)->print_z != print_z) ? nullptr : *it;
}



Layer* PrintObject::get_layer_at_printz(coordf_t print_z) { return const_cast<Layer*>(std::as_const(*this).get_layer_at_printz(print_z)); }



// Get a layer approximately at print_z.
const Layer* PrintObject::get_layer_at_printz(coordf_t print_z, coordf_t epsilon) const {
    coordf_t limit = print_z - epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.end() || (*it)->print_z > print_z + epsilon) ? nullptr : *it;
}



Layer* PrintObject::get_layer_at_printz(coordf_t print_z, coordf_t epsilon) { return const_cast<Layer*>(std::as_const(*this).get_layer_at_printz(print_z, epsilon)); }

} // namespace Slic3r
