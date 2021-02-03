#ifndef slic3r_Brim_hpp_
#define slic3r_Brim_hpp_

namespace Slic3r {

class Print;

// Produce brim lines around those objects, that have the brim enabled.
// Collect islands_area to be merged into the final 1st layer convex hull.
ExtrusionEntityCollection make_brim(const Print &print, PrintTryCancel try_cancel, Polygons &islands_area);

} // Slic3r

#endif // slic3r_Brim_hpp_
