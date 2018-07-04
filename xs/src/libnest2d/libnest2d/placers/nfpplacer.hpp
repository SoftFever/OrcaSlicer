#ifndef NOFITPOLY_HPP
#define NOFITPOLY_HPP

#ifndef NDEBUG
#include <iostream>
#endif
#include "placer_boilerplate.hpp"
#include "../geometries_nfp.hpp"
#include <libnest2d/optimizers/subplex.hpp>
//#include <libnest2d/optimizers/genetic.hpp>

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

    std::function<double(const Nfp::Shapes<RawShape>&, double, double, double)>
    object_function;

    NfpPConfig(): rotations({0.0, Pi/2.0, Pi, 3*Pi/2}),
        alignment(Alignment::CENTER) {}
};

// A class for getting a point on the circumference of the polygon (in log time)
template<class RawShape> class EdgeCache {
    using Vertex = TPoint<RawShape>;
    using Coord = TCoord<Vertex>;
    using Edge = _Segment<Vertex>;

    enum Corners {
        BOTTOM,
        LEFT,
        RIGHT,
        TOP,
        NUM_CORNERS
    };

    mutable std::vector<double> corners_;

    std::vector<Edge> emap_;
    std::vector<double> distances_;
    double full_distance_ = 0;

    void createCache(const RawShape& sh) {
        auto first = ShapeLike::cbegin(sh);
        auto next = first + 1;
        auto endit = ShapeLike::cend(sh);

        distances_.reserve(ShapeLike::contourVertexCount(sh));

        while(next != endit) {
            emap_.emplace_back(*(first++), *(next++));
            full_distance_ += emap_.back().length();
            distances_.push_back(full_distance_);
        }
    }

    void fetchCorners() const {
        if(!corners_.empty()) return;

        corners_ = std::vector<double>(NUM_CORNERS, 0.0);

        std::vector<unsigned> idx_ud(emap_.size(), 0);
        std::vector<unsigned> idx_lr(emap_.size(), 0);

        std::iota(idx_ud.begin(), idx_ud.end(), 0);
        std::iota(idx_lr.begin(), idx_lr.end(), 0);

        std::sort(idx_ud.begin(), idx_ud.end(),
                  [this](unsigned idx1, unsigned idx2)
        {
            const Vertex& v1 = emap_[idx1].first();
            const Vertex& v2 = emap_[idx2].first();
            auto diff = getY(v1) - getY(v2);
            if(std::abs(diff) <= std::numeric_limits<Coord>::epsilon())
              return getX(v1) < getX(v2);

            return diff < 0;
        });

        std::sort(idx_lr.begin(), idx_lr.end(),
                  [this](unsigned idx1, unsigned idx2)
        {
            const Vertex& v1 = emap_[idx1].first();
            const Vertex& v2 = emap_[idx2].first();

            auto diff = getX(v1) - getX(v2);
            if(std::abs(diff) <= std::numeric_limits<Coord>::epsilon())
                return getY(v1) < getY(v2);

            return diff < 0;
        });

        corners_[BOTTOM] = distances_[idx_ud.front()]/full_distance_;
        corners_[TOP] = distances_[idx_ud.back()]/full_distance_;
        corners_[LEFT] = distances_[idx_lr.front()]/full_distance_;
        corners_[RIGHT] = distances_[idx_lr.back()]/full_distance_;
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
    inline Vertex coords(double distance) {
        assert(distance >= .0 && distance <= 1.0);

        // distance is from 0.0 to 1.0, we scale it up to the full length of
        // the circumference
        double d = distance*full_distance_;

        // Magic: we find the right edge in log time
        auto it = std::lower_bound(distances_.begin(), distances_.end(), d);
        auto idx = it - distances_.begin();     // get the index of the edge
        auto edge = emap_[idx];   // extrac the edge

        // Get the remaining distance on the target edge
        auto ed = d - (idx > 0 ? *std::prev(it) : 0 );
        auto angle = edge.angleToXaxis();
        Vertex ret = edge.first();

        // Get the point on the edge which lies in ed distance from the start
        ret += { static_cast<Coord>(std::round(ed*std::cos(angle))),
                 static_cast<Coord>(std::round(ed*std::sin(angle))) };

        return ret;
    }

    inline double circumference() const BP2D_NOEXCEPT { return full_distance_; }

    inline double corner(Corners c) const BP2D_NOEXCEPT {
        assert(c < NUM_CORNERS);
        fetchCorners();
        return corners_[c];
    }

    inline const std::vector<double>& corners() const BP2D_NOEXCEPT {
        fetchCorners();
        return corners_;
    }

};

// Nfp for a bunch of polygons. If the polygons are convex, the nfp calculated
// for trsh can be the union of nfp-s calculated with each polygon
template<class RawShape, class Container>
Nfp::Shapes<RawShape> nfp(const Container& polygons, const RawShape& trsh )
{
    using Item = _Item<RawShape>;

    Nfp::Shapes<RawShape> nfps;

    for(Item& sh : polygons) {
        auto subnfp = Nfp::noFitPolygon(sh.transformedShape(),
                                        trsh);
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

template<class RawShape>
class _NofitPolyPlacer: public PlacerBoilerplate<_NofitPolyPlacer<RawShape>,
        RawShape, _Box<TPoint<RawShape>>, NfpPConfig<RawShape>> {

    using Base = PlacerBoilerplate<_NofitPolyPlacer<RawShape>,
    RawShape, _Box<TPoint<RawShape>>, NfpPConfig<RawShape>>;

    DECLARE_PLACER(Base)

    using Box = _Box<TPoint<RawShape>>;

    const double norm_;
    const double penality_;

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

                nfps = nfp(items_, trsh);
                auto iv = Nfp::referenceVertex(trsh);

                auto startpos = item.translation();

                std::vector<EdgeCache<RawShape>> ecache;
                ecache.reserve(nfps.size());

                for(auto& nfp : nfps ) ecache.emplace_back(nfp);

                auto getNfpPoint = [&ecache](double relpos) {
                    auto relpfloor = std::floor(relpos);
                    auto nfp_idx = static_cast<unsigned>(relpfloor);
                    if(nfp_idx >= ecache.size()) nfp_idx--;
                    auto p = relpos - relpfloor;
                    return ecache[nfp_idx].coords(p);
                };

                Nfp::Shapes<RawShape> pile;
                pile.reserve(items_.size()+1);
                double pile_area = 0;
                for(Item& mitem : items_) {
                    pile.emplace_back(mitem.transformedShape());
                    pile_area += mitem.area();
                }

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
                auto objfunc = [&] (double relpos)
                {
                    Vertex v = getNfpPoint(relpos);
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
                stopcr.stoplimit = 0.01;
                stopcr.type = opt::StopLimitType::RELATIVE;
                opt::TOptimizer<opt::Method::L_SUBPLEX> solver(stopcr);

                double optimum = 0;
                double best_score = penality_;

                // double max_bound = 1.0*nfps.size();
                // Genetic should look like this:
                /*auto result = solver.optimize_min(objfunc,
                                opt::initvals<double>(0.0),
                                opt::bound(0.0, max_bound)
                                );

                if(result.score < penality_) {
                    best_score = result.score;
                    optimum = std::get<0>(result.optimum);
                }*/

                // Local optimization with the four polygon corners as
                // starting points
                for(unsigned ch = 0; ch < ecache.size(); ch++) {
                    auto& cache = ecache[ch];

                    std::for_each(cache.corners().begin(),
                                  cache.corners().end(),
                                  [ch, &solver, &objfunc,
                                  &best_score, &optimum]
                                  (double pos)
                    {
                        try {
                            auto result = solver.optimize_min(objfunc,
                                            opt::initvals<double>(ch+pos),
                                            opt::bound<double>(ch, 1.0 + ch)
                                            );

                            if(result.score < best_score) {
                                best_score = result.score;
                                optimum = std::get<0>(result.optimum);
                            }
                        } catch(std::exception&
                        #ifndef NDEBUG
                                e
                        #endif
                                ) {
                        #ifndef NDEBUG
                            std::cerr << "ERROR " << e.what() << std::endl;
                        #endif
                        }
                    });
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
    }

private:

    void setInitialPosition(Item& item) {
        Box&& bb = item.boundingBox();

        Vertex ci = bb.minCorner();
        Vertex cb = bin_.minCorner();

        auto&& d = cb - ci;
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
