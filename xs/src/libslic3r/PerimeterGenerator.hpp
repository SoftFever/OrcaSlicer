#ifndef slic3r_PerimeterGenerator_hpp_
#define slic3r_PerimeterGenerator_hpp_

#include <myinit.h>

namespace Slic3r {

class PerimeterGeneratorLoop;

class PerimeterGeneratorLoop {
    public:
    Polygon polygon;
    bool is_contour;
    unsigned short depth;
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(Polygon polygon, unsigned short depth)
        : polygon(polygon), depth(depth)
        {};
    bool is_external() const;
    bool is_internal_contour() const;
};

class PerimeterGenerator {
    public:
    SurfaceCollection* slices;
    SurfaceCollection* lower_slices;
    double layer_height;
    int layer_id;
    Flow perimeter_flow;
    Flow ext_perimeter_flow;
    Flow overhang_flow;
    Flow solid_infill_flow;
    PrintRegionConfig* config;
    PrintObjectConfig* object_config;
    PrintConfig* print_config;
    double _ext_mm3_per_mm;
    double _mm3_per_mm;
    double _mm3_per_mm_overhang;
    ExtrusionEntityCollection* loops;
    ExtrusionEntityCollection* gap_fill;
    SurfaceCollection* fill_surfaces;
    
    PerimeterGenerator(SurfaceCollection* slices, double layer_height,
        ExtrusionEntityCollection* loops, ExtrusionEntityCollection* gap_fill,
        SurfaceCollection* fill_surfaces)
        : slices(slices), layer_height(layer_height), layer_id(-1),
            _ext_mm3_per_mm(-1), _mm3_per_mm(-1), _mm3_per_mm_overhang(-1),
            loops(loops), gap_fill(gap_fill), fill_surfaces(fill_surfaces)
        {};
    void process();
    
    private:
    Polygons _lower_slices_p;
    
    ExtrusionEntityCollection _traverse_loops(const std::vector<PerimeterGeneratorLoop> &loops,
        const Polylines &thin_walls) const;
    ExtrusionEntityCollection _fill_gaps(double min, double max, double w,
        const Polygons &gaps) const;
};

}

#endif
