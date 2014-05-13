#ifndef slic3r_MotionPlanner_hpp_
#define slic3r_MotionPlanner_hpp_

#include <myinit.h>
#include "ClipperUtils.hpp"
#include "ExPolygonCollection.hpp"
#include "Polyline.hpp"
#include "visilibity.hpp"
#include <vector>

#define MP_INNER_MARGIN scale_(1.0)
#define MP_OUTER_MARGIN scale_(2.0)

namespace Slic3r {

class MotionPlanner
{
    public:
    MotionPlanner(const ExPolygons &islands);
    ~MotionPlanner();
    void shortest_path(const Point &from, const Point &to, Polyline* polyline);
    
    private:
    ExPolygons islands;
    bool initialized;
    ExPolygon outer;
    ExPolygonCollections inner;
    std::vector<VisiLibity::Environment*> envs;
    std::vector<VisiLibity::Visibility_Graph*> graphs;
    
    void initialize();
    void generate_environment(int island_idx);
    static VisiLibity::Polyline convert_polyline(const Polyline &polyline);
    static Polyline convert_polyline(const VisiLibity::Polyline &v_polyline);
    static VisiLibity::Polygon convert_polygon(const Polygon &polygon);
    static VisiLibity::Point convert_point(const Point &point);
    static Point convert_point(const VisiLibity::Point &v_point);
};

}

#endif
