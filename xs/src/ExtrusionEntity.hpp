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
    virtual ExtrusionEntity* clone() const = 0;
    virtual ~ExtrusionEntity() {};
    ExtrusionRole role;
    double height;  // vertical thickness of the extrusion expressed in mm
    double flow_spacing;
    virtual void reverse() = 0;
    virtual Point* first_point() const = 0;
    virtual Point* last_point() const = 0;
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
    public:
    ExtrusionPath* clone() const;
    Polyline polyline;
    void reverse();
    Point* first_point() const;
    Point* last_point() const;
};

class ExtrusionLoop : public ExtrusionEntity
{
    public:
    ExtrusionLoop* clone() const;
    Polygon polygon;
    ExtrusionPath* split_at_index(int index) const;
    ExtrusionPath* split_at_first_point() const;
    bool make_counter_clockwise();
    void reverse();
    Point* first_point() const;
    Point* last_point() const;
};

}

#endif
