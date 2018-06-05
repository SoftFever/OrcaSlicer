#ifndef PRINTER_PARTS_H
#define PRINTER_PARTS_H

#include <vector>
#include <clipper.hpp>

using TestData = std::vector<ClipperLib::Path>;

extern const TestData PRINTER_PART_POLYGONS;
extern const TestData STEGOSAUR_POLYGONS;

#endif // PRINTER_PARTS_H
