#ifndef slic3r_MultiMaterialSegmentation_hpp_
#define slic3r_MultiMaterialSegmentation_hpp_

#include <utility>
#include <vector>

namespace Slic3r {


class PrintObject;
class ExPolygon;

// Returns MMU segmentation based on painting in MMU segmentation gizmo
std::vector<std::vector<std::pair<ExPolygon, size_t>>> multi_material_segmentation_by_painting(const PrintObject &print_object, const std::function<void()> &throw_on_cancel_callback);

} // namespace Slic3r

#endif // slic3r_MultiMaterialSegmentation_hpp_
