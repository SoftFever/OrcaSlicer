#include "clipper_backend.hpp"
#include <atomic>

namespace libnest2d {

namespace  {

class SpinLock {
    std::atomic_flag& lck_;
public:

    inline SpinLock(std::atomic_flag& flg): lck_(flg) {}

    inline void lock() {
        while(lck_.test_and_set(std::memory_order_acquire)) {}
    }

    inline void unlock() { lck_.clear(std::memory_order_release); }
};

class HoleCache {
    friend struct libnest2d::ShapeLike;

    std::unordered_map< const PolygonImpl*, ClipperLib::Paths> map;

    ClipperLib::Paths& _getHoles(const PolygonImpl* p) {
        static std::atomic_flag flg = ATOMIC_FLAG_INIT;
        SpinLock lock(flg);

        lock.lock();
        ClipperLib::Paths& paths = map[p];
        lock.unlock();

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

template<> PolygonImpl ShapeLike::create( const PathImpl& path )
{
    PolygonImpl p;
    p.Contour = path;

    // Expecting that the coordinate system Y axis is positive in upwards
    // direction
    if(ClipperLib::Orientation(p.Contour)) {
        // Not clockwise then reverse the b*tch
        ClipperLib::ReversePath(p.Contour);
    }

    return p;
}

template<> PolygonImpl ShapeLike::create( PathImpl&& path )
{
    PolygonImpl p;
    p.Contour.swap(path);

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

