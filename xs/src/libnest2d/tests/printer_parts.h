#ifndef PRINTER_PARTS_H
#define PRINTER_PARTS_H

#include <vector>
#include <clipper.hpp>

#ifndef CLIPPER_BACKEND_HPP
namespace ClipperLib {
using PointImpl = IntPoint;
using PathImpl = Path;
using HoleStore = std::vector<PathImpl>;

struct PolygonImpl {
    PathImpl Contour;
    HoleStore Holes;

    inline PolygonImpl() {}

    inline explicit PolygonImpl(const PathImpl& cont): Contour(cont) {}
    inline explicit PolygonImpl(const HoleStore& holes):
        Holes(holes) {}
    inline PolygonImpl(const Path& cont, const HoleStore& holes):
        Contour(cont), Holes(holes) {}

    inline explicit PolygonImpl(PathImpl&& cont): Contour(std::move(cont)) {}
    inline explicit PolygonImpl(HoleStore&& holes): Holes(std::move(holes)) {}
    inline PolygonImpl(Path&& cont, HoleStore&& holes):
        Contour(std::move(cont)), Holes(std::move(holes)) {}
};
}
#endif

using TestData = std::vector<ClipperLib::Path>;
using TestDataEx = std::vector<ClipperLib::PolygonImpl>;

extern const TestData PRINTER_PART_POLYGONS;
extern const TestData STEGOSAUR_POLYGONS;
extern const TestDataEx PRINTER_PART_POLYGONS_EX;

#endif // PRINTER_PARTS_H
