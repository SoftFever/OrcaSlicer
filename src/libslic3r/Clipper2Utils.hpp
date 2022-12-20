#ifndef slic3r_Clipper2Utils_hpp_
#define slic3r_Clipper2Utils_hpp_

#include "libslic3r.h"
#include "clipper2/clipper.h"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

Slic3r::Polylines  intersection_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);
Slic3r::Polylines  diff_pl_2(const Slic3r::Polylines& subject, const Slic3r::Polygons& clip);

}

#endif

