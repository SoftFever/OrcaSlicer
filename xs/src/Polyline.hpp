#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include "Line.hpp"
#include "MultiPoint.hpp"

namespace Slic3r {

class Polyline : public MultiPoint {
    public:
    Lines lines();
};

typedef std::vector<Polyline> Polylines;

}

#endif
