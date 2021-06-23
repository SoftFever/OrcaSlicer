#include "Exception.hpp"
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
#include "Tesselate.hpp"
#include "TriangleMeshSlicer.hpp"
#include "Utils.hpp"
#include "Fill/FillAdaptive.hpp"
#include "Format/STL.hpp"

#include <float.h>
#include <string_view>
#include <utility>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

#include <Shiny/Shiny.h>

using namespace std::literals;

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

std::vector<std::reference_wrapper<const PrintRegion>> PrintObject::all_regions() const
{
    std::vector<std::reference_wrapper<const PrintRegion>> out;
    out.reserve(m_shared_regions->all_regions.size());
    for (const std::unique_ptr<Slic3r::PrintRegion> &region : m_shared_regions->all_regions)
        out.emplace_back(*region.get());
    return out;
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
    
    // Revert the typed slices into untyped slices.
    if (m_typed_slices) {
        for (Layer *layer : m_layers) {
            layer->restore_untyped_slices();
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
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        if (! region.config().extra_perimeters || region.config().perimeters == 0 || region.config().fill_density == 0 || this->layer_count() < 2)
            continue;

        BOOST_LOG_TRIVIAL(debug) << "Generating extra perimeters for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size() - 1),
            [this, &region, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    LayerRegion &layerm                     = *m_layers[layer_idx]->get_region(region_id);
                    const LayerRegion &upper_layerm         = *m_layers[layer_idx+1]->get_region(region_id);
                    const Polygons upper_layerm_polygons    = to_polygons(upper_layerm.slices.surfaces);
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
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
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
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
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
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
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
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
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
        auto [adaptive_fill_octree, support_fill_octree] = this->prepare_adaptive_infill_data();

        BOOST_LOG_TRIVIAL(debug) << "Filling layers in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, &adaptive_fill_octree = adaptive_fill_octree, &support_fill_octree = support_fill_octree](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    m_layers[layer_idx]->make_fills(adaptive_fill_octree.get(), support_fill_octree.get());
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
            // Ironing starting with layer 0 to support ironing all surfaces.
            tbb::blocked_range<size_t>(0, m_layers.size()),
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
        if ((this->has_support() && m_layers.size() > 1) || (this->has_raft() && ! m_layers.empty())) {
            m_print->set_status(85, L("Generating support material"));    
            this->_generate_support_material();
            m_print->throw_if_canceled();
        } else {
#if 0
            // Printing without supports. Empty layer means some objects or object parts are levitating,
            // therefore they cannot be printed without supports.
            for (const Layer *layer : m_layers)
                if (layer->empty())
                    throw Slic3r::SlicingError("Levitating objects cannot be printed without supports.");
#endif

            // Do we have custom support data that would not be used?
            // Notify the user in that case.
            if (! this->has_support()) {
                for (const ModelVolume* mv : this->model_object()->volumes) {
                    bool has_enforcers = mv->is_support_enforcer()
                        || (mv->is_model_part()
                            && ! mv->supported_facets.empty()
                            && ! mv->supported_facets.get_facets(*mv, EnforcerBlockerType::ENFORCER).indices.empty());
                    if (has_enforcers) {
                        this->active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL,
                            L("An object has custom support enforcers which will not be used "
                              "because supports are off. Consider turning them on.") + "\n" +
                            (L("Object name")) + ": " + this->model_object()->name);
                        break;
                    }
                }
            }
        }
        this->set_done(posSupportMaterial);
    }
}

std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> PrintObject::prepare_adaptive_infill_data()
{
    using namespace FillAdaptive;

    auto [adaptive_line_spacing, support_line_spacing] = adaptive_fill_line_spacing(*this);
    if ((adaptive_line_spacing == 0. && support_line_spacing == 0.) || this->layers().empty())
        return std::make_pair(OctreePtr(), OctreePtr());

    indexed_triangle_set mesh = this->model_object()->raw_indexed_triangle_set();
    // Rotate mesh and build octree on it with axis-aligned (standart base) cubes.
    Transform3d m = m_trafo;
    m.pretranslate(Vec3d(- unscale<float>(m_center_offset.x()), - unscale<float>(m_center_offset.y()), 0));
    auto to_octree = transform_to_octree().toRotationMatrix();
    its_transform(mesh, to_octree * m, true);

    // Triangulate internal bridging surfaces.
    std::vector<std::vector<Vec3d>> overhangs(this->layers().size());
    tbb::parallel_for(
        tbb::blocked_range<int>(0, int(m_layers.size()) - 1),
        [this, &to_octree, &overhangs](const tbb::blocked_range<int> &range) {
            std::vector<Vec3d> &out = overhangs[range.begin()];
            for (int idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                m_print->throw_if_canceled();
                const Layer *layer = this->layers()[idx_layer];
                for (const LayerRegion *layerm : layer->regions())
                    for (const Surface &surface : layerm->fill_surfaces.surfaces)
                        if (surface.surface_type == stInternalBridge)
                            append(out, triangulate_expolygon_3d(surface.expolygon, layer->bottom_z()));
            }
            for (Vec3d &p : out)
                p = (to_octree * p).eval();
        });
    // and gather them.
    for (size_t i = 1; i < overhangs.size(); ++ i)
        append(overhangs.front(), std::move(overhangs[i]));

    return std::make_pair(
        adaptive_line_spacing ? build_octree(mesh, overhangs.front(), adaptive_line_spacing, false) : OctreePtr(),
        support_line_spacing  ? build_octree(mesh, overhangs.front(), support_line_spacing, true) : OctreePtr());
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

SupportLayerPtrs::iterator PrintObject::insert_support_layer(SupportLayerPtrs::iterator pos, size_t id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    return m_support_layers.insert(pos, new SupportLayer(id, this, height, print_z, slice_z));
}

// Called by Print::apply().
// This method only accepts PrintObjectConfig and PrintRegionConfig option keys.
bool PrintObject::invalidate_state_by_config_options(
    const ConfigOptionResolver &old_config, const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<PrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (   opt_key == "brim_width"
            || opt_key == "brim_offset"
            || opt_key == "brim_type") {
            // Brim is printed below supports, support invalidates brim and skirt.
            steps.emplace_back(posSupportMaterial);
        } else if (
               opt_key == "perimeters"
            || opt_key == "extra_perimeters"
            || opt_key == "gap_fill_enabled"
            || opt_key == "gap_fill_speed"
            || opt_key == "first_layer_extrusion_width"
            || opt_key == "perimeter_extrusion_width"
            || opt_key == "infill_overlap"
            || opt_key == "external_perimeters_first") {
            steps.emplace_back(posPerimeters);
        } else if (
               opt_key == "layer_height"
            || opt_key == "first_layer_height"
            || opt_key == "mmu_segmented_region_max_width"
            || opt_key == "raft_layers"
            || opt_key == "raft_contact_distance"
            || opt_key == "slice_closing_radius"
            || opt_key == "slicing_mode") {
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
            || opt_key == "support_material_bottom_contact_distance"
            || opt_key == "support_material_interface_layers"
            || opt_key == "support_material_bottom_interface_layers"
            || opt_key == "support_material_interface_pattern"
            || opt_key == "support_material_interface_contact_loops"
            || opt_key == "support_material_interface_extruder"
            || opt_key == "support_material_interface_spacing"
            || opt_key == "support_material_pattern"
            || opt_key == "support_material_style"
            || opt_key == "support_material_xy_spacing"
            || opt_key == "support_material_spacing"
            || opt_key == "support_material_closing_radius"
            || opt_key == "support_material_synchronize_layers"
            || opt_key == "support_material_threshold"
            || opt_key == "support_material_with_sheath"
            || opt_key == "raft_expansion"
            || opt_key == "raft_first_layer_density"
            || opt_key == "raft_first_layer_expansion"
            || opt_key == "dont_support_bridges"
            || opt_key == "first_layer_extrusion_width") {
            steps.emplace_back(posSupportMaterial);
        } else if (opt_key == "bottom_solid_layers") {
            steps.emplace_back(posPrepareInfill);
            if (m_print->config().spiral_vase) {
                // Changing the number of bottom layers when a spiral vase is enabled requires re-slicing the object again.
                // Otherwise, holes in the bottom layers could be filled, as is reported in GH #5528.
                steps.emplace_back(posSlice);
            }
        } else if (
               opt_key == "interface_shells"
            || opt_key == "infill_only_where_needed"
            || opt_key == "infill_every_layers"
            || opt_key == "solid_infill_every_layers"
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
            || opt_key == "infill_anchor"
            || opt_key == "infill_anchor_max"
            || opt_key == "top_infill_extrusion_width"
            || opt_key == "first_layer_extrusion_width") {
            steps.emplace_back(posInfill);
        } else if (opt_key == "fill_density") {
            // One likely wants to reslice only when switching between zero infill to simulate boolean difference (subtracting volumes),
            // normal infill and 100% (solid) infill.
            const auto *old_density = old_config.option<ConfigOptionPercent>(opt_key);
            const auto *new_density = new_config.option<ConfigOptionPercent>(opt_key);
            assert(old_density && new_density);
            //FIXME Vojtech is not quite sure about the 100% here, maybe it is not needed.
            if (is_approx(old_density->value, 0.) || is_approx(old_density->value, 100.) ||
                is_approx(new_density->value, 0.) || is_approx(new_density->value, 100.))
                steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (opt_key == "solid_infill_extrusion_width") {
            // This value is used for calculating perimeter - infill overlap, thus perimeters need to be recalculated.
            steps.emplace_back(posPerimeters);
            steps.emplace_back(posPrepareInfill);
        } else if (
               opt_key == "external_perimeter_extrusion_width"
            || opt_key == "perimeter_extruder"
            || opt_key == "fuzzy_skin"
            || opt_key == "fuzzy_skin_thickness"
            || opt_key == "fuzzy_skin_point_dist"
            || opt_key == "overhangs"
            || opt_key == "thin_walls"
            || opt_key == "thick_bridges") {
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
		invalidated |= this->invalidate_steps({ posPrepareInfill, posInfill, posIroning });
        invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
    } else if (step == posPrepareInfill) {
        invalidated |= this->invalidate_steps({ posInfill, posIroning });
    } else if (step == posInfill) {
        invalidated |= this->invalidate_steps({ posIroning });
        invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
    } else if (step == posSlice) {
		invalidated |= this->invalidate_steps({ posPerimeters, posPrepareInfill, posInfill, posIroning, posSupportMaterial });
		invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
        m_slicing_params.valid = false;
    } else if (step == posSupportMaterial) {
        invalidated |= m_print->invalidate_steps({ psSkirt, psBrim });
        m_slicing_params.valid = false;
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
	m_slicing_params.valid = false;
	return result;
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
    size_t num_layers     = spiral_vase ? std::min(size_t(this->printing_region(0).config().bottom_solid_layers), m_layers.size()) : m_layers.size();

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " in parallel - start";
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
        for (Layer *layer : m_layers)
            layer->m_regions[region_id]->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-initial");
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
            [this, region_id, interface_shells, &surfaces_new](const tbb::blocked_range<size_t>& range) {
                // If we have soluble support material, don't bridge. The overhang will be squished against a soluble layer separating
                // the support from the print.
                SurfaceType surface_type_bottom_other =
                    (this->has_support() && m_config.support_material_contact_distance.value == 0) ?
                    stBottom : stBottomBridge;
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Detecting solid surfaces for region " << region_id << " and layer " << layer->print_z;
                    Layer       *layer  = m_layers[idx_layer];
                    LayerRegion *layerm = layer->m_regions[region_id];
                    // comparison happens against the *full* slices (considering all regions)
                    // unless internal shells are requested
                    Layer       *upper_layer = (idx_layer + 1 < this->layer_count()) ? m_layers[idx_layer + 1] : nullptr;
                    Layer       *lower_layer = (idx_layer > 0) ? m_layers[idx_layer - 1] : nullptr;
                    // collapse very narrow parts (using the safety offset in the diff is not enough)
                    float        offset = layerm->flow(frExternalPerimeter).scaled_width() / 10.f;

                    // find top surfaces (difference between current surfaces
                    // of current layer and upper one)
                    Surfaces top;
                    if (upper_layer) {
                        ExPolygons upper_slices = interface_shells ? 
                            diff_ex(layerm->slices.surfaces, upper_layer->m_regions[region_id]->slices.surfaces, ApplySafetyOffset::Yes) :
                            diff_ex(layerm->slices.surfaces, upper_layer->lslices, ApplySafetyOffset::Yes);
                        surfaces_append(top, offset2_ex(upper_slices, -offset, offset), stTop);
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
                            to_polygons(lower_layer->get_region(region_id)->slices.surfaces) : 
                            to_polygons(lower_layer->slices);
                        surfaces_append(bottom,
                            offset2_ex(diff(layerm->slices.surfaces, lower_slices, true), -offset, offset),
                            surface_type_bottom_other);
#else
                        // Any surface lying on the void is a true bottom bridge (an overhang)
                        surfaces_append(
                            bottom,
                            offset2_ex(
                                diff_ex(layerm->slices.surfaces, lower_layer->lslices, ApplySafetyOffset::Yes),
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
                                    diff_ex(
                                        intersection(layerm->slices.surfaces, lower_layer->lslices), // supported
                                        lower_layer->m_regions[region_id]->slices.surfaces,
                                        ApplySafetyOffset::Yes),
                                    -offset, offset),
                                stBottom);
                        }
#endif
                    } else {
                        // if no lower layer, all surfaces of this one are solid
                        // we clone surfaces because we're going to clear the slices collection
                        bottom = layerm->slices.surfaces;
                        for (Surface &surface : bottom)
                            surface.surface_type = stBottom;
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
                        surfaces_append(top, diff_ex(top_polygons, bottom), stTop);
                    }

        #ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        static int iRun = 0;
                        std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(top),                           SVG::ExPolygonAttributes("green")));
                        expolygons_with_attributes.emplace_back(std::make_pair(union_ex(bottom),                        SVG::ExPolygonAttributes("brown")));
                        expolygons_with_attributes.emplace_back(std::make_pair(to_expolygons(layerm->slices.surfaces),  SVG::ExPolygonAttributes("black")));
                        SVG::export_expolygons(debug_out_path("1_detect_surfaces_type_%d_region%d-layer_%f.svg", iRun ++, region_id, layer->print_z).c_str(), expolygons_with_attributes);
                    }
        #endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                    
                    // save surfaces to layer
                    Surfaces &surfaces_out = interface_shells ? surfaces_new[idx_layer] : layerm->slices.surfaces;
                    Surfaces  surfaces_backup;
                    if (! interface_shells) {
                        surfaces_backup = std::move(surfaces_out);
                        surfaces_out.clear();
                    }
                    const Surfaces &surfaces_prev = interface_shells ? layerm->slices.surfaces : surfaces_backup;

                    // find internal surfaces (difference between top/bottom surfaces and others)
                    {
                        Polygons topbottom = to_polygons(top);
                        polygons_append(topbottom, to_polygons(bottom));
                        surfaces_append(surfaces_out, diff_ex(surfaces_prev, topbottom), stInternal);
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
                m_layers[idx_layer]->m_regions[region_id]->slices.surfaces = std::move(surfaces_new[idx_layer]);
        }

        if (spiral_vase) {
        	if (num_layers > 1)
	        	// Turn the last bottom layer infill to a top infill, so it will be extruded with a proper pattern.
	        	m_layers[num_layers - 1]->m_regions[region_id]->slices.set_type(stTop);
	        for (size_t i = num_layers; i < m_layers.size(); ++ i)
	        	m_layers[i]->m_regions[region_id]->slices.set_type(stInternal);
        }

        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " - clipping in parallel - start";
        // Fill in layerm->fill_surfaces by trimming the layerm->slices by the cummulative layerm->fill_surfaces.
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                    m_print->throw_if_canceled();
                    LayerRegion *layerm = m_layers[idx_layer]->m_regions[region_id];
                    layerm->slices_to_fill_surfaces_clipped();
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    layerm->export_region_fill_surfaces_to_svg_debug("1_detect_surfaces_type-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
                } // for each layer of a region
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Detecting solid surfaces for region " << region_id << " - clipping in parallel - end";
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
	for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id)
		if (this->printing_region(region_id).config().fill_density == 0) {
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
		                	if (layerm->region().config().fill_density.value == 0.)
		                		for (const Surface &surface : layerm->fill_surfaces.surfaces)
		                			// Shrink the holes, let the layer above expand slightly inside the unsupported areas.
		                			polygons_append(voids, offset(surface.expolygon, unsupported_width));
		                }
		                surfaces_covered[layer_idx] = diff(m_layers[layer_idx]->lslices, voids);
	            	}
	        }
	    );
	    m_print->throw_if_canceled();
	    BOOST_LOG_TRIVIAL(debug) << "Collecting surfaces covered with extrusions in parallel - end";
	}

	for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
        BOOST_LOG_TRIVIAL(debug) << "Processing external surfaces for region " << region_id << " in parallel - start";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_layers.size()),
            [this, &surfaces_covered, region_id](const tbb::blocked_range<size_t>& range) {
                for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    m_print->throw_if_canceled();
                    // BOOST_LOG_TRIVIAL(trace) << "Processing external surface, layer" << m_layers[layer_idx]->print_z;
                    m_layers[layer_idx]->get_region(int(region_id))->process_external_surfaces(
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
    size_t   num_layers       = spiral_vase ? std::min(size_t(this->printing_region(0).config().bottom_solid_layers), m_layers.size()) : m_layers.size();
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
    bool top_bottom_surfaces_all_regions = this->num_printing_regions() > 1 && ! m_config.interface_shells.value;
    if (top_bottom_surfaces_all_regions) {
        // This is a multi-material print and interface_shells are disabled, meaning that the vertical shell thickness
        // is calculated over all materials.
        // Is the "ensure vertical wall thickness" applicable to any region?
        bool has_extra_layers = false;
        for (size_t region_id = 0; region_id < this->num_printing_regions(); ++region_id) {
            const PrintRegionConfig &config = this->printing_region(region_id).config();
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
                const size_t num_regions = this->num_printing_regions();
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
                    for (size_t region_id = 0; region_id < num_regions; ++ region_id) {
                        LayerRegion &layerm                       = *layer.m_regions[region_id];
                        float        min_perimeter_infill_spacing = float(layerm.flow(frSolidInfill).scaled_spacing()) * 1.05f;
                        // Top surfaces.
                        append(cache.top_surfaces, offset(layerm.slices.filter_by_type(stTop), min_perimeter_infill_spacing));
                        append(cache.top_surfaces, offset(layerm.fill_surfaces.filter_by_type(stTop), min_perimeter_infill_spacing));
                        // Bottom surfaces.
                        append(cache.bottom_surfaces, offset(layerm.slices.filter_by_types(surfaces_bottom, 2), min_perimeter_infill_spacing));
                        append(cache.bottom_surfaces, offset(layerm.fill_surfaces.filter_by_types(surfaces_bottom, 2), min_perimeter_infill_spacing));
                        // Calculate the maximum perimeter offset as if the slice was extruded with a single extruder only.
                        // First find the maxium number of perimeters per region slice.
                        unsigned int perimeters = 0;
                        for (Surface &s : layerm.slices.surfaces)
                            perimeters = std::max<unsigned int>(perimeters, s.extra_perimeters);
                        perimeters += layerm.region().config().perimeters.value;
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
                    cache.top_surfaces    = union_(cache.top_surfaces);
                    cache.bottom_surfaces = union_(cache.bottom_surfaces);
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
                    cache.holes = union_(cache.holes);
                }
            });
        m_print->throw_if_canceled();
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells in parallel - end : cache top / bottom";
    }

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        PROFILE_BLOCK(discover_vertical_shells_region);

        const PrintRegion &region = this->printing_region(region_id);
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
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - start : cache top / bottom";
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, num_layers, grain_size),
                [this, region_id, &cache_top_botom_regions](const tbb::blocked_range<size_t>& range) {
                    const SurfaceType surfaces_bottom[2] = { stBottom, stBottomBridge };
                    for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                        m_print->throw_if_canceled();
                        Layer       &layer                        = *m_layers[idx_layer];
                        LayerRegion &layerm                       = *layer.m_regions[region_id];
                        float        min_perimeter_infill_spacing = float(layerm.flow(frSolidInfill).scaled_spacing()) * 1.05f;
                        // Top surfaces.
                        auto &cache = cache_top_botom_regions[idx_layer];
                        cache.top_surfaces = offset(layerm.slices.filter_by_type(stTop), min_perimeter_infill_spacing);
                        append(cache.top_surfaces, offset(layerm.fill_surfaces.filter_by_type(stTop), min_perimeter_infill_spacing));
                        // Bottom surfaces.
                        cache.bottom_surfaces = offset(layerm.slices.filter_by_types(surfaces_bottom, 2), min_perimeter_infill_spacing);
                        append(cache.bottom_surfaces, offset(layerm.fill_surfaces.filter_by_types(surfaces_bottom, 2), min_perimeter_infill_spacing));
                        // Holes over all regions. Only collect them once, they are valid for all region_id iterations.
                        if (cache.holes.empty()) {
                            for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id)
                                polygons_append(cache.holes, to_polygons(layer.regions()[region_id]->fill_expolygons));
                        }
                    }
                });
            m_print->throw_if_canceled();
            BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - end : cache top / bottom";
        }

        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - start : ensure vertical wall thickness";
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, num_layers, grain_size),
            [this, region_id, &cache_top_botom_regions]
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
                    LayerRegion 	        *layerm         = layer->m_regions[region_id];
                    const PrintRegionConfig &region_config  = layerm->region().config();

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
	                               shell = union_(shell);
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
		                            shell = union_(shell);
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
                        shell_ex = union_safety_offset_ex(shell);
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
                    shell = intersection(shell, polygonsInternal, ApplySafetyOffset::Yes);
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
                    ExPolygons new_internal_solid = intersection_ex(polygonsInternal, shell);
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
                    {
                        Slic3r::SVG svg(debug_out_path("discover_vertical_shells-regularized-%d.svg", debug_idx), get_extents(shell_before));
                        // Source shell.
                        svg.draw(union_safety_offset_ex(shell_before));
                        // Shell trimmed to the internal surfaces.
                        svg.draw_outline(union_safety_offset_ex(shell), "black", "blue", scale_(0.05));
                        // Regularized infill region.
                        svg.draw_outline(new_internal_solid, "red", "magenta", scale_(0.05));
                        svg.Close();  
                    }
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

                    // Trim the internal & internalvoid by the shell.
                    Slic3r::ExPolygons new_internal = diff_ex(layerm->fill_surfaces.filter_by_type(stInternal), shell);
                    Slic3r::ExPolygons new_internal_void = diff_ex(layerm->fill_surfaces.filter_by_type(stInternalVoid), shell);

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
        BOOST_LOG_TRIVIAL(debug) << "Discovering vertical shells for region " << region_id << " in parallel - end";

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
		for (size_t idx_layer = 0; idx_layer < m_layers.size(); ++idx_layer) {
			LayerRegion *layerm = m_layers[idx_layer]->get_region(region_id);
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

    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        
        // skip bridging in case there are no voids
        if (region.config().fill_density.value == 100)
            continue;

		for (LayerPtrs::iterator layer_it = m_layers.begin(); layer_it != m_layers.end(); ++ layer_it) {
            // skip first layer
			if (layer_it == m_layers.begin())
                continue;
            
            Layer       *layer       = *layer_it;
            LayerRegion *layerm      = layer->m_regions[region_id];
            Flow         bridge_flow = layerm->bridging_flow(frSolidInfill);

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
                double bottom_z = layer->print_z - bridge_flow.height();
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
            ExPolygons not_to_bridge = diff_ex(internal_solid, to_bridge, ApplySafetyOffset::Yes);
            to_bridge = intersection_ex(to_bridge, internal_solid, ApplySafetyOffset::Yes);
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
    {
        DynamicPrintConfig src_normalized(object.config.get());
        src_normalized.normalize_fdm();
        config.apply(src_normalized, true);
    }
    // Clamp invalid extruders to the default extruder (with index 1).
    clamp_exturder_to_default(config.support_material_extruder,           num_extruders);
    clamp_exturder_to_default(config.support_material_interface_extruder, num_extruders);
    return config;
}

const std::string                                                    key_extruder { "extruder" };
static constexpr const std::initializer_list<const std::string_view> keys_extruders { "infill_extruder"sv, "solid_infill_extruder"sv, "perimeter_extruder"sv };

static void apply_to_print_region_config(PrintRegionConfig &out, const DynamicPrintConfig &in)
{
    // 1) Copy the "extruder key to infill_extruder and perimeter_extruder.
    auto *opt_extruder = in.opt<ConfigOptionInt>(key_extruder);
    if (opt_extruder)
        if (int extruder = opt_extruder->value; extruder != 0) {
            // Not a default extruder.
            out.infill_extruder      .value = extruder;
            out.solid_infill_extruder.value = extruder;
            out.perimeter_extruder   .value = extruder;
        }
    // 2) Copy the rest of the values.
    for (auto it = in.cbegin(); it != in.cend(); ++ it)
        if (it->first != key_extruder)
            if (ConfigOption* my_opt = out.option(it->first, false); my_opt != nullptr) {
                if (one_of(it->first, keys_extruders)) {
                    // Ignore "default" extruders.
                    int extruder = static_cast<const ConfigOptionInt*>(it->second.get())->value;
                    if (extruder > 0)
                        my_opt->setInt(extruder);
                } else
                    my_opt->set(it->second.get());
            }
}

PrintRegionConfig region_config_from_model_volume(const PrintRegionConfig &default_or_parent_region_config, const DynamicPrintConfig *layer_range_config, const ModelVolume &volume, size_t num_extruders)
{
    PrintRegionConfig config = default_or_parent_region_config;
    if (volume.is_model_part()) {
        // default_or_parent_region_config contains the Print's PrintRegionConfig.
        // Override with ModelObject's PrintRegionConfig values.
        apply_to_print_region_config(config, volume.get_object()->config.get());
    } else {
        // default_or_parent_region_config contains parent PrintRegion config, which already contains ModelVolume's config.
    }
    if (layer_range_config != nullptr) {
        // Not applicable to modifiers.
        assert(volume.is_model_part());
    	apply_to_print_region_config(config, *layer_range_config);
    }
    apply_to_print_region_config(config, volume.config.get());
    if (! volume.material_id().empty())
        apply_to_print_region_config(config, volume.material()->config.get());
    // Clamp invalid extruders to the default extruder (with index 1).
    clamp_exturder_to_default(config.infill_extruder,       num_extruders);
    clamp_exturder_to_default(config.perimeter_extruder,    num_extruders);
    clamp_exturder_to_default(config.solid_infill_extruder, num_extruders);
    if (config.fill_density.value < 0.00011f)
        // Switch of infill for very low infill rates, also avoid division by zero in infill generator for these very low rates.
        // See GH issue #5910.
        config.fill_density.value = 0;
    else 
        config.fill_density.value = std::min(config.fill_density.value, 100.);
    return config;
}

void PrintObject::update_slicing_parameters()
{
#if ENABLE_ALLOW_NEGATIVE_Z
    if (!m_slicing_params.valid)
        m_slicing_params = SlicingParameters::create_from_config(
            this->print()->config(), m_config, this->model_object()->bounding_box().max.z(), this->object_extruders());
#else
    if (! m_slicing_params.valid)
        m_slicing_params = SlicingParameters::create_from_config(
            this->print()->config(), m_config, unscale<double>(this->height()), this->object_extruders());
#endif // ENABLE_ALLOW_NEGATIVE_Z
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
                object_config.brim_type != btNoBrim && object_config.brim_width > 0.,
				object_extruders);
			for (const std::pair<const t_layer_height_range, ModelConfig> &range_and_config : model_object.layer_config_ranges)
				if (range_and_config.second.has("perimeter_extruder") ||
					range_and_config.second.has("infill_extruder") ||
					range_and_config.second.has("solid_infill_extruder"))
					PrintRegion::collect_object_printing_extruders(
						print_config,
						region_config_from_model_volume(default_region_config, &range_and_config.second.get(), *model_volume, num_extruders),
                        object_config.brim_type != btNoBrim && object_config.brim_width > 0.,
						object_extruders);
		}
    sort_remove_duplicates(object_extruders);
    //FIXME add painting extruders

    if (object_max_z <= 0.f)
        object_max_z = (float)model_object.raw_bounding_box().size().z();
    return SlicingParameters::create_from_config(print_config, object_config, object_max_z, object_extruders);
}

// returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
std::vector<unsigned int> PrintObject::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(this->all_regions().size() * 3);
    for (const PrintRegion &region : this->all_regions())
        region.collect_object_printing_extruders(*this->print(), extruders);
    sort_remove_duplicates(extruders);
    return extruders;
}

bool PrintObject::update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile)
{
    bool updated = false;

    if (layer_height_profile.empty()) {
        // use the constructor because the assignement is crashing on ASAN OsX
        layer_height_profile = std::vector<coordf_t>(model_object.layer_height_profile.get());
//        layer_height_profile = model_object.layer_height_profile;
        updated = true;
    }

#if ENABLE_ALLOW_NEGATIVE_Z
    // Verify the layer_height_profile.
    if (!layer_height_profile.empty() &&
        // Must not be of even length.
        ((layer_height_profile.size() & 1) != 0 ||
            // Last entry must be at the top of the object.
            std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_parameters.object_print_z_max) > 1e-3))
        layer_height_profile.clear();
#else
    // Verify the layer_height_profile.
    if (! layer_height_profile.empty() && 
            // Must not be of even length.
            ((layer_height_profile.size() & 1) != 0 || 
            // Last entry must be at the top of the object.
             std::abs(layer_height_profile[layer_height_profile.size() - 2] - slicing_parameters.object_print_z_height()) > 1e-3))
        layer_height_profile.clear();
#endif // ENABLE_ALLOW_NEGATIVE_Z

    if (layer_height_profile.empty()) {
        //layer_height_profile = layer_height_profile_adaptive(slicing_parameters, model_object.layer_config_ranges, model_object.volumes);
        layer_height_profile = layer_height_profile_from_ranges(slicing_parameters, model_object.layer_config_ranges);
        updated = true;
    }
    return updated;
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
    if (! m_config.infill_only_where_needed.value)
        return;
    bool has_infill = false;
    for (size_t i = 0; i < this->num_printing_regions(); ++ i)
        if (this->printing_region(i).config().fill_density > 0) {
            has_infill = true;
            break;
        }
    if (! has_infill)
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
            if (layerm->region().config().fill_density.value == 0)
                continue;
            SurfaceType internal_surface_types[] = { stInternal, stInternalVoid };
            Polygons internal;
            for (Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal || surface.surface_type == stInternalVoid)
                    polygons_append(internal, std::move(surface.expolygon));
            layerm->fill_surfaces.remove_types(internal_surface_types, 2);
            layerm->fill_surfaces.append(intersection_ex(internal, upper_internal, ApplySafetyOffset::Yes), stInternal);
            layerm->fill_surfaces.append(diff_ex        (internal, upper_internal, ApplySafetyOffset::Yes), stInternalVoid);
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
    
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        for (size_t i = 0; i < m_layers.size(); ++ i) {
            m_print->throw_if_canceled();
            Layer 					*layer  = m_layers[i];
            LayerRegion             *layerm = layer->regions()[region_id];
            const PrintRegionConfig &region_config = layerm->region().config();
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
                        new_internal_solid = intersection(solid, internal, ApplySafetyOffset::Yes);
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
                            offset2(new_internal_solid, -margin, +margin + ClipperSafetyOffset, jtMiter, 5));
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
                            offset2(new_internal_solid, -margin, +margin + ClipperSafetyOffset, ClipperLib::jtMiter, 5));
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
                    ExPolygons internal_solid = union_ex(new_internal_solid);
                    // assign new internal-solid surfaces to layer
                    neighbor_layerm->fill_surfaces.set(internal_solid, stInternalSolid);
                    // subtract intersections from layer surfaces to get resulting internal surfaces
                    Polygons polygons_internal = to_polygons(std::move(internal_solid));
                    ExPolygons internal = diff_ex(backup.filter_by_type(stInternal), polygons_internal, ApplySafetyOffset::Yes);
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
                            diff_ex(group, polygons_internal),
                            // Use an existing surface as a template, it carries the bridge angle etc.
                            *group.front());
                }
		EXTERNAL:;
            } // foreach type (stTop, stBottom, stBottomBridge)
        } // for each layer
    } // for each region

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
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
    for (size_t region_id = 0; region_id < this->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = this->printing_region(region_id);
        const size_t every = region.config().infill_every_layers.value;
        if (every < 2 || region.config().fill_density == 0.)
            continue;
        // Limit the number of combined layers to the maximum height allowed by this regions' nozzle.
        //FIXME limit the layer height to max_layer_height
        double nozzle_diameter = std::min(
            this->print()->config().nozzle_diameter.get_at(region.config().infill_extruder.value - 1),
            this->print()->config().nozzle_diameter.get_at(region.config().solid_infill_extruder.value - 1));
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
                intersection = intersection_ex(layerms[i]->fill_surfaces.filter_by_type(stInternal), intersection);
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
                ((region.config().fill_pattern == ipRectilinear   ||
                  region.config().fill_pattern == ipMonotonic     ||
                  region.config().fill_pattern == ipGrid          ||
                  region.config().fill_pattern == ipLine          ||
                  region.config().fill_pattern == ipHoneycomb) ? 1.5f : 0.5f) * 
                    layerms.back()->flow(frSolidInfill).scaled_width();
            for (ExPolygon &expoly : intersection)
                polygons_append(intersection_with_clearance, offset(expoly, clearance_offset));
            for (LayerRegion *layerm : layerms) {
                Polygons internal = to_polygons(std::move(layerm->fill_surfaces.filter_by_type(stInternal)));
                layerm->fill_surfaces.remove_type(stInternal);
                layerm->fill_surfaces.append(diff_ex(internal, intersection_with_clearance), stInternal);
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
                        intersection_ex(internal, intersection_with_clearance),
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


void PrintObject::project_and_append_custom_facets(
        bool seam, EnforcerBlockerType type, std::vector<ExPolygons>& expolys) const
{
    for (const ModelVolume* mv : this->model_object()->volumes) {
        const indexed_triangle_set custom_facets = seam
                ? mv->seam_facets.get_facets(*mv, type)
                : mv->supported_facets.get_facets(*mv, type);
        if (! mv->is_model_part() || custom_facets.indices.empty())
            continue;

        const Transform3f& tr1 = mv->get_matrix().cast<float>();
        const Transform3f& tr2 = this->trafo().cast<float>();
        const Transform3f  tr  = tr2 * tr1;
        const float        tr_det_sign = (tr.matrix().determinant() > 0. ? 1.f : -1.f);
        const Vec2f        center = unscaled<float>(this->center_offset());
        ConstLayerPtrsAdaptor layers = this->layers();

        // The projection will be at most a pentagon. Let's minimize heap
        // reallocations by saving in in the following struct.
        // Points are used so that scaling can be done in parallel
        // and they can be moved from to create an ExPolygon later.
        struct LightPolygon {
            LightPolygon() { pts.reserve(5); }
            LightPolygon(const std::array<Vec2f, 3>& tri) {
                pts.reserve(3);
                pts.emplace_back(scaled<coord_t>(tri.front()));
                pts.emplace_back(scaled<coord_t>(tri[1]));
                pts.emplace_back(scaled<coord_t>(tri.back()));
            }

            Points pts;

            void add(const Vec2f& pt) {
                pts.emplace_back(scaled<coord_t>(pt));
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
            [center, &custom_facets, &tr, tr_det_sign, seam, layers, &projections_of_triangles](const tbb::blocked_range<size_t>& range) {
            for (size_t idx = range.begin(); idx < range.end(); ++ idx) {

            std::array<Vec3f, 3> facet;

            // Transform the triangle into worlds coords.
            for (int i=0; i<3; ++i)
                facet[i] = tr * custom_facets.vertices[custom_facets.indices[idx](i)];

            // Ignore triangles with upward-pointing normal. Don't forget about mirroring.
            float z_comp = (facet[1]-facet[0]).cross(facet[2]-facet[0]).z();
            if (! seam && tr_det_sign * z_comp > 0.)
                continue;

            // The algorithm does not process vertical triangles, but it should for seam.
            // In that case, tilt the triangle a bit so the projection does not degenerate.
            if (seam && z_comp == 0.f)
                facet[0].x() += float(EPSILON);

            // Sort the three vertices according to z-coordinate.
            std::sort(facet.begin(), facet.end(),
                      [](const Vec3f& pt1, const Vec3f&pt2) {
                          return pt1.z() < pt2.z();
                      });

            std::array<Vec2f, 3> trianglef;
            for (int i=0; i<3; ++i)
                trianglef[i] = to_2d(facet[i]) - center;

            // Find lowest slice not below the triangle.
            auto it = std::lower_bound(layers.begin(), layers.end(), facet[0].z()+EPSILON,
                          [](const Layer* l1, float z) {
                               return l1->slice_z < z;
                          });

            // Count how many projections will be generated for this triangle
            // and allocate respective amount in projections_of_triangles.
            size_t first_layer_id = projections_of_triangles[idx].first_layer_id = it - layers.begin();
            size_t last_layer_id  = first_layer_id;
            // The cast in the condition below is important. The comparison must
            // be an exact opposite of the one lower in the code where
            // the polygons are appended. And that one is on floats.
            while (last_layer_id + 1 < layers.size()
                && float(layers[last_layer_id]->slice_z) <= facet[2].z())
                ++last_layer_id;

            if (first_layer_id == last_layer_id) {
                // The triangle fits just a single slab, just project it. This also avoids division by zero for horizontal triangles.
                float dz = facet[2].z() - facet[0].z();
                assert(dz >= 0);
                // The face is nearly horizontal and it crosses the slicing plane at first_layer_id - 1.
                // Rather add this face to both the planes.
                bool add_below = dz < float(2. * EPSILON) && first_layer_id > 0 && layers[first_layer_id - 1]->slice_z > facet[0].z() - EPSILON;
                projections_of_triangles[idx].polygons.reserve(add_below ? 2 : 1);
                projections_of_triangles[idx].polygons.emplace_back(trianglef);
                if (add_below) {
                    -- projections_of_triangles[idx].first_layer_id;
                    projections_of_triangles[idx].polygons.emplace_back(trianglef);
                }
                continue;
            }

            projections_of_triangles[idx].polygons.resize(last_layer_id - first_layer_id + 1);

            // Calculate how to move points on triangle sides per unit z increment.
            Vec2f ta(trianglef[1] - trianglef[0]);
            Vec2f tb(trianglef[2] - trianglef[0]);
            ta *= 1.f/(facet[1].z() - facet[0].z());
            tb *= 1.f/(facet[2].z() - facet[0].z());

            // Projection on current slice will be build directly in place.
            LightPolygon* proj = &projections_of_triangles[idx].polygons[0];
            proj->add(trianglef[0]);

            bool passed_first = false;
            bool stop = false;

            // Project a sub-polygon on all slices intersecting the triangle.
            while (it != layers.end()) {
                const float z = float((*it)->slice_z);

                // Projections of triangle sides intersections with slices.
                // a moves along one side, b tracks the other.
                Vec2f a;
                Vec2f b;

                // If the middle vertex was already passed, append the vertex
                // and use ta for tracking the remaining side.
                if (z > facet[1].z() && ! passed_first) {
                    proj->add(trianglef[1]);
                    ta = trianglef[2]-trianglef[1];
                    ta *= 1.f/(facet[2].z() - facet[1].z());
                    passed_first = true;
                }

                // This slice is above the triangle already.
                if (z > facet[2].z() || it+1 == layers.end()) {
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
        expolys.resize(layers.size());

        // Now append the collected polygons to respective layers.
        for (auto& trg : projections_of_triangles) {
            int layer_id = int(trg.first_layer_id);
            for (LightPolygon &poly : trg.polygons) {
                if (layer_id >= int(expolys.size()))
                    break; // part of triangle could be projected above top layer
                assert(! poly.pts.empty());
                // The resulting triangles are fed to the Clipper library, which seem to handle flipped triangles well.
//                if (cross2(Vec2d((poly.pts[1] - poly.pts[0]).cast<double>()), Vec2d((poly.pts[2] - poly.pts[1]).cast<double>())) < 0)
//                    std::swap(poly.pts.front(), poly.pts.back());
                    
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

const Layer *PrintObject::get_first_layer_bellow_printz(coordf_t print_z, coordf_t epsilon) const
{
    coordf_t limit = print_z + epsilon;
    auto it = Slic3r::lower_bound_by_predicate(m_layers.begin(), m_layers.end(), [limit](const Layer *layer) { return layer->print_z < limit; });
    return (it == m_layers.begin()) ? nullptr : *(--it);
}

} // namespace Slic3r
