#ifndef GEOMETRIES_NOFITPOLYGON_HPP
#define GEOMETRIES_NOFITPOLYGON_HPP

#include "geometry_traits.hpp"
#include <algorithm>
#include <vector>

namespace libnest2d {

struct Nfp {

template<class RawShape>
using Shapes = typename ShapeLike::Shapes<RawShape>;

template<class RawShape>
static RawShape& minkowskiAdd(RawShape& sh, const RawShape& /*other*/)
{
    static_assert(always_false<RawShape>::value,
                  "Nfp::minkowskiAdd() unimplemented!");
    return sh;
}

template<class RawShape>
static Shapes<RawShape> merge(const Shapes<RawShape>& shc, const RawShape& sh)
{
    static_assert(always_false<RawShape>::value,
                  "Nfp::merge(shapes, shape) unimplemented!");
}

template<class RawShape>
inline static TPoint<RawShape> referenceVertex(const RawShape& sh)
{
    return rightmostUpVertex(sh);
}

template<class RawShape>
static TPoint<RawShape> leftmostDownVertex(const RawShape& sh) {

    // find min x and min y vertex
    auto it = std::min_element(ShapeLike::cbegin(sh), ShapeLike::cend(sh),
                               _vsort<RawShape>);

    return *it;
}

template<class RawShape>
static TPoint<RawShape> rightmostUpVertex(const RawShape& sh) {

    // find min x and min y vertex
    auto it = std::max_element(ShapeLike::cbegin(sh), ShapeLike::cend(sh),
                               _vsort<RawShape>);

    return *it;
}

template<class RawShape>
static RawShape noFitPolygon(const RawShape& sh, const RawShape& other) {
    auto isConvex = [](const RawShape& sh) {

        return true;
    };

    using Vertex = TPoint<RawShape>;
    using Edge = _Segment<Vertex>;

    auto nfpConvexConvex = [] (
            const RawShape& sh,
            const RawShape& cother)
    {
        RawShape other = cother;

        // Make the other polygon counter-clockwise
        std::reverse(ShapeLike::begin(other), ShapeLike::end(other));

        RawShape rsh;   // Final nfp placeholder
        std::vector<Edge> edgelist;

        size_t cap = ShapeLike::contourVertexCount(sh) +
                ShapeLike::contourVertexCount(other);

        // Reserve the needed memory
        edgelist.reserve(cap);
        ShapeLike::reserve(rsh, cap);

        { // place all edges from sh into edgelist
            auto first = ShapeLike::cbegin(sh);
            auto next = first + 1;
            auto endit = ShapeLike::cend(sh);

            while(next != endit) edgelist.emplace_back(*(first++), *(next++));
        }

        { // place all edges from other into edgelist
            auto first = ShapeLike::cbegin(other);
            auto next = first + 1;
            auto endit = ShapeLike::cend(other);

            while(next != endit) edgelist.emplace_back(*(first++), *(next++));
        }

        // Sort the edges by angle to X axis.
        std::sort(edgelist.begin(), edgelist.end(),
                  [](const Edge& e1, const Edge& e2)
        {
            return e1.angleToXaxis() > e2.angleToXaxis();
        });

        // Add the two vertices from the first edge into the final polygon.
        ShapeLike::addVertex(rsh, edgelist.front().first());
        ShapeLike::addVertex(rsh, edgelist.front().second());

        auto tmp = std::next(ShapeLike::begin(rsh));

        // Construct final nfp by placing each edge to the end of the previous
        for(auto eit = std::next(edgelist.begin());
            eit != edgelist.end();
            ++eit)
        {
            auto d = *tmp - eit->first();
            auto p = eit->second() + d;

            ShapeLike::addVertex(rsh, p);

            tmp = std::next(tmp);
        }

        // Now we have an nfp somewhere in the dark. We need to get it
        // to the right position around the stationary shape.
        // This is done by choosing the leftmost lowest vertex of the
        // orbiting polygon to be touched with the rightmost upper
        // vertex of the stationary polygon. In this configuration, the
        // reference vertex of the orbiting polygon (which can be dragged around
        // the nfp) will be its rightmost upper vertex that coincides with the
        // rightmost upper vertex of the nfp. No proof provided other than Jonas
        // Lindmark's reasoning about the reference vertex of nfp in his thesis
        // ("No fit polygon problem" - section 2.1.9)

        auto csh = sh;  // Copy sh, we will sort the verices in the copy
        auto& cmp = _vsort<RawShape>;
        std::sort(ShapeLike::begin(csh), ShapeLike::end(csh), cmp);
        std::sort(ShapeLike::begin(other), ShapeLike::end(other), cmp);

        // leftmost lower vertex of the stationary polygon
        auto& touch_sh = *(std::prev(ShapeLike::end(csh)));
        // rightmost upper vertex of the orbiting polygon
        auto& touch_other = *(ShapeLike::begin(other));

        // Calculate the difference and move the orbiter to the touch position.
        auto dtouch = touch_sh - touch_other;
        auto top_other = *(std::prev(ShapeLike::end(other))) + dtouch;

        // Get the righmost upper vertex of the nfp and move it to the RMU of
        // the orbiter because they should coincide.
        auto&& top_nfp = rightmostUpVertex(rsh);
        auto dnfp = top_other - top_nfp;
        std::for_each(ShapeLike::begin(rsh), ShapeLike::end(rsh),
                      [&dnfp](Vertex& v) { v+= dnfp; } );

        return rsh;
    };

    RawShape rsh;

    enum e_dispatch {
        CONVEX_CONVEX,
        CONCAVE_CONVEX,
        CONVEX_CONCAVE,
        CONCAVE_CONCAVE
    };

    int sel = isConvex(sh) ? CONVEX_CONVEX : CONCAVE_CONVEX;
    sel += isConvex(other) ? CONVEX_CONVEX : CONVEX_CONCAVE;

    switch(sel) {
    case CONVEX_CONVEX:
        rsh = nfpConvexConvex(sh, other); break;
    case CONCAVE_CONVEX:
        break;
    case CONVEX_CONCAVE:
        break;
    case CONCAVE_CONCAVE:
        break;
    }

    return rsh;
}

template<class RawShape>
static inline Shapes<RawShape> noFitPolygon(const Shapes<RawShape>& shapes,
                                            const RawShape& other)
{
    assert(shapes.size() >= 1);
    auto shit = shapes.begin();

    Shapes<RawShape> ret;
    ret.emplace_back(noFitPolygon(*shit, other));

    while(++shit != shapes.end()) ret = merge(ret, noFitPolygon(*shit, other));

    return ret;
}

private:

// Do not specialize this...
template<class RawShape>
static inline bool _vsort(const TPoint<RawShape>& v1,
                          const TPoint<RawShape>& v2)
{
    using Coord = TCoord<TPoint<RawShape>>;
    auto diff = getY(v1) - getY(v2);
    if(std::abs(diff) <= std::numeric_limits<Coord>::epsilon())
        return getX(v1) < getX(v2);

    return diff < 0;
}

};

}

#endif // GEOMETRIES_NOFITPOLYGON_HPP
