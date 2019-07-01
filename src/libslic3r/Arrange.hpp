#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include "Polygon.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

namespace arrangement {

/// A geometry abstraction for a circular print bed. Similarly to BoundingBox.
class CircleBed {
    Point center_;
    double radius_;
public:

    inline CircleBed(): center_(0, 0), radius_(std::nan("")) {}
    inline CircleBed(const Point& c, double r): center_(c), radius_(r) {}

    inline double radius() const { return radius_; }
    inline const Point& center() const { return center_; }
    inline operator bool() { return !std::isnan(radius_); }
};

/// Representing an unbounded bin
struct InfiniteBed { Point center; };

/// Types of print bed shapes.
enum class BedShapeType {
    BOX,
    CIRCLE,
    IRREGULAR,
    INFINITE,
    UNKNOWN
};

/// Info about the print bed for the arrange() function.
struct BedShapeHint {
    BedShapeType type = BedShapeType::INFINITE;
    /*union*/ struct {  // I know but who cares... TODO: use variant from cpp17?
        CircleBed   circ;
        BoundingBox box;
        Polyline    polygon;
        InfiniteBed infinite;
    } shape;
};

/// Get a bed shape hint for arrange() from a naked Polyline.
BedShapeHint bedShape(const Polyline& bed);

/**
 * @brief Classes implementing the Arrangeable interface can be used as input 
 * to the arrange function.
 */
class Arrangeable {
public:
    
    virtual ~Arrangeable() = default;
    
    /// Apply the result transformation calculated by the arrangement.
    virtual void apply_arrange_result(Vec2d offset, double rotation_rads) = 0;
    
    /// Get the 2D silhouette to arrange and an initial offset and rotation
    virtual std::tuple<Polygon, Vec2crd, double> get_arrange_polygon() const = 0;
};

using ArrangeablePtrs = std::vector<Arrangeable*>;

/**
 * \brief Arranges the model objects on the screen.
 *
 * The arrangement considers multiple bins (aka. print beds) for placing
 * all the items provided in the model argument. If the items don't fit on
 * one print bed, the remaining will be placed onto newly created print
 * beds. The first_bin_only parameter, if set to true, disables this
 * behavior and makes sure that only one print bed is filled and the
 * remaining items will be untouched. When set to false, the items which
 * could not fit onto the print bed will be placed next to the print bed so
 * the user should see a pile of items on the print bed and some other
 * piles outside the print area that can be dragged later onto the print
 * bed as a group.
 *
 * \param items Input which are object pointers implementing the
 * Arrangeable interface.
 *
 * \param min_obj_distance The minimum distance which is allowed for any
 * pair of items on the print bed in any direction.
 *
 * \param bedhint Info about the shape and type of the
 * bed. remaining items which do not fit onto the print area next to the
 * print bed or leave them untouched (let the user arrange them by hand or
 * remove them).
 *
 * \param progressind Progress indicator callback called when
 * an object gets packed. The unsigned argument is the number of items
 * remaining to pack.
 *
 * \param stopcondition A predicate returning true if abort is needed.
 */
bool arrange(ArrangeablePtrs &items,
             coord_t min_obj_distance,
             const BedShapeHint& bedhint,
             std::function<void(unsigned)> progressind = nullptr,
             std::function<bool(void)> stopcondition = nullptr);

/// Same as the previous, only that it takes unmovable items as an
/// additional argument.
bool arrange(ArrangeablePtrs &items,
             const ArrangeablePtrs &excludes,
             coord_t min_obj_distance,
             const BedShapeHint& bedhint,
             std::function<void(unsigned)> progressind = nullptr,
             std::function<bool(void)> stopcondition = nullptr);

}   // arr
}   // Slic3r
#endif // MODELARRANGE_HPP
