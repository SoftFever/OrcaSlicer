#include "Layer.hpp"
#include "Print.hpp"

namespace Slic3r {
struct GridifyCfg
{
    const double            angle; // in rad
    const Point::coord_type gap_width;
    const int               gap_layers;
    const Point::coord_type grid_width;
    const float             inset;

    bool operator==(const GridifyCfg& r) const
    {
        return angle == r.angle
            && gap_width == r.gap_width
            && gap_layers == r.gap_layers
            && grid_width == r.grid_width
            && inset == r.inset;
    }

    bool operator!=(const GridifyCfg& r) const { return !(*this == r); }
};
} // namespace Slic3r
namespace std {
template<> struct hash<Slic3r::GridifyCfg>
{
    size_t operator()(const Slic3r::GridifyCfg& c) const noexcept
    {
        std::size_t seed = std::hash<double>{}(c.angle);
        boost::hash_combine(seed, std::hash<Slic3r::Point::coord_type>{}(c.gap_width));
        boost::hash_combine(seed, std::hash<int>{}(c.gap_layers));
        boost::hash_combine(seed, std::hash<Slic3r::Point::coord_type>{}(c.grid_width));
        boost::hash_combine(seed, std::hash<float>{}(c.inset));
        return seed;
    }
};
} // namespace std

namespace Slic3r {

// Offsets based on the eight queens puzzle
static constexpr size_t GRIDIFY_PATTERN_OFFSET_COUNT = 8;
static size_t           GRIDIFY_PATTERN_OFFSETS[GRIDIFY_PATTERN_OFFSET_COUNT][2] = {
    {4, 4}, {7, 1}, {1, 5}, {5, 2}, {3, 6}, {0, 3}, {6, 7}, {2, 0},
};

void Layer::gridify()
{
    // Group region that has the same gridify settings
    std::unordered_map<GridifyCfg, LayerRegionPtrs> regions;
    for (auto region : m_regions) {
        const auto region_config = region->region().config();
        if (region->slices.empty() || !region_config.gridify_enabled)continue;

        const GridifyCfg cfg{
            Geometry::deg2rad(region_config.gridify_angle.value),
            (Point::coord_type)scale_(region_config.gridify_gap_width.value),
            region_config.gridify_gap_layers,
            (Point::coord_type)scale_(region_config.gridify_grid_width),
            (float)scale_(region_config.gridify_inset),
        };
        auto& r = regions[cfg];
        r.push_back(region);
    }
    if (regions.empty())return;

    // Process each group individually
    for (const auto& [cfg, regs] : regions) {
        ExPolygons current_poly;
        for (const auto r : regs) {
            expolygons_append(current_poly, offset_ex(r->slices.surfaces, EPSILON));
        }
        current_poly = union_ex(current_poly);
        current_poly = offset_ex(current_poly, -EPSILON);

        // Apply pattern rotation
        expolygons_rotate(current_poly, -cfg.angle);

        // Generate pattern within boundary
        BoundingBox bounding_box = get_extents(current_poly);
        assert(bounding_box.defined);
        // Expand boundary a little bit to make sure no overlapping edges
        bounding_box.offset(scale_(1));

        // Draw patterns
        const auto&             grid_offset = GRIDIFY_PATTERN_OFFSETS[(id() / cfg.gap_layers) % GRIDIFY_PATTERN_OFFSET_COUNT];
        const Point::coord_type offset_x    = cfg.grid_width / GRIDIFY_PATTERN_OFFSET_COUNT * grid_offset[0];
        const Point::coord_type offset_y    = cfg.grid_width / GRIDIFY_PATTERN_OFFSET_COUNT * grid_offset[1];

        Polygons pattern;
        Point    start_pt = align_to_grid(bounding_box.min, Point(cfg.grid_width, cfg.grid_width));
        // Draw vertical stripes
        for (auto x = start_pt.x(); x < bounding_box.max.x(); x += cfg.grid_width) {
            const Point::coord_type x1 = x - cfg.gap_width / 2 + offset_x;

            auto& p = pattern.emplace_back();
            p.points.emplace_back(x1, bounding_box.min.y());
            p.points.emplace_back(x1 + cfg.gap_width, bounding_box.min.y());
            p.points.emplace_back(x1 + cfg.gap_width, bounding_box.max.y());
            p.points.emplace_back(x1, bounding_box.max.y());
        }
        // Draw horizontal stripes
        for (auto y = start_pt.y(); y < bounding_box.max.y(); y += cfg.grid_width) {
            const Point::coord_type y1 = y - cfg.gap_width / 2 + offset_y;

            auto& p = pattern.emplace_back();
            p.points.emplace_back(bounding_box.min.x(), y1);
            p.points.emplace_back(bounding_box.max.x(), y1);
            p.points.emplace_back(bounding_box.max.x(), y1 + cfg.gap_width);
            p.points.emplace_back(bounding_box.min.x(), y1 + cfg.gap_width);
        }

        ExPolygons ep = union_ex(pattern);

        // Apply inset
        if (cfg.inset > 0) {
            ep = intersection_ex(ep, offset_ex(current_poly, -cfg.inset));
        }

        // Rotation it back
        expolygons_rotate(ep, cfg.angle);

        // Apply the pattern to original slices
        for (const auto r : regs) {
            ExPolygons layer_polygons = to_expolygons(r->slices.surfaces);
            r->slices.set(diff_ex(layer_polygons, ep), stInternal);
        }
    }
}

}
