#ifndef slic3r_AvoidCrossingPerimeters_hpp_
#define slic3r_AvoidCrossingPerimeters_hpp_

#include "../libslic3r.h"
#include "../ExPolygon.hpp"
#include "../EdgeGrid.hpp"

namespace Slic3r {

// Forward declarations.
class GCode;
class Layer;
class MotionPlanner;
class Point;
class Print;
class PrintObject;

struct PrintInstance;
using PrintObjectPtrs = std::vector<PrintObject *>;

class AvoidCrossingPerimeters
{
public:
    // this flag triggers the use of the external configuration space
    bool use_external_mp;
    bool use_external_mp_once; // just for the next travel move

    // this flag disables avoid_crossing_perimeters just for the next travel move
    // we enable it by default for the first travel move in print
    bool disable_once;

    AvoidCrossingPerimeters() : use_external_mp(false), use_external_mp_once(false), disable_once(true) {}
    virtual ~AvoidCrossingPerimeters() = default;

    void reset()
    {
        m_external_mp.reset();
        m_layer_mp.reset();
    }
    virtual void init_external_mp(const Print &print);
    virtual void init_layer_mp(const ExPolygons &islands) { m_layer_mp = Slic3r::make_unique<MotionPlanner>(islands); }

    virtual Polyline travel_to(const GCode &gcodegen, const Point &point);

    virtual Polyline travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled)
    {
        *could_be_wipe_disabled = true;
        return this->travel_to(gcodegen, point);
    }

protected:
    // For initializing the regions to avoid.
    static Polygons collect_contours_all_layers(const PrintObjectPtrs &objects);

    std::unique_ptr<MotionPlanner> m_external_mp;
    std::unique_ptr<MotionPlanner> m_layer_mp;
};

class AvoidCrossingPerimeters2 : public AvoidCrossingPerimeters
{
public:
    struct Intersection
    {
        // Index of the polygon containing this point of intersection.
        size_t border_idx;
        // Index of the line on the polygon containing this point of intersection.
        size_t line_idx;
        // Point of intersection projected on the travel path.
        Point  point_transformed;
        // Point of intersection.
        Point  point;

        Intersection(size_t border_idx, size_t line_idx, const Point &point_transformed, const Point &point)
            : border_idx(border_idx), line_idx(line_idx), point_transformed(point_transformed), point(point){};

        inline bool operator<(const Intersection &other) const { return this->point_transformed.x() < other.point_transformed.x(); }
    };

    enum class Direction { Forward, Backward };

private:
    static ExPolygons get_boundary(const Layer &layer);

    static ExPolygons get_boundary_external(const Layer &layer);

    static Direction get_shortest_direction(
        const Lines &lines, const size_t start_idx, const size_t end_idx, const Point &intersection_first, const Point &intersection_last);

    static Polyline simplify_travel(const EdgeGrid::Grid &edge_grid, const Polyline &travel);

    static size_t avoid_perimeters(const Polygons &boundaries, const EdgeGrid::Grid &grid, const Point &start, const Point &end, Polyline *result_out);

    bool need_wipe(const GCode &gcodegen, const Line &original_travel, const Polyline &result_travel, const size_t intersection_count);

    // Slice of layer with elephant foot compensation
    ExPolygons     m_slice;
    // Collection of boundaries used for detection of crossing perimetrs for travels inside object
    Polygons       m_boundaries;
    // Collection of boundaries used for detection of crossing perimetrs for travels outside object
    Polygons       m_boundaries_external;
    // Bounding box of m_boundaries
    BoundingBox    m_bbox;
    // Bounding box of m_boundaries_external
    BoundingBox    m_bbox_external;
    EdgeGrid::Grid m_grid;
    EdgeGrid::Grid m_grid_external;

public:
    AvoidCrossingPerimeters2() : AvoidCrossingPerimeters() {}

    virtual ~AvoidCrossingPerimeters2() = default;

    // Used for disabling unnecessary calling collect_contours_all_layers
    virtual void init_external_mp(const Print &print) override {};
    virtual void init_layer_mp(const ExPolygons &islands) override {};

    virtual Polyline travel_to(const GCode &gcodegen, const Point &point) override
    {
        bool could_be_wipe_disabled;
        return this->travel_to(gcodegen, point, &could_be_wipe_disabled);
    }

    virtual Polyline travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled) override;

    void init_layer(const Layer &layer);
};
} // namespace Slic3r

#endif // slic3r_AvoidCrossingPerimeters_hpp_