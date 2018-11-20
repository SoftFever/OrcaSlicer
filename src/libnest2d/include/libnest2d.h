#ifndef LIBNEST2D_H
#define LIBNEST2D_H

// The type of backend should be set conditionally by the cmake configuriation
// for now we set it statically to clipper backend
#ifdef LIBNEST2D_BACKEND_CLIPPER
#include <libnest2d/backends/clipper/geometries.hpp>
#endif

#ifdef LIBNEST2D_OPTIMIZER_NLOPT
// We include the stock optimizers for local and global optimization
#include <libnest2d/optimizers/nlopt/subplex.hpp>     // Local subplex for NfpPlacer
#include <libnest2d/optimizers/nlopt/genetic.hpp>     // Genetic for min. bounding box
#endif

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
using Circle = _Circle<PointImpl>;

using Item = _Item<PolygonImpl>;
using Rectangle = _Rectangle<PolygonImpl>;

using PackGroup = _PackGroup<PolygonImpl>;
using IndexedPackGroup = _IndexedPackGroup<PolygonImpl>;

using FillerSelection = selections::_FillerSelection<PolygonImpl>;
using FirstFitSelection = selections::_FirstFitSelection<PolygonImpl>;
using DJDHeuristic  = selections::_DJDHeuristic<PolygonImpl>;

template<class Bin> // Generic placer for arbitrary bin types
using _NfpPlacer = placers::_NofitPolyPlacer<PolygonImpl, Bin>;

// NfpPlacer is with Box bin
using NfpPlacer = _NfpPlacer<Box>;

// This supports only box shaped bins
using BottomLeftPlacer = placers::_BottomLeftPlacer<PolygonImpl>;

template<class Placer = NfpPlacer,
         class Selector = FirstFitSelection,
         class Iterator = std::vector<Item>::iterator>
PackGroup nest(Iterator from, Iterator to,
               const typename Placer::BinType& bin,
               Coord dist = 0,
               const typename Placer::Config& pconf = {},
               const typename Selector::Config& sconf = {})
{
    Nester<Placer, Selector> nester(bin, dist, pconf, sconf);
    return nester.execute(from, to);
}

template<class Placer = NfpPlacer,
         class Selector = FirstFitSelection,
         class Container = std::vector<Item>>
PackGroup nest(Container&& cont,
               const typename Placer::BinType& bin,
               Coord dist = 0,
               const typename Placer::Config& pconf = {},
               const typename Selector::Config& sconf = {})
{
    return nest<Placer, Selector>(cont.begin(), cont.end(),
                                  bin, dist, pconf, sconf);
}

template<class Placer = NfpPlacer,
         class Selector = FirstFitSelection,
         class Iterator = std::vector<Item>::iterator>
PackGroup nest(Iterator from, Iterator to,
               const typename Placer::BinType& bin,
               ProgressFunction prg,
               StopCondition scond = []() { return false; },
               Coord dist = 0,
               const typename Placer::Config& pconf = {},
               const typename Selector::Config& sconf = {})
{
    Nester<Placer, Selector> nester(bin, dist, pconf, sconf);
    if(prg) nester.progressIndicator(prg);
    if(scond) nester.stopCondition(scond);
    return nester.execute(from, to);
}

template<class Placer = NfpPlacer,
         class Selector = FirstFitSelection,
         class Container = std::vector<Item>>
PackGroup nest(Container&& cont,
               const typename Placer::BinType& bin,
               ProgressFunction prg,
               StopCondition scond = []() { return false; },
               Coord dist = 0,
               const typename Placer::Config& pconf = {},
               const typename Selector::Config& sconf = {})
{
    return nest<Placer, Selector>(cont.begin(), cont.end(),
                                  bin, prg, scond, dist, pconf, sconf);
}

#ifdef LIBNEST2D_STATIC
extern template
PackGroup nest<NfpPlacer, FirstFitSelection, std::vector<Item>&>(
    std::vector<Item>& cont,
    const Box& bin,
    Coord dist,
    const NfpPlacer::Config& pcfg,
    const FirstFitSelection::Config& scfg
);

extern template
PackGroup nest<NfpPlacer, FirstFitSelection, std::vector<Item>&>(
    std::vector<Item>& cont,
    const Box& bin,
    ProgressFunction prg,
    StopCondition scond,
    Coord dist,
    const NfpPlacer::Config& pcfg,
    const FirstFitSelection::Config& scfg
);

extern template
PackGroup nest<NfpPlacer, FirstFitSelection, std::vector<Item>>(
    std::vector<Item>&& cont,
    const Box& bin,
    Coord dist,
    const NfpPlacer::Config& pcfg,
    const FirstFitSelection::Config& scfg
);

extern template
PackGroup nest<NfpPlacer, FirstFitSelection, std::vector<Item>>(
    std::vector<Item>&& cont,
    const Box& bin,
    ProgressFunction prg,
    StopCondition scond,
    Coord dist,
    const NfpPlacer::Config& pcfg,
    const FirstFitSelection::Config& scfg
);

extern template
PackGroup nest<NfpPlacer, FirstFitSelection, std::vector<Item>::iterator>(
    std::vector<Item>::iterator from,
    std::vector<Item>::iterator to,
    const Box& bin,
    Coord dist,
    const NfpPlacer::Config& pcfg,
    const FirstFitSelection::Config& scfg
);

extern template
PackGroup nest<NfpPlacer, FirstFitSelection, std::vector<Item>::iterator>(
    std::vector<Item>::iterator from,
    std::vector<Item>::iterator to,
    const Box& bin,
    ProgressFunction prg,
    StopCondition scond,
    Coord dist,
    const NfpPlacer::Config& pcfg,
    const FirstFitSelection::Config& scfg
);

#endif

}

#endif // LIBNEST2D_H
