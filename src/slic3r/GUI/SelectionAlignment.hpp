#ifndef slic3r_SelectionAlignment_hpp_
#define slic3r_SelectionAlignment_hpp_

namespace Slic3r {
namespace GUI {

class GLCanvas3D;
class Selection;

// Align / distribute the currently selected objects (instances) or parts (volumes) along one axis.
// Axis is 0 = X, 1 = Y, 2 = Z. Whole objects always rest on the bed, so Z is available for parts only.
namespace SelectionAlignment {

enum class Mode {
    Min,    // align the lower edges
    Center, // align the centers
    Max     // align the upper edges
};

// Align needs at least two selected objects/parts, or a single part (it is aligned to its object).
bool can_align(const Selection& selection, int axis);
// Distribute needs at least three selected objects/parts.
bool can_distribute(const Selection& selection, int axis);

// Both return true when the selection was modified; the change is recorded as one undo step.
bool align(GLCanvas3D& canvas, int axis, Mode mode);
bool distribute(GLCanvas3D& canvas, int axis);

} // namespace SelectionAlignment

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SelectionAlignment_hpp_
