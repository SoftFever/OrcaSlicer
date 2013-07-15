#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Line.hpp"
#include "MultiPoint.hpp"

namespace Slic3r {

class Polyline : public MultiPoint {
    public:
    Lines lines();
};

typedef std::vector<Polyline> Polylines;

Lines
Polyline::lines()
{
    Lines lines;
    for (int i = 0; i < this->points.size()-1; i++) {
        lines.push_back(Line(this->points[i], this->points[i+1]));
    }
    return lines;
}

}

#endif
