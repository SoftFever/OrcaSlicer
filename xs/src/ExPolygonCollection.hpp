#ifndef slic3r_ExPolygonCollection_hpp_
#define slic3r_ExPolygonCollection_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "ExPolygon.hpp"

namespace Slic3r {

class ExPolygonCollection
{
    public:
    ExPolygons expolygons;
    SV* arrayref();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
};

void
ExPolygonCollection::scale(double factor)
{
    for (ExPolygons::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (*it).scale(factor);
    }
}

void
ExPolygonCollection::translate(double x, double y)
{
   for (ExPolygons::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (*it).translate(x, y);
    }
}

void
ExPolygonCollection::rotate(double angle, Point* center)
{
    for (ExPolygons::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (*it)._rotate(angle, center);
    }
}

}

#endif
