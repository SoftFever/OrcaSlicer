#ifndef libslic3r_SeamPlacer_hpp_
#define libslic3r_SeamPlacer_hpp_

#include <optional>
#include <vector>

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"

namespace Slic3r {

class PrintObject;
class ExtrusionLoop;
class Print;
class Layer;
namespace EdgeGrid { class Grid; }


class SeamHistory {
public:
    SeamHistory() { clear(); }
    std::optional<Point> get_last_seam(const PrintObject* po, size_t layer_id, const BoundingBox& island_bb);
    void add_seam(const PrintObject* po, const Point& pos, const BoundingBox& island_bb);
    void clear();

private:
    struct SeamPoint {
        Point m_pos;
        BoundingBox m_island_bb;
    };

    std::map<const PrintObject*, std::vector<SeamPoint>> m_data_last_layer;
    std::map<const PrintObject*, std::vector<SeamPoint>> m_data_this_layer;
    size_t m_layer_id;
};



class SeamPlacer {
public:
    void init(const Print& print);

    // When perimeters are printed, first call this function with the respective
    // external perimeter. SeamPlacer will find a location for its seam and remember it.
    // Subsequent calls to get_seam will return this position.


    void plan_perimeters(const std::vector<const ExtrusionEntity*> perimeters,
        const Layer& layer, SeamPosition seam_position,
        Point last_pos, coordf_t nozzle_dmr, const PrintObject* po,
        const EdgeGrid::Grid* lower_layer_edge_grid);

    void place_seam(ExtrusionLoop& loop, const Point& last_pos, bool external_first, double nozzle_diameter,
                    const EdgeGrid::Grid* lower_layer_edge_grid);
    

    using TreeType = AABBTreeIndirect::Tree<2, coord_t>;
    using AlignedBoxType = Eigen::AlignedBox<TreeType::CoordType, TreeType::NumDimensions>;

private:

    // When given an external perimeter (!), returns the seam.
    Point calculate_seam(const Layer& layer, const SeamPosition seam_position,
        const ExtrusionLoop& loop, coordf_t nozzle_dmr, const PrintObject* po,
        const EdgeGrid::Grid* lower_layer_edge_grid, Point last_pos);

    struct CustomTrianglesPerLayer {
        Polygons polys;
        TreeType tree;
    };

    // Just a cache to save some lookups.
    const Layer* m_last_layer_po = nullptr;
    coordf_t m_last_print_z = -1.;
    const PrintObject* m_last_po = nullptr;

    struct SeamPoint {
        Point pt;
        bool precalculated = false;
        bool external = false;
        const Layer* layer = nullptr;
        SeamPosition seam_position;
        const PrintObject* po = nullptr;
    };
    std::vector<SeamPoint> m_plan;
    size_t m_plan_idx;

    std::vector<std::vector<CustomTrianglesPerLayer>> m_enforcers;
    std::vector<std::vector<CustomTrianglesPerLayer>> m_blockers;
    std::vector<const PrintObject*> m_po_list;

    //std::map<const PrintObject*, Point>  m_last_seam_position;
    SeamHistory  m_seam_history;

    // Get indices of points inside enforcers and blockers.
    void get_enforcers_and_blockers(size_t layer_id,
                                    const Polygon& polygon,
                                    size_t po_id,
                                    std::vector<size_t>& enforcers_idxs,
                                    std::vector<size_t>& blockers_idxs) const;

    // Apply penalties to points inside enforcers/blockers.
    void apply_custom_seam(const Polygon& polygon, size_t po_id,
                           std::vector<float>& penalties,
                           const std::vector<float>& lengths,
                           int layer_id, SeamPosition seam_position) const;

    // Return random point of a polygon. The distribution will be uniform
    // along the contour and account for enforcers and blockers.
    Point get_random_seam(size_t layer_idx, const Polygon& polygon, size_t po_id,
                          bool* saw_custom = nullptr) const;

    // Is there any enforcer/blocker on this layer?
    bool is_custom_seam_on_layer(size_t layer_id, size_t po_idx) const {
        return is_custom_enforcer_on_layer(layer_id, po_idx)
            || is_custom_blocker_on_layer(layer_id, po_idx);
    }

    bool is_custom_enforcer_on_layer(size_t layer_id, size_t po_idx) const {
        return (! m_enforcers.at(po_idx).empty() && ! m_enforcers.at(po_idx)[layer_id].polys.empty());
    }

    bool is_custom_blocker_on_layer(size_t layer_id, size_t po_idx) const {
        return (! m_blockers.at(po_idx).empty() && ! m_blockers.at(po_idx)[layer_id].polys.empty());
    }
};


}

#endif // libslic3r_SeamPlacer_hpp_
