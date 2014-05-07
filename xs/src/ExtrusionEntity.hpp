#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include <myinit.h>
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygonCollection;
class ExtrusionEntityCollection;
class Extruder;

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
    ExtrusionEntity() : mm3_per_mm(-1), width(-1), height(-1) {};
    virtual ExtrusionEntity* clone() const = 0;
    virtual ~ExtrusionEntity() {};
    ExtrusionRole role;
    double mm3_per_mm;  // mm^3 of plastic per mm of linear head motion
    float width;
    float height;
    virtual void reverse() = 0;
    virtual Point first_point() const = 0;
    virtual Point last_point() const = 0;
    bool is_perimeter() const;
    bool is_fill() const;
    bool is_bridge() const;
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
    public:
    ExtrusionPath* clone() const;
    Polyline polyline;
    void reverse();
    Point first_point() const;
    Point last_point() const;
    void intersect_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    void subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(double distance);
    void simplify(double tolerance);
    double length() const;

    #ifdef SLIC3RXS
    std::string gcode(Extruder* extruder, double e, double F,
        double xofs, double yofs, std::string extrusion_axis,
        std::string gcode_line_suffix) const;
    #endif

    private:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;
};

class ExtrusionLoop : public ExtrusionEntity
{
    public:
    Polylines polylines;
    
    ExtrusionLoop(const Polygon &polygon, ExtrusionRole role);
    ExtrusionLoop* clone() const;
    void split_at_index(int index, ExtrusionPath* path) const;
    void split_at_first_point(ExtrusionPath* path) const;
    bool make_counter_clockwise();
    void reverse();
    Point first_point() const;
    Point last_point() const;
    void set_polygon(const Polygon &polygon);
    void polygon(Polygon* polygon) const;
};

}

#endif
