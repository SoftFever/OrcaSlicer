#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

enum ExtrusionRole {
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erContourInternalPerimeter,
    erFill,
    erSolidFill,
    erTopSolidFill,
    erBrige,
    erInternalBridge,
    erSkirt,
    erSupportMaterial,
    erGapFill,
};

class ExtrusionEntity
{
    public:
    ExtrusionRole role;
    double height;  // vertical thickness of the extrusion expressed in mm
    double flow_spacing;
};

class ExtrusionPath : public ExtrusionEntity
{
    public:
    Polyline polyline;
    void reverse();
};

class ExtrusionLoop : public ExtrusionEntity
{
    public:
    Polygon polygon;
    ExtrusionPath* split_at_index(int index);
    ExtrusionPath* split_at_first_point();
};

void
ExtrusionPath::reverse()
{
    this->polyline.reverse();
}

ExtrusionPath*
ExtrusionLoop::split_at_index(int index)
{
    Polyline* poly = this->polygon.split_at_index(index);
    
    ExtrusionPath* path = new ExtrusionPath();
    path->polyline      = *poly;
    path->role          = this->role;
    path->height        = this->height;
    path->flow_spacing  = this->flow_spacing;
    
    delete poly;
    return path;
}

ExtrusionPath*
ExtrusionLoop::split_at_first_point()
{
    return this->split_at_index(0);
}

}

#endif
