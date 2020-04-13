#ifndef RASTERTOPOLYGONS_HPP
#define RASTERTOPOLYGONS_HPP

#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {
namespace sla {

class RasterGrayscaleAA;

ExPolygons raster_to_polygons(const RasterGrayscaleAA &rst, float accuracy = 1.f);

}}

#endif // RASTERTOPOLYGONS_HPP
