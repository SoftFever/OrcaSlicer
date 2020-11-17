#ifndef slic3r_AvoidCrossingPerimeters_hpp_
#define slic3r_AvoidCrossingPerimeters_hpp_

#include "../libslic3r.h"
#include "../ExPolygon.hpp"
#include "../EdgeGrid.hpp"

namespace Slic3r {

// Forward declarations.
class GCode;
class Layer;
class Point;

class AvoidCrossingPerimeters
{
public:
    // Routing around the objects vs. inside a single object.
    void        use_external_mp(bool use = true) { m_use_external_mp = use; };
    void        use_external_mp_once()  { m_use_external_mp_once = true; }
    void        disable_once()          { m_disabled_once = true; }
    bool        disabled_once() const   { return m_disabled_once; }
    void        reset_once_modifiers()  { m_use_external_mp_once = false; m_disabled_once = false; }

    void        init_layer(const Layer &layer);

    Polyline    travel_to(const GCode& gcodegen, const Point& point)
    {
        bool could_be_wipe_disabled;
        return this->travel_to(gcodegen, point, &could_be_wipe_disabled);
    }

    Polyline    travel_to(const GCode& gcodegen, const Point& point, bool* could_be_wipe_disabled);

private:
    bool           m_use_external_mp { false };
    // just for the next travel move
    bool           m_use_external_mp_once { false };
    // this flag disables avoid_crossing_perimeters just for the next travel move
    // we enable it by default for the first travel move in print
    bool           m_disabled_once { true };

    // Slice of layer with elephant foot compensation
    ExPolygons     m_slice;
    // Collection of boundaries used for detection of crossing perimetrs for travels inside object
    Polygons       m_boundaries;
    // Collection of boundaries used for detection of crossing perimetrs for travels outside object
    Polygons       m_boundaries_external;
    // Bounding box of m_boundaries
    BoundingBoxf   m_bbox;
    // Bounding box of m_boundaries_external
    BoundingBoxf   m_bbox_external;
    EdgeGrid::Grid m_grid;
    EdgeGrid::Grid m_grid_external;
};

} // namespace Slic3r

#endif // slic3r_AvoidCrossingPerimeters_hpp_
