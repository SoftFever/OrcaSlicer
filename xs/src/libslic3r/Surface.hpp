#ifndef slic3r_Surface_hpp_
#define slic3r_Surface_hpp_

#include "libslic3r.h"
#include "ExPolygon.hpp"

namespace Slic3r {

enum SurfaceType { stTop, stBottom, stBottomBridge, stInternal, stInternalSolid, stInternalBridge, stInternalVoid, stPerimeter };

class Surface
{
    public:
    SurfaceType     surface_type;
    ExPolygon       expolygon;
    double          thickness;          // in mm
    unsigned short  thickness_layers;   // in layers
    double          bridge_angle;       // in radians, ccw, 0 = East, only 0+ (negative means undefined)
    unsigned short  extra_perimeters;
    
    Surface(SurfaceType _surface_type, const ExPolygon &_expolygon)
        : surface_type(_surface_type), expolygon(_expolygon),
            thickness(-1), thickness_layers(1), bridge_angle(-1), extra_perimeters(0)
        {};
    operator Polygons() const;
    double area() const;
    bool is_solid() const;
    bool is_external() const;
    bool is_internal() const;
    bool is_bottom() const;
    bool is_bridge() const;
};

typedef std::vector<Surface> Surfaces;
typedef std::vector<Surface*> SurfacesPtr;

inline Polygons to_polygons(const SurfacesPtr &src)
{
    Polygons polygons;
    for (SurfacesPtr::const_iterator it = src.begin(); it != src.end(); ++it) {
        polygons.push_back((*it)->expolygon.contour);
        for (Polygons::const_iterator ith = (*it)->expolygon.holes.begin(); ith != (*it)->expolygon.holes.end(); ++ith) {
            polygons.push_back(*ith);
        }
    }
    return polygons;
}

#if SLIC3R_CPPVER > 11
inline Polygons to_polygons(SurfacesPtr &&src)
{
    Polygons polygons;
    for (ExPolygons::const_iterator it = src.begin(); it != src.end(); ++it) {
        polygons.push_back(std::move((*it)->expolygon.contour));
        for (Polygons::const_iterator ith = (*it)->expolygon.holes.begin(); ith != (*it)->expolygon.holes.end(); ++ith) {
            polygons.push_back(std::move(*ith));
        }
    }
    return polygons;
}
#endif

extern BoundingBox get_extents(const Surface &surface);
extern BoundingBox get_extents(const Surfaces &surfaces);
extern BoundingBox get_extents(const SurfacesPtr &surfaces);

class SVG;

extern const char* surface_type_to_color_name(const SurfaceType surface_type);
extern void export_surface_type_legend_to_svg(SVG &svg, const Point &pos);
extern Point export_surface_type_legend_to_svg_box_size();
extern bool export_to_svg(const char *path, const Surfaces &surfaces, const float transparency = 1.f);

}

#endif
