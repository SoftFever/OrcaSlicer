#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

#include "libslic3r.h"
#include "clipper.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Surface.hpp"

// import these wherever we're included
using Slic3r::ClipperLib::jtMiter;
using Slic3r::ClipperLib::jtRound;
using Slic3r::ClipperLib::jtSquare;

static constexpr const float ClipperSafetyOffset = 10.f;
enum class ApplySafetyOffset {
    No,
    Yes
};

#define CLIPPERUTILS_UNSAFE_OFFSET

namespace Slic3r {

namespace ClipperUtils {
    class PathsProviderIteratorBase {
    public:
        using value_type        = Points;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const Points*;
        using reference         = const Points&;
        using iterator_category = std::input_iterator_tag;
    };

    class EmptyPathsProvider {
    public:
        struct iterator : public PathsProviderIteratorBase {
        public:
            const Points& operator*() { assert(false); return s_empty_points; }
            // all iterators point to end.
            constexpr bool operator==(const iterator &rhs) const { return true; }
            constexpr bool operator!=(const iterator &rhs) const { return false; }
            const Points& operator++(int) { assert(false); return s_empty_points; }
            const iterator& operator++() { assert(false); return *this; }
        };

        constexpr EmptyPathsProvider() {}
        static constexpr iterator cend()   throw() { return iterator{}; }
        static constexpr iterator end()    throw() { return cend(); }
        static constexpr iterator cbegin() throw() { return cend(); }
        static constexpr iterator begin()  throw() { return cend(); }
        static constexpr size_t   size()   throw() { return 0; }

        static Points s_empty_points;
    };

    class SinglePathProvider {
    public:
        SinglePathProvider(const Points &points) : m_points(points) {}

        struct iterator : public PathsProviderIteratorBase {
        public:
            explicit iterator(const Points &points) : m_ptr(&points) {}
            const Points& operator*() const { return *m_ptr; }
            bool operator==(const iterator &rhs) const { return m_ptr == rhs.m_ptr; }
            bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
            const Points& operator++(int) { auto out = m_ptr; m_ptr = &s_end; return *out; }
            iterator& operator++() { m_ptr = &s_end; return *this; }
        private:
            const Points *m_ptr;
        };

        iterator cbegin() const { return iterator(m_points); }
        iterator begin()  const { return this->cbegin(); }
        iterator cend()   const { return iterator(s_end); }
        iterator end()    const { return this->cend(); }
        size_t   size()   const { return 1; }

    private:
        const  Points &m_points;
        static Points  s_end;
    };

    template<typename MultiPointType>
    class MultiPointsProvider {
    public:
        MultiPointsProvider(const std::vector<MultiPointType> &multipoints) : m_multipoints(multipoints) {}

        struct iterator : public PathsProviderIteratorBase {
        public:
            explicit iterator(typename std::vector<MultiPointType>::const_iterator it) : m_it(it) {}
            const Points& operator*() const { return m_it->points; }
            bool operator==(const iterator &rhs) const { return m_it == rhs.m_it; }
            bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
            const Points& operator++(int) { return (m_it ++)->points; }
            iterator& operator++() { ++ m_it; return *this; }
        private:
            typename std::vector<MultiPointType>::const_iterator m_it;
        };

        iterator cbegin() const { return iterator(m_multipoints.begin()); }
        iterator begin()  const { return this->cbegin(); }
        iterator cend()   const { return iterator(m_multipoints.end()); }
        iterator end()    const { return this->cend(); }
        size_t   size()   const { return m_multipoints.size(); }

    private:
        const std::vector<MultiPointType> &m_multipoints;
    };

    using PolygonsProvider  = MultiPointsProvider<Polygon>;
    using PolylinesProvider = MultiPointsProvider<Polyline>;

    struct ExPolygonProvider {
        ExPolygonProvider(const ExPolygon &expoly) : m_expoly(expoly) {}

        struct iterator : public PathsProviderIteratorBase {
        public:
            explicit iterator(const ExPolygon &expoly, int idx) : m_expoly(expoly), m_idx(idx) {}
            const Points& operator*() const { return (m_idx == 0) ? m_expoly.contour.points : m_expoly.holes[m_idx - 1].points; }
            bool operator==(const iterator &rhs) const { assert(m_expoly == rhs.m_expoly); return m_idx == rhs.m_idx; }
            bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
            const Points& operator++(int) { const Points &out = **this; ++ m_idx; return out; }
            iterator& operator++() { ++ m_idx; return *this; }
        private:
            const ExPolygon &m_expoly;
            int              m_idx;
        };

        iterator cbegin() const { return iterator(m_expoly, 0); }
        iterator begin()  const { return this->cbegin(); }
        iterator cend()   const { return iterator(m_expoly, m_expoly.holes.size() + 1); }
        iterator end()    const { return this->cend(); }
        size_t   size()   const { return m_expoly.holes.size() + 1; }

    private:
        const ExPolygon &m_expoly;
    };

    struct ExPolygonsProvider {
        ExPolygonsProvider(const ExPolygons &expolygons) : m_expolygons(expolygons) {
            m_size = 0;
            for (const ExPolygon &expoly : expolygons)
                m_size += expoly.holes.size() + 1;
        }

        struct iterator : public PathsProviderIteratorBase {
        public:
            explicit iterator(ExPolygons::const_iterator it) : m_it_expolygon(it), m_idx_contour(0) {}
            const Points& operator*() const { return (m_idx_contour == 0) ? m_it_expolygon->contour.points : m_it_expolygon->holes[m_idx_contour - 1].points; }
            bool operator==(const iterator &rhs) const { return m_it_expolygon == rhs.m_it_expolygon && m_idx_contour == rhs.m_idx_contour; }
            bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
            iterator& operator++() { 
                if (++ m_idx_contour == m_it_expolygon->holes.size() + 1) {
                    ++ m_it_expolygon;
                    m_idx_contour = 0;
                }
                return *this;
            }
            const Points& operator++(int) { 
                const Points &out = **this;
                ++ (*this);
                return out;
            }
        private:
            ExPolygons::const_iterator  m_it_expolygon;
            size_t                      m_idx_contour;
        };

        iterator cbegin() const { return iterator(m_expolygons.cbegin()); }
        iterator begin()  const { return this->cbegin(); }
        iterator cend()   const { return iterator(m_expolygons.cend()); }
        iterator end()    const { return this->cend(); }
        size_t   size()   const { return m_size; }

    private:
        const ExPolygons &m_expolygons;
        size_t            m_size;
    };

    struct SurfacesProvider {
        SurfacesProvider(const Surfaces &surfaces) : m_surfaces(surfaces) {
            m_size = 0;
            for (const Surface &surface : surfaces)
                m_size += surface.expolygon.holes.size() + 1;
        }

        struct iterator : public PathsProviderIteratorBase {
        public:
            explicit iterator(Surfaces::const_iterator it) : m_it_surface(it), m_idx_contour(0) {}
            const Points& operator*() const { return (m_idx_contour == 0) ? m_it_surface->expolygon.contour.points : m_it_surface->expolygon.holes[m_idx_contour - 1].points; }
            bool operator==(const iterator &rhs) const { return m_it_surface == rhs.m_it_surface && m_idx_contour == rhs.m_idx_contour; }
            bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
            iterator& operator++() { 
                if (++ m_idx_contour == m_it_surface->expolygon.holes.size() + 1) {
                    ++ m_it_surface;
                    m_idx_contour = 0;
                }
                return *this;
            }
            const Points& operator++(int) { 
                const Points &out = **this;
                ++ (*this);
                return out;
            }
        private:
            Surfaces::const_iterator  m_it_surface;
            size_t                    m_idx_contour;
        };

        iterator cbegin() const { return iterator(m_surfaces.cbegin()); }
        iterator begin()  const { return this->cbegin(); }
        iterator cend()   const { return iterator(m_surfaces.cend()); }
        iterator end()    const { return this->cend(); }
        size_t   size()   const { return m_size; }

    private:
        const Surfaces &m_surfaces;
        size_t          m_size;
    };

    struct SurfacesPtrProvider {
        SurfacesPtrProvider(const SurfacesPtr &surfaces) : m_surfaces(surfaces) {
            m_size = 0;
            for (const Surface *surface : surfaces)
                m_size += surface->expolygon.holes.size() + 1;
        }

        struct iterator : public PathsProviderIteratorBase {
        public:
            explicit iterator(SurfacesPtr::const_iterator it) : m_it_surface(it), m_idx_contour(0) {}
            const Points& operator*() const { return (m_idx_contour == 0) ? (*m_it_surface)->expolygon.contour.points : (*m_it_surface)->expolygon.holes[m_idx_contour - 1].points; }
            bool operator==(const iterator &rhs) const { return m_it_surface == rhs.m_it_surface && m_idx_contour == rhs.m_idx_contour; }
            bool operator!=(const iterator &rhs) const { return !(*this == rhs); }
            iterator& operator++() { 
                if (++ m_idx_contour == (*m_it_surface)->expolygon.holes.size() + 1) {
                    ++ m_it_surface;
                    m_idx_contour = 0;
                }
                return *this;
            }
            const Points& operator++(int) { 
                const Points &out = **this;
                ++ (*this);
                return out;
            }
        private:
            SurfacesPtr::const_iterator  m_it_surface;
            size_t                       m_idx_contour;
        };

        iterator cbegin() const { return iterator(m_surfaces.cbegin()); }
        iterator begin()  const { return this->cbegin(); }
        iterator cend()   const { return iterator(m_surfaces.cend()); }
        iterator end()    const { return this->cend(); }
        size_t   size()   const { return m_size; }

    private:
        const SurfacesPtr &m_surfaces;
        size_t             m_size;
    };
}

ExPolygons ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input);

// offset Polygons
Slic3r::Polygons offset(const Slic3r::Polygon &polygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter,  double miterLimit = 3);

// offset Polylines
Slic3r::Polygons   offset(const Slic3r::Polyline &polyline, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtSquare, double miterLimit = 3);
Slic3r::Polygons   offset(const Slic3r::Polylines &polylines, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtSquare, double miterLimit = 3);
Slic3r::Polygons   offset(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::Polygons   offset(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::Polygons   offset(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::Polygons   offset(const Slic3r::SurfacesPtr &surfaces, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::ExPolygons offset_ex(const Slic3r::Surfaces &surfaces, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::Polygons   union_safety_offset(const Slic3r::ExPolygons &expolygons);
Slic3r::ExPolygons union_safety_offset_ex(const Slic3r::ExPolygons &expolygons);

Slic3r::Polygons   offset2(const Slic3r::ExPolygons &expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::ExPolygons offset2_ex(const Slic3r::ExPolygons &expolygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);

#ifdef CLIPPERUTILS_UNSAFE_OFFSET
Slic3r::Polygons   offset(const Slic3r::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::ExPolygons offset_ex(const Slic3r::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
ClipperLib::Paths _offset2(const Slic3r::Polygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::Polygons   offset2(const Slic3r::Polygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::ExPolygons offset2_ex(const Slic3r::Polygons &polygons, const float delta1, const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3);
Slic3r::Polygons   union_safety_offset(const Slic3r::Polygons &expolygons);
Slic3r::ExPolygons union_safety_offset_ex(const Slic3r::Polygons &polygons);
#endif // CLIPPERUTILS_UNSAFE_OFFSET

Slic3r::Lines _clipper_ln(ClipperLib::ClipType clipType, const Slic3r::Lines &subject, const Slic3r::Polygons &clip);

// Safety offset is applied to the clipping polygons only.
Slic3r::Polygons   diff(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   diff(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   diff(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   diff(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Polygon &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polylines  diff_pl(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygon &clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygons &clip);
Slic3r::Polylines  diff_pl(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip);

inline Slic3r::Lines diff_ln(const Slic3r::Lines &subject, const Slic3r::Polygons &clip)
{
    return _clipper_ln(ClipperLib::ctDifference, subject, clip);
}

// Safety offset is applied to the clipping polygons only.
Slic3r::Polygons   intersection(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   intersection(const Slic3r::ExPolygon &subject, const Slic3r::ExPolygon &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   intersection(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   intersection(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   intersection(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polygons   intersection(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::ExPolygons intersection_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset = ApplySafetyOffset::No);
Slic3r::Polylines  intersection_pl(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygons &clip);
Slic3r::Polylines  intersection_pl(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip);

inline Slic3r::Lines intersection_ln(const Slic3r::Lines &subject, const Slic3r::Polygons &clip)
{
    return _clipper_ln(ClipperLib::ctIntersection, subject, clip);
}

inline Slic3r::Lines intersection_ln(const Slic3r::Line &subject, const Slic3r::Polygons &clip)
{
    Slic3r::Lines lines;
    lines.emplace_back(subject);
    return _clipper_ln(ClipperLib::ctIntersection, lines, clip);
}

Slic3r::Polygons union_(const Slic3r::Polygons &subject);
Slic3r::Polygons union_(const Slic3r::ExPolygons &subject);
Slic3r::Polygons union_(const Slic3r::Polygons &subject, const Slic3r::Polygons &subject2);
// May be used to "heal" unusual models (3DLabPrints etc.) by providing fill_type (pftEvenOdd, pftNonZero, pftPositive, pftNegative).
Slic3r::ExPolygons union_ex(const Slic3r::Polygons &subject, ClipperLib::PolyFillType fill_type = ClipperLib::pftNonZero);
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons &subject);
Slic3r::ExPolygons union_ex(const Slic3r::Surfaces &subject);

ClipperLib::PolyTree union_pt(const Slic3r::Polygons &subject);
ClipperLib::PolyTree union_pt(const Slic3r::ExPolygons &subject);

Slic3r::Polygons union_pt_chained_outside_in(const Slic3r::Polygons &subject);

ClipperLib::PolyNodes order_nodes(const ClipperLib::PolyNodes &nodes);

// Implementing generalized loop (foreach) over a list of nodes which can be
// ordered or unordered (performance gain) based on template parameter
enum class e_ordering {
    ON,
    OFF
};

// Create a template struct, template functions can not be partially specialized
template<e_ordering o, class Fn> struct _foreach_node {
    void operator()(const ClipperLib::PolyNodes &nodes, Fn &&fn);
};

// Specialization with NO ordering
template<class Fn> struct _foreach_node<e_ordering::OFF, Fn> {
    void operator()(const ClipperLib::PolyNodes &nodes, Fn &&fn)
    {
        for (auto &n : nodes) fn(n);    
    }
};

// Specialization with ordering
template<class Fn> struct _foreach_node<e_ordering::ON, Fn> {
    void operator()(const ClipperLib::PolyNodes &nodes, Fn &&fn)
    {
        auto ordered_nodes = order_nodes(nodes);
        for (auto &n : nodes) fn(n);    
    }
};

// Wrapper function for the foreach_node which can deduce arguments automatically
template<e_ordering o, class Fn>
void foreach_node(const ClipperLib::PolyNodes &nodes, Fn &&fn)
{
    _foreach_node<o, Fn>()(nodes, std::forward<Fn>(fn));
}

// Collecting polygons of the tree into a list of Polygons, holes have clockwise
// orientation.
template<e_ordering ordering = e_ordering::OFF>
void traverse_pt(const ClipperLib::PolyNode *tree, Polygons *out)
{
    if (!tree) return; // terminates recursion
    
    // Push the contour of the current level
    out->emplace_back(tree->Contour);
    
    // Do the recursion for all the children.
    traverse_pt<ordering>(tree->Childs, out);
}

// Collecting polygons of the tree into a list of ExPolygons.
template<e_ordering ordering = e_ordering::OFF>
void traverse_pt(const ClipperLib::PolyNode *tree, ExPolygons *out)
{
    if (!tree) return;
    else if(tree->IsHole()) {
        // Levels of holes are skipped and handled together with the
        // contour levels.
        traverse_pt<ordering>(tree->Childs, out);
        return;
    }
    
    ExPolygon level;
    level.contour.points = tree->Contour;
    
    foreach_node<ordering>(tree->Childs, 
                           [out, &level] (const ClipperLib::PolyNode *node) {
        
        // Holes are collected here. 
        level.holes.emplace_back(node->Contour);
        
        // By doing a recursion, a new level expoly is created with the contour
        // and holes of the lower level. Doing this for all the childs.
        traverse_pt<ordering>(node->Childs, out);
    }); 
    
    out->emplace_back(level);
}

template<e_ordering o = e_ordering::OFF, class ExOrJustPolygons>
void traverse_pt(const ClipperLib::PolyNodes &nodes, ExOrJustPolygons *retval)
{
    foreach_node<o>(nodes, [&retval](const ClipperLib::PolyNode *node) {
        traverse_pt<o>(node, retval);
    });
}


/* OTHER */
Slic3r::Polygons simplify_polygons(const Slic3r::Polygons &subject, bool preserve_collinear = false);
Slic3r::ExPolygons simplify_polygons_ex(const Slic3r::Polygons &subject, bool preserve_collinear = false);

Polygons top_level_islands(const Slic3r::Polygons &polygons);

ClipperLib::Path mittered_offset_path_scaled(const Points &contour, const std::vector<float> &deltas, double miter_limit);
Polygons  variable_offset_inner(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);
Polygons  variable_offset_outer(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);
ExPolygons variable_offset_outer_ex(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);
ExPolygons variable_offset_inner_ex(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit = 2.);

}

#endif
