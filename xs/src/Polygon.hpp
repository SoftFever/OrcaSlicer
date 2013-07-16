#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

#include <myinit.h>
#include <vector>
#include "Line.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class Polygon : public MultiPoint {
    public:
    SV* to_SV_ref();
    Lines lines();
    Polyline* split_at_index(int index);
    Polyline* split_at_first_point();
    bool is_counter_clockwise();
    bool make_counter_clockwise();
    bool make_clockwise();
};

typedef std::vector<Polygon> Polygons;

}

#endif
