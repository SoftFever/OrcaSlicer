#ifndef NOFITPOLY_HPP
#define NOFITPOLY_HPP

#ifndef NDEBUG
#include <iostream>
#endif
#include "placer_boilerplate.hpp"
#include "../geometry_traits_nfp.hpp"

namespace libnest2d { namespace strategies {

template<class RawShape>
struct NfpPConfig {

    enum class Alignment {
        CENTER,
        BOTTOM_LEFT,
        BOTTOM_RIGHT,
        TOP_LEFT,
        TOP_RIGHT,
    };

    /// Which angles to try out for better results
    std::vector<Radians> rotations;

    /// Where to align the resulting packed pile
    Alignment alignment;

    Alignment starting_point;

    std::function<double(const Nfp::Shapes<RawShape>&, double, double, double)>
    object_function;

    /**
     * @brief The quality of search for an optimal placement.
     * This is a compromise slider between quality and speed. Zero is the
     * fast and poor solution while 1.0 is the slowest but most accurate.
     */
    float accuracy = 1.0;

    NfpPConfig(): rotations({0.0, Pi/2.0, Pi, 3*Pi/2}),
        alignment(Alignment::CENTER), starting_point(Alignment::CENTER) {}
};

// A class for getting a point on the circumference of the polygon (in log time)
template<class RawShape> class EdgeCache {
    using Vertex = TPoint<RawShape>;
    using Coord = TCoord<Vertex>;
    using Edge = _Segment<Vertex>;

    struct ContourCache {
        mutable std::vector<double> corners;
        std::vector<Edge> emap;
        std::vector<double> distances;
        double full_distance = 0;
    } contour_;

    std::vector<ContourCache> holes_;

    void createCache(const RawShape& sh) {
        {   // For the contour
            auto first = ShapeLike::cbegin(sh);
            auto next = std::next(first);
            auto endit = ShapeLike::cend(sh);

            contour_.distances.reserve(ShapeLike::contourVertexCount(sh));

            while(next != endit) {
                contour_.emap.emplace_back(*(first++), *(next++));
                contour_.full_distance += contour_.emap.back().length();
                contour_.distances.push_back(contour_.full_distance);
            }
        }

        for(auto& h : ShapeLike::holes(sh)) { // For the holes
            auto first = h.begin();
            auto next = std::next(first);
            auto endit = h.end();

            ContourCache hc;
            hc.distances.reserve(endit - first);

            while(next != endit) {
                hc.emap.emplace_back(*(first++), *(next++));
                hc.full_distance += hc.emap.back().length();
                hc.distances.push_back(hc.full_distance);
            }

            holes_.push_back(hc);
        }
    }

    void fetchCorners() const {
        if(!contour_.corners.empty()) return;

        // TODO Accuracy
        contour_.corners = contour_.distances;
        for(auto& d : contour_.corners) d /= contour_.full_distance;
    }

    void fetchHoleCorners(unsigned hidx) const {
        auto& hc = holes_[hidx];
        if(!hc.corners.empty()) return;

        // TODO Accuracy
        hc.corners = hc.distances;
        for(auto& d : hc.corners) d /= hc.full_distance;
    }

    inline Vertex coords(const ContourCache& cache, double distance) const {
        assert(distance >= .0 && distance <= 1.0);

        // distance is from 0.0 to 1.0, we scale it up to the full length of
        // the circumference
        double d = distance*cache.full_distance;

        auto& distances = cache.distances;

        // Magic: we find the right edge in log time
        auto it = std::lower_bound(distances.begin(), distances.end(), d);
        auto idx = it - distances.begin();      // get the index of the edge
        auto edge = cache.emap[idx];         // extrac the edge

        // Get the remaining distance on the target edge
        auto ed = d - (idx > 0 ? *std::prev(it) : 0 );
        auto angle = edge.angleToXaxis();
        Vertex ret = edge.first();

        // Get the point on the edge which lies in ed distance from the start
        ret += { static_cast<Coord>(std::round(ed*std::cos(angle))),
                 static_cast<Coord>(std::round(ed*std::sin(angle))) };

        return ret;
    }

public:

    using iterator = std::vector<double>::iterator;
    using const_iterator = std::vector<double>::const_iterator;

    inline EdgeCache() = default;

    inline EdgeCache(const _Item<RawShape>& item)
    {
        createCache(item.transformedShape());
    }

    inline EdgeCache(const RawShape& sh)
    {
        createCache(sh);
    }

    /**
     * @brief Get a point on the circumference of a polygon.
     * @param distance A relative distance from the starting point to the end.
     * Can be from 0.0 to 1.0 where 0.0 is the starting point and 1.0 is the
     * closing point (which should be eqvivalent with the starting point with
     * closed polygons).
     * @return Returns the coordinates of the point lying on the polygon
     * circumference.
     */
    inline Vertex coords(double distance) const {
        return coords(contour_, distance);
    }

    inline Vertex coords(unsigned hidx, double distance) const {
        assert(hidx < holes_.size());
        return coords(holes_[hidx], distance);
    }

    inline double circumference() const BP2D_NOEXCEPT {
        return contour_.full_distance;
    }

    inline double circumference(unsigned hidx) const BP2D_NOEXCEPT {
        return holes_[hidx].full_distance;
    }

    inline const std::vector<double>& corners() const BP2D_NOEXCEPT {
        fetchCorners();
        return contour_.corners;
    }

    inline const std::vector<double>&
    corners(unsigned holeidx) const BP2D_NOEXCEPT {
        fetchHoleCorners(holeidx);
        return holes_[holeidx].corners;
    }

    inline unsigned holeCount() const BP2D_NOEXCEPT { return holes_.size(); }

};

template<NfpLevel lvl>
struct Lvl { static const NfpLevel value = lvl; };

template<class RawShape, class Container>
Nfp::Shapes<RawShape> nfp( const Container& polygons,
                           const _Item<RawShape>& trsh,
                           Lvl<NfpLevel::CONVEX_ONLY>)
{
    using Item = _Item<RawShape>;

    Nfp::Shapes<RawShape> nfps;

    for(Item& sh : polygons) {
        auto subnfp = Nfp::noFitPolygon<NfpLevel::CONVEX_ONLY>(
                    sh.transformedShape(), trsh.transformedShape());
        #ifndef NDEBUG
            auto vv = ShapeLike::isValid(sh.transformedShape());
            assert(vv.first);

            auto vnfp = ShapeLike::isValid(subnfp);
            assert(vnfp.first);
        #endif

        nfps = Nfp::merge(nfps, subnfp);
    }

    return nfps;
}

template<class RawShape, class Container, class Level>
Nfp::Shapes<RawShape> nfp( const Container& polygons,
                           const _Item<RawShape>& trsh,
                           Level)
{
    using Item = _Item<RawShape>;

    Nfp::Shapes<RawShape> nfps, stationary;

    for(Item& sh : polygons) {
        stationary = Nfp::merge(stationary, sh.transformedShape());
    }

    std::cout << "pile size: " << stationary.size() << std::endl;
    for(RawShape& sh : stationary) {

        RawShape subnfp;
//        if(sh.isContourConvex() && trsh.isContourConvex()) {
//            subnfp = Nfp::noFitPolygon<NfpLevel::CONVEX_ONLY>(
//                        sh.transformedShape(), trsh.transformedShape());
//        } else {
            subnfp = Nfp::noFitPolygon<Level::value>( sh/*.transformedShape()*/,
                                                      trsh.transformedShape());
//        }

//        #ifndef NDEBUG
//            auto vv = ShapeLike::isValid(sh.transformedShape());
//            assert(vv.first);

//            auto vnfp = ShapeLike::isValid(subnfp);
//            assert(vnfp.first);
//        #endif

//            auto vnfp = ShapeLike::isValid(subnfp);
//            if(!vnfp.first) {
//                std::cout << vnfp.second << std::endl;
//                std::cout << ShapeLike::toString(subnfp) << std::endl;
//            }

        nfps = Nfp::merge(nfps, subnfp);
    }

    return nfps;
}

template<class RawShape>
class _NofitPolyPlacer: public PlacerBoilerplate<_NofitPolyPlacer<RawShape>,
        RawShape, _Box<TPoint<RawShape>>, NfpPConfig<RawShape>> {

    using Base = PlacerBoilerplate<_NofitPolyPlacer<RawShape>,
    RawShape, _Box<TPoint<RawShape>>, NfpPConfig<RawShape>>;

    DECLARE_PLACER(Base)

    using Box = _Box<TPoint<RawShape>>;

    const double norm_;
    const double penality_;

    using MaxNfpLevel = Nfp::MaxNfpLevel<RawShape>;

public:

    using Pile = const Nfp::Shapes<RawShape>&;

    inline explicit _NofitPolyPlacer(const BinType& bin):
        Base(bin),
        norm_(std::sqrt(ShapeLike::area<RawShape>(bin))),
        penality_(1e6*norm_) {}

    bool static inline wouldFit(const RawShape& chull, const RawShape& bin) {
        auto bbch = ShapeLike::boundingBox<RawShape>(chull);
        auto bbin = ShapeLike::boundingBox<RawShape>(bin);
        auto d = bbin.minCorner() - bbch.minCorner();
        auto chullcpy = chull;
        ShapeLike::translate(chullcpy, d);
        return ShapeLike::isInside<RawShape>(chullcpy, bbin);
    }

    bool static inline wouldFit(const RawShape& chull, const Box& bin)
    {
        auto bbch = ShapeLike::boundingBox<RawShape>(chull);
        return wouldFit(bbch, bin);
    }

    bool static inline wouldFit(const Box& bb, const Box& bin)
    {
        return bb.width() <= bin.width() && bb.height() <= bin.height();
    }

    PackResult trypack(Item& item) {

        PackResult ret;

        bool can_pack = false;

        if(items_.empty()) {
            setInitialPosition(item);
            can_pack = item.isInside(bin_);
        } else {

            double global_score = penality_;

            auto initial_tr = item.translation();
            auto initial_rot = item.rotation();
            Vertex final_tr = {0, 0};
            Radians final_rot = initial_rot;
            Nfp::Shapes<RawShape> nfps;

            for(auto rot : config_.rotations) {

                item.translation(initial_tr);
                item.rotation(initial_rot + rot);

                // place the new item outside of the print bed to make sure
                // it is disjuct from the current merged pile
                placeOutsideOfBin(item);

                auto trsh = item.transformedShape();

                nfps = nfp(items_, item, Lvl<MaxNfpLevel::value>());
                auto iv = Nfp::referenceVertex(trsh);

                auto startpos = item.translation();

                std::vector<EdgeCache<RawShape>> ecache;
                ecache.reserve(nfps.size());

                for(auto& nfp : nfps ) ecache.emplace_back(nfp);

                struct Optimum {
                    double relpos;
                    unsigned nfpidx;
                    int hidx;
                    Optimum(double pos, unsigned nidx):
                        relpos(pos), nfpidx(nidx), hidx(-1) {}
                    Optimum(double pos, unsigned nidx, int holeidx):
                        relpos(pos), nfpidx(nidx), hidx(holeidx) {}
                };

                auto getNfpPoint = [&ecache](const Optimum& opt)
                {
                    return opt.hidx < 0? ecache[opt.nfpidx].coords(opt.relpos) :
                            ecache[opt.nfpidx].coords(opt.nfpidx, opt.relpos);
                };

                Nfp::Shapes<RawShape> pile;
                pile.reserve(items_.size()+1);
                double pile_area = 0;
                for(Item& mitem : items_) {
                    pile.emplace_back(mitem.transformedShape());
                    pile_area += mitem.area();
                }

                // This is the kernel part of the object function that is
                // customizable by the library client
                auto _objfunc = config_.object_function?
                            config_.object_function :
                [this](const Nfp::Shapes<RawShape>& pile, double occupied_area,
                            double /*norm*/, double penality)
                {
                    auto ch = ShapeLike::convexHull(pile);

                    // The pack ratio -- how much is the convex hull occupied
                    double pack_rate = occupied_area/ShapeLike::area(ch);

                    // ratio of waste
                    double waste = 1.0 - pack_rate;

                    // Score is the square root of waste. This will extend the
                    // range of good (lower) values and shring the range of bad
                    // (larger) values.
                    auto score = std::sqrt(waste);

                    if(!wouldFit(ch, bin_)) score = 2*penality - score;

                    return score;
                };

                // Our object function for placement
                auto rawobjfunc = [&] (Vertex v)
                {
                    auto d = v - iv;
                    d += startpos;
                    item.translation(d);

                    pile.emplace_back(item.transformedShape());

                    double occupied_area = pile_area + item.area();

                    double score = _objfunc(pile, occupied_area,
                                            norm_, penality_);

                    pile.pop_back();

                    return score;
                };

                opt::StopCriteria stopcr;
                stopcr.max_iterations = 1000;
                stopcr.stoplimit = 0.001;
                stopcr.type = opt::StopLimitType::RELATIVE;
                opt::TOptimizer<opt::Method::L_SIMPLEX> solver(stopcr);

                Optimum optimum(0, 0);
                double best_score = penality_;

                // Local optimization with the four polygon corners as
                // starting points
                for(unsigned ch = 0; ch < ecache.size(); ch++) {
                    auto& cache = ecache[ch];

                    auto contour_ofn = [&rawobjfunc, &getNfpPoint, ch]
                            (double relpos)
                    {
                        return rawobjfunc(getNfpPoint(Optimum(relpos, ch)));
                    };

                    std::for_each(cache.corners().begin(),
                                  cache.corners().end(),
                                  [ch, &contour_ofn, &solver, &best_score,
                                  &optimum] (double pos)
                    {
                        try {
                            auto result = solver.optimize_min(contour_ofn,
                                            opt::initvals<double>(pos),
                                            opt::bound<double>(0, 1.0)
                                            );

                            if(result.score < best_score) {
                                best_score = result.score;
                                optimum.relpos = std::get<0>(result.optimum);
                                optimum.nfpidx = ch;
                                optimum.hidx = -1;
                            }
                        } catch(std::exception& e) {
                            derr() << "ERROR: " << e.what() << "\n";
                        }
                    });

                    for(unsigned hidx = 0; hidx < cache.holeCount(); ++hidx) {
                        auto hole_ofn =
                                [&rawobjfunc, &getNfpPoint, ch, hidx]
                                (double pos)
                        {
                            Optimum opt(pos, ch, hidx);
                            return rawobjfunc(getNfpPoint(opt));
                        };

                        std::for_each(cache.corners(hidx).begin(),
                                      cache.corners(hidx).end(),
                                      [&hole_ofn, &solver, &best_score,
                                       &optimum, ch, hidx]
                                      (double pos)
                        {
                            try {
                                auto result = solver.optimize_min(hole_ofn,
                                                opt::initvals<double>(pos),
                                                opt::bound<double>(0, 1.0)
                                                );

                                if(result.score < best_score) {
                                    best_score = result.score;
                                    Optimum o(std::get<0>(result.optimum),
                                              ch, hidx);
                                    optimum = o;
                                }
                            } catch(std::exception& e) {
                                derr() << "ERROR: " << e.what() << "\n";
                            }
                        });
                    }
                }

                if( best_score < global_score ) {
                    auto d = getNfpPoint(optimum) - iv;
                    d += startpos;
                    final_tr = d;
                    final_rot = initial_rot + rot;
                    can_pack = true;
                    global_score = best_score;
                }
            }

            item.translation(final_tr);
            item.rotation(final_rot);
        }

        if(can_pack) {
            ret = PackResult(item);
        }

        return ret;
    }

    ~_NofitPolyPlacer() {
        clearItems();
    }

    inline void clearItems() {
        Nfp::Shapes<RawShape> m;
        m.reserve(items_.size());

        for(Item& item : items_) m.emplace_back(item.transformedShape());
        auto&& bb = ShapeLike::boundingBox<RawShape>(m);

        Vertex ci, cb;

        switch(config_.alignment) {
        case Config::Alignment::CENTER: {
            ci = bb.center();
            cb = bin_.center();
            break;
        }
        case Config::Alignment::BOTTOM_LEFT: {
            ci = bb.minCorner();
            cb = bin_.minCorner();
            break;
        }
        case Config::Alignment::BOTTOM_RIGHT: {
            ci = {getX(bb.maxCorner()), getY(bb.minCorner())};
            cb = {getX(bin_.maxCorner()), getY(bin_.minCorner())};
            break;
        }
        case Config::Alignment::TOP_LEFT: {
            ci = {getX(bb.minCorner()), getY(bb.maxCorner())};
            cb = {getX(bin_.minCorner()), getY(bin_.maxCorner())};
            break;
        }
        case Config::Alignment::TOP_RIGHT: {
            ci = bb.maxCorner();
            cb = bin_.maxCorner();
            break;
        }
        }

        auto d = cb - ci;
        for(Item& item : items_) item.translate(d);

        Base::clearItems();
    }

private:

    void setInitialPosition(Item& item) {
        Box&& bb = item.boundingBox();
        Vertex ci, cb;

        switch(config_.starting_point) {
        case Config::Alignment::CENTER: {
            ci = bb.center();
            cb = bin_.center();
            break;
        }
        case Config::Alignment::BOTTOM_LEFT: {
            ci = bb.minCorner();
            cb = bin_.minCorner();
            break;
        }
        case Config::Alignment::BOTTOM_RIGHT: {
            ci = {getX(bb.maxCorner()), getY(bb.minCorner())};
            cb = {getX(bin_.maxCorner()), getY(bin_.minCorner())};
            break;
        }
        case Config::Alignment::TOP_LEFT: {
            ci = {getX(bb.minCorner()), getY(bb.maxCorner())};
            cb = {getX(bin_.minCorner()), getY(bin_.maxCorner())};
            break;
        }
        case Config::Alignment::TOP_RIGHT: {
            ci = bb.maxCorner();
            cb = bin_.maxCorner();
            break;
        }
        }

        auto d = cb - ci;
        item.translate(d);
    }

    void placeOutsideOfBin(Item& item) {
        auto&& bb = item.boundingBox();
        Box binbb = ShapeLike::boundingBox<RawShape>(bin_);

        Vertex v = { getX(bb.maxCorner()), getY(bb.minCorner()) };

        Coord dx = getX(binbb.maxCorner()) - getX(v);
        Coord dy = getY(binbb.maxCorner()) - getY(v);

        item.translate({dx, dy});
    }

};


}
}

#endif // NOFITPOLY_H
