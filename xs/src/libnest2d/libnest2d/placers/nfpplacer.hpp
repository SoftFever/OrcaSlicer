#ifndef NOFITPOLY_HPP
#define NOFITPOLY_HPP

#ifndef NDEBUG
#include <iostream>
#endif
#include "placer_boilerplate.hpp"
#include "../geometry_traits_nfp.hpp"
#include "libnest2d/optimizer.hpp"
#include <cassert>

#include "tools/svgtools.hpp"

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

    /// Which angles to try out for better results.
    std::vector<Radians> rotations;

    /// Where to align the resulting packed pile.
    Alignment alignment;

    /// Where to start putting objects in the bin.
    Alignment starting_point;

    /**
     * @brief A function object representing the fitting function in the
     * placement optimization process. (Optional)
     *
     * This is the most versatile tool to configure the placer. The fitting
     * function is evaluated many times when a new item is being placed into the
     * bin. The output should be a rated score of the new item's position.
     *
     * This is not a mandatory option as there is a default fitting function
     * that will optimize for the best pack efficiency. With a custom fitting
     * function you can e.g. influence the shape of the arranged pile.
     *
     * \param shapes The first parameter is a container with all the placed
     * polygons excluding the current candidate. You can calculate a bounding
     * box or convex hull on this pile of polygons without the candidate item
     * or push back the candidate item into the container and then calculate
     * some features.
     *
     * \param item The second parameter is the candidate item.
     *
     * \param occupied_area The third parameter is the sum of areas of the
     * items in the first parameter so you don't have to iterate through them
     * if you only need their area.
     *
     * \param norm A norming factor for physical dimensions. E.g. if your score
     * is the distance between the item and the bin center, you should divide
     * that distance with the norming factor. If the score is an area than
     * divide it with the square of the norming factor. Imagine it as a unit of
     * distance.
     *
     * \param penality The fifth parameter is the amount of minimum penality if
     * the arranged pile would't fit into the bin. You can use the wouldFit()
     * function to check this. Note that the pile can be outside the bin's
     * boundaries while the placement algorithm is running. Your job is only to
     * check if the pile could be translated into a position in the bin where
     * all the items would be inside. For a box shaped bin you can use the
     * pile's bounding box to check whether it's width and height is small
     * enough. If the pile would not fit, you have to make sure that the
     * resulting score will be higher then the penality value. A good solution
     * would be to set score = 2*penality-score in case the pile wouldn't fit
     * into the bin.
     *
     */
    std::function<double(Nfp::Shapes<RawShape>&, const _Item<RawShape>&,
                         double, double, double)>
    object_function;

    /**
     * @brief The quality of search for an optimal placement.
     * This is a compromise slider between quality and speed. Zero is the
     * fast and poor solution while 1.0 is the slowest but most accurate.
     */
    float accuracy = 1.0;

    /**
     * @brief If you want to see items inside other item's holes, you have to
     * turn this switch on.
     *
     * This will only work if a suitable nfp implementation is provided.
     * The library has no such implementation right now.
     */
    bool explore_holes = false;

    NfpPConfig(): rotations({0.0, Pi/2.0, Pi, 3*Pi/2}),
        alignment(Alignment::CENTER), starting_point(Alignment::CENTER) {}
};

/**
 * A class for getting a point on the circumference of the polygon (in log time)
 *
 * This is a transformation of the provided polygon to be able to pinpoint
 * locations on the circumference. The optimizer will pass a floating point
 * value e.g. within <0,1> and we have to transform this value quickly into a
 * coordinate on the circumference. By definition 0 should yield the first
 * vertex and 1.0 would be the last (which should coincide with first).
 *
 * We also have to make this work for the holes of the captured polygon.
 */
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

    double accuracy_ = 1.0;

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

    size_t stride(const size_t N) const {
        using std::ceil;
        using std::round;
        using std::pow;

        return static_cast<Coord>(
                    std::round(N/std::pow(N, std::pow(accuracy_, 1.0/3.0)))
                );
    }

    void fetchCorners() const {
        if(!contour_.corners.empty()) return;

        const auto N = contour_.distances.size();
        const auto S = stride(N);

        contour_.corners.reserve(N / S + 1);
        auto N_1 = N-1;
        contour_.corners.emplace_back(0.0);
        for(size_t i = 0; i < N_1; i += S) {
            contour_.corners.emplace_back(
                    contour_.distances.at(i) / contour_.full_distance);
        }
    }

    void fetchHoleCorners(unsigned hidx) const {
        auto& hc = holes_[hidx];
        if(!hc.corners.empty()) return;

        const auto N = hc.distances.size();
        const auto S = stride(N);
        auto N_1 = N-1;
        hc.corners.reserve(N / S + 1);
        hc.corners.emplace_back(0.0);
        for(size_t i = 0; i < N_1; i += S) {
            hc.corners.emplace_back(
                    hc.distances.at(i) / hc.full_distance);
        }
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

    /// Resolution of returned corners. The stride is derived from this value.
    void accuracy(double a /* within <0.0, 1.0>*/) { accuracy_ = a; }

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

    /// Get the normalized distance values for each vertex
    inline const std::vector<double>& corners() const BP2D_NOEXCEPT {
        fetchCorners();
        return contour_.corners;
    }

    /// corners for a specific hole
    inline const std::vector<double>&
    corners(unsigned holeidx) const BP2D_NOEXCEPT {
        fetchHoleCorners(holeidx);
        return holes_[holeidx].corners;
    }

    /// The number of holes in the abstracted polygon
    inline size_t holeCount() const BP2D_NOEXCEPT { return holes_.size(); }

};

template<NfpLevel lvl>
struct Lvl { static const NfpLevel value = lvl; };

template<class RawShape>
inline void correctNfpPosition(Nfp::NfpResult<RawShape>& nfp,
                               const _Item<RawShape>& stationary,
                               const _Item<RawShape>& orbiter)
{
    // The provided nfp is somewhere in the dark. We need to get it
    // to the right position around the stationary shape.
    // This is done by choosing the leftmost lowest vertex of the
    // orbiting polygon to be touched with the rightmost upper
    // vertex of the stationary polygon. In this configuration, the
    // reference vertex of the orbiting polygon (which can be dragged around
    // the nfp) will be its rightmost upper vertex that coincides with the
    // rightmost upper vertex of the nfp. No proof provided other than Jonas
    // Lindmark's reasoning about the reference vertex of nfp in his thesis
    // ("No fit polygon problem" - section 2.1.9)

    auto touch_sh = stationary.rightmostTopVertex();
    auto touch_other = orbiter.leftmostBottomVertex();
    auto dtouch = touch_sh - touch_other;
    auto top_other = orbiter.rightmostTopVertex() + dtouch;
    auto dnfp = top_other - nfp.second; // nfp.second is the nfp reference point
    ShapeLike::translate(nfp.first, dnfp);
}

template<class RawShape>
inline void correctNfpPosition(Nfp::NfpResult<RawShape>& nfp,
                               const RawShape& stationary,
                               const _Item<RawShape>& orbiter)
{
    auto touch_sh = Nfp::rightmostUpVertex(stationary);
    auto touch_other = orbiter.leftmostBottomVertex();
    auto dtouch = touch_sh - touch_other;
    auto top_other = orbiter.rightmostTopVertex() + dtouch;
    auto dnfp = top_other - nfp.second;
    ShapeLike::translate(nfp.first, dnfp);
}

template<class RawShape, class Container>
Nfp::Shapes<RawShape> nfp( const Container& polygons,
                           const _Item<RawShape>& trsh,
                           Lvl<NfpLevel::CONVEX_ONLY>)
{
    using Item = _Item<RawShape>;

    Nfp::Shapes<RawShape> nfps;

    //int pi = 0;
    for(Item& sh : polygons) {
        auto subnfp_r = Nfp::noFitPolygon<NfpLevel::CONVEX_ONLY>(
                            sh.transformedShape(), trsh.transformedShape());
        #ifndef NDEBUG
            auto vv = ShapeLike::isValid(sh.transformedShape());
            assert(vv.first);

            auto vnfp = ShapeLike::isValid(subnfp_r.first);
            assert(vnfp.first);
        #endif

        correctNfpPosition(subnfp_r, sh, trsh);

        nfps = Nfp::merge(nfps, subnfp_r.first);

//        double SCALE = 1000000;
//        using SVGWriter = svg::SVGWriter<RawShape>;
//        SVGWriter::Config conf;
//        conf.mm_in_coord_units = SCALE;
//        SVGWriter svgw(conf);
//        Box bin(250*SCALE, 210*SCALE);
//        svgw.setSize(bin);
//        for(int i = 0; i <= pi; i++) svgw.writeItem(polygons[i]);
//        svgw.writeItem(trsh);
////        svgw.writeItem(Item(subnfp_r.first));
//        for(auto& n : nfps) svgw.writeItem(Item(n));
//        svgw.save("nfpout");
//        pi++;
    }

    return nfps;
}

template<class RawShape, class Container, class Level>
Nfp::Shapes<RawShape> nfp( const Container& polygons,
                           const _Item<RawShape>& trsh,
                           Level)
{
    using Item = _Item<RawShape>;

    Nfp::Shapes<RawShape> nfps;

    auto& orb = trsh.transformedShape();
    bool orbconvex = trsh.isContourConvex();

    for(Item& sh : polygons) {
        Nfp::NfpResult<RawShape> subnfp;
        auto& stat = sh.transformedShape();

        if(sh.isContourConvex() && orbconvex)
            subnfp = Nfp::noFitPolygon<NfpLevel::CONVEX_ONLY>(stat, orb);
        else if(orbconvex)
            subnfp = Nfp::noFitPolygon<NfpLevel::ONE_CONVEX>(stat, orb);
        else
            subnfp = Nfp::noFitPolygon<Level::value>(stat, orb);

        correctNfpPosition(subnfp, sh, trsh);

        nfps = Nfp::merge(nfps, subnfp.first);
    }

    return nfps;


//    using Item = _Item<RawShape>;
//    using sl = ShapeLike;

//    Nfp::Shapes<RawShape> nfps, stationary;

//    for(Item& sh : polygons) {
//        stationary = Nfp::merge(stationary, sh.transformedShape());
//    }

//    for(RawShape& sh : stationary) {

////        auto vv = sl::isValid(sh);
////        std::cout << vv.second << std::endl;


//        Nfp::NfpResult<RawShape> subnfp;
//        bool shconvex = sl::isConvex<RawShape>(sl::getContour(sh));
//        if(shconvex && trsh.isContourConvex()) {
//            subnfp = Nfp::noFitPolygon<NfpLevel::CONVEX_ONLY>(
//                        sh, trsh.transformedShape());
//        } else if(trsh.isContourConvex()) {
//            subnfp = Nfp::noFitPolygon<NfpLevel::ONE_CONVEX>(
//                        sh, trsh.transformedShape());
//        }
//        else {
//            subnfp = Nfp::noFitPolygon<Level::value>( sh,
//                                                      trsh.transformedShape());
//        }

//        correctNfpPosition(subnfp, sh, trsh);

//        nfps = Nfp::merge(nfps, subnfp.first);
//    }

//    return nfps;
}

template<class RawShape, class TBin = _Box<TPoint<RawShape>>>
class _NofitPolyPlacer: public PlacerBoilerplate<_NofitPolyPlacer<RawShape, TBin>,
        RawShape, TBin, NfpPConfig<RawShape>> {

    using Base = PlacerBoilerplate<_NofitPolyPlacer<RawShape, TBin>,
    RawShape, TBin, NfpPConfig<RawShape>>;

    DECLARE_PLACER(Base)

    using Box = _Box<TPoint<RawShape>>;

    const double norm_;
    const double penality_;

    using MaxNfpLevel = Nfp::MaxNfpLevel<RawShape>;
    using sl = ShapeLike;

public:

    using Pile = Nfp::Shapes<RawShape>;

    inline explicit _NofitPolyPlacer(const BinType& bin):
        Base(bin),
        norm_(std::sqrt(sl::area<RawShape>(bin))),
        penality_(1e6*norm_) {}

    _NofitPolyPlacer(const _NofitPolyPlacer&) = default;
    _NofitPolyPlacer& operator=(const _NofitPolyPlacer&) = default;

#ifndef BP2D_COMPILER_MSVC12 // MSVC2013 does not support default move ctors
    _NofitPolyPlacer(_NofitPolyPlacer&&) BP2D_NOEXCEPT = default;
    _NofitPolyPlacer& operator=(_NofitPolyPlacer&&) BP2D_NOEXCEPT = default;
#endif

    bool static inline wouldFit(const Box& bb, const RawShape& bin) {
        auto bbin = sl::boundingBox<RawShape>(bin);
        auto d = bbin.center() - bb.center();
        _Rectangle<RawShape> rect(bb.width(), bb.height());
        rect.translate(bb.minCorner() + d);
        return sl::isInside<RawShape>(rect.transformedShape(), bin);
    }

    bool static inline wouldFit(const RawShape& chull, const RawShape& bin) {
        auto bbch = sl::boundingBox<RawShape>(chull);
        auto bbin = sl::boundingBox<RawShape>(bin);
        auto d =  bbch.center() - bbin.center();
        auto chullcpy = chull;
        sl::translate(chullcpy, d);
        return sl::isInside<RawShape>(chullcpy, bin);
    }

    bool static inline wouldFit(const RawShape& chull, const Box& bin)
    {
        auto bbch = sl::boundingBox<RawShape>(chull);
        return wouldFit(bbch, bin);
    }

    bool static inline wouldFit(const Box& bb, const Box& bin)
    {
        return bb.width() <= bin.width() && bb.height() <= bin.height();
    }

    bool static inline wouldFit(const Box& bb, const _Circle<Vertex>& bin)
    {
        return sl::isInside<RawShape>(bb, bin);
    }

    bool static inline wouldFit(const RawShape& chull,
                                const _Circle<Vertex>& bin)
    {
        return sl::isInside<RawShape>(chull, bin);
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

                for(auto& nfp : nfps ) {
                    ecache.emplace_back(nfp);
                    ecache.back().accuracy(config_.accuracy);
                }

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
                            ecache[opt.nfpidx].coords(opt.hidx, opt.relpos);
                };

                Nfp::Shapes<RawShape> pile;
                pile.reserve(items_.size()+1);
                double pile_area = 0;
                for(Item& mitem : items_) {
                    pile.emplace_back(mitem.transformedShape());
                    pile_area += mitem.area();
                }

                auto merged_pile = Nfp::merge(pile);

                // This is the kernel part of the object function that is
                // customizable by the library client
                auto _objfunc = config_.object_function?
                            config_.object_function :
                [this, &merged_pile](
                            Nfp::Shapes<RawShape>& /*pile*/,
                            const Item& item,
                            double occupied_area,
                            double norm,
                            double /*penality*/)
                {
                    merged_pile.emplace_back(item.transformedShape());
                    auto ch = sl::convexHull(merged_pile);
                    merged_pile.pop_back();

                    // The pack ratio -- how much is the convex hull occupied
                    double pack_rate = occupied_area/sl::area(ch);

                    // ratio of waste
                    double waste = 1.0 - pack_rate;

                    // Score is the square root of waste. This will extend the
                    // range of good (lower) values and shring the range of bad
                    // (larger) values.
                    auto score = std::sqrt(waste);

                    if(!wouldFit(ch, bin_)) score += norm;

                    return score;
                };

                // Our object function for placement
                auto rawobjfunc = [&] (Vertex v)
                {
                    auto d = v - iv;
                    d += startpos;
                    item.translation(d);

                    double occupied_area = pile_area + item.area();

                    double score = _objfunc(pile, item, occupied_area,
                                            norm_, penality_);

                    return score;
                };

                auto boundaryCheck = [&](const Optimum& o) {
                    auto v = getNfpPoint(o);
                    auto d = v - iv;
                    d += startpos;
                    item.translation(d);

                    merged_pile.emplace_back(item.transformedShape());
                    auto chull = sl::convexHull(merged_pile);
                    merged_pile.pop_back();

                    return wouldFit(chull, bin_);
                };

                opt::StopCriteria stopcr;
                stopcr.max_iterations = 100;
                stopcr.relative_score_difference = 1e-6;
                opt::TOptimizer<opt::Method::L_SUBPLEX> solver(stopcr);

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
                                  &optimum, &boundaryCheck] (double pos)
                    {
                        try {
                            auto result = solver.optimize_min(contour_ofn,
                                            opt::initvals<double>(pos),
                                            opt::bound<double>(0, 1.0)
                                            );

                            if(result.score < best_score) {
                                Optimum o(std::get<0>(result.optimum), ch, -1);
                                if(boundaryCheck(o)) {
                                    best_score = result.score;
                                    optimum = o;
                                }
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
                                       &optimum, ch, hidx, &boundaryCheck]
                                      (double pos)
                        {
                            try {
                                auto result = solver.optimize_min(hole_ofn,
                                                opt::initvals<double>(pos),
                                                opt::bound<double>(0, 1.0)
                                                );

                                if(result.score < best_score) {
                                    Optimum o(std::get<0>(result.optimum),
                                              ch, hidx);
                                    if(boundaryCheck(o)) {
                                        best_score = result.score;
                                        optimum = o;
                                    }
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
        auto&& bb = sl::boundingBox<RawShape>(m);

        Vertex ci, cb;
        auto bbin = sl::boundingBox<RawShape>(bin_);

        switch(config_.alignment) {
        case Config::Alignment::CENTER: {
            ci = bb.center();
            cb = bbin.center();
            break;
        }
        case Config::Alignment::BOTTOM_LEFT: {
            ci = bb.minCorner();
            cb = bbin.minCorner();
            break;
        }
        case Config::Alignment::BOTTOM_RIGHT: {
            ci = {getX(bb.maxCorner()), getY(bb.minCorner())};
            cb = {getX(bbin.maxCorner()), getY(bbin.minCorner())};
            break;
        }
        case Config::Alignment::TOP_LEFT: {
            ci = {getX(bb.minCorner()), getY(bb.maxCorner())};
            cb = {getX(bbin.minCorner()), getY(bbin.maxCorner())};
            break;
        }
        case Config::Alignment::TOP_RIGHT: {
            ci = bb.maxCorner();
            cb = bbin.maxCorner();
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
        auto bbin = sl::boundingBox<RawShape>(bin_);

        switch(config_.starting_point) {
        case Config::Alignment::CENTER: {
            ci = bb.center();
            cb = bbin.center();
            break;
        }
        case Config::Alignment::BOTTOM_LEFT: {
            ci = bb.minCorner();
            cb = bbin.minCorner();
            break;
        }
        case Config::Alignment::BOTTOM_RIGHT: {
            ci = {getX(bb.maxCorner()), getY(bb.minCorner())};
            cb = {getX(bbin.maxCorner()), getY(bbin.minCorner())};
            break;
        }
        case Config::Alignment::TOP_LEFT: {
            ci = {getX(bb.minCorner()), getY(bb.maxCorner())};
            cb = {getX(bbin.minCorner()), getY(bbin.maxCorner())};
            break;
        }
        case Config::Alignment::TOP_RIGHT: {
            ci = bb.maxCorner();
            cb = bbin.maxCorner();
            break;
        }
        }

        auto d = cb - ci;
        item.translate(d);
    }

    void placeOutsideOfBin(Item& item) {
        auto&& bb = item.boundingBox();
        Box binbb = sl::boundingBox<RawShape>(bin_);

        Vertex v = { getX(bb.maxCorner()), getY(bb.minCorner()) };

        Coord dx = getX(binbb.maxCorner()) - getX(v);
        Coord dy = getY(binbb.maxCorner()) - getY(v);

        item.translate({dx, dy});
    }

};


}
}

#endif // NOFITPOLY_H
