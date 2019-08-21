#include "Arrange.hpp"
#include "Geometry.hpp"
#include "SVG.hpp"
#include "MTUtils.hpp"

#include <libnest2d/backends/clipper/geometries.hpp>
#include <libnest2d/optimizers/nlopt/subplex.hpp>
#include <libnest2d/placers/nfpplacer.hpp>
#include <libnest2d/selections/firstfit.hpp>

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
inline SLIC3R_CONSTEXPR Eigen::Matrix<Tout, 2, EigenArgs...> unscaled(
    const ClipperLib::IntPoint &v) SLIC3R_NOEXCEPT
{
    return Eigen::Matrix<Tout, 2, EigenArgs...>{unscaled<Tout>(v.X),
                                                unscaled<Tout>(v.Y)};
}

namespace arrangement {

using namespace libnest2d;
namespace clppr = ClipperLib;

// Get the libnest2d types for clipper backend
using Item         = _Item<clppr::Polygon>;
using Box          = _Box<clppr::IntPoint>;
using Circle       = _Circle<clppr::IntPoint>;
using Segment      = _Segment<clppr::IntPoint>;
using MultiPolygon = TMultiShape<clppr::Polygon>;

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
void fillConfig(PConf& pcfg) {

    // Align the arranged pile into the center of the bin
    pcfg.alignment = PConf::Alignment::CENTER;

    // Start placing the items from the center of the print bed
    pcfg.starting_point = PConf::Alignment::CENTER;

    // TODO cannot use rotations until multiple objects of same geometry can
    // handle different rotations.
    pcfg.rotations = { 0.0 };

    // The accuracy of optimization.
    // Goes from 0.0 to 1.0 and scales performance as well
    pcfg.accuracy = 0.65f;
    
    // Allow parallel execution.
    pcfg.parallel = true;
}

// Apply penalty to object function result. This is used only when alignment
// after arrange is explicitly disabled (PConfig::Alignment::DONT_ALIGN)
double fixed_overfit(const std::tuple<double, Box>& result, const Box &binbb)
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
    using Placer = typename placers::_NofitPolyPlacer<clppr::Polygon, TBin>;
    using Selector = selections::_FirstFitSelection<clppr::Polygon>;
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
    objfunc(const Item &item, const clppr::IntPoint &bincenter)
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
            const clppr::IntPoint& minc = ibb.minCorner(); // bottom left corner
            const clppr::IntPoint& maxc = ibb.maxCorner(); // top right corner

            // top left and bottom right corners
            clppr::IntPoint top_left{getX(minc), getY(maxc)};
            clppr::IntPoint bottom_right{getX(maxc), getY(minc)};

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
                score = R * 0.60 * dist +
                        (1.0 - R) * 0.20 * density +
                        0.20 * alignment_score;
            
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
                 Distance                      dist,
                 std::function<void(unsigned)> progressind,
                 std::function<bool(void)>     stopcond)
        : m_pck(bin, dist)
        , m_bin(bin)
        , m_bin_area(sl::area(bin))
        , m_norm(std::sqrt(m_bin_area))
    {
        fillConfig(m_pconf);

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
        
        if (progressind) m_pck.progressIndicator(progressind);
        if (stopcond) m_pck.stopCondition(stopcond);
        
        m_pck.configure(m_pconf);
    }
    
    template<class It> inline void operator()(It from, It to) {
        m_rtree.clear();
        m_item_count += size_t(to - from);
        m_pck.execute(from, to);
        m_item_count = 0;
    }
    
    inline void preload(std::vector<Item>& fixeditems) {
        m_pconf.alignment = PConfig::Alignment::DONT_ALIGN;
        auto bb = sl::boundingBox(m_bin);
        auto bbcenter = bb.center();
        m_pconf.object_function = [this, bb, bbcenter](const Item &item) {
            return fixed_overfit(objfunc(item, bbcenter), bb);
        };

        // Build the rtree for queries to work
        
        for(unsigned idx = 0; idx < fixeditems.size(); ++idx) {
            Item& itm = fixeditems[idx];
            itm.markAsFixed();
        }

        m_pck.configure(m_pconf);
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
        score += miss*miss;
        
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
std::function<double(const Item &)> AutoArranger<clppr::Polygon>::get_objfn()
{
    auto bincenter = sl::boundingBox(m_bin).center();
    return [this, bincenter](const Item &item) {
        return std::get<0>(objfunc(item, bincenter));
    };
}

inline Circle to_lnCircle(const CircleBed& circ) {
    return Circle({circ.center()(0), circ.center()(1)}, circ.radius());
}

// Get the type of bed geometry from a simple vector of points.
BedShapeHint::BedShapeHint(const Polyline &bed) {
    auto x = [](const Point& p) { return p(X); };
    auto y = [](const Point& p) { return p(Y); };

    auto width = [x](const BoundingBox& box) {
        return x(box.max) - x(box.min);
    };

    auto height = [y](const BoundingBox& box) {
        return y(box.max) - y(box.min);
    };

    auto area = [&width, &height](const BoundingBox& box) {
        double w = width(box);
        double h = height(box);
        return w * h;
    };

    auto poly_area = [](Polyline p) {
        Polygon pp; pp.points.reserve(p.points.size() + 1);
        pp.points = std::move(p.points);
        pp.points.emplace_back(pp.points.front());
        return std::abs(pp.area());
    };

    auto distance_to = [x, y](const Point& p1, const Point& p2) {
        double dx = x(p2) - x(p1);
        double dy = y(p2) - y(p1);
        return std::sqrt(dx*dx + dy*dy);
    };

    auto bb = bed.bounding_box();

    auto isCircle = [bb, distance_to](const Polyline& polygon) {
        auto center = bb.center();
        std::vector<double> vertex_distances;
        double avg_dist = 0;
        for (auto pt: polygon.points)
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
                ret = CircleBed();
                break;
            }
        }

        return ret;
    };

    auto parea = poly_area(bed);

    if( (1.0 - parea/area(bb)) < 1e-3 ) {
        m_type = BedShapes::bsBox;
        m_bed.box = bb;
    }
    else if(auto c = isCircle(bed)) {
        m_type = BedShapes::bsCircle;
        m_bed.circ = c;
    } else {
        assert(m_type != BedShapes::bsIrregular);
        m_type = BedShapes::bsIrregular;
        ::new (&m_bed.polygon) Polyline(bed);
    }
}

template<class BinT> // Arrange for arbitrary bin type
void _arrange(
    std::vector<Item> &           shapes,
    std::vector<Item> &           excludes,
    const BinT &                  bin,
    coord_t                       minobjd,
    std::function<void(unsigned)> prind,
    std::function<bool()>         stopfn)
{
    // Integer ceiling the min distance from the bed perimeters
    coord_t md = minobjd - 2 * scaled(0.1 + EPSILON);
    md = (md % 2) ? md / 2 + 1 : md / 2;
    
    auto corrected_bin = bin;
    sl::offset(corrected_bin, md);
    
    AutoArranger<BinT> arranger{corrected_bin, 0, prind, stopfn};
    
    auto infl = coord_t(std::ceil(minobjd / 2.0));
    for (Item& itm : shapes) itm.inflate(infl);
    for (Item& itm : excludes) itm.inflate(infl);
    
    auto it = excludes.begin();
    while (it != excludes.end())
        sl::isInside(it->transformedShape(), corrected_bin) ?
            ++it : it = excludes.erase(it);

    // If there is something on the plate
    if (!excludes.empty()) arranger.preload(excludes);

    std::vector<std::reference_wrapper<Item>> inp;
    inp.reserve(shapes.size() + excludes.size());
    for (auto &itm : shapes  ) inp.emplace_back(itm);
    for (auto &itm : excludes) inp.emplace_back(itm);
    
    arranger(inp.begin(), inp.end());
    for (Item &itm : inp) itm.inflate(-infl);
}

// The final client function for arrangement. A progress indicator and
// a stop predicate can be also be passed to control the process.
void arrange(ArrangePolygons &             arrangables,
             const ArrangePolygons &       excludes,
             coord_t                       min_obj_dist,
             const BedShapeHint &          bedhint,
             std::function<void(unsigned)> progressind,
             std::function<bool()>         stopcondition)
{
    namespace clppr = ClipperLib;
    
    std::vector<Item> items, fixeditems;
    items.reserve(arrangables.size());
    
    // Create Item from Arrangeable
    auto process_arrangeable =
        [](const ArrangePolygon &arrpoly, std::vector<Item> &outp)
    {
        Polygon p        = arrpoly.poly.contour;
        const Vec2crd &  offs     = arrpoly.translation;
        double           rotation = arrpoly.rotation;

        if (p.is_counter_clockwise()) p.reverse();

        clppr::Polygon clpath(Slic3rMultiPoint_to_ClipperPath(p));

        auto firstp = clpath.Contour.front();
        clpath.Contour.emplace_back(firstp);

        outp.emplace_back(std::move(clpath));
        outp.back().rotation(rotation);
        outp.back().translation({offs.x(), offs.y()});
        outp.back().binId(arrpoly.bed_idx);
        outp.back().priority(arrpoly.priority);
    };

    for (ArrangePolygon &arrangeable : arrangables)
        process_arrangeable(arrangeable, items);
    
    for (const ArrangePolygon &fixed: excludes)
        process_arrangeable(fixed, fixeditems);
    
    for (Item &itm : fixeditems) itm.inflate(scaled(-2. * EPSILON));
    
    auto &cfn = stopcondition;
    auto &pri = progressind;
    
    switch (bedhint.get_type()) {
    case bsBox: {
        // Create the arranger for the box shaped bed
        BoundingBox bbb = bedhint.get_box();
        Box binbb{{bbb.min(X), bbb.min(Y)}, {bbb.max(X), bbb.max(Y)}};
        
        _arrange(items, fixeditems, binbb, min_obj_dist, pri, cfn);
        break;
    }
    case bsCircle: {
        auto cc = to_lnCircle(bedhint.get_circle());
        
        _arrange(items, fixeditems, cc, min_obj_dist, pri, cfn);
        break;
    }
    case bsIrregular: {
        auto ctour = Slic3rMultiPoint_to_ClipperPath(bedhint.get_irregular());
        auto irrbed = sl::create<clppr::Polygon>(std::move(ctour));
        BoundingBox polybb(bedhint.get_irregular());
        
        _arrange(items, fixeditems, irrbed, min_obj_dist, pri, cfn);
        break;
    }
    case bsInfinite: {
        const InfiniteBed& nobin = bedhint.get_infinite();
        auto infbb = Box::infinite({nobin.center.x(), nobin.center.y()});
        
        _arrange(items, fixeditems, infbb, min_obj_dist, pri, cfn);
        break;
    }
    case bsUnknown: {
        // We know nothing about the bed, let it be infinite and zero centered
        _arrange(items, fixeditems, Box::infinite(), min_obj_dist, pri, cfn);
        break;
    }
    };
    
    for(size_t i = 0; i < items.size(); ++i) {
        clppr::IntPoint tr = items[i].translation();
        arrangables[i].translation = {coord_t(tr.X), coord_t(tr.Y)};
        arrangables[i].rotation    = items[i].rotation();
        arrangables[i].bed_idx     = items[i].binId();
    }
}

// Arrange, without the fixed items (excludes)
void arrange(ArrangePolygons &             inp,
            coord_t                       min_d,
            const BedShapeHint &          bedhint,
            std::function<void(unsigned)> prfn,
            std::function<bool()>         stopfn)
{
    arrange(inp, {}, min_d, bedhint, prfn, stopfn);
}

} // namespace arr
} // namespace Slic3r
