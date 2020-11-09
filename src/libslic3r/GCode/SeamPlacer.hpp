#ifndef libslic3r_SeamPlacer_hpp_
#define libslic3r_SeamPlacer_hpp_

#include <optional>

#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"

namespace Slic3r {

class PrintObject;
class ExtrusionLoop;
class Print;
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

    Point get_seam(const size_t layer_idx, const SeamPosition seam_position,
                   const ExtrusionLoop& loop, Point last_pos,
                   coordf_t nozzle_diameter, const PrintObject* po,
                   bool was_clockwise, const EdgeGrid::Grid* lower_layer_edge_grid);

    using TreeType = AABBTreeIndirect::Tree<2, coord_t>;
    using AlignedBoxType = Eigen::AlignedBox<TreeType::CoordType, TreeType::NumDimensions>;

private:

    struct CustomTrianglesPerLayer {
        Polygons polys;
        TreeType tree;
    };


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
