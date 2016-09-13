#ifndef slic3r_FillBase_hpp_
#define slic3r_FillBase_hpp_

#include <memory.h>
#include <float.h>
#include <stdint.h>

#include "../libslic3r.h"
#include "../BoundingBox.hpp"

namespace Slic3r {

class Surface;

struct FillParams
{
    FillParams() { memset(this, 0, sizeof(FillParams)); }

    coordf_t    width;
    // Fraction in <0, 1>
    float       density;
    coordf_t    distance;

    // Don't connect the fill lines around the inner perimeter.
    bool        dont_connect;

    // Don't adjust spacing to fill the space evenly.
    bool        dont_adjust;

    // For Honeycomb.
    // we were requested to complete each loop;
    // in this case we don't try to make more continuous paths
    bool        complete;
};

class Fill
{
public:
    // Index of the layer.
    size_t      layer_id;
    // Height of the layer, in unscaled coordinates
    coordf_t    z;
    // in unscaled coordinates
    coordf_t    spacing;
    // in radians, ccw, 0 = East
    float       angle;
    // in scaled coordinates
    coord_t     loop_clipping;
    // in scaled coordinates
    BoundingBox bounding_box;

public:
    virtual ~Fill() {}

    static Fill* new_from_type(const std::string &type);

    void         set_bounding_box(const Slic3r::BoundingBox &bbox) { bounding_box = bbox; }

    // Use bridge flow for the fill?
    virtual bool use_bridge_flow() const { return false; }

    // Do not sort the fill lines to optimize the print head path?
    virtual bool no_sort() const { return false; }

    // Perform the fill.
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
    Fill() :
        layer_id(size_t(-1)),
        z(0.f),
        spacing(0.f),
        // Initial angle is undefined.
        angle(FLT_MAX),
        loop_clipping(0),
        // The initial bounding box is empty, therefore undefined.
        bounding_box(Point(0, 0), Point(-1, -1))
        {}

    // The expolygon may be modified by the method to avoid a copy.
    virtual void    _fill_surface_single(
        const FillParams                &params, 
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction, 
        ExPolygon                       &expolygon, 
        Polylines                       &polylines_out) {}

    static coord_t  _adjust_solid_spacing(const coord_t width, const coord_t distance);

    virtual float _layer_angle(size_t idx) const { 
        bool odd = idx & 1;
        return (idx & 1) ? float(M_PI/2.) : 0;
    }

    virtual std::pair<float, Point> _infill_direction(const Surface *surface) const;

    // Align a coordinate to a grid. The coordinate may be negative,
    // the aligned value will never be bigger than the original one.
    static coord_t _align_to_grid(const coord_t coord, const coord_t spacing) {
        // Current C++ standard defines the result of integer division to be rounded to zero,
        // for both positive and negative numbers. Here we want to round down for negative
        // numbers as well.
        coord_t aligned = (coord < 0) ?
        		((coord - spacing + 1) / spacing) * spacing :
        		(coord / spacing) * spacing;
        assert(aligned <= coord);
        return aligned;
    }
    static Point   _align_to_grid(Point   coord, Point   spacing) 
        { return Point(_align_to_grid(coord.x, spacing.x), _align_to_grid(coord.y, spacing.y)); }
    static coord_t _align_to_grid(coord_t coord, coord_t spacing, coord_t base) 
        { return base + _align_to_grid(coord - base, spacing); }
    static Point   _align_to_grid(Point   coord, Point   spacing, Point   base)
        { return Point(_align_to_grid(coord.x, spacing.x, base.x), _align_to_grid(coord.y, spacing.y, base.y)); }
};

// An interface class to Perl, aggregating an instance of a Fill and a FillData.
class Filler
{
public:
    Filler() : fill(NULL) {}
    ~Filler() { 
        delete fill; 
        fill = NULL;
    }
    Fill        *fill;
    FillParams   params;
};

} // namespace Slic3r

#endif // slic3r_FillBase_hpp_
