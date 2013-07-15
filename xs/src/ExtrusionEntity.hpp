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
};

void
ExtrusionPath::reverse()
{
    this->polyline.reverse();
}

}

#endif
