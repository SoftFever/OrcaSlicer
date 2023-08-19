// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "TreeModelVolumes.hpp"
#include "TreeSupport3D.hpp"

#include "BuildVolume.hpp"
#include "ClipperUtils.hpp"
#include "Flow.hpp"
#include "Layer.hpp"
#include "Point.hpp"
#include "Print.hpp"
#include "PrintConfig.hpp"
#include "Utils.hpp"

#include <string_view>

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/task_group.h>

namespace Slic3r::TreeSupport3D
{

using namespace std::literals;

// or warning
// had to use a define beacuse the macro processing inside macro BOOST_LOG_TRIVIAL()
#define error_level_not_in_cache error

TreeSupportMeshGroupSettings::TreeSupportMeshGroupSettings(const PrintObject &print_object)
{
    const PrintConfig       &print_config       = print_object.print()->config();
    const PrintObjectConfig &config             = print_object.config();
    const SlicingParameters &slicing_params     = print_object.slicing_parameters();
//    const std::vector<unsigned int>  printing_extruders = print_object.object_extruders();

    // Support must be enabled and set to Tree style.
    //assert(config.support_material);
    //assert(config.support_material_style == smsTree || config.support_material_style == smsOrganic);

    // Calculate maximum external perimeter width over all printing regions, taking into account the default layer height.
    coordf_t external_perimeter_width = 0.;
    for (size_t region_id = 0; region_id < print_object.num_printing_regions(); ++ region_id) {
        const PrintRegion &region = print_object.printing_region(region_id);
        external_perimeter_width = std::max<coordf_t>(external_perimeter_width, region.flow(print_object, frExternalPerimeter, config.layer_height).width());
    }

    this->layer_height              = scaled<coord_t>(config.layer_height.value);
    this->resolution                = scaled<coord_t>(print_config.resolution.value);
    // Arache feature
    this->min_feature_size          = scaled<coord_t>(config.min_feature_size.value);
    // +1 makes the threshold inclusive
    this->support_angle             = 0.5 * M_PI - std::clamp<double>((config.support_threshold_angle + 1) * M_PI / 180., 0., 0.5 * M_PI);
    this->support_line_width        = support_material_flow(&print_object, config.layer_height).scaled_width();
    this->support_roof_line_width   = support_material_interface_flow(&print_object, config.layer_height).scaled_width();
    //FIXME add it to SlicingParameters and reuse in both tree and normal supports?
    this->support_bottom_enable     = config.support_interface_top_layers.value > 0 && config.support_interface_bottom_layers.value != 0;
    this->support_bottom_height     = this->support_bottom_enable ?
        (config.support_interface_bottom_layers.value > 0 ?
            config.support_interface_bottom_layers.value :
            config.support_interface_top_layers.value) * this->layer_height :
        0;
    this->support_material_buildplate_only = config.support_on_build_plate_only;
    this->support_xy_distance       = scaled<coord_t>(config.support_object_xy_distance.value);
    // Separation of interfaces, it is likely smaller than support_xy_distance.
    this->support_xy_distance_overhang = std::min(this->support_xy_distance, scaled<coord_t>(0.5 * external_perimeter_width));
    this->support_top_distance      = scaled<coord_t>(slicing_params.gap_support_object);
    this->support_bottom_distance   = scaled<coord_t>(slicing_params.gap_object_support);
//    this->support_interface_skip_height =
//    this->support_infill_angles     = 
    this->support_roof_enable       = config.support_interface_top_layers.value > 0;
    this->support_roof_height       = config.support_interface_top_layers.value * this->layer_height;
//    this->minimum_roof_area         = 
//    this->support_roof_angles       = 
    this->support_roof_pattern      = config.support_interface_pattern;
    this->support_pattern           = config.support_base_pattern;
    this->support_line_spacing      = scaled<coord_t>(config.support_base_pattern_spacing.value);
//    this->support_bottom_offset     = 
//    this->support_wall_count        = config.support_material_with_sheath ? 1 : 0;
    this->support_wall_count        = 1;
    this->support_roof_line_distance = scaled<coord_t>(config.support_interface_spacing.value) + this->support_roof_line_width;
//    this->minimum_support_area      = 
//    this->minimum_bottom_area       = 
//    this->support_offset            = 
//    this->support_tree_branch_distance = 2.5 * line_width ??
    double support_tree_angle_slow = 25;// TODO add a setting?
    double support_tree_branch_diameter_angle = 5; // TODO add a setting?
    double tree_support_tip_diameter = 0.8;
    this->support_tree_angle          = std::clamp<double>(config.tree_support_branch_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
    this->support_tree_angle_slow     = std::clamp<double>(support_tree_angle_slow * M_PI / 180., 0., this->support_tree_angle - EPSILON);
    this->support_tree_branch_diameter = scaled<coord_t>(config.tree_support_branch_diameter.value);
    this->support_tree_branch_diameter_angle = std::clamp<double>(support_tree_branch_diameter_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
    this->support_tree_top_rate = 30; // percent
//    this->support_tree_tip_diameter = this->support_line_width;
    this->support_tree_tip_diameter = std::clamp(scaled<coord_t>(tree_support_tip_diameter), 0, this->support_tree_branch_diameter);
}

//FIXME Machine border is currently ignored.
static Polygons calculateMachineBorderCollision(Polygon machine_border)
{
    // Put a border of 1m around the print volume so that we don't collide.
#if 1
    //FIXME just returning no border will let tree support legs collide with print bed boundary
    return {};
#else
    //FIXME offsetting by 1000mm easily overflows int32_tr coordinate.
    Polygons out = offset(machine_border, scaled<float>(1000.), jtMiter, 1.2);
    machine_border.reverse(); // Makes the polygon negative so that we subtract the actual volume from the collision area.
    out.emplace_back(std::move(machine_border));
    return out;
#endif
}

TreeModelVolumes::TreeModelVolumes(
    const PrintObject &print_object,
    const BuildVolume &build_volume,
    const coord_t max_move, const coord_t max_move_slow, size_t current_mesh_idx, 
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    double progress_multiplier, double progress_offset, 
#endif // SLIC3R_TREESUPPORTS_PROGRESS
    const std::vector<Polygons>& additional_excluded_areas) :
    // -2 to avoid rounding errors
    m_max_move{ std::max<coord_t>(max_move - 2, 0) }, m_max_move_slow{ std::max<coord_t>(max_move_slow - 2, 0) },
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    m_progress_multiplier{ progress_multiplier }, m_progress_offset{ progress_offset },
#endif // SLIC3R_TREESUPPORTS_PROGRESS
    m_machine_border{ calculateMachineBorderCollision(build_volume.polygon()) }
{
    m_bed_area = build_volume.polygon();
#if 0
    std::unordered_map<size_t, size_t> mesh_to_layeroutline_idx;
    for (size_t mesh_idx = 0; mesh_idx < storage.meshes.size(); ++ mesh_idx) {
        SliceMeshStorage mesh = storage.meshes[mesh_idx];
        bool added = false;
        for (size_t idx = 0; idx < m_layer_outlines.size(); ++ idx)
            if (TreeSupport::TreeSupportSettings(m_layer_outlines[idx].first) == TreeSupport::TreeSupportSettings(mesh.settings)) {
                added = true;
                mesh_to_layeroutline_idx[mesh_idx] = idx;
            }
        if (! added) {
            mesh_to_layeroutline_idx[mesh_idx] = m_layer_outlines.size();
            m_layer_outlines.emplace_back(mesh.settings, std::vector<Polygons>(storage.support.supportLayers.size(), Polygons()));
        }
    }
    for (size_t idx = 0; idx < m_layer_outlines.size(); ++ idx) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layer_outlines[idx].second.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (const size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                m_layer_outlines[idx].second[layer_idx] = union_(m_layer_outlines[idx].second[layer_idx]); 
        });
    }
    m_current_outline_idx = mesh_to_layeroutline_idx[current_mesh_idx];

#else
    {
        m_anti_overhang = print_object.slice_support_blockers();
        TreeSupportMeshGroupSettings mesh_settings(print_object);
        m_layer_outlines.emplace_back(mesh_settings, std::vector<Polygons>{});
        m_current_outline_idx = 0;
        std::vector<Polygons> &outlines = m_layer_outlines.front().second;
        outlines.assign(print_object.layer_count(), Polygons{});
        tbb::parallel_for(tbb::blocked_range<size_t>(0, print_object.layer_count()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                outlines[layer_idx] = to_polygons(expolygons_simplify(print_object.get_layer(layer_idx)->lslices, mesh_settings.resolution));
        });
    }
#endif

    m_support_rests_on_model = false;
    m_min_resolution  = std::numeric_limits<coord_t>::max();
    for (auto data_pair : m_layer_outlines) {
        m_support_rests_on_model |= ! data_pair.first.support_material_buildplate_only;
        m_min_resolution = std::min(m_min_resolution, data_pair.first.resolution);
    }

    const TreeSupportSettings config{ m_layer_outlines[m_current_outline_idx].first };
    m_current_min_xy_dist = config.xy_min_distance;
    m_current_min_xy_dist_delta = config.xy_distance - m_current_min_xy_dist;
    assert(m_current_min_xy_dist_delta >= 0);
    m_increase_until_radius = config.increase_radius_until_radius;
    m_radius_0 = config.getRadius(0);

#if 0
    for (size_t mesh_idx = 0; mesh_idx < storage.meshes.size(); mesh_idx++) {
        SliceMeshStorage mesh = storage.meshes[mesh_idx];
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_layer_outlines[mesh_to_layeroutline_idx[mesh_idx]].second.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx)
                if (layer_idx < mesh.layer_nr_max_filled_layer) {
                    Polygons outline = extractOutlineFromMesh(mesh, layer_idx);
                    append(m_layer_outlines[mesh_to_layeroutline_idx[mesh_idx]].second[layer_idx], outline);
                }
        });
    }
    if (! additional_excluded_areas.empty()) {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, m_anti_overhang.size()),
            [&](const tbb::blocked_range<size_t> &range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                if (layer_idx < coord_t(additional_excluded_areas.size()))
                    append(m_anti_overhang[layer_idx], additional_excluded_areas[layer_idx]);
    //          if (SUPPORT_TREE_AVOID_SUPPORT_BLOCKER)
    //              append(m_anti_overhang[layer_idx], storage.support.supportLayers[layer_idx].anti_overhang);
    //FIXME block wipe tower
    //          if (storage.primeTower.enabled)
    //              append(m_anti_overhang[layer_idx], layer_idx == 0 ? storage.primeTower.outer_poly_first_layer : storage.primeTower.outer_poly);
                m_anti_overhang[layer_idx] = union_(m_anti_overhang[layer_idx]);
            }
        });
    }
#endif
}

void TreeModelVolumes::precalculate(const coord_t max_layer, std::function<void()> throw_on_cancel)
{
    auto t_start = std::chrono::high_resolution_clock::now();
    m_precalculated = true;

    // Get the config corresponding to one mesh that is in the current group. Which one has to be irrelevant.
    // Not the prettiest way to do this, but it ensures some calculations that may be a bit more complex
    // like inital layer diameter are only done in once.
    TreeSupportSettings config(m_layer_outlines[m_current_outline_idx].first);

    {
        // calculate which radius each layer in the tip may have.
        std::vector<coord_t> possible_tip_radiis;
        for (size_t distance_to_top = 0; distance_to_top <= config.tip_layers; ++ distance_to_top) {
            possible_tip_radiis.emplace_back(ceilRadius(config.getRadius(distance_to_top)));
            possible_tip_radiis.emplace_back(ceilRadius(config.getRadius(distance_to_top) + m_current_min_xy_dist_delta));
        }
        sort_remove_duplicates(possible_tip_radiis);
        // It theoretically may happen in the tip, that the radius can change so much in-between 2 layers, 
        // that a ceil step is skipped (as in there is a radius r so that ceilRadius(radius(dtt))<ceilRadius(r)<ceilRadius(radius(dtt+1))). 
        // As such a radius will not reasonable happen in the tree and it will most likely not be requested,
        // there is no need to calculate them. So just skip these.
        for (coord_t radius_eval = m_radius_0; radius_eval <= config.branch_radius; radius_eval = ceilRadius(radius_eval + 1))
            if (! std::binary_search(possible_tip_radiis.begin(), possible_tip_radiis.end(), radius_eval))
                m_ignorable_radii.emplace_back(radius_eval);
    }

    throw_on_cancel();

    // it may seem that the required avoidance can be of a smaller radius when going to model (no initial layer diameter for to model branches)
    // but as for every branch going towards the bp, the to model avoidance is required to check for possible merges with to model branches, this assumption is in-fact wrong.
    std::unordered_map<coord_t, LayerIndex> radius_until_layer;
    // while it is possible to calculate, up to which layer the avoidance should be calculated, this simulation is easier to understand, and does not need to be adjusted if something of the radius calculation is changed.
    // Overhead with an assumed worst case of 6600 layers was about 2ms
    for (LayerIndex distance_to_top = 0; distance_to_top <= max_layer; ++ distance_to_top) {
        const LayerIndex current_layer = max_layer - distance_to_top;
        auto update_radius_until_layer = [&radius_until_layer, current_layer](coord_t r) {
            auto it = radius_until_layer.find(r);
            if (it == radius_until_layer.end())
                radius_until_layer.emplace_hint(it, r, current_layer);
        };
        // regular radius
        update_radius_until_layer(ceilRadius(config.getRadius(distance_to_top, 0) + m_current_min_xy_dist_delta));
        // the maximum radius that the radius with the min_xy_dist can achieve
        update_radius_until_layer(ceilRadius(config.getRadius(distance_to_top, 0)));
        update_radius_until_layer(ceilRadius(config.recommendedMinRadius(current_layer) + m_current_min_xy_dist_delta));
    }

    throw_on_cancel();

    // Copy to deque to use in parallel for later.
    std::vector<RadiusLayerPair> relevant_avoidance_radiis{ radius_until_layer.begin(), radius_until_layer.end() };

    // Append additional radiis needed for collision.
    // To calculate collision holefree for every radius, the collision of radius m_increase_until_radius will be required.
    radius_until_layer[ceilRadius(m_increase_until_radius + m_current_min_xy_dist_delta)] = max_layer;
    // Collision for radius 0 needs to be calculated everywhere, as it will be used to ensure valid xy_distance in drawAreas.
    radius_until_layer[0] = max_layer;
    if (m_current_min_xy_dist_delta != 0)
        radius_until_layer[m_current_min_xy_dist_delta] = max_layer;

    // Now that required_avoidance_limit contains the maximum of ild and regular required radius just copy.
    std::vector<RadiusLayerPair> relevant_collision_radiis{ radius_until_layer.begin(), radius_until_layer.end() };

    // Calculate the relevant collisions
    calculateCollision(relevant_collision_radiis, throw_on_cancel);

    // calculate a separate Collisions with all holes removed. These are relevant for some avoidances that try to avoid holes (called safe)
    std::vector<RadiusLayerPair> relevant_hole_collision_radiis;
    for (RadiusLayerPair key : relevant_avoidance_radiis)
        if (key.first < m_increase_until_radius + m_current_min_xy_dist_delta)
            relevant_hole_collision_radiis.emplace_back(key);

    // Calculate collisions without holes, built from regular collision
    calculateCollisionHolefree(relevant_hole_collision_radiis, throw_on_cancel);
    // Let placables be calculated from calculateAvoidance() for better parallelization.
    if (m_support_rests_on_model)
        calculatePlaceables(relevant_avoidance_radiis, throw_on_cancel);

    auto t_coll = std::chrono::high_resolution_clock::now();

    // Calculate the relevant avoidances in parallel as far as possible
    {
        tbb::task_group task_group;
        task_group.run([this, relevant_avoidance_radiis, throw_on_cancel]{ calculateAvoidance(relevant_avoidance_radiis, true, m_support_rests_on_model, throw_on_cancel); });
        task_group.run([this, relevant_avoidance_radiis, throw_on_cancel]{ calculateWallRestrictions(relevant_avoidance_radiis, throw_on_cancel); });
        task_group.wait();
    }
    auto t_end = std::chrono::high_resolution_clock::now();
    auto dur_col = 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(t_coll - t_start).count();
    auto dur_avo = 0.001 * std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_coll).count();

//    m_precalculated = true;
    BOOST_LOG_TRIVIAL(info) << "Precalculating collision took" << dur_col << " ms. Precalculating avoidance took " << dur_avo << " ms.";

#if 0
    // Paint caches into SVGs:
    auto paint_cache_into_SVGs = [this](const RadiusLayerPolygonCache &cache, std::string_view name) {
        const std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> sorted = cache.sorted();
        static constexpr const std::string_view colors[] = {
            "red", "green", "blue", "magenta", "orange"
        };
        static constexpr const size_t num_colors = sizeof(colors) / sizeof(colors[0]);
        for (size_t i = 0; i < sorted.size();) {
            // Find range of cache items with the same layer index.
            size_t j = i;
            for (++ j; j < sorted.size() && sorted[i].first.second == sorted[j].first.second; ++ j) ;
            // Collect expolygons in reverse order (largest to smallest).
            std::vector<std::pair<Slic3r::ExPolygons, SVG::ExPolygonAttributes>> expolygons_with_attributes;
            for (int k = int(j - 1); k >= int(i); -- k) {
                std::string legend = format("radius-%1%", unscaled<float>(sorted[k].first.first));
                expolygons_with_attributes.push_back({ union_ex(sorted[k].second), SVG::ExPolygonAttributes(legend, std::string(colors[(k - int(i)) % num_colors]), 1.) });
            }
            // Render the range of per radius collision polygons into a common SVG.
            SVG::export_expolygons(debug_out_path("treesupport_cache-%s-%d.svg", name.data(), sorted[i].first.second), expolygons_with_attributes);
            i = j;
        }
    };
    paint_cache_into_SVGs(m_collision_cache,                    "collision_cache");
    paint_cache_into_SVGs(m_collision_cache_holefree,           "collision_cache_holefree");
    paint_cache_into_SVGs(m_avoidance_cache,                    "avoidance_cache");
    paint_cache_into_SVGs(m_avoidance_cache_slow,               "avoidance_cache_slow");
    paint_cache_into_SVGs(m_avoidance_cache_to_model,           "avoidance_cache_to_model");
    paint_cache_into_SVGs(m_avoidance_cache_to_model_slow,      "avoidance_cache_to_model_slow");
    paint_cache_into_SVGs(m_placeable_areas_cache,              "placable_areas_cache");
    paint_cache_into_SVGs(m_avoidance_cache_holefree,           "avoidance_cache_holefree");
    paint_cache_into_SVGs(m_avoidance_cache_holefree_to_model,  "avoidance_cache_holefree_to_model");
    paint_cache_into_SVGs(m_wall_restrictions_cache,            "wall_restrictions_cache");
    paint_cache_into_SVGs(m_wall_restrictions_cache_min,        "wall_restrictions_cache_min");
#endif
}

const Polygons& TreeModelVolumes::getCollision(const coord_t orig_radius, LayerIndex layer_idx, bool min_xy_dist) const
{
    const coord_t radius = this->ceilRadius(orig_radius, min_xy_dist);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_collision_cache.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate collision at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Collision requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateCollision(radius, layer_idx, {});
    return getCollision(orig_radius, layer_idx, min_xy_dist);
}

// Get a collision area at a given layer for a radius that is a lower or equial to the key radius.
// It is expected that the collision area is precalculated for a given layer at least for the radius zero.
// Used for pushing tree supports away from object during the final Organic optimization step.
std::optional<std::pair<coord_t, std::reference_wrapper<const Polygons>>> TreeModelVolumes::get_collision_lower_bound_area(LayerIndex layer_id, coord_t max_radius) const
{
    return m_collision_cache.get_lower_bound_area({ max_radius, layer_id });
}

// Private. Only called internally by calculateAvoidance() and calculateAvoidanceToModel(), radius is already snapped to grid.
const Polygons& TreeModelVolumes::getCollisionHolefree(coord_t radius, LayerIndex layer_idx) const
{
    assert(radius == this->ceilRadius(radius));
    assert(radius < m_increase_until_radius + m_current_min_xy_dist_delta);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_collision_cache_holefree.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate collision holefree at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Holefree Collision requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateCollisionHolefree({ radius, layer_idx });
    return getCollisionHolefree(radius, layer_idx);
}

const Polygons& TreeModelVolumes::getAvoidance(const coord_t orig_radius, LayerIndex layer_idx, AvoidanceType type, bool to_model, bool min_xy_dist) const
{
    if (layer_idx == 0) // What on the layer directly above buildplate do i have to avoid to reach the buildplate ...
        return getCollision(orig_radius, layer_idx, min_xy_dist);

    const coord_t radius = this->ceilRadius(orig_radius, min_xy_dist);
    if (type == AvoidanceType::FastSafe && radius >= m_increase_until_radius + m_current_min_xy_dist_delta)
        // no holes anymore by definition at this request
        type = AvoidanceType::Fast;

    if (std::optional<std::reference_wrapper<const Polygons>> result = 
            this->avoidance_cache(type, to_model).getArea({ radius, layer_idx }); 
        result)
        return (*result).get();

    if (m_precalculated) {
        if (to_model) {
            BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Avoidance to model at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
            tree_supports_show_error("Not precalculated Avoidance(to model) requested."sv, false);
        } else {
            BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Avoidance at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
            tree_supports_show_error("Not precalculated Avoidance(to buildplate) requested."sv, false);
        }
    }
    const_cast<TreeModelVolumes*>(this)->calculateAvoidance({ radius, layer_idx }, ! to_model, to_model);
    // Retrive failed and correct result was calculated. Now it has to be retrived.
    return getAvoidance(orig_radius, layer_idx, type, to_model, min_xy_dist);
}

const Polygons& TreeModelVolumes::getPlaceableAreas(const coord_t orig_radius, LayerIndex layer_idx, std::function<void()> throw_on_cancel) const
{
    if (orig_radius == 0)
        return this->getCollision(0, layer_idx, true);

    const coord_t radius = ceilRadius(orig_radius);
    if (std::optional<std::reference_wrapper<const Polygons>> result = m_placeable_areas_cache.getArea({ radius, layer_idx }); result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Placeable Areas at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error("Not precalculated Placeable areas requested."sv, false);
    }
    const_cast<TreeModelVolumes*>(this)->calculatePlaceables(radius, layer_idx, throw_on_cancel);
    return getPlaceableAreas(orig_radius, layer_idx, throw_on_cancel);
}

const Polygons& TreeModelVolumes::getWallRestriction(const coord_t orig_radius, LayerIndex layer_idx, bool min_xy_dist) const
{
    assert(layer_idx > 0);
    if (layer_idx == 0) 
        // Should never be requested as there will be no going below layer 0 ..., 
        // but just to be sure some semi-sane catch. Alternative would be empty Polygon.
        return getCollision(orig_radius, layer_idx, min_xy_dist);

    min_xy_dist &= m_current_min_xy_dist_delta > 0;

    const coord_t radius = ceilRadius(orig_radius);
    if (std::optional<std::reference_wrapper<const Polygons>> result = 
        (min_xy_dist ? m_wall_restrictions_cache_min : m_wall_restrictions_cache).getArea({ radius, layer_idx });
        result)
        return (*result).get();
    if (m_precalculated) {
        BOOST_LOG_TRIVIAL(error_level_not_in_cache) << "Had to calculate Wall restricions at radius " << radius << " and layer " << layer_idx << ", but precalculate was called. Performance may suffer!";
        tree_supports_show_error(
            min_xy_dist ? 
                "Not precalculated Wall restriction of minimum xy distance requested )." :
                "Not precalculated Wall restriction requested )."sv
            , false);
    }
    const_cast<TreeModelVolumes*>(this)->calculateWallRestrictions({ radius, layer_idx });
    return getWallRestriction(orig_radius, layer_idx, min_xy_dist); // Retrieve failed and correct result was calculated. Now it has to be retrieved.
}

void TreeModelVolumes::calculateCollision(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&](const tbb::blocked_range<size_t> &range) {
        for (size_t ikey = range.begin(); ikey != range.end(); ++ ikey) {
            const LayerIndex radius        = keys[ikey].first;
            const size_t     max_layer_idx = keys[ikey].second;
            // recursive call to parallel_for.
            calculateCollision(radius, max_layer_idx, throw_on_cancel);
        }
    });
}

void TreeModelVolumes::calculateCollision(const coord_t radius, const LayerIndex max_layer_idx, std::function<void()> throw_on_cancel)
{
//    assert(radius == this->ceilRadius(radius));

    // Process the outlines from least layers to most layers so that the final union will run over the longest vector.
    std::vector<size_t> layer_outline_indices(m_layer_outlines.size(), 0);
    std::iota(layer_outline_indices.begin(), layer_outline_indices.end(), 0);
    std::sort(layer_outline_indices.begin(), layer_outline_indices.end(),
        [this](size_t i, size_t j) { return m_layer_outlines[i].second.size() < m_layer_outlines[j].second.size(); });

    const LayerIndex            min_layer_last = m_collision_cache.getMaxCalculatedLayer(radius);
    std::vector<Polygons>       data(max_layer_idx + 1 - min_layer_last, Polygons{});
    const bool                  calculate_placable = m_support_rests_on_model && radius == 0;
    std::vector<Polygons>       data_placeable;
    if (calculate_placable)
        data_placeable = std::vector<Polygons>(max_layer_idx + 1 - min_layer_last, Polygons{});

    for (size_t outline_idx : layer_outline_indices)
        if (const std::vector<Polygons> &outlines = m_layer_outlines[outline_idx].second; ! outlines.empty()) {
            const TreeSupportMeshGroupSettings  &settings = m_layer_outlines[outline_idx].first;
            const coord_t       layer_height              = settings.layer_height;
            const coord_t       z_distance_bottom         = settings.support_bottom_distance;
            const int           z_distance_bottom_layers  = round_up_divide<int>(z_distance_bottom, layer_height);
            const int           z_distance_top_layers     = round_up_divide<int>(settings.support_top_distance, layer_height);
            const LayerIndex    max_required_layer        = std::min<LayerIndex>(outlines.size(), max_layer_idx + std::max(coord_t(1), z_distance_top_layers));
            const LayerIndex    min_layer_bottom          = std::max<LayerIndex>(0, min_layer_last - int(z_distance_bottom_layers));
            const coord_t       xy_distance               = outline_idx == m_current_outline_idx ? m_current_min_xy_dist : 
                // technically this causes collision for the normal xy_distance to be larger by m_current_min_xy_dist_delta for all 
                // not currently processing meshes as this delta will be added at request time.
                // avoiding this would require saving each collision for each outline_idx separately.
                // and later for each avoidance... But avoidance calculation has to be for the whole scene and can NOT be done for each outline_idx separately and combined later.
                // so avoiding this inaccuracy seems infeasible as it would require 2x the avoidance calculations => 0.5x the performance.
                //FIXME support_xy_distance is not corrected for "soluble" flag, see TreeSupportSettings constructor.
                settings.support_xy_distance;

            // 1) Calculate offsets of collision areas in parallel.
            std::vector<Polygons> collision_areas_offsetted(max_required_layer + 1 - min_layer_bottom);
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(min_layer_bottom, max_required_layer + 1),
                [&outlines, &machine_border = m_machine_border, offset_value = radius + xy_distance, min_layer_bottom, &collision_areas_offsetted, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex> &range) {
                for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                    Polygons collision_areas = machine_border;
                    append(collision_areas, outlines[layer_idx]);
                    // jtRound is not needed here, as the overshoot can not cause errors in the algorithm, because no assumptions are made about the model.
                    // if a key does not exist when it is accessed it is added!
                    collision_areas_offsetted[layer_idx - min_layer_bottom] = offset_value == 0 ? union_(collision_areas) : offset(union_ex(collision_areas), offset_value, ClipperLib::jtMiter, 1.2);
                    if(throw_on_cancel)
                        throw_on_cancel();
                }
            });

            // 2) Sum over top / bottom ranges.
            const bool last = outline_idx == layer_outline_indices.size();
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(min_layer_last + 1, max_layer_idx + 1),
                [&collision_areas_offsetted, &anti_overhang = m_anti_overhang, min_layer_bottom, radius, z_distance_bottom_layers, z_distance_top_layers, min_resolution = m_min_resolution, &data, min_layer_last, last, &throw_on_cancel]
            (const tbb::blocked_range<LayerIndex>& range) {
                    for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++layer_idx) {
                        Polygons collisions;
                        for (int i = -z_distance_bottom_layers; i <= z_distance_top_layers; ++ i) {
                            int j = layer_idx + i - min_layer_bottom;
                            if (j >= 0 && j < int(collision_areas_offsetted.size()))
                                append(collisions, collision_areas_offsetted[j]);
                        }
                        collisions = last && layer_idx < int(anti_overhang.size()) ? union_(collisions, offset(union_ex(anti_overhang[layer_idx]), radius, ClipperLib::jtMiter, 1.2)) : union_(collisions);
                        auto &dst = data[layer_idx - (min_layer_last + 1)];
                        if (last) {
                            if (! dst.empty())
                                collisions = union_(collisions, dst);
                            dst = polygons_simplify(collisions, min_resolution);
                        } else
                            append(dst, collisions);
                        if (throw_on_cancel)
                            throw_on_cancel();
                    }
                });

            // 3) Optionally calculate placables.
            if (calculate_placable) {
                // Calculating both the collision areas and placable areas.
                tbb::parallel_for(tbb::blocked_range<LayerIndex>(std::max(min_layer_last + 1, z_distance_bottom_layers + 1), max_layer_idx + 1),
                    [&collision_areas_offsetted, &anti_overhang = m_anti_overhang, min_layer_bottom, z_distance_bottom_layers, last, min_resolution = m_min_resolution, &data_placeable, min_layer_last, &throw_on_cancel]
                (const tbb::blocked_range<LayerIndex>& range) {
                    for (LayerIndex layer_idx = range.begin(); layer_idx != range.end(); ++ layer_idx) {
                        LayerIndex layer_idx_below = layer_idx - (z_distance_bottom_layers + 1) - min_layer_bottom;
                        assert(layer_idx_below >= 0);
                        auto &current = collision_areas_offsetted[layer_idx - min_layer_bottom];
                        auto &below   = collision_areas_offsetted[layer_idx_below];
                        auto placable = diff(below, layer_idx < int(anti_overhang.size()) ? union_(current, anti_overhang[layer_idx - (z_distance_bottom_layers + 1)]) : current);
                        auto &dst     = data_placeable[layer_idx - (min_layer_last + 1)];
                        if (last) {
                            if (! dst.empty())
                                placable = union_(placable, dst);
                            dst = polygons_simplify(placable, min_resolution);
                        } else
                            append(dst, placable);
                        if (throw_on_cancel)
                            throw_on_cancel();
                    }
                });
            } else {
                // Calculating just the collision areas.
            }
        }
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    {
        std::lock_guard<std::mutex> critical_section(*m_critical_progress);
        if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL) {
            m_precalculation_progress += TREE_PROGRESS_PRECALC_COLL / keys.size();
            Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
        }
    }
#endif
    if (throw_on_cancel)
        throw_on_cancel();
    m_collision_cache.insert(std::move(data), min_layer_last + 1, radius);
    if (calculate_placable)
        m_placeable_areas_cache.insert(std::move(data_placeable), min_layer_last + 1, radius);
}

void TreeModelVolumes::calculateCollisionHolefree(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    LayerIndex max_layer = 0;
    for (long long unsigned int i = 0; i < keys.size(); i++)
        max_layer = std::max(max_layer, keys[i].second);

    tbb::parallel_for(tbb::blocked_range<LayerIndex>(0, max_layer + 1, keys.size()),
        [&](const tbb::blocked_range<LayerIndex> &range) {
        std::vector<std::pair<RadiusLayerPair, Polygons>> data;
        data.reserve(range.size() * keys.size());
        for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
            for (RadiusLayerPair key : keys)
                if (layer_idx <= key.second) {
                    // Logically increase the collision by m_increase_until_radius
                    coord_t radius = key.first;
                    assert(radius == this->ceilRadius(radius));
                    assert(radius < m_increase_until_radius + m_current_min_xy_dist_delta);
                    coord_t increase_radius_ceil = ceilRadius(m_increase_until_radius, false) - radius;
                    assert(increase_radius_ceil > 0);
                    // this union is important as otherwise holes(in form of lines that will increase to holes in a later step) can get unioned onto the area.
                    data.emplace_back(RadiusLayerPair(radius, layer_idx), polygons_simplify(
                        offset(union_ex(this->getCollision(m_increase_until_radius, layer_idx, false)),
                            5 - increase_radius_ceil, ClipperLib::jtRound, m_min_resolution),
                        m_min_resolution));
                    if (throw_on_cancel)
                        throw_on_cancel();
                }
        }
        m_collision_cache_holefree.insert(std::move(data));
    });
}

void TreeModelVolumes::calculateAvoidance(const std::vector<RadiusLayerPair> &keys, bool to_build_plate, bool to_model, std::function<void()> throw_on_cancel)
{
    // For every RadiusLayer pair there are 3 avoidances that have to be calculated.
    // Prepare tasks for parallelization.
    struct AvoidanceTask {
        AvoidanceType   type;
        coord_t         radius;
        LayerIndex      max_required_layer;
        bool            to_model;
        LayerIndex      start_layer;

        bool slow()     const { return this->type == AvoidanceType::Slow; }
        bool holefree() const { return this->type == AvoidanceType::FastSafe; }
    };

    std::vector<AvoidanceTask> avoidance_tasks;
    avoidance_tasks.reserve((int(to_build_plate) + int(to_model)) * keys.size() * size_t(AvoidanceType::Count));

    for (int iter_idx = 0; iter_idx < 2 * int(keys.size()) * int(AvoidanceType::Count); ++ iter_idx) {
        AvoidanceTask task{
            AvoidanceType(iter_idx % int(AvoidanceType::Count)),
            keys[iter_idx / 6].first,  // radius
            keys[iter_idx / 6].second, // max_layer
            ((iter_idx / 3) & 1) != 0  // to_model
        };
        // Ensure start_layer is at least 1 as if no avoidance was calculated yet getMaxCalculatedLayer() returns -1.
        task.start_layer = std::max<LayerIndex>(1, 1 + avoidance_cache(task.type, task.to_model).getMaxCalculatedLayer(task.radius));
        if (task.start_layer > task.max_required_layer) {
            BOOST_LOG_TRIVIAL(debug) << "Calculation requested for value already calculated?";
            continue;
        }
        if (! task.holefree() || task.radius < m_increase_until_radius + m_current_min_xy_dist_delta)
            avoidance_tasks.emplace_back(task);
    }

    if(throw_on_cancel)
        throw_on_cancel();

    tbb::parallel_for(tbb::blocked_range<size_t>(0, avoidance_tasks.size(), 1),
        [this, &avoidance_tasks, &throw_on_cancel](const tbb::blocked_range<size_t> &range) {
        for (size_t task_idx = range.begin(); task_idx < range.end(); ++ task_idx) {
            const AvoidanceTask &task = avoidance_tasks[task_idx];
            assert(! task.holefree() || task.radius < m_increase_until_radius + m_current_min_xy_dist_delta);
            if (task.to_model)
                // ensuring Placeableareas are calculated
                //FIXME pass throw_on_cancel
                getPlaceableAreas(task.radius, task.max_required_layer, throw_on_cancel);
            // The following loop propagating avoidance regions bottom up is inherently serial.
            const bool  collision_holefree = (task.slow() || task.holefree()) && task.radius < m_increase_until_radius + m_current_min_xy_dist_delta;
            const float max_move           = task.slow() ? m_max_move_slow : m_max_move;
            // Limiting the offset step so that unioning the shrunk latest_avoidance with the current layer collisions
            // will not create gaps in the resulting avoidance region letting a tree support branch tunneling through an object wall.
            float move_step      = 1.9 * std::max(task.radius, m_current_min_xy_dist);
            int   move_steps     = round_up_divide<int>(max_move, move_step);
            assert(move_steps > 0);
            float last_move_step = max_move - (move_steps - 1) * move_step;
            if (last_move_step < scaled<float>(0.05)) {
                assert(move_steps > 1);
                if (move_steps > 1) {
                    // Avoid taking a very short last step, stretch the other steps a bit instead.
                    move_step = max_move / (-- move_steps);
                    last_move_step = move_step;
                }
            }
            // minDist as the delta was already added, also avoidance for layer 0 will return the collision.
            Polygons    latest_avoidance   = getAvoidance(task.radius, task.start_layer - 1, task.type, task.to_model, true);
            std::vector<std::pair<RadiusLayerPair, Polygons>> data;
            data.reserve(task.max_required_layer + 1 - task.start_layer);
            for (LayerIndex layer_idx = task.start_layer; layer_idx <= task.max_required_layer; ++ layer_idx) {
                // Merge current layer collisions with shrunk last_avoidance.
                const Polygons &current_layer_collisions = collision_holefree ? getCollisionHolefree(task.radius, layer_idx) : getCollision(task.radius, layer_idx, true);
                // For mildly steep branch angles only one step will be taken.
                for (int istep = 0; istep < move_steps; ++ istep)
                    latest_avoidance = union_(current_layer_collisions,
                        offset(latest_avoidance,
                            istep + 1 == move_steps ? - last_move_step : - move_step,
                            ClipperLib::jtRound, m_min_resolution));
                if (task.to_model)
                    latest_avoidance = diff(latest_avoidance, getPlaceableAreas(task.radius, layer_idx, throw_on_cancel));
                latest_avoidance = polygons_simplify(latest_avoidance, m_min_resolution);
                data.emplace_back(RadiusLayerPair{task.radius, layer_idx}, latest_avoidance);
                if (throw_on_cancel)
                    throw_on_cancel();
            }
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
            {
                std::lock_guard<std::mutex> critical_section(*m_critical_progress);
                if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL + TREE_PROGRESS_PRECALC_AVO) {
                    m_precalculation_progress += to_model ? 
                        0.4 * TREE_PROGRESS_PRECALC_AVO / (keys.size() * 3) :
                        m_support_rests_on_model ? 0.4 : 1 * TREE_PROGRESS_PRECALC_AVO / (keys.size() * 3);
                    Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
                }
            }
#endif
            avoidance_cache(task.type, task.to_model).insert(std::move(data));
        }
    });
}


void TreeModelVolumes::calculatePlaceables(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&, keys](const tbb::blocked_range<size_t>& range) {
            for (size_t key_idx = range.begin(); key_idx < range.end(); ++ key_idx)
                this->calculatePlaceables(keys[key_idx].first, keys[key_idx].second, throw_on_cancel);
        });
}

void TreeModelVolumes::calculatePlaceables(const coord_t radius, const LayerIndex max_required_layer, std::function<void()> throw_on_cancel)
{
    LayerIndex start_layer = 1 + m_placeable_areas_cache.getMaxCalculatedLayer(radius);
    if (start_layer > max_required_layer) {
        BOOST_LOG_TRIVIAL(debug) << "Requested calculation for value already calculated ?";
        return;
    }

    std::vector<Polygons> data(max_required_layer + 1 - start_layer, Polygons{});

    if (start_layer == 0)
        data[0] = diff(m_machine_border, getCollision(radius, 0, true));

    tbb::parallel_for(tbb::blocked_range<LayerIndex>(std::max(1, start_layer), max_required_layer + 1),
        [this, &data, radius, start_layer, &throw_on_cancel](const tbb::blocked_range<LayerIndex>& range) {
            for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                data[layer_idx - start_layer] = offset(
                    union_ex(getPlaceableAreas(0, layer_idx, throw_on_cancel)),
                    // As a placeable area is calculated by (collision of the layer below) - (collision of the current layer) and the collision is offset by xy_distance, 
                    // it can happen that a small line is considered a flat area to place something onto, even though it is mostly 
                    // xy_distance that cant support it. Making the area smaller by xy_distance fixes this.
                    - (radius + m_current_min_xy_dist + m_current_min_xy_dist_delta),
                    jtMiter, 1.2);
                if(throw_on_cancel)
                    throw_on_cancel();
            }
        });
#ifdef SLIC3R_TREESUPPORTS_PROGRESS
    {
        std::lock_guard<std::mutex> critical_section(*m_critical_progress);
        if (m_precalculated && m_precalculation_progress < TREE_PROGRESS_PRECALC_COLL + TREE_PROGRESS_PRECALC_AVO) {
            m_precalculation_progress += 0.2 * TREE_PROGRESS_PRECALC_AVO / (keys.size());
            Progress::messageProgress(Progress::Stage::SUPPORT, m_precalculation_progress * m_progress_multiplier + m_progress_offset, TREE_PROGRESS_TOTAL);
        }
    }
#endif
    m_placeable_areas_cache.insert(std::move(data), start_layer, radius);
}

void TreeModelVolumes::calculateWallRestrictions(const std::vector<RadiusLayerPair> &keys, std::function<void()> throw_on_cancel)
{
    // Wall restrictions are mainly important when they represent actual walls that are printed, and not "just" the configured z_distance, because technically valid placement is no excuse for moving through a wall.
    // As they exist to prevent accidentially moving though a wall at high speed between layers like thie (x = wall,i = influence area,o= empty space,d = blocked area because of z distance) Assume maximum movement distance is two characters and maximum safe movement distance of one character

    /* Potential issue addressed by the wall restrictions: Influence area may lag through a wall
     *  layer z+1:iiiiiiiiiiioooo
     *  layer z+0:xxxxxiiiiiiiooo
     *  layer z-1:ooooixxxxxxxxxx
     */

    // The radius for the upper collission has to be 0 as otherwise one may not enter areas that may be forbidden on layer_idx but not one below (c = not an influence area even though it should ):
    /*
     *  layer z+1:xxxxxiiiiiioo
     *  layer z+0:dddddiiiiiiio
     *  layer z-1:dddocdddddddd
     */
    // Also there can not just the collision of the lower layer be used because if it were:
    /*
     *  layer z+1:dddddiiiiiiiiiio
     *  layer z+0:xxxxxddddddddddc
     *  layer z-1:dddddxxxxxxxxxxc
     */
    // Or of the upper layer be used because if it were:
    /*
     *  layer z+1:dddddiiiiiiiiiio
     *  layer z+0:xxxxcddddddddddc
     *  layer z-1:ddddcxxxxxxxxxxc
     */

    // And just offseting with maximum movement distance (and not in multiple steps) could cause:
    /*
     *  layer z:   oxiiiiiiiiioo
     *  layer z-1: ixiiiiiiiiiii
     */

    tbb::parallel_for(tbb::blocked_range<size_t>(0, keys.size()),
        [&, keys](const tbb::blocked_range<size_t> &range) {
        for (size_t key_idx = range.begin(); key_idx < range.end(); ++ key_idx) {
            const coord_t    radius             = keys[key_idx].first;
            const LayerIndex max_required_layer = keys[key_idx].second;
            const coord_t    min_layer_bottom   = std::max(1, m_wall_restrictions_cache.getMaxCalculatedLayer(radius));
            const size_t     buffer_size        = max_required_layer + 1 - min_layer_bottom;
            std::vector<Polygons> data(buffer_size, Polygons{});
            std::vector<Polygons> data_min;
            if (m_current_min_xy_dist_delta > 0)
                data_min.assign(buffer_size, Polygons{});
            tbb::parallel_for(tbb::blocked_range<LayerIndex>(min_layer_bottom, max_required_layer + 1),
                [this, &data, &data_min, radius, min_layer_bottom, &throw_on_cancel](const tbb::blocked_range<LayerIndex> &range) {
                for (LayerIndex layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                    data[layer_idx - min_layer_bottom] = polygons_simplify(
                        // radius contains m_current_min_xy_dist_delta already if required
                        intersection(getCollision(0, layer_idx, false), getCollision(radius, layer_idx - 1, true)),
                        m_min_resolution);
                    if (! data_min.empty())
                        data_min[layer_idx - min_layer_bottom] = 
                            polygons_simplify(
                                intersection(getCollision(0, layer_idx, true), getCollision(radius, layer_idx - 1, true)),
                                m_min_resolution);
                    if (throw_on_cancel)
                        throw_on_cancel();
                }
            });
            m_wall_restrictions_cache.insert(std::move(data), min_layer_bottom, radius);
            if (! data_min.empty())
                m_wall_restrictions_cache_min.insert(std::move(data_min), min_layer_bottom, radius);
        }
    });
}

coord_t TreeModelVolumes::ceilRadius(const coord_t radius) const
{
    if (radius == 0)
        return 0;

    coord_t out = m_radius_0;
    if (radius > m_radius_0) {
        // generate SUPPORT_TREE_PRE_EXPONENTIAL_STEPS of radiis before starting to exponentially increase them.
        coord_t initial_radius_delta = SUPPORT_TREE_EXPONENTIAL_THRESHOLD - m_radius_0;
        auto ignore = [this](coord_t r) { return std::binary_search(m_ignorable_radii.begin(), m_ignorable_radii.end(), r); };
        if (initial_radius_delta > SUPPORT_TREE_COLLISION_RESOLUTION) {
            const int num_steps = round_up_divide(initial_radius_delta, SUPPORT_TREE_EXPONENTIAL_THRESHOLD);
            const int stepsize  = initial_radius_delta / num_steps;
            out += stepsize;
            for (auto step = 0; step < num_steps; ++ step) {
                if (out >= radius && ! ignore(out))
                    return out;
                out += stepsize;
            }
        } else
            out += SUPPORT_TREE_COLLISION_RESOLUTION;
        while (out < radius || ignore(out)) {
            assert(out * SUPPORT_TREE_EXPONENTIAL_FACTOR > out + SUPPORT_TREE_COLLISION_RESOLUTION);
            out = out * SUPPORT_TREE_EXPONENTIAL_FACTOR;
        }
    }
    return out;
}

void TreeModelVolumes::RadiusLayerPolygonCache::allocate_layers(size_t num_layers)
{
    if (num_layers > m_data.size()) {
        if (num_layers > m_data.capacity())
            m_data.reserve(next_highest_power_of_2(num_layers));
        m_data.resize(num_layers, {});
    }
}

// For debugging purposes, sorted by layer index, then by radius.
std::vector<std::pair<TreeModelVolumes::RadiusLayerPair, std::reference_wrapper<const Polygons>>> TreeModelVolumes::RadiusLayerPolygonCache::sorted() const
{
    std::vector<std::pair<RadiusLayerPair, std::reference_wrapper<const Polygons>>> out;
    for (auto &layer : m_data) {
        auto layer_idx = LayerIndex(&layer - m_data.data());
        for (auto &radius_polygons : layer)
            out.emplace_back(std::make_pair(radius_polygons.first, layer_idx), radius_polygons.second);
    }
    assert(std::is_sorted(out.begin(), out.end(), [](auto &l, auto &r){ return l.first.second < r.first.second || (l.first.second == r.first.second) && l.first.first < r.first.first; }));
    return out;
}

} // namespace Slic3r::TreeSupport3D
