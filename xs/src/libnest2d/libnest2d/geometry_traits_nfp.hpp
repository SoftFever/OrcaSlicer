#ifndef GEOMETRIES_NOFITPOLYGON_HPP
#define GEOMETRIES_NOFITPOLYGON_HPP

#include "geometry_traits.hpp"
#include <algorithm>
#include <vector>

namespace libnest2d {

/// The complexity level of a polygon that an NFP implementation can handle.
enum class NfpLevel: unsigned {
    CONVEX_ONLY,
    ONE_CONVEX,
    BOTH_CONCAVE,
    ONE_CONVEX_WITH_HOLES,
    BOTH_CONCAVE_WITH_HOLES
};

/// A collection of static methods for handling the no fit polygon creation.
struct Nfp {

// Shorthand for a pile of polygons
template<class RawShape>
using Shapes = typename ShapeLike::Shapes<RawShape>;

/// Minkowski addition (not used yet)
template<class RawShape>
static RawShape minkowskiDiff(const RawShape& sh, const RawShape& /*other*/)
{


    return sh;
}

/**
 * Merge a bunch of polygons with the specified additional polygon.
 *
 * \tparam RawShape the Polygon data type.
 * \param shc The pile of polygons that will be unified with sh.
 * \param sh A single polygon to unify with shc.
 *
 * \return A set of polygons that is the union of the input polygons. Note that
 * mostly it will be a set containing only one big polygon but if the input
 * polygons are disjuct than the resulting set will contain more polygons.
 */
template<class RawShape>
static Shapes<RawShape> merge(const Shapes<RawShape>& shc, const RawShape& sh)
{
    static_assert(always_false<RawShape>::value,
                  "Nfp::merge(shapes, shape) unimplemented!");
}

/**
 * A method to get a vertex from a polygon that always maintains a relative
 * position to the coordinate system: It is always the rightmost top vertex.
 *
 * This way it does not matter in what order the vertices are stored, the
 * reference will be always the same for the same polygon.
 */
template<class RawShape>
inline static TPoint<RawShape> referenceVertex(const RawShape& sh)
{
    return rightmostUpVertex(sh);
}

/**
 * Get the vertex of the polygon that is at the lowest values (bottom) in the Y
 * axis and if there are more than one vertices on the same Y coordinate than
 * the result will be the leftmost (with the highest X coordinate).
 */
template<class RawShape>
static TPoint<RawShape> leftmostDownVertex(const RawShape& sh)
{

    // find min x and min y vertex
    auto it = std::min_element(ShapeLike::cbegin(sh), ShapeLike::cend(sh),
                               _vsort<RawShape>);

    return *it;
}

/**
 * Get the vertex of the polygon that is at the highest values (top) in the Y
 * axis and if there are more than one vertices on the same Y coordinate than
 * the result will be the rightmost (with the lowest X coordinate).
 */
template<class RawShape>
static TPoint<RawShape> rightmostUpVertex(const RawShape& sh)
{

    // find min x and min y vertex
    auto it = std::max_element(ShapeLike::cbegin(sh), ShapeLike::cend(sh),
                               _vsort<RawShape>);

    return *it;
}

/// Helper function to get the NFP
template<NfpLevel nfptype, class RawShape>
static RawShape noFitPolygon(const RawShape& sh, const RawShape& other)
{
    NfpImpl<RawShape, nfptype> nfp;
    return nfp(sh, other);
}

/**
 * The "trivial" Cuninghame-Green implementation of NFP for convex polygons.
 *
 * You can use this even if you provide implementations for the more complex
 * cases (Through specializing the the NfpImpl struct). Currently, no other
 * cases are covered in the library.
 *
 * Complexity should be no more than linear in the number of edges of the input
 * polygons.
 *
 * \tparam RawShape the Polygon data type.
 * \param sh The stationary polygon
 * \param cother The orbiting polygon
 * \return Returns the NFP of the two input polygons which have to be strictly
 * convex. The resulting NFP is proven to be convex as well in this case.
 *
 */
template<class RawShape>
static RawShape nfpConvexOnly(const RawShape& sh, const RawShape& cother)
{
    using Vertex = TPoint<RawShape>; using Edge = _Segment<Vertex>;

    RawShape other = cother;

    // Make the other polygon counter-clockwise
    std::reverse(ShapeLike::begin(other), ShapeLike::end(other));

    RawShape rsh;   // Final nfp placeholder
    std::vector<Edge> edgelist;

    auto cap = ShapeLike::contourVertexCount(sh) +
            ShapeLike::contourVertexCount(other);

    // Reserve the needed memory
    edgelist.reserve(cap);
    ShapeLike::reserve(rsh, static_cast<unsigned long>(cap));

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
}

// Specializable NFP implementation class. Specialize it if you have a faster
// or better NFP implementation
template<class RawShape, NfpLevel nfptype>
struct NfpImpl {
    RawShape operator()(const RawShape& sh, const RawShape& other) {
        static_assert(nfptype == NfpLevel::CONVEX_ONLY,
                      "Nfp::noFitPolygon() unimplemented!");

        // Libnest2D has a default implementation for convex polygons and will
        // use it if feasible.
        return nfpConvexOnly(sh, other);
    }
};

template<class RawShape> struct MaxNfpLevel {
    static const BP2D_CONSTEXPR NfpLevel value = NfpLevel::CONVEX_ONLY;
};

private:

// Do not specialize this...
template<class RawShape>
static inline bool _vsort(const TPoint<RawShape>& v1,
                          const TPoint<RawShape>& v2)
{
    using Coord = TCoord<TPoint<RawShape>>;
    Coord &&x1 = getX(v1), &&x2 = getX(v2), &&y1 = getY(v1), &&y2 = getY(v2);
    auto diff = y1 - y2;
    if(std::abs(diff) <= std::numeric_limits<Coord>::epsilon())
        return x1 < x2;

    return diff < 0;
}

};

}

#endif // GEOMETRIES_NOFITPOLYGON_HPP
