#include "clipper_backend.hpp"

namespace libnest2d {

namespace  {
class HoleCache {
    friend struct libnest2d::ShapeLike;

    std::unordered_map< const PolygonImpl*, ClipperLib::Paths> map;

    ClipperLib::Paths& _getHoles(const PolygonImpl* p) {
        ClipperLib::Paths& paths = map[p];

        if(paths.size() != p->Childs.size()) {
            paths.reserve(p->Childs.size());

            for(auto np : p->Childs) {
                paths.emplace_back(np->Contour);
            }
        }

        return paths;
    }

    ClipperLib::Paths& getHoles(PolygonImpl& p) {
        return _getHoles(&p);
    }

    const ClipperLib::Paths& getHoles(const PolygonImpl& p) {
        return _getHoles(&p);
    }
};
}

HoleCache holeCache;

template<>
std::string ShapeLike::toString(const PolygonImpl& sh)
{
   std::stringstream ss;

   for(auto p : sh.Contour) {
       ss << p.X << " " << p.Y << "\n";
   }

   return ss.str();
}

template<> PolygonImpl ShapeLike::create( std::initializer_list< PointImpl > il)
{
    PolygonImpl p;
    p.Contour = il;

    // Expecting that the coordinate system Y axis is positive in upwards
    // direction
    if(ClipperLib::Orientation(p.Contour)) {
        // Not clockwise then reverse the b*tch
        ClipperLib::ReversePath(p.Contour);
    }

    return p;
}

template<>
const THolesContainer<PolygonImpl>& ShapeLike::holes(
        const PolygonImpl& sh)
{
    return holeCache.getHoles(sh);
}

template<>
THolesContainer<PolygonImpl>& ShapeLike::holes(PolygonImpl& sh) {
    return holeCache.getHoles(sh);
}

}

