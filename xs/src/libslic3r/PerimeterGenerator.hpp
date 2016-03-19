#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

#include "libslic3r.h"
#include <vector>
#include "ExPolygonCollection.hpp"
#include "Flow.hpp"
#include "Polygon.hpp"
#include "PrintConfig.hpp"
#include "SurfaceCollection.hpp"

namespace Slic3r {

class PerimeterGeneratorLoop;
typedef std::vector<PerimeterGeneratorLoop> PerimeterGeneratorLoops;

class PerimeterGeneratorLoop {
    public:
    Polygon polygon;
    bool is_contour;
    unsigned short depth;
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(Polygon polygon, unsigned short depth)
        : polygon(polygon), is_contour(false), depth(depth)
        {};
    bool is_external() const;
    bool is_internal_contour() const;
};

class PerimeterGenerator {
    public:
    const SurfaceCollection* slices;
    const ExPolygonCollection* lower_slices;
    double layer_height;
    int layer_id;
    Flow perimeter_flow;
    Flow ext_perimeter_flow;
    Flow overhang_flow;
    Flow solid_infill_flow;
    PrintRegionConfig* config;
    PrintObjectConfig* object_config;
    PrintConfig* print_config;
    ExtrusionEntityCollection* loops;
    ExtrusionEntityCollection* gap_fill;
    SurfaceCollection* fill_surfaces;
    
    PerimeterGenerator(const SurfaceCollection* slices, double layer_height, Flow flow,
        PrintRegionConfig* config, PrintObjectConfig* object_config, 
        PrintConfig* print_config, ExtrusionEntityCollection* loops, 
        ExtrusionEntityCollection* gap_fill, SurfaceCollection* fill_surfaces)
        : slices(slices), lower_slices(NULL), layer_height(layer_height),
            layer_id(-1), perimeter_flow(flow), ext_perimeter_flow(flow),
            overhang_flow(flow), solid_infill_flow(flow),
            config(config), object_config(object_config), print_config(print_config),
            loops(loops), gap_fill(gap_fill), fill_surfaces(fill_surfaces),
            _ext_mm3_per_mm(-1), _mm3_per_mm(-1), _mm3_per_mm_overhang(-1)
        {};
    void process();
    
    private:
    double _ext_mm3_per_mm;
    double _mm3_per_mm;
    double _mm3_per_mm_overhang;
    Polygons _lower_slices_p;
    
    ExtrusionEntityCollection _traverse_loops(const PerimeterGeneratorLoops &loops,
        ThickPolylines &thin_walls) const;
    ExtrusionEntityCollection _variable_width
        (const ThickPolylines &polylines, ExtrusionRole role, Flow flow) const;
};

}

#endif
