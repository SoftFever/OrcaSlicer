#ifndef libslic3r_SeamPlacer_hpp_
#define libslic3r_SeamPlacer_hpp_

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class PrintObject;
class ExtrusionLoop;
class Print;
namespace EdgeGrid { class Grid; }

class SeamPlacer {
public:
    void init(const Print& print);

    bool is_custom(size_t layer_id) const {
        return ! ((m_enforcers.empty() || m_enforcers[layer_id].empty())
                && (m_blockers.empty() || m_blockers[layer_id].empty()));
    }

    Point get_seam(const size_t layer_idx, const SeamPosition seam_position,
                   const ExtrusionLoop& loop, Point last_pos,
                   coordf_t nozzle_diameter, const PrintObject* po,
                   bool was_clockwise, const EdgeGrid::Grid* lower_layer_edge_grid);

private:
    std::vector<ExPolygons> m_enforcers;
    std::vector<ExPolygons> m_blockers;

    std::map<const PrintObject*, Point>  m_last_seam_position;

    // Get indices of points inside enforcers and blockers.
    void get_indices(size_t layer_id,
                    const Polygon& polygon,
                    std::vector<size_t>& enforcers_idxs,
                    std::vector<size_t>& blockers_idxs) const;

    void penalize_polygon(const Polygon& polygon,
                          std::vector<float>& penalties,
                          const std::vector<float>& lengths,
                          int layer_id) const;

    static constexpr float ENFORCER_BLOCKER_PENALTY = 1e6;
};


}

#endif // libslic3r_SeamPlacer_hpp_
