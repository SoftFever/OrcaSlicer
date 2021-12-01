//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "Generator.hpp"
#include "TreeNode.hpp"

#include "../../ClipperUtils.hpp"
#include "../../Layer.hpp"
#include "../../Print.hpp"
#include "../../Surface.hpp"

/* Possible future tasks/optimizations,etc.:
 * - Improve connecting heuristic to favor connecting to shorter trees
 * - Change which node of a tree is the root when that would be better in reconnectRoots.
 * - (For implementation in Infill classes & elsewhere): Outline offset, infill-overlap & perimeter gaps.
 * - Allow for polylines, i.e. merge Tims PR about polyline fixes
 * - Unit Tests?
 * - Optimization: let the square grid store the closest point on boundary
 * - Optimization: only compute the closest dist to / point on boundary for the outer cells and flood-fill the rest
 * - Make a pass with Arachne over the output. Somehow.
 * - Generate all to-be-supported points at once instead of sequentially: See branch interlocking_gen PolygonUtils::spreadDots (Or work with sparse grids.)
 * - Lots of magic values ... to many to parameterize. But are they the best?
 * - Move more complex computations from Generator constructor to elsewhere.
 */

namespace Slic3r::FillLightning {

Generator::Generator(const PrintObject &print_object)
{
    const PrintConfig         &print_config         = print_object.print()->config();
    const PrintObjectConfig   &object_config        = print_object.config();
    const PrintRegionConfig   &region_config        = print_object.shared_regions()->all_regions.front()->config();
    const std::vector<double> &nozzle_diameters     = print_config.nozzle_diameter.values;
    double                     max_nozzle_diameter  = *std::max_element(nozzle_diameters.begin(), nozzle_diameters.end());
//    const int                  infill_extruder      = region_config.infill_extruder.value;
    const double               default_infill_extrusion_width = Flow::auto_extrusion_width(FlowRole::frInfill, float(max_nozzle_diameter));
    // Note: There's not going to be a layer below the first one, so the 'initial layer height' doesn't have to be taken into account.
    const double               layer_thickness      = object_config.layer_height;

    m_infill_extrusion_width = scaled<float>(region_config.infill_extrusion_width.percent ? default_infill_extrusion_width * 0.01 * region_config.infill_extrusion_width : region_config.infill_extrusion_width);
    m_supporting_radius = scaled<coord_t>(m_infill_extrusion_width * 0.001 / region_config.fill_density);

    const double lightning_infill_overhang_angle = M_PI / 4; // 45 degrees
    const double lightning_infill_prune_angle = M_PI / 4; // 45 degrees
    const double lightning_infill_straightening_angle = M_PI / 4; // 45 degrees
    m_wall_supporting_radius = layer_thickness * std::tan(lightning_infill_overhang_angle);
    m_prune_length = layer_thickness * std::tan(lightning_infill_prune_angle);
    m_straightening_max_distance = layer_thickness * std::tan(lightning_infill_straightening_angle);

    generateInitialInternalOverhangs(print_object);
    generateTrees(print_object);
}

void Generator::generateInitialInternalOverhangs(const PrintObject &print_object)
{
    m_overhang_per_layer.resize(print_object.layers().size());
    const float infill_wall_offset = - m_infill_extrusion_width;

    Polygons infill_area_above;
    //Iterate from top to bottom, to subtract the overhang areas above from the overhang areas on the layer below, to get only overhang in the top layer where it is overhanging.
    for (int layer_nr = print_object.layers().size() - 1; layer_nr >= 0; layer_nr--) {
        Polygons infill_area_here;
        for (const LayerRegion* layerm : print_object.get_layer(layer_nr)->regions())
            for (const Surface& surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal)
                    append(infill_area_here, offset(surface.expolygon, infill_wall_offset));

        //Remove the part of the infill area that is already supported by the walls.
        Polygons overhang = diff(offset(infill_area_here, -m_wall_supporting_radius), infill_area_above);

        m_overhang_per_layer[layer_nr] = overhang;
        infill_area_above = std::move(infill_area_here);
    }
}

const Layer& Generator::getTreesForLayer(const size_t& layer_id) const
{
    assert(layer_id < m_lightning_layers.size());
    return m_lightning_layers[layer_id];
}

void Generator::generateTrees(const PrintObject &print_object)
{
    m_lightning_layers.resize(print_object.layers().size());
    const coord_t infill_wall_offset = - m_infill_extrusion_width;

    std::vector<Polygons> infill_outlines(print_object.layers().size(), Polygons());

    // For-each layer from top to bottom:
    for (int layer_id = print_object.layers().size() - 1; layer_id >= 0; layer_id--)
        for (const LayerRegion *layerm : print_object.get_layer(layer_id)->regions())
            for (const Surface &surface : layerm->fill_surfaces.surfaces)
                if (surface.surface_type == stInternal)
                    append(infill_outlines[layer_id], offset(surface.expolygon, infill_wall_offset));

    // For various operations its beneficial to quickly locate nearby features on the polygon:
    const size_t top_layer_id = print_object.layers().size() - 1;
    EdgeGrid::Grid outlines_locator(get_extents(infill_outlines[top_layer_id]).inflated(SCALED_EPSILON));
    outlines_locator.create(infill_outlines[top_layer_id], locator_cell_size);

    // For-each layer from top to bottom:
    for (int layer_id = top_layer_id; layer_id >= 0; layer_id--)
    {
        Layer& current_lightning_layer = m_lightning_layers[layer_id];
        Polygons& current_outlines = infill_outlines[layer_id];

        // register all trees propagated from the previous layer as to-be-reconnected
        std::vector<NodeSPtr> to_be_reconnected_tree_roots = current_lightning_layer.tree_roots;

        current_lightning_layer.generateNewTrees(m_overhang_per_layer[layer_id], current_outlines, outlines_locator, m_supporting_radius, m_wall_supporting_radius);
        current_lightning_layer.reconnectRoots(to_be_reconnected_tree_roots, current_outlines, outlines_locator, m_supporting_radius, m_wall_supporting_radius);

        // Initialize trees for next lower layer from the current one.
        if (layer_id == 0)
            return;

        const Polygons& below_outlines = infill_outlines[layer_id - 1];
        outlines_locator.set_bbox(get_extents(below_outlines).inflated(SCALED_EPSILON));
        outlines_locator.create(below_outlines, locator_cell_size);

        std::vector<NodeSPtr>& lower_trees = m_lightning_layers[layer_id - 1].tree_roots;
        for (auto& tree : current_lightning_layer.tree_roots)
            tree->propagateToNextLayer(lower_trees, below_outlines, outlines_locator, m_prune_length, m_straightening_max_distance, locator_cell_size / 2);
    }
}

} // namespace Slic3r::FillLightning
