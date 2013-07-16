#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include <vector>
#include "Polyline.hpp"

namespace Slic3r {

class Polygon : public MultiPoint {
    public:
    Lines lines();
    Polyline* split_at_index(int index);
    Polyline* split_at_first_point();
};

typedef std::vector<Polygon> Polygons;

Lines
Polygon::lines()
{
    Lines lines;
    for (int i = 0; i < this->points.size()-1; i++) {
        lines.push_back(Line(this->points[i], this->points[i+1]));
    }
    lines.push_back(Line(this->points.back(), this->points.front()));
    return lines;
}

Polyline*
Polygon::split_at_index(int index)
{
    Polyline* poly = new Polyline;
    for (int i = index; i < this->points.size(); i++) {
        poly->points.push_back( this->points[i] );
    }
    for (int i = 0; i <= index; i++) {
        poly->points.push_back( this->points[i] );
    }
    return poly;
}

Polyline*
Polygon::split_at_first_point()
{
    return this->split_at_index(0);
}

}

#endif
