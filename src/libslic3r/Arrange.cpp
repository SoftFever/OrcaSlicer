#include "Arrange.hpp"

#include "BoundingBox.hpp"

#include <libnest2d/backends/libslic3r/geometries.hpp>
#include <libnest2d/optimizers/nlopt/subplex.hpp>
#include <libnest2d/placers/nfpplacer.hpp>
#include <libnest2d/selections/firstfit.hpp>
#include <libnest2d/utils/rotcalipers.hpp>

#include <numeric>
#include <ClipperUtils.hpp>

#include <boost/geometry/index/rtree.hpp>

#if defined(_MSC_VER) && defined(__clang__)
#define BOOST_NO_CXX17_HDR_STRING_VIEW
#endif

#include <boost/multiprecision/integer.hpp>
#include <boost/rational.hpp>

namespace libnest2d {
#if !defined(_MSC_VER) && defined(__SIZEOF_INT128__) && !defined(__APPLE__)
using LargeInt = __int128;
#else
using LargeInt = boost::multiprecision::int128_t;
template<> struct _NumTag<LargeInt>
{
    using Type = ScalarTag;
};
#endif

template<class T> struct _NumTag<boost::rational<T>>
{
    using Type = RationalTag;
};

namespace nfp {

template<class S> struct NfpImpl<S, NfpLevel::CONVEX_ONLY>
{
    NfpResult<S> operator()(const S &sh, const S &other)
    {
        return nfpConvexOnly<S, boost::rational<LargeInt>>(sh, other);
    }
};

} // namespace nfp
} // namespace libnest2d

namespace Slic3r {

template<class Tout = double, class = FloatingOnly<Tout>, int...EigenArgs>
inline constexpr Eigen::Matrix<Tout, 2, EigenArgs...> unscaled(
    const Slic3r::ClipperLib::IntPoint &v) noexcept
{
    return Eigen::Matrix<Tout, 2, EigenArgs...>{unscaled<Tout>(v.x()),
                                                unscaled<Tout>(v.y())};
}

namespace arrangement {

using namespace libnest2d;

// Get the libnest2d types for clipper backend
using Item         = _Item<ExPolygon>;
using Box          = _Box<Point>;
using Circle       = _Circle<Point>;
using Segment      = _Segment<Point>;
using MultiPolygon = ExPolygons;

// Summon the spatial indexing facilities from boost
namespace bgi = boost::geometry::index;
using SpatElement = std::pair<Box, unsigned>;
using SpatIndex = bgi::rtree< SpatElement, bgi::rstar<16, 4> >;
using ItemGroup = std::vector<std::reference_wrapper<Item>>;

// A coefficient used in separating bigger items and smaller items.
const double BIG_ITEM_TRESHOLD = 0.02;

// Fill in the placer algorithm configuration with values carefully chosen for
// Slic3r.
template<class PConf>
void fill_config(PConf& pcfg, const ArrangeParams &params) {

    // Align the arranged pile into the center of the bin
    pcfg.alignment = PConf::Alignment::CENTER;

    // Start placing the items from the center of the print bed
    pcfg.starting_point = PConf::Alignment::CENTER;

    // TODO cannot use rotations until multiple objects of same geometry can
    // handle different rotations.
    if (params.allow_rotations)
        pcfg.rotations = {0., PI / 2., PI, 3. * PI / 2. };
    else
        pcfg.rotations = {0.};

    // The accuracy of optimization.
    // Goes from 0.0 to 1.0 and scales performance as well
    pcfg.accuracy = params.accuracy;
    
    // Allow parallel execution.
    pcfg.parallel = params.parallel;
}

// Apply penalty to object function result. This is used only when alignment
// after arrange is explicitly disabled (PConfig::Alignment::DONT_ALIGN)
// Also, this will only work well for Box shaped beds.
static double fixed_overfit(const std::tuple<double, Box>& result, const Box &binbb)
{
    double score = std::get<0>(result);
    Box pilebb  = std::get<1>(result);
    Box fullbb  = sl::boundingBox(pilebb, binbb);
    auto diff = double(fullbb.area()) - binbb.area();
    if(diff > 0) score += diff;
    
    return score;
}

// A class encapsulating the libnest2d Nester class and extending it with other
// management and spatial index structures for acceleration.
template<class TBin>
class AutoArranger {
public:
    // Useful type shortcuts...
    using Placer = typename placers::_NofitPolyPlacer<ExPolygon, TBin>;
    using Selector = selections::_FirstFitSelection<ExPolygon>;
    using Packer   = _Nester<Placer, Selector>;
    using PConfig  = typename Packer::PlacementConfig;
    using Distance = TCoord<PointImpl>;

protected:
    Packer    m_pck;
    PConfig   m_pconf; // Placement configuration
    TBin      m_bin;
    double    m_bin_area;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif
    SpatIndex m_rtree; // spatial index for the normal (bigger) objects
    SpatIndex m_smallsrtree;    // spatial index for only the smaller items
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    double    m_norm;           // A coefficient to scale distances
    MultiPolygon m_merged_pile; // The already merged pile (vector of items)
    Box          m_pilebb;      // The bounding box of the merged pile.
    ItemGroup m_remaining;      // Remaining items
    ItemGroup m_items;          // allready packed items
    size_t    m_item_count = 0; // Number of all items to be packed
    
    template<class T> ArithmeticOnly<T, double> norm(T val)
    {
        return double(val) / m_norm;
    }

    // This is "the" object function which is evaluated many times for each
    // vertex (decimated with the accuracy parameter) of each object.
    // Therefore it is upmost crucial for this function to be as efficient
    // as it possibly can be but at the same time, it has to provide
    // reasonable results.
    std::tuple<double /*score*/, Box /*farthest point from bin center*/>
    objfunc(const Item &item, const Point &bincenter)
    {
        const double bin_area = m_bin_area;
        const SpatIndex& spatindex = m_rtree;
        const SpatIndex& smalls_spatindex = m_smallsrtree;
        
        // We will treat big items (compared to the print bed) differently
        auto isBig = [bin_area](double a) {
            return a/bin_area > BIG_ITEM_TRESHOLD ;
        };
        
        // Candidate item bounding box
        auto ibb = item.boundingBox();
        
        // Calculate the full bounding box of the pile with the candidate item
        auto fullbb = sl::boundingBox(m_pilebb, ibb);
        
        // The bounding box of the big items (they will accumulate in the center
        // of the pile
        Box bigbb;
        if(spatindex.empty()) bigbb = fullbb;
        else {
            auto boostbb = spatindex.bounds();
            boost::geometry::convert(boostbb, bigbb);
        }
        
        // Will hold the resulting score
        double score = 0;
        
        // Density is the pack density: how big is the arranged pile
        double density = 0;
        
        // Distinction of cases for the arrangement scene
        enum e_cases {
            // This branch is for big items in a mixed (big and small) scene
            // OR for all items in a small-only scene.
            BIG_ITEM,
            
            // This branch is for the last big item in a mixed scene
            LAST_BIG_ITEM,
            
            // For small items in a mixed scene.
            SMALL_ITEM
        } compute_case;
        
        bool bigitems = isBig(item.area()) || spatindex.empty();
        if(bigitems && !m_remaining.empty()) compute_case = BIG_ITEM;
        else if (bigitems && m_remaining.empty()) compute_case = LAST_BIG_ITEM;
        else compute_case = SMALL_ITEM;
        
        switch (compute_case) {
        case BIG_ITEM: {
            const Point& minc = ibb.minCorner(); // bottom left corner
            const Point& maxc = ibb.maxCorner(); // top right corner

            // top left and bottom right corners
            Point top_left{getX(minc), getY(maxc)};
            Point bottom_right{getX(maxc), getY(minc)};

            // Now the distance of the gravity center will be calculated to the
            // five anchor points and the smallest will be chosen.
            std::array<double, 5> dists;
            auto cc = fullbb.center(); // The gravity center
            dists[0] = pl::distance(minc, cc);
            dists[1] = pl::distance(maxc, cc);
            dists[2] = pl::distance(ibb.center(), cc);
            dists[3] = pl::distance(top_left, cc);
            dists[4] = pl::distance(bottom_right, cc);

            // The smalles distance from the arranged pile center:
            double dist = norm(*(std::min_element(dists.begin(), dists.end())));
            double bindist = norm(pl::distance(ibb.center(), bincenter));
            dist = 0.8 * dist + 0.2 * bindist;

            // Prepare a variable for the alignment score.
            // This will indicate: how well is the candidate item
            // aligned with its neighbors. We will check the alignment
            // with all neighbors and return the score for the best
            // alignment. So it is enough for the candidate to be
            // aligned with only one item.
            auto alignment_score = 1.0;

            auto query = bgi::intersects(ibb);
            auto& index = isBig(item.area()) ? spatindex : smalls_spatindex;

            // Query the spatial index for the neighbors
            std::vector<SpatElement> result;
            result.reserve(index.size());

            index.query(query, std::back_inserter(result));

            // now get the score for the best alignment
            for(auto& e : result) { 
                auto idx = e.second;
                Item& p = m_items[idx];
                auto parea = p.area();
                if(std::abs(1.0 - parea/item.area()) < 1e-6) {
                    auto bb = sl::boundingBox(p.boundingBox(), ibb);
                    auto bbarea = bb.area();
                    auto ascore = 1.0 - (item.area() + parea)/bbarea;

                    if(ascore < alignment_score) alignment_score = ascore;
                }
            }
            
            density = std::sqrt(norm(fullbb.width()) * norm(fullbb.height()));
            double R = double(m_remaining.size()) / m_item_count;
            
            // The final mix of the score is the balance between the
            // distance from the full pile center, the pack density and
            // the alignment with the neighbors
            if (result.empty())
                score = 0.50 * dist + 0.50 * density;
            else
                // Let the density matter more when fewer objects remain
                score = 0.50 * dist + (1.0 - R) * 0.20 * density +
                        0.30 * alignment_score;

            break;
        }
        case LAST_BIG_ITEM: {
            score = norm(pl::distance(ibb.center(), m_pilebb.center()));
            break;
        }
        case SMALL_ITEM: {
            // Here there are the small items that should be placed around the
            // already processed bigger items.
            // No need to play around with the anchor points, the center will be
            // just fine for small items
            score = norm(pl::distance(ibb.center(), bigbb.center()));
            break;
        }            
        }
        
        return std::make_tuple(score, fullbb);
    }
    
    std::function<double(const Item&)> get_objfn();
    
public:
    AutoArranger(const TBin &                  bin,
                 const ArrangeParams           &params,
                 std::function<void(unsigned)> progressind,
                 std::function<bool(void)>     stopcond)
        : m_pck(bin, params.min_obj_distance)
        , m_bin(bin)
        , m_bin_area(sl::area(bin))
        , m_norm(std::sqrt(m_bin_area))
    {
        fill_config(m_pconf, params);

        // Set up a callback that is called just before arranging starts
        // This functionality is provided by the Nester class (m_pack).
        m_pconf.before_packing =
        [this](const MultiPolygon& merged_pile,            // merged pile
               const ItemGroup& items,             // packed items
               const ItemGroup& remaining)         // future items to be packed
        {
            m_items = items;
            m_merged_pile = merged_pile;
            m_remaining = remaining;

            m_pilebb = sl::boundingBox(merged_pile);

            m_rtree.clear();
            m_smallsrtree.clear();
            
            // We will treat big items (compared to the print bed) differently
            auto isBig = [this](double a) {
                return a / m_bin_area > BIG_ITEM_TRESHOLD ;
            };

            for(unsigned idx = 0; idx < items.size(); ++idx) {
                Item& itm = items[idx];
                if(isBig(itm.area())) m_rtree.insert({itm.boundingBox(), idx});
                m_smallsrtree.insert({itm.boundingBox(), idx});
            }
        };
        
        m_pconf.object_function = get_objfn();

        m_pconf.on_preload = [this](const ItemGroup &items, PConfig &cfg) {
            if (items.empty()) return;

            cfg.alignment = PConfig::Alignment::DONT_ALIGN;
            auto bb = sl::boundingBox(m_bin);
            auto bbcenter = bb.center();
            cfg.object_function = [this, bb, bbcenter](const Item &item) {
                return fixed_overfit(objfunc(item, bbcenter), bb);
            };
        };

        auto on_packed = params.on_packed;
        
        if (progressind || on_packed)
            m_pck.progressIndicator([this, progressind, on_packed](unsigned rem) {

            if (progressind)
                progressind(rem);

            if (on_packed) {
                int last_bed = m_pck.lastPackedBinId();
                if (last_bed >= 0) {
                    Item &last_packed = m_pck.lastResult()[last_bed].back();
                    ArrangePolygon ap;
                    ap.bed_idx = last_packed.binId();
                    ap.priority = last_packed.priority();
                    on_packed(ap);
                }
            }
        });

        if (stopcond) m_pck.stopCondition(stopcond);

        m_pck.configure(m_pconf);
    }
     
    template<class It> inline void operator()(It from, It to) {
        m_rtree.clear();
        m_item_count += size_t(to - from);
        m_pck.execute(from, to);
        m_item_count = 0;
    }
    
    PConfig& config() { return m_pconf; }
    const PConfig& config() const { return m_pconf; }
    
    inline void preload(std::vector<Item>& fixeditems) {        
        for(unsigned idx = 0; idx < fixeditems.size(); ++idx) {
            Item& itm = fixeditems[idx];
            itm.markAsFixedInBin(itm.binId());
        }

        m_item_count += fixeditems.size();
    }
};

template<> std::function<double(const Item&)> AutoArranger<Box>::get_objfn()
{
    auto bincenter = m_bin.center();

    return [this, bincenter](const Item &itm) {
        auto result = objfunc(itm, bincenter);
        
        double score = std::get<0>(result);
        auto& fullbb = std::get<1>(result);

        double miss = Placer::overfit(fullbb, m_bin);
        miss = miss > 0? miss : 0;
        score += miss * miss;
        
        return score;
    };
}

template<> std::function<double(const Item&)> AutoArranger<Circle>::get_objfn()
{
    auto bincenter = m_bin.center();
    return [this, bincenter](const Item &item) {
        
        auto result = objfunc(item, bincenter);
        
        double score = std::get<0>(result);
        
        auto isBig = [this](const Item& itm) {
            return itm.area() / m_bin_area > BIG_ITEM_TRESHOLD ;
        };
        
        if(isBig(item)) {
            auto mp = m_merged_pile;
            mp.push_back(item.transformedShape());
            auto chull = sl::convexHull(mp);
            double miss = Placer::overfit(chull, m_bin);
            if(miss < 0) miss = 0;
            score += miss*miss;
        }
        
        return score;
    };
}

// Specialization for a generalized polygon.
// Warning: this is unfinished business. It may or may not work.
template<>
std::function<double(const Item &)> AutoArranger<ExPolygon>::get_objfn()
{
    auto bincenter = sl::boundingBox(m_bin).center();
    return [this, bincenter](const Item &item) {
        return std::get<0>(objfunc(item, bincenter));
    };
}

template<class Bin> void remove_large_items(std::vector<Item> &items, Bin &&bin)
{
    auto it = items.begin();
    while (it != items.end())
        sl::isInside(it->transformedShape(), bin) ?
            ++it : it = items.erase(it);
}

template<class S> Radians min_area_boundingbox_rotation(const S &sh)
{
    return minAreaBoundingBox<S, TCompute<S>, boost::rational<LargeInt>>(sh)
        .angleToX();
}

template<class S>
Radians fit_into_box_rotation(const S &sh, const _Box<TPoint<S>> &box)
{
    return fitIntoBoxRotation<S, TCompute<S>, boost::rational<LargeInt>>(sh, box);
}

template<class BinT> // Arrange for arbitrary bin type
void _arrange(
        std::vector<Item> &           shapes,
        std::vector<Item> &           excludes,
        const BinT &                  bin,
        const ArrangeParams           &params,
        std::function<void(unsigned)> progressfn,
        std::function<bool()>         stopfn)
{
    // Integer ceiling the min distance from the bed perimeters
    coord_t md = params.min_obj_distance;
    md = md / 2;
    
    auto corrected_bin = bin;
    sl::offset(corrected_bin, md);
    ArrangeParams mod_params = params;
    mod_params.min_obj_distance = 0;

    AutoArranger<BinT> arranger{corrected_bin, mod_params, progressfn, stopfn};
    
    auto infl = coord_t(std::ceil(params.min_obj_distance / 2.0));
    for (Item& itm : shapes) itm.inflate(infl);
    for (Item& itm : excludes) itm.inflate(infl);
    
    remove_large_items(excludes, corrected_bin);

    // If there is something on the plate
    if (!excludes.empty()) arranger.preload(excludes);

    std::vector<std::reference_wrapper<Item>> inp;
    inp.reserve(shapes.size() + excludes.size());
    for (auto &itm : shapes  ) inp.emplace_back(itm);
    for (auto &itm : excludes) inp.emplace_back(itm);
    
    // Use the minimum bounding box rotation as a starting point.
    // TODO: This only works for convex hull. If we ever switch to concave
    // polygon nesting, a convex hull needs to be calculated.
    if (params.allow_rotations) {
        for (auto &itm : shapes) {
            itm.rotation(min_area_boundingbox_rotation(itm.rawShape()));

            // If the item is too big, try to find a rotation that makes it fit
            if constexpr (std::is_same_v<BinT, Box>) {
                auto bb = itm.boundingBox();
                if (bb.width() >= bin.width() || bb.height() >= bin.height())
                    itm.rotate(fit_into_box_rotation(itm.transformedShape(), bin));
            }
        }
    }

    arranger(inp.begin(), inp.end());
    for (Item &itm : inp) itm.inflate(-infl);
}

inline Box to_nestbin(const BoundingBox &bb) { return Box{{bb.min(X), bb.min(Y)}, {bb.max(X), bb.max(Y)}};}
inline Circle to_nestbin(const CircleBed &c) { return Circle({c.center()(0), c.center()(1)}, c.radius()); }
inline ExPolygon to_nestbin(const Polygon &p) { return ExPolygon{p}; }
inline Box to_nestbin(const InfiniteBed &bed) { return Box::infinite({bed.center.x(), bed.center.y()}); }

inline coord_t width(const BoundingBox& box) { return box.max.x() - box.min.x(); }
inline coord_t height(const BoundingBox& box) { return box.max.y() - box.min.y(); }
inline double area(const BoundingBox& box) { return double(width(box)) * height(box); }
inline double poly_area(const Points &pts) { return std::abs(Polygon::area(pts)); }
inline double distance_to(const Point& p1, const Point& p2)
{
    double dx = p2.x() - p1.x();
    double dy = p2.y() - p1.y();
    return std::sqrt(dx*dx + dy*dy);
}

static CircleBed to_circle(const Point &center, const Points& points) {
    std::vector<double> vertex_distances;
    double avg_dist = 0;
    
    for (auto pt : points)
    {
        double distance = distance_to(center, pt);
        vertex_distances.push_back(distance);
        avg_dist += distance;
    }
    
    avg_dist /= vertex_distances.size();
    
    CircleBed ret(center, avg_dist);
    for(auto el : vertex_distances)
    {
        if (std::abs(el - avg_dist) > 10 * SCALED_EPSILON) {
            ret = {};
            break;
        }
    }
    
    return ret;
}

// Create Item from Arrangeable
static void process_arrangeable(const ArrangePolygon &arrpoly,
                                std::vector<Item> &   outp)
{
    Polygon        p        = arrpoly.poly.contour;
    const Vec2crd &offs     = arrpoly.translation;
    double         rotation = arrpoly.rotation;

    // This fixes:
    // https://github.com/prusa3d/PrusaSlicer/issues/2209
    if (p.points.size() < 3)
        return;

    outp.emplace_back(std::move(p));
    outp.back().rotation(rotation);
    outp.back().translation({offs.x(), offs.y()});
    outp.back().binId(arrpoly.bed_idx);
    outp.back().priority(arrpoly.priority);
}

template<class Fn> auto call_with_bed(const Points &bed, Fn &&fn)
{
    if (bed.empty())
        return fn(InfiniteBed{});
    else if (bed.size() == 1)
        return fn(InfiniteBed{bed.front()});
    else {
        auto      bb    = BoundingBox(bed);
        CircleBed circ  = to_circle(bb.center(), bed);
        auto      parea = poly_area(bed);

        if ((1.0 - parea / area(bb)) < 1e-3)
            return fn(bb);
        else if (!std::isnan(circ.radius()))
            return fn(circ);
        else
            return fn(Polygon(bed));
    }
}

template<>
void arrange(ArrangePolygons &      items,
             const ArrangePolygons &excludes,
             const Points &         bed,
             const ArrangeParams &  params)
{
    call_with_bed(bed, [&](const auto &bin) {
        arrange(items, excludes, bin, params);
    });
}

template<class BedT>
void arrange(ArrangePolygons &      arrangables,
             const ArrangePolygons &excludes,
             const BedT &           bed,
             const ArrangeParams &  params)
{
    namespace clppr = Slic3r::ClipperLib;
    
    std::vector<Item> items, fixeditems;
    items.reserve(arrangables.size());
    
    for (ArrangePolygon &arrangeable : arrangables)
        process_arrangeable(arrangeable, items);
    
    for (const ArrangePolygon &fixed: excludes)
        process_arrangeable(fixed, fixeditems);
    
    for (Item &itm : fixeditems) itm.inflate(scaled(-2. * EPSILON));
    
    auto &cfn = params.stopcondition;
    auto &pri = params.progressind;
    
    _arrange(items, fixeditems, to_nestbin(bed), params, pri, cfn);
    
    for(size_t i = 0; i < items.size(); ++i) {
        Point tr = items[i].translation();
        arrangables[i].translation = {coord_t(tr.x()), coord_t(tr.y())};
        arrangables[i].rotation    = items[i].rotation();
        arrangables[i].bed_idx     = items[i].binId();
    }
}

template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const BoundingBox &bed, const ArrangeParams &params);
template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const CircleBed &bed, const ArrangeParams &params);
template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const Polygon &bed, const ArrangeParams &params);
template void arrange(ArrangePolygons &items, const ArrangePolygons &excludes, const InfiniteBed &bed, const ArrangeParams &params);

} // namespace arr
} // namespace Slic3r
