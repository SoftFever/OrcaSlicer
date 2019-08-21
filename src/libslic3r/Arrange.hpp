#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include "ExPolygon.hpp"
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

/// Representing an unbounded bed.
struct InfiniteBed { Point center; };

/// Types of print bed shapes.
enum BedShapes {
    bsBox,
    bsCircle,
    bsIrregular,
    bsInfinite,
    bsUnknown
};

/// Info about the print bed for the arrange() function. This is a variant 
/// holding one of the four shapes a bed can be.
class BedShapeHint {
    BedShapes m_type = BedShapes::bsInfinite;
    
    // The union neither calls constructors nor destructors of its members.
    // The only member with non-trivial constructor / destructor is the polygon,
    // a placement new / delete needs to be called over it.
    union BedShape_u {  // TODO: use variant from cpp17?
        CircleBed   circ;
        BoundingBox box;
        Polyline    polygon;
        InfiniteBed infbed{};
        ~BedShape_u() {}
        BedShape_u() {}
    } m_bed;
    
    // Reset the type, allocate m_bed properly
    void reset(BedShapes type);
    
public:

    BedShapeHint(){}
    
    /// Get a bed shape hint for arrange() from a naked Polyline.
    explicit BedShapeHint(const Polyline &polyl);
    explicit BedShapeHint(const BoundingBox &bb)
    {
        m_type = bsBox; m_bed.box = bb;
    }
    
    explicit BedShapeHint(const CircleBed &c)
    {
        m_type = bsCircle; m_bed.circ = c;
    }
    
    explicit BedShapeHint(const InfiniteBed &ibed)
    {
        m_type = bsInfinite; m_bed.infbed = ibed;
    }

    ~BedShapeHint()
    {
        if (m_type == BedShapes::bsIrregular)
            m_bed.polygon.Slic3r::Polyline::~Polyline();
    }

    BedShapeHint(const BedShapeHint &cpy) { *this = cpy; }
    BedShapeHint(BedShapeHint &&cpy) { *this = std::move(cpy); }

    BedShapeHint &operator=(const BedShapeHint &cpy);
    BedShapeHint& operator=(BedShapeHint &&cpy);
    
    BedShapes get_type() const { return m_type; }

    const BoundingBox &get_box() const
    {
        assert(m_type == bsBox); return m_bed.box;
    }
    const CircleBed &get_circle() const
    {
        assert(m_type == bsCircle); return m_bed.circ;
    }
    const Polyline &get_irregular() const
    {
        assert(m_type == bsIrregular); return m_bed.polygon;
    }
    const InfiniteBed &get_infinite() const
    {
        assert(m_type == bsInfinite); return m_bed.infbed;
    }
};

/// A logical bed representing an object not being arranged. Either the arrange
/// has not yet successfully run on this ArrangePolygon or it could not fit the
/// object due to overly large size or invalid geometry.
static const constexpr int UNARRANGED = -1;

/// Input/Output structure for the arrange() function. The poly field will not
/// be modified during arrangement. Instead, the translation and rotation fields
/// will mark the needed transformation for the polygon to be in the arranged
/// position. These can also be set to an initial offset and rotation.
/// 
/// The bed_idx field will indicate the logical bed into which the
/// polygon belongs: UNARRANGED means no place for the polygon
/// (also the initial state before arrange), 0..N means the index of the bed.
/// Zero is the physical bed, larger than zero means a virtual bed.
struct ArrangePolygon { 
    ExPolygon poly;                 /// The 2D silhouette to be arranged
    Vec2crd   translation{0, 0};    /// The translation of the poly
    double    rotation{0.0};        /// The rotation of the poly in radians
    int       bed_idx{UNARRANGED};  /// To which logical bed does poly belong...
    int       priority{0};
    
    /// Optional setter function which can store arbitrary data in its closure
    std::function<void(const ArrangePolygon&)> setter = nullptr;
    
    /// Helper function to call the setter with the arrange data arguments
    void apply() const { if (setter) setter(*this); }

    /// Test if arrange() was called previously and gave a successful result.
    bool is_arranged() const { return bed_idx != UNARRANGED; }
};

using ArrangePolygons = std::vector<ArrangePolygon>;

/**
 * \brief Arranges the input polygons.
 *
 * WARNING: Currently, only convex polygons are supported by the libnest2d 
 * library which is used to do the arrangement. This might change in the future
 * this is why the interface contains a general polygon capable to have holes.
 *
 * \param items Input vector of ArrangePolygons. The transformation, rotation 
 * and bin_idx fields will be changed after the call finished and can be used
 * to apply the result on the input polygon.
 *
 * \param min_obj_distance The minimum distance which is allowed for any
 * pair of items on the print bed in any direction.
 *
 * \param bedhint Info about the shape and type of the bed.
 *
 * \param progressind Progress indicator callback called when
 * an object gets packed. The unsigned argument is the number of items
 * remaining to pack.
 *
 * \param stopcondition A predicate returning true if abort is needed.
 */
void arrange(ArrangePolygons &             items,
             coord_t                       min_obj_distance,
             const BedShapeHint &          bedhint,
             std::function<void(unsigned)> progressind   = nullptr,
             std::function<bool(void)>     stopcondition = nullptr);

/// Same as the previous, only that it takes unmovable items as an
/// additional argument. Those will be considered as already arranged objects.
void arrange(ArrangePolygons &             items,
             const ArrangePolygons &       excludes,
             coord_t                       min_obj_distance,
             const BedShapeHint &          bedhint,
             std::function<void(unsigned)> progressind   = nullptr,
             std::function<bool(void)>     stopcondition = nullptr);

}   // arr
}   // Slic3r
#endif // MODELARRANGE_HPP
