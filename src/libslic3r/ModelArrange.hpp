#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include "Model.hpp"

namespace Slic3r {

class Model;

namespace arr {

class Circle {
    Point center_;
    double radius_;
public:

    inline Circle(): center_(0, 0), radius_(std::nan("")) {}
    inline Circle(const Point& c, double r): center_(c), radius_(r) {}

    inline double radius() const { return radius_; }
    inline const Point& center() const { return center_; }
    inline operator bool() { return !std::isnan(radius_); }
};

enum class BedShapeType {
    BOX,
    CIRCLE,
    IRREGULAR,
    WHO_KNOWS
};

struct BedShapeHint {
    BedShapeType type;
    /*union*/ struct {  // I know but who cares...
        Circle circ;
        BoundingBox box;
        Polyline polygon;
    } shape;
};

BedShapeHint bedShape(const Polyline& bed);

struct WipeTowerInfo {
    bool is_wipe_tower = false;
    Vec2d pos;
    Vec2d bb_size;
    double rotation;
};

/**
 * \brief Arranges the model objects on the screen.
 *
 * The arrangement considers multiple bins (aka. print beds) for placing all
 * the items provided in the model argument. If the items don't fit on one
 * print bed, the remaining will be placed onto newly created print beds.
 * The first_bin_only parameter, if set to true, disables this behavior and
 * makes sure that only one print bed is filled and the remaining items will be
 * untouched. When set to false, the items which could not fit onto the
 * print bed will be placed next to the print bed so the user should see a
 * pile of items on the print bed and some other piles outside the print
 * area that can be dragged later onto the print bed as a group.
 *
 * \param model The model object with the 3D content.
 * \param dist The minimum distance which is allowed for any pair of items
 * on the print bed  in any direction.
 * \param bb The bounding box of the print bed. It corresponds to the 'bin'
 * for bin packing.
 * \param first_bin_only This parameter controls whether to place the
 * remaining items which do not fit onto the print area next to the print
 * bed or leave them untouched (let the user arrange them by hand or remove
 * them).
 * \param progressind Progress indicator callback called when an object gets
 * packed. The unsigned argument is the number of items remaining to pack.
 * \param stopcondition A predicate returning true if abort is needed.
 */
bool arrange(Model &model,
             WipeTowerInfo& wipe_tower_info,
             coord_t min_obj_distance,
             const Slic3r::Polyline& bed,
             BedShapeHint bedhint,
             bool first_bin_only,
             std::function<void(unsigned)> progressind,
             std::function<bool(void)> stopcondition);

/// This will find a suitable position for a new object instance and leave the
/// old items untouched.
void find_new_position(const Model& model,
                       ModelInstancePtrs instances_to_add,
                       coord_t min_obj_distance,
                       const Slic3r::Polyline& bed,
                       WipeTowerInfo& wti);

}   // arr
}   // Slic3r
#endif // MODELARRANGE_HPP
