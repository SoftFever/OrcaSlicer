#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include <myinit.h>
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygonCollection;
class ExtrusionEntityCollection;
class Extruder;

/* Each ExtrusionRole value identifies a distinct set of { extruder, speed } */
enum ExtrusionRole {
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erTopSolidInfill,
    erBridgeInfill,
    erGapFill,
    erSkirt,
    erSupportMaterial,
    erSupportMaterialInterface,
};

/* Special flags describing loop */
enum ExtrusionLoopRole {
    elrDefault,
    elrExternalPerimeter,
    elrContourInternalPerimeter,
};

class ExtrusionEntity
{
    public:
    virtual ExtrusionEntity* clone() const = 0;
    virtual ~ExtrusionEntity() {};
    virtual void reverse() = 0;
    virtual Point first_point() const = 0;
    virtual Point last_point() const = 0;
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
    public:
    Polyline polyline;
    ExtrusionRole role;
    double mm3_per_mm;  // mm^3 of plastic per mm of linear head motion
    float width;
    float height;
    
    ExtrusionPath(ExtrusionRole role) : role(role), mm3_per_mm(-1), width(-1), height(-1) {};
    ExtrusionPath* clone() const;
    void reverse();
    Point first_point() const;
    Point last_point() const;
    void intersect_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    void subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(double distance);
    void simplify(double tolerance);
    double length() const;
    bool is_perimeter() const;
    bool is_fill() const;
    bool is_bridge() const;
    std::string gcode(Extruder* extruder, double e, double F,
        double xofs, double yofs, std::string extrusion_axis,
        std::string gcode_line_suffix) const;

    private:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;
};

typedef std::vector<ExtrusionPath> ExtrusionPaths;

class ExtrusionLoop : public ExtrusionEntity
{
    public:
    ExtrusionPaths paths;
    ExtrusionLoopRole role;
    
    ExtrusionLoop(ExtrusionLoopRole role = elrDefault) : role(role) {};
    operator Polygon() const;
    ExtrusionLoop* clone() const;
    bool make_clockwise();
    bool make_counter_clockwise();
    void reverse();
    Point first_point() const;
    Point last_point() const;
    void polygon(Polygon* polygon) const;
    double length() const;
    void split_at_vertex(const Point &point);
    void split_at(const Point &point);
    void clip_end(double distance, ExtrusionPaths* paths) const;
    bool has_overhang_point(const Point &point) const;
};

}

#endif
