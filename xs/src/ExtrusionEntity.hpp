#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include <myinit.h>
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
    virtual ~ExtrusionEntity() {};
    ExtrusionRole role;
    double height;  // vertical thickness of the extrusion expressed in mm
    double flow_spacing;
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

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

}

#endif
