#ifndef GEOMETRIES_NOFITPOLYGON_HPP
#define GEOMETRIES_NOFITPOLYGON_HPP

#include "geometry_traits.hpp"
#include <algorithm>

namespace libnest2d {

struct Nfp {

template<class RawShape>
static RawShape& minkowskiAdd(RawShape& sh, const RawShape& /*other*/) {
    static_assert(always_false<RawShape>::value,
                  "ShapeLike::minkowskiAdd() unimplemented!");
    return sh;
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

        // Make it counter-clockwise
        for(auto shit = ShapeLike::begin(other);
            shit != ShapeLike::end(other); ++shit ) {
            auto& v = *shit;
            setX(v, -getX(v));
            setY(v, -getY(v));
        }

        RawShape rsh;
        std::vector<Edge> edgelist;

        size_t cap = ShapeLike::contourVertexCount(sh) +
                ShapeLike::contourVertexCount(other);

        edgelist.reserve(cap);
        ShapeLike::reserve(rsh, cap);

        {
            auto first = ShapeLike::cbegin(sh);
            auto next = first + 1;
            auto endit = ShapeLike::cend(sh);

            while(next != endit) edgelist.emplace_back(*(first++), *(next++));
        }

        {
            auto first = ShapeLike::cbegin(other);
            auto next = first + 1;
            auto endit = ShapeLike::cend(other);

            while(next != endit) edgelist.emplace_back(*(first++), *(next++));
        }

        std::sort(edgelist.begin(), edgelist.end(),
                  [](const Edge& e1, const Edge& e2)
        {
            return e1.angleToXaxis() > e2.angleToXaxis();
        });

        ShapeLike::addVertex(rsh, edgelist.front().first());
        ShapeLike::addVertex(rsh, edgelist.front().second());

        auto tmp = std::next(ShapeLike::begin(rsh));

        // Construct final nfp
        for(auto eit = std::next(edgelist.begin());
            eit != edgelist.end();
            ++eit) {

            auto dx = getX(*tmp) - getX(eit->first());
            auto dy = getY(*tmp) - getY(eit->first());

            ShapeLike::addVertex(rsh, getX(eit->second())+dx,
                                      getY(eit->second())+dy );

            tmp = std::next(tmp);
        }

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

};

}

#endif // GEOMETRIES_NOFITPOLYGON_HPP
