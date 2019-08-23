#ifndef GEOMETRIES_NOFITPOLYGON_HPP
#define GEOMETRIES_NOFITPOLYGON_HPP

#include <algorithm>
#include <functional>
#include <vector>
#include <iterator>

#include <libnest2d/geometry_traits.hpp>

namespace libnest2d {

namespace __nfp {
// Do not specialize this...
template<class RawShape, class Unit = TCompute<RawShape>>
inline bool _vsort(const TPoint<RawShape>& v1, const TPoint<RawShape>& v2)
{
    Unit x1 = getX(v1), x2 = getX(v2), y1 = getY(v1), y2 = getY(v2);
    return y1 == y2 ? x1 < x2 : y1 < y2;
}

template<class EdgeList, class RawShape, class Vertex = TPoint<RawShape>>
inline void buildPolygon(const EdgeList& edgelist,
                         RawShape& rpoly,
                         Vertex& top_nfp)
{
    namespace sl = shapelike;

    auto& rsh = sl::contour(rpoly);

    sl::reserve(rsh, 2*edgelist.size());

    // Add the two vertices from the first edge into the final polygon.
    sl::addVertex(rsh, edgelist.front().first());
    sl::addVertex(rsh, edgelist.front().second());

    // Sorting function for the nfp reference vertex search
    auto& cmp = _vsort<RawShape>;

    // the reference (rightmost top) vertex so far
    top_nfp = *std::max_element(sl::cbegin(rsh), sl::cend(rsh), cmp );

    auto tmp = std::next(sl::begin(rsh));

    // Construct final nfp by placing each edge to the end of the previous
    for(auto eit = std::next(edgelist.begin());
        eit != edgelist.end();
        ++eit)
    {
        auto d = *tmp - eit->first();
        Vertex p = eit->second() + d;

        sl::addVertex(rsh, p);

        // Set the new reference vertex
        if(cmp(top_nfp, p)) top_nfp = p;

        tmp = std::next(tmp);
    }

}

template<class Container, class Iterator = typename Container::iterator>
void advance(Iterator& it, Container& cont, bool direction)
{
    int dir = direction ? 1 : -1;
    if(dir < 0 && it == cont.begin()) it = std::prev(cont.end());
    else it += dir;
    if(dir > 0 && it == cont.end()) it = cont.begin();
}

}

/// A collection of static methods for handling the no fit polygon creation.
namespace nfp {

const double BP2D_CONSTEXPR TwoPi = 2*Pi;

/// The complexity level of a polygon that an NFP implementation can handle.
enum class NfpLevel: unsigned {
    CONVEX_ONLY,
    ONE_CONVEX,
    BOTH_CONCAVE,
    ONE_CONVEX_WITH_HOLES,
    BOTH_CONCAVE_WITH_HOLES
};

template<class RawShape>
using NfpResult = std::pair<RawShape, TPoint<RawShape>>;

template<class RawShape> struct MaxNfpLevel {
    static const BP2D_CONSTEXPR NfpLevel value = NfpLevel::CONVEX_ONLY;
};


// Shorthand for a pile of polygons
template<class RawShape>
using Shapes = TMultiShape<RawShape>;

/**
 * Merge a bunch of polygons with the specified additional polygon.
 *
 * \tparam RawShape the Polygon data type.
 * \param shc The pile of polygons that will be unified with sh.
 * \param sh A single polygon to unify with shc.
 *
 * \return A set of polygons that is the union of the input polygons. Note that
 * mostly it will be a set containing only one big polygon but if the input
 * polygons are disjunct than the resulting set will contain more polygons.
 */
template<class RawShapes>
inline RawShapes merge(const RawShapes& /*shc*/)
{
    static_assert(always_false<RawShapes>::value,
                  "Nfp::merge(shapes, shape) unimplemented!");
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
 * polygons are disjunct than the resulting set will contain more polygons.
 */
template<class RawShape>
inline TMultiShape<RawShape> merge(const TMultiShape<RawShape>& shc,
                                   const RawShape& sh)
{
    auto m = nfp::merge(shc);
    m.emplace_back(sh);
    return nfp::merge(m);
}

/**
 * Get the vertex of the polygon that is at the lowest values (bottom) in the Y
 * axis and if there are more than one vertices on the same Y coordinate than
 * the result will be the leftmost (with the highest X coordinate).
 */
template<class RawShape>
inline TPoint<RawShape> leftmostDownVertex(const RawShape& sh)
{

    // find min x and min y vertex
    auto it = std::min_element(shapelike::cbegin(sh), shapelike::cend(sh),
                               __nfp::_vsort<RawShape>);

    return it == shapelike::cend(sh) ? TPoint<RawShape>() : *it;;
}

/**
 * Get the vertex of the polygon that is at the highest values (top) in the Y
 * axis and if there are more than one vertices on the same Y coordinate than
 * the result will be the rightmost (with the lowest X coordinate).
 */
template<class RawShape>
TPoint<RawShape> rightmostUpVertex(const RawShape& sh)
{

    // find max x and max y vertex
    auto it = std::max_element(shapelike::cbegin(sh), shapelike::cend(sh),
                               __nfp::_vsort<RawShape>);

    return it == shapelike::cend(sh) ? TPoint<RawShape>() : *it;
}

/**
 * A method to get a vertex from a polygon that always maintains a relative
 * position to the coordinate system: It is always the rightmost top vertex.
 *
 * This way it does not matter in what order the vertices are stored, the
 * reference will be always the same for the same polygon.
 */
template<class RawShape>
inline TPoint<RawShape> referenceVertex(const RawShape& sh)
{
    return rightmostUpVertex(sh);
}

/**
 * The "trivial" Cuninghame-Green implementation of NFP for convex polygons.
 *
 * You can use this even if you provide implementations for the more complex
 * cases (Through specializing the the NfpImpl struct). Currently, no other
 * cases are covered in the library.
 *
 * Complexity should be no more than nlogn (std::sort) in the number of edges
 * of the input polygons.
 *
 * \tparam RawShape the Polygon data type.
 * \param sh The stationary polygon
 * \param cother The orbiting polygon
 * \return Returns a pair of the NFP and its reference vertex of the two input
 * polygons which have to be strictly convex. The resulting NFP is proven to be
 * convex as well in this case.
 *
 */
template<class RawShape, class Ratio = double>
inline NfpResult<RawShape> nfpConvexOnly(const RawShape& sh,
                                         const RawShape& other)
{
    using Vertex = TPoint<RawShape>; using Edge = _Segment<Vertex>;
    namespace sl = shapelike;

    RawShape rsh;   // Final nfp placeholder
    Vertex top_nfp;
    std::vector<Edge> edgelist;

    auto cap = sl::contourVertexCount(sh) + sl::contourVertexCount(other);

    // Reserve the needed memory
    edgelist.reserve(cap);
    sl::reserve(rsh, static_cast<unsigned long>(cap));

    { // place all edges from sh into edgelist
        auto first = sl::cbegin(sh);
        auto next = std::next(first);

        while(next != sl::cend(sh)) {
            edgelist.emplace_back(*(first), *(next));
            ++first; ++next;
        }
    }

    { // place all edges from other into edgelist
        auto first = sl::cbegin(other);
        auto next = std::next(first);

        while(next != sl::cend(other)) {
            edgelist.emplace_back(*(next), *(first));
            ++first; ++next;
        }
    }
   
    std::sort(edgelist.begin(), edgelist.end(), 
              [](const Edge& e1, const Edge& e2) 
    {
        Vertex ax(1, 0); // Unit vector for the X axis
        
        // get cectors from the edges
        Vertex p1 = e1.second() - e1.first();
        Vertex p2 = e2.second() - e2.first();

        // Quadrant mapping array. The quadrant of a vector can be determined
        // from the dot product of the vector and its perpendicular pair
        // with the unit vector X axis. The products will carry the values
        // lcos = dot(p, ax) = l * cos(phi) and
        // lsin = -dotperp(p, ax) = l * sin(phi) where
        // l is the length of vector p. From the signs of these values we can
        // construct an index which has the sign of lcos as MSB and the
        // sign of lsin as LSB. This index can be used to retrieve the actual
        // quadrant where vector p resides using the following map:
        // (+ is 0, - is 1)
        // cos | sin | decimal | quadrant
        //  +  |  +  |    0    |    0
        //  +  |  -  |    1    |    3
        //  -  |  +  |    2    |    1
        //  -  |  -  |    3    |    2
        std::array<int, 4> quadrants {0, 3, 1, 2 };

        std::array<int, 2> q {0, 0}; // Quadrant indices for p1 and p2

        using TDots = std::array<TCompute<Vertex>, 2>;
        TDots lcos { pl::dot(p1, ax), pl::dot(p2, ax) };
        TDots lsin { -pl::dotperp(p1, ax), -pl::dotperp(p2, ax) };

        // Construct the quadrant indices for p1 and p2
        for(size_t i = 0; i < 2; ++i)
            if(lcos[i] == 0) q[i] = lsin[i] > 0 ? 1 : 3;
            else if(lsin[i] == 0) q[i] = lcos[i] > 0 ? 0 : 2;
            else q[i] = quadrants[((lcos[i] < 0) << 1) + (lsin[i] < 0)];
            
        if(q[0] == q[1]) { // only bother if p1 and p2 are in the same quadrant
            auto lsq1 = pl::magnsq(p1);     // squared magnitudes, avoid sqrt
            auto lsq2 = pl::magnsq(p2);     // squared magnitudes, avoid sqrt

            // We will actually compare l^2 * cos^2(phi) which saturates the
            // cos function. But with the quadrant info we can get the sign back
            int sign = q[0] == 1 || q[0] == 2 ? -1 : 1;
            
            // If Ratio is an actual rational type, there is no precision loss
            auto pcos1 = Ratio(lcos[0]) / lsq1 * sign * lcos[0];
            auto pcos2 = Ratio(lcos[1]) / lsq2 * sign * lcos[1];
            
            return q[0] < 2 ? pcos1 < pcos2 : pcos1 > pcos2;
        }
        
        // If in different quadrants, compare the quadrant indices only.
        return q[0] > q[1];
    });

    __nfp::buildPolygon(edgelist, rsh, top_nfp);

    return {rsh, top_nfp};
}

template<class RawShape>
NfpResult<RawShape> nfpSimpleSimple(const RawShape& cstationary,
                                    const RawShape& cother)
{
    return {};
}

// Specializable NFP implementation class. Specialize it if you have a faster
// or better NFP implementation
template<class RawShape, NfpLevel nfptype>
struct NfpImpl {
    NfpResult<RawShape> operator()(const RawShape& sh, const RawShape& other)
    {
        static_assert(nfptype == NfpLevel::CONVEX_ONLY,
                      "Nfp::noFitPolygon() unimplemented!");

        // Libnest2D has a default implementation for convex polygons and will
        // use it if feasible.
        return nfpConvexOnly(sh, other);
    }
};

/// Helper function to get the NFP
template<NfpLevel nfptype, class RawShape>
inline NfpResult<RawShape> noFitPolygon(const RawShape& sh,
                                        const RawShape& other)
{
    NfpImpl<RawShape, nfptype> nfps;
    return nfps(sh, other);
}

}

}

#endif // GEOMETRIES_NOFITPOLYGON_HPP
