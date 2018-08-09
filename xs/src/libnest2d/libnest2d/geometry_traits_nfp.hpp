#ifndef GEOMETRIES_NOFITPOLYGON_HPP
#define GEOMETRIES_NOFITPOLYGON_HPP

#include "geometry_traits.hpp"
#include <algorithm>
#include <functional>
#include <vector>
#include <iterator>

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
static Shapes<RawShape> merge(const Shapes<RawShape>& /*shc*/)
{
    static_assert(always_false<RawShape>::value,
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
 * polygons are disjuct than the resulting set will contain more polygons.
 */
template<class RawShape>
static Shapes<RawShape> merge(const Shapes<RawShape>& shc,
                              const RawShape& sh)
{
    auto m = merge(shc);
    m.push_back(sh);
    return merge(m);
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

    // find max x and max y vertex
    auto it = std::max_element(ShapeLike::cbegin(sh), ShapeLike::cend(sh),
                               _vsort<RawShape>);

    return *it;
}

template<class RawShape>
using NfpResult = std::pair<RawShape, TPoint<RawShape>>;

/// Helper function to get the NFP
template<NfpLevel nfptype, class RawShape>
static NfpResult<RawShape> noFitPolygon(const RawShape& sh,
                                        const RawShape& other)
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
 * \return Returns a pair of the NFP and its reference vertex of the two input
 * polygons which have to be strictly convex. The resulting NFP is proven to be
 * convex as well in this case.
 *
 */
template<class RawShape>
static NfpResult<RawShape> nfpConvexOnly(const RawShape& sh,
                                         const RawShape& other)
{
    using Vertex = TPoint<RawShape>; using Edge = _Segment<Vertex>;
    using sl = ShapeLike;

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

    // Sort the edges by angle to X axis.
    std::sort(edgelist.begin(), edgelist.end(),
              [](const Edge& e1, const Edge& e2)
    {
        return e1.angleToXaxis() > e2.angleToXaxis();
    });

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

    return {rsh, top_nfp};
}

template<class RawShape>
static NfpResult<RawShape> nfpSimpleSimple(const RawShape& cstationary,
                                           const RawShape& cother)
{

    // Algorithms are from the original algorithm proposed in paper:
    // https://eprints.soton.ac.uk/36850/1/CORMSIS-05-05.pdf

    // /////////////////////////////////////////////////////////////////////////
    // Algorithm 1: Obtaining the minkowski sum
    // /////////////////////////////////////////////////////////////////////////

    // I guess this is not a full minkowski sum of the two input polygons by
    // definition. This yields a subset that is compatible with the next 2
    // algorithms.

    using Result = NfpResult<RawShape>;
    using Vertex = TPoint<RawShape>;
    using Coord = TCoord<Vertex>;
    using Edge = _Segment<Vertex>;
    using sl = ShapeLike;
    using std::signbit;
    using std::sort;
    using std::vector;
    using std::ref;
    using std::reference_wrapper;

    // TODO The original algorithms expects the stationary polygon in
    // counter clockwise and the orbiter in clockwise order.
    // So for preventing any further complication, I will make the input
    // the way it should be, than make my way around the orientations.

    // Reverse the stationary contour to counter clockwise
    auto stcont = sl::getContour(cstationary);
    std::reverse(stcont.begin(), stcont.end());
    RawShape stationary;
    sl::getContour(stationary) = stcont;

    // Reverse the orbiter contour to counter clockwise
    auto orbcont = sl::getContour(cother);

    std::reverse(orbcont.begin(), orbcont.end());

    // Copy the orbiter (contour only), we will have to work on it
    RawShape orbiter;
    sl::getContour(orbiter) = orbcont;

    // Step 1: Make the orbiter reverse oriented
    for(auto &v : sl::getContour(orbiter)) v = -v;

    // An egde with additional data for marking it
    struct MarkedEdge {
        Edge e; Radians turn_angle = 0; bool is_turning_point = false;
        MarkedEdge() = default;
        MarkedEdge(const Edge& ed, Radians ta, bool tp):
            e(ed), turn_angle(ta), is_turning_point(tp) {}
    };

    // Container for marked edges
    using EdgeList = vector<MarkedEdge>;

    EdgeList A, B;

    // This is how an edge list is created from the polygons
    auto fillEdgeList = [](EdgeList& L, const RawShape& poly, int dir) {
        L.reserve(sl::contourVertexCount(poly));

        auto it = sl::cbegin(poly);
        auto nextit = std::next(it);

        double turn_angle = 0;
        bool is_turn_point = false;

        while(nextit != sl::cend(poly)) {
            L.emplace_back(Edge(*it, *nextit), turn_angle, is_turn_point);
            it++; nextit++;
        }

        auto getTurnAngle = [](const Edge& e1, const Edge& e2) {
            auto phi = e1.angleToXaxis();
            auto phi_prev = e2.angleToXaxis();
            auto TwoPi = 2.0*Pi;
            if(phi > Pi) phi -= TwoPi;
            if(phi_prev > Pi) phi_prev -= TwoPi;
            auto turn_angle = phi-phi_prev;
            if(turn_angle > Pi) turn_angle -= TwoPi;
            return phi-phi_prev;
        };

        if(dir > 0) {
            auto eit = L.begin();
            auto enext = std::next(eit);

            eit->turn_angle = getTurnAngle(L.front().e, L.back().e);

            while(enext != L.end()) {
                enext->turn_angle = getTurnAngle( enext->e, eit->e);
                enext->is_turning_point =
                        signbit(enext->turn_angle) != signbit(eit->turn_angle);
                ++eit; ++enext;
            }

            L.front().is_turning_point = signbit(L.front().turn_angle) !=
                                         signbit(L.back().turn_angle);
        } else {
            std::cout << L.size() << std::endl;

            auto eit = L.rbegin();
            auto enext = std::next(eit);

            eit->turn_angle = getTurnAngle(L.back().e, L.front().e);

            while(enext != L.rend()) {
                enext->turn_angle = getTurnAngle(enext->e, eit->e);
                enext->is_turning_point =
                        signbit(enext->turn_angle) != signbit(eit->turn_angle);
                std::cout << enext->is_turning_point << " " << enext->turn_angle << std::endl;

                ++eit; ++enext;
            }

            L.back().is_turning_point = signbit(L.back().turn_angle) !=
                                        signbit(L.front().turn_angle);
        }
    };

    // Step 2: Fill the edgelists
    fillEdgeList(A, stationary, 1);
    fillEdgeList(B, orbiter, -1);

    // A reference to a marked edge that also knows its container
    struct MarkedEdgeRef {
        reference_wrapper<MarkedEdge> eref;
        reference_wrapper<vector<MarkedEdgeRef>> container;
        Coord dir = 1;  // Direction modifier

        inline Radians angleX() const { return eref.get().e.angleToXaxis(); }
        inline const Edge& edge() const { return eref.get().e; }
        inline Edge& edge() { return eref.get().e; }
        inline bool isTurningPoint() const {
            return eref.get().is_turning_point;
        }
        inline bool isFrom(const vector<MarkedEdgeRef>& cont ) {
            return &(container.get()) == &cont;
        }
        inline bool eq(const MarkedEdgeRef& mr) {
            return &(eref.get()) == &(mr.eref.get());
        }

        MarkedEdgeRef(reference_wrapper<MarkedEdge> er,
                      reference_wrapper<vector<MarkedEdgeRef>> ec):
            eref(er), container(ec), dir(1) {}

        MarkedEdgeRef(reference_wrapper<MarkedEdge> er,
                      reference_wrapper<vector<MarkedEdgeRef>> ec,
                      Coord d):
            eref(er), container(ec), dir(d) {}
    };

    using EdgeRefList = vector<MarkedEdgeRef>;

    // Comparing two marked edges
    auto sortfn = [](const MarkedEdgeRef& e1, const MarkedEdgeRef& e2) {
        return e1.angleX() < e2.angleX();
    };

    EdgeRefList Aref, Bref;     // We create containers for the references
    Aref.reserve(A.size()); Bref.reserve(B.size());

    // Fill reference container for the stationary polygon
    std::for_each(A.begin(), A.end(), [&Aref](MarkedEdge& me) {
        Aref.emplace_back( ref(me), ref(Aref) );
    });

    // Fill reference container for the orbiting polygon
    std::for_each(B.begin(), B.end(), [&Bref](MarkedEdge& me) {
        Bref.emplace_back( ref(me), ref(Bref) );
    });

    struct EdgeGroup { typename EdgeRefList::const_iterator first, last; };

    auto mink = [sortfn] // the Mink(Q, R, direction) sub-procedure
            (const EdgeGroup& Q, const EdgeGroup& R, bool positive)
    {

        // Step 1 "merge sort_list(Q) and sort_list(R) to form merge_list(Q,R)"
        // Sort the containers of edge references and merge them.
        // Q could be sorted only once and be reused here but we would still
        // need to merge it with sorted(R).

        EdgeRefList merged;
        EdgeRefList S, seq;
        merged.reserve((Q.last - Q.first) + (R.last - R.first));

        merged.insert(merged.end(), Q.first, Q.last);
        merged.insert(merged.end(), R.first, R.last);
        sort(merged.begin(), merged.end(), sortfn);

        // Step 2 "set i = 1, k = 1, direction = 1, s1 = q1"
        // we dont use i, instead, q is an iterator into Q. k would be an index
        // into the merged sequence but we use "it" as an iterator for that

        // here we obtain references for the containers for later comparisons
        const auto& Rcont = R.first->container.get();
        const auto& Qcont = Q.first->container.get();

        // Set the intial direction
        Coord dir = positive? 1 : -1;

        // roughly i = 1 (so q = Q.first) and s1 = q1 so S[0] = q;
        auto q = Q.first;
        S.push_back(*q++);

        // Roughly step 3
        while(q != Q.last) {
            auto it = merged.begin();
            while(it != merged.end() && !(it->eq(*(Q.first))) ) {
                if(it->isFrom(Rcont)) {
                    auto s = *it;
                    s.dir = dir;
                    S.push_back(s);
                }
                if(it->eq(*q)) {
                    S.push_back(*q);
                    if(it->isTurningPoint()) dir = -dir;
                    if(q != Q.first) it += dir;
                }
                else it += dir;
            }
            ++q; // "Set i = i + 1"
        }

        // Step 4:

        // "Let starting edge r1 be in position si in sequence"
        // whaaat? I guess this means the following:
        S[0] = *R.first;
        auto it = S.begin();

        // "Set j = 1, next = 2, direction = 1, seq1 = si"
        // we dont use j, seq is expanded dynamically.
        dir = 1; auto next = std::next(R.first);

        // Step 5:
        // "If all si edges have been allocated to seqj" should mean that
        // we loop until seq has equal size with S
        while(seq.size() < S.size()) {
            ++it; if(it == S.end()) it = S.begin();

            if(it->isFrom(Qcont)) {
                seq.push_back(*it); // "If si is from Q, j = j + 1, seqj = si"

                // "If si is a turning point in Q,
                // direction = - direction, next = next + direction"
                if(it->isTurningPoint()) { dir = -dir; next += dir; }
            }

            if(it->eq(*next) && dir == next->dir) { // "If si = direction.rnext"
                // "j = j + 1, seqj = si, next = next + direction"
                seq.push_back(*it); next += dir;
            }
        }

        return seq;
    };

    EdgeGroup R{ Bref.begin(), Bref.begin() }, Q{ Aref.begin(), Aref.end() };
    auto it = Bref.begin();
    bool orientation = true;
    EdgeRefList seqlist;
    seqlist.reserve(3*(Aref.size() + Bref.size()));

    while(it != Bref.end()) // This is step 3 and step 4 in one loop
        if(it->isTurningPoint()) {
            R = {R.last, it++};
            auto seq = mink(Q, R, orientation);

            // TODO step 6 (should be 5 shouldn't it?): linking edges from A
            // I don't get this step

            seqlist.insert(seqlist.end(), seq.begin(), seq.end());
            orientation = !orientation;
        } else ++it;

    if(seqlist.empty()) seqlist = mink(Q, {Bref.begin(), Bref.end()}, true);

    // /////////////////////////////////////////////////////////////////////////
    // Algorithm 2: breaking Minkowski sums into track line trips
    // /////////////////////////////////////////////////////////////////////////


    // /////////////////////////////////////////////////////////////////////////
    // Algorithm 3: finding the boundary of the NFP from track line trips
    // /////////////////////////////////////////////////////////////////////////



    return Result(stationary, Vertex());
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
