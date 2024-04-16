#ifndef slic3r_MultiMaterialSegmentation_hpp_
#define slic3r_MultiMaterialSegmentation_hpp_

#include <utility>
#include <vector>

namespace Slic3r {

class PrintObject;
class ExPolygon;
using ExPolygons = std::vector<ExPolygon>;

struct ColoredLine
{
    Line line;
    int  color;
    int  poly_idx       = -1;
    int  local_line_idx = -1;
};

using ColoredLines = std::vector<ColoredLine>;

// Returns MMU segmentation based on painting in MMU segmentation gizmo
std::vector<std::vector<ExPolygons>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);

} // namespace Slic3r

namespace boost::polygon {
template<> struct geometry_concept<Slic3r::ColoredLine>
{
    typedef segment_concept type;
};

template<> struct segment_traits<Slic3r::ColoredLine>
{
    typedef coord_t       coordinate_type;
    typedef Slic3r::Point point_type;

    static inline point_type get(const Slic3r::ColoredLine &line, const direction_1d &dir)
    {
        return dir.to_int() ? line.line.b : line.line.a;
    }
};
} // namespace boost::polygon

#endif // slic3r_MultiMaterialSegmentation_hpp_
