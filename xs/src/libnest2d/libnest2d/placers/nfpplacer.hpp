#ifndef NOFITPOLY_HPP
#define NOFITPOLY_HPP

#include "placer_boilerplate.hpp"

namespace libnest2d { namespace strategies {

template<class RawShape>
class _NofitPolyPlacer: public PlacerBoilerplate<_NofitPolyPlacer<RawShape>,
        RawShape, _Box<TPoint<RawShape>>> {

    using Base = PlacerBoilerplate<_NofitPolyPlacer<RawShape>,
    RawShape, _Box<TPoint<RawShape>>>;

    DECLARE_PLACER(Base)

public:

    inline explicit _NofitPolyPlacer(const BinType& bin): Base(bin) {}

    PackResult trypack(Item& item) {

        return PackResult();
    }

};

}
}

#endif // NOFITPOLY_H
