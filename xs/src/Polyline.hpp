#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

#include "Line.hpp"
#include "MultiPoint.hpp"

namespace Slic3r {

class Polyline : public MultiPoint {
    public:
    Point* last_point() const;
    Lines lines() const;
    
    #ifdef SLIC3RXS
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    #endif
};

typedef std::vector<Polyline> Polylines;

}

#endif
