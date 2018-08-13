#ifndef LIBNEST2D_H
#define LIBNEST2D_H

// The type of backend should be set conditionally by the cmake configuriation
// for now we set it statically to clipper backend
#include <libnest2d/clipper_backend/clipper_backend.hpp>

// We include the stock optimizers for local and global optimization
#include <libnest2d/optimizers/subplex.hpp>     // Local subplex for NfpPlacer
#include <libnest2d/optimizers/genetic.hpp>     // Genetic for min. bounding box

#include <libnest2d/libnest2d.hpp>
#include <libnest2d/placers/bottomleftplacer.hpp>
#include <libnest2d/placers/nfpplacer.hpp>
#include <libnest2d/selections/firstfit.hpp>
#include <libnest2d/selections/filler.hpp>
#include <libnest2d/selections/djd_heuristic.hpp>

namespace libnest2d {

using Point = PointImpl;
using Coord = TCoord<PointImpl>;
using Box = _Box<PointImpl>;
using Segment = _Segment<PointImpl>;

using Item = _Item<PolygonImpl>;
using Rectangle = _Rectangle<PolygonImpl>;

using PackGroup = _PackGroup<PolygonImpl>;
using IndexedPackGroup = _IndexedPackGroup<PolygonImpl>;

using FillerSelection = strategies::_FillerSelection<PolygonImpl>;
using FirstFitSelection = strategies::_FirstFitSelection<PolygonImpl>;
using DJDHeuristic  = strategies::_DJDHeuristic<PolygonImpl>;

using NfpPlacer = strategies::_NofitPolyPlacer<PolygonImpl>;
using BottomLeftPlacer = strategies::_BottomLeftPlacer<PolygonImpl>;

//template<NfpLevel lvl = NfpLevel::BOTH_CONCAVE_WITH_HOLES>
//using NofitPolyPlacer = strategies::_NofitPolyPlacer<PolygonImpl, lvl>;

}

#endif // LIBNEST2D_H
