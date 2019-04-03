#ifndef PRINTER_PARTS_H
#define PRINTER_PARTS_H

#include <vector>
#include <libnest2d/backends/clipper/clipper_polygon.hpp>

using TestData = std::vector<ClipperLib::Path>;
using TestDataEx = std::vector<ClipperLib::Polygon>;

extern const TestData PRINTER_PART_POLYGONS;
extern const TestData STEGOSAUR_POLYGONS;
extern const TestDataEx PRINTER_PART_POLYGONS_EX;

#endif // PRINTER_PARTS_H
