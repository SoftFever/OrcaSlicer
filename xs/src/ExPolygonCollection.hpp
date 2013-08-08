#ifndef slic3r_ExPolygonCollection_hpp_
#define slic3r_ExPolygonCollection_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"

namespace Slic3r {

class ExPolygonCollection
{
    public:
    ExPolygons expolygons;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
};

}

#endif
