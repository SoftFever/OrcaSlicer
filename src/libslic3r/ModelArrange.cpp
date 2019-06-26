#include "ModelArrange.hpp"
#include "Model.hpp"
#include "Geometry.hpp"
#include "SVG.hpp"
#include "MTUtils.hpp"

#include <libnest2d.h>

#include <numeric>
#include <ClipperUtils.hpp>

#include <boost/geometry/index/rtree.hpp>
#include <boost/multiprecision/integer.hpp>
#include <boost/rational.hpp>

namespace libnest2d {
#if !defined(_MSC_VER) && defined(__SIZEOF_INT128__) && !defined(__APPLE__)
using LargeInt = __int128;
#else
using LargeInt = boost::multiprecision::int128_t;
template<> struct _NumTag<LargeInt> { using Type = ScalarTag; };
#endif
template<class T> struct _NumTag<boost::rational<T>> { using Type = RationalTag; };

namespace nfp {

template<class S>
struct NfpImpl<S, NfpLevel::CONVEX_ONLY>
{
    NfpResult<S> operator()(const S &sh, const S &other)
    {
        return nfpConvexOnly<S, boost::rational<LargeInt>>(sh, other);
    }
};

}
}

namespace Slic3r {

namespace arr {

using namespace libnest2d;

// Only for debugging. Prints the model object vertices on stdout.
std::string toString(const Model& model, bool holes = true) {
    std::stringstream  ss;

    ss << "{\n";

    for(auto objptr : model.objects) {
        if(!objptr) continue;

        auto rmesh = objptr->raw_mesh();

        for(auto objinst : objptr->instances) {
            if(!objinst) continue;

            Slic3r::TriangleMesh tmpmesh = rmesh;
            // CHECK_ME -> Is the following correct ?
            tmpmesh.scale(objinst->get_scaling_factor());
            objinst->transform_mesh(&tmpmesh);
            ExPolygons expolys = tmpmesh.horizontal_projection();
            for(auto& expoly_complex : expolys) {
                
                ExPolygons tmp = expoly_complex.simplify(scaled<double>(1.));
                if(tmp.empty()) continue;
                ExPolygon expoly = tmp.front();
                expoly.contour.make_clockwise();
                for(auto& h : expoly.holes) h.make_counter_clockwise();

                ss << "\t{\n";
                ss << "\t\t{\n";

                for(auto v : expoly.contour.points) ss << "\t\t\t{"
                                                    << v(0) << ", "
                                                    << v(1) << "},\n";
                {
                    auto v = expoly.contour.points.front();
                    ss << "\t\t\t{" << v(0) << ", " << v(1) << "},\n";
                }
                ss << "\t\t},\n";

                // Holes:
                ss << "\t\t{\n";
                if(holes) for(auto h : expoly.holes) {
                    ss << "\t\t\t{\n";
                    for(auto v : h.points) ss << "\t\t\t\t{"
                                           << v(0) << ", "
                                           << v(1) << "},\n";
                    {
                        auto v = h.points.front();
                        ss << "\t\t\t\t{" << v(0) << ", " << v(1) << "},\n";
                    }
                    ss << "\t\t\t},\n";
                }
                ss << "\t\t},\n";

                ss << "\t},\n";
            }
        }
    }

    ss << "}\n";

    return ss.str();
}

// Debugging: Save model to svg file.
void toSVG(SVG& svg, const Model& model) {
    for(auto objptr : model.objects) {
        if(!objptr) continue;

        auto rmesh = objptr->raw_mesh();

        for(auto objinst : objptr->instances) {
            if(!objinst) continue;

            Slic3r::TriangleMesh tmpmesh = rmesh;
            tmpmesh.scale(objinst->get_scaling_factor());
            objinst->transform_mesh(&tmpmesh);
            ExPolygons expolys = tmpmesh.horizontal_projection();
            svg.draw(expolys);
        }
    }
}

namespace bgi = boost::geometry::index;

using SpatElement = std::pair<Box, unsigned>;
using SpatIndex = bgi::rtree< SpatElement, bgi::rstar<16, 4> >;
using ItemGroup = std::vector<std::reference_wrapper<Item>>;
template<class TBin>
using TPacker = typename placers::_NofitPolyPlacer<PolygonImpl, TBin>;

const double BIG_ITEM_TRESHOLD = 0.02;

Box boundingBox(const Box& pilebb, const Box& ibb ) {
    auto& pminc = pilebb.minCorner();
    auto& pmaxc = pilebb.maxCorner();
    auto& iminc = ibb.minCorner();
    auto& imaxc = ibb.maxCorner();
    PointImpl minc, maxc;

    setX(minc, std::min(getX(pminc), getX(iminc)));
    setY(minc, std::min(getY(pminc), getY(iminc)));

    setX(maxc, std::max(getX(pmaxc), getX(imaxc)));
    setY(maxc, std::max(getY(pmaxc), getY(imaxc)));
    return Box(minc, maxc);
}

// This is "the" object function which is evaluated many times for each vertex
// (decimated with the accuracy parameter) of each object. Therefore it is
// upmost crucial for this function to be as efficient as it possibly can be but
// at the same time, it has to provide reasonable results.
std::tuple<double /*score*/, Box /*farthest point from bin center*/>
objfunc(const PointImpl& bincenter,
        const TMultiShape<PolygonImpl>& merged_pile,
        const Box& pilebb,
        const ItemGroup& items,
        const Item &item,
        double bin_area,
        double norm,            // A norming factor for physical dimensions
        // a spatial index to quickly get neighbors of the candidate item
        const SpatIndex& spatindex,
        const SpatIndex& smalls_spatindex,
        const ItemGroup& remaining
        )
{
    // We will treat big items (compared to the print bed) differently
    auto isBig = [bin_area](double a) {
        return a/bin_area > BIG_ITEM_TRESHOLD ;
    };

    // Candidate item bounding box
    auto ibb = sl::boundingBox(item.transformedShape());

    // Calculate the full bounding box of the pile with the candidate item
    auto fullbb = boundingBox(pilebb, ibb);

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

    if(isBig(item.area()) || spatindex.empty()) {
        // This branch is for the bigger items..

        auto minc = ibb.minCorner(); // bottom left corner
        auto maxc = ibb.maxCorner(); // top right corner

        // top left and bottom right corners
        auto top_left = PointImpl{getX(minc), getY(maxc)};
        auto bottom_right = PointImpl{getX(maxc), getY(minc)};

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
        auto dist = *(std::min_element(dists.begin(), dists.end())) / norm;
        auto bindist = pl::distance(ibb.center(), bincenter) / norm;
        dist = 0.8*dist + 0.2*bindist;

        // Density is the pack density: how big is the arranged pile
        double density = 0;

        if(remaining.empty()) {

            auto mp = merged_pile;
            mp.emplace_back(item.transformedShape());
            auto chull = sl::convexHull(mp);

            placers::EdgeCache<PolygonImpl> ec(chull);

            double circ = ec.circumference() / norm;
            double bcirc = 2.0*(fullbb.width() + fullbb.height()) / norm;
            score = 0.5*circ + 0.5*bcirc;

        } else {
            // Prepare a variable for the alignment score.
            // This will indicate: how well is the candidate item aligned with
            // its neighbors. We will check the alignment with all neighbors and
            // return the score for the best alignment. So it is enough for the
            // candidate to be aligned with only one item.
            auto alignment_score = 1.0;

            density = std::sqrt((fullbb.width() / norm )*
                                (fullbb.height() / norm));
            auto querybb = item.boundingBox();

            // Query the spatial index for the neighbors
            std::vector<SpatElement> result;
            result.reserve(spatindex.size());
            if(isBig(item.area())) {
                spatindex.query(bgi::intersects(querybb),
                                std::back_inserter(result));
            } else {
                smalls_spatindex.query(bgi::intersects(querybb),
                                       std::back_inserter(result));
            }

            for(auto& e : result) { // now get the score for the best alignment
                auto idx = e.second;
                Item& p = items[idx];
                auto parea = p.area();
                if(std::abs(1.0 - parea/item.area()) < 1e-6) {
                    auto bb = boundingBox(p.boundingBox(), ibb);
                    auto bbarea = bb.area();
                    auto ascore = 1.0 - (item.area() + parea)/bbarea;

                    if(ascore < alignment_score) alignment_score = ascore;
                }
            }

            // The final mix of the score is the balance between the distance
            // from the full pile center, the pack density and the
            // alignment with the neighbors
            if(result.empty())
                score = 0.5 * dist + 0.5 * density;
            else
                score = 0.40 * dist + 0.40 * density + 0.2 * alignment_score;
        }
    } else {
        // Here there are the small items that should be placed around the
        // already processed bigger items.
        // No need to play around with the anchor points, the center will be
        // just fine for small items
        score = pl::distance(ibb.center(), bigbb.center()) / norm;
    }

    return std::make_tuple(score, fullbb);
}

// Fill in the placer algorithm configuration with values carefully chosen for
// Slic3r.
template<class PConf>
void fillConfig(PConf& pcfg) {

    // Align the arranged pile into the center of the bin
    pcfg.alignment = PConf::Alignment::CENTER;

    // Start placing the items from the center of the print bed
    pcfg.starting_point = PConf::Alignment::CENTER;

    // TODO cannot use rotations until multiple objects of same geometry can
    // handle different rotations
    // arranger.useMinimumBoundigBoxRotation();
    pcfg.rotations = { 0.0 };

    // The accuracy of optimization.
    // Goes from 0.0 to 1.0 and scales performance as well
    pcfg.accuracy = 0.65f;

    pcfg.parallel = true;
}

// Type trait for an arranger class for different bin types (box, circle,
// polygon, etc...)
template<class TBin>
class AutoArranger {};


// A class encapsulating the libnest2d Nester class and extending it with other
// management and spatial index structures for acceleration.
template<class TBin>
class _ArrBase {
public:

    // Useful type shortcuts...
    using Placer = TPacker<TBin>;
    using Selector = FirstFitSelection;
    using Packer = Nester<Placer, Selector>;
    using PConfig = typename Packer::PlacementConfig;
    using Distance = TCoord<PointImpl>;
    using Pile = TMultiShape<PolygonImpl>;
    
protected:

    Packer m_pck;
    PConfig m_pconf;            // Placement configuration
    double m_bin_area;
    SpatIndex m_rtree;          // spatial index for the normal (bigger) objects
    SpatIndex m_smallsrtree;    // spatial index for only the smaller items
    double m_norm;              // A coefficient to scale distances
    Pile m_merged_pile;         // The already merged pile (vector of items)
    Box m_pilebb;               // The bounding box of the merged pile.
    ItemGroup m_remaining;      // Remaining items (m_items at the beginning)
    ItemGroup m_items;          // The items to be packed
public:

    _ArrBase(const TBin& bin, Distance dist,
             std::function<void(unsigned)> progressind,
             std::function<bool(void)> stopcond):
       m_pck(bin, dist), m_bin_area(sl::area(bin)),
       m_norm(std::sqrt(sl::area(bin)))
    {
        fillConfig(m_pconf);

        // Set up a callback that is called just before arranging starts
        // This functionality is provided by the Nester class (m_pack).
        m_pconf.before_packing =
        [this](const Pile& merged_pile,            // merged pile
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
                return a/m_bin_area > BIG_ITEM_TRESHOLD ;
            };

            for(unsigned idx = 0; idx < items.size(); ++idx) {
                Item& itm = items[idx];
                if(isBig(itm.area())) m_rtree.insert({itm.boundingBox(), idx});
                m_smallsrtree.insert({itm.boundingBox(), idx});
            }
        };

        m_pck.progressIndicator(progressind);
        m_pck.stopCondition(stopcond);
    }

    template<class...Args> inline IndexedPackGroup operator()(Args&&...args) {
        m_rtree.clear();
        return m_pck.executeIndexed(std::forward<Args>(args)...);
    }

    inline void preload(const PackGroup& pg) {
        m_pconf.alignment = PConfig::Alignment::DONT_ALIGN;
        m_pconf.object_function = nullptr; // drop the special objectfunction
        m_pck.preload(pg);

        // Build the rtree for queries to work
        for(const ItemGroup& grp : pg)
        for(unsigned idx = 0; idx < grp.size(); ++idx) {
            Item& itm = grp[idx];
            m_rtree.insert({itm.boundingBox(), idx});
        }

        m_pck.configure(m_pconf);
    }

    bool is_colliding(const Item& item) {
        if(m_rtree.empty()) return false;
        std::vector<SpatElement> result;
        m_rtree.query(bgi::intersects(item.boundingBox()),
                      std::back_inserter(result));
        return !result.empty();
    }
};

// Arranger specialization for a Box shaped bin.
template<> class AutoArranger<Box>: public _ArrBase<Box> {
public:

    AutoArranger(const Box& bin, Distance dist,
                 std::function<void(unsigned)> progressind = [](unsigned){},
                 std::function<bool(void)> stopcond = [](){return false;}):
        _ArrBase<Box>(bin, dist, progressind, stopcond)
    {

        // Here we set up the actual object function that calls the common
        // object function for all bin shapes than does an additional inside
        // check for the arranged pile.
        m_pconf.object_function = [this, bin] (const Item &item) {

            auto result = objfunc(bin.center(),
                                  m_merged_pile,
                                  m_pilebb,
                                  m_items,
                                  item,
                                  m_bin_area,
                                  m_norm,
                                  m_rtree,
                                  m_smallsrtree,
                                  m_remaining);

            double score = std::get<0>(result);
            auto& fullbb = std::get<1>(result);

            double miss = Placer::overfit(fullbb, bin);
            miss = miss > 0? miss : 0;
            score += miss*miss;

            return score;
        };

        m_pck.configure(m_pconf);
    }
};

using lnCircle = libnest2d::_Circle<libnest2d::PointImpl>;

inline lnCircle to_lnCircle(const Circle& circ) {
    return lnCircle({circ.center()(0), circ.center()(1)}, circ.radius());
}

// Arranger specialization for circle shaped bin.
template<> class AutoArranger<lnCircle>: public _ArrBase<lnCircle> {
public:

    AutoArranger(const lnCircle& bin, Distance dist,
                 std::function<void(unsigned)> progressind = [](unsigned){},
                 std::function<bool(void)> stopcond = [](){return false;}):
        _ArrBase<lnCircle>(bin, dist, progressind, stopcond) {

        // As with the box, only the inside check is different.
        m_pconf.object_function = [this, &bin] (const Item &item) {

            auto result = objfunc(bin.center(),
                                  m_merged_pile,
                                  m_pilebb,
                                  m_items,
                                  item,
                                  m_bin_area,
                                  m_norm,
                                  m_rtree,
                                  m_smallsrtree,
                                  m_remaining);

            double score = std::get<0>(result);

            auto isBig = [this](const Item& itm) {
                return itm.area()/m_bin_area > BIG_ITEM_TRESHOLD ;
            };

            if(isBig(item)) {
                auto mp = m_merged_pile;
                mp.push_back(item.transformedShape());
                auto chull = sl::convexHull(mp);
                double miss = Placer::overfit(chull, bin);
                if(miss < 0) miss = 0;
                score += miss*miss;
            }

            return score;
        };

        m_pck.configure(m_pconf);
    }
};

// Arranger specialization for a generalized polygon.
// Warning: this is unfinished business. It may or may not work.
template<> class AutoArranger<PolygonImpl>: public _ArrBase<PolygonImpl> {
public:
    AutoArranger(const PolygonImpl& bin, Distance dist,
                 std::function<void(unsigned)> progressind = [](unsigned){},
                 std::function<bool(void)> stopcond = [](){return false;}):
        _ArrBase<PolygonImpl>(bin, dist, progressind, stopcond)
    {
        m_pconf.object_function = [this, &bin] (const Item &item) {

            auto binbb = sl::boundingBox(bin);
            auto result = objfunc(binbb.center(),
                                  m_merged_pile,
                                  m_pilebb,
                                  m_items,
                                  item,
                                  m_bin_area,
                                  m_norm,
                                  m_rtree,
                                  m_smallsrtree,
                                  m_remaining);
            double score = std::get<0>(result);

            return score;
        };

        m_pck.configure(m_pconf);
    }
};

// Specialization with no bin. In this case the arranger should just arrange
// all objects into a minimum sized pile but it is not limited by a bin. A
// consequence is that only one pile should be created.
template<> class AutoArranger<bool>: public _ArrBase<Box> {
public:

    AutoArranger(Distance dist, std::function<void(unsigned)> progressind,
                 std::function<bool(void)> stopcond):
        _ArrBase<Box>(Box(0, 0), dist, progressind, stopcond)
    {
        this->m_pconf.object_function = [this] (const Item &item) {

            auto result = objfunc({0, 0},
                                  m_merged_pile,
                                  m_pilebb,
                                  m_items,
                                  item,
                                  0,
                                  m_norm,
                                  m_rtree,
                                  m_smallsrtree,
                                  m_remaining);
            return std::get<0>(result);
        };

        this->m_pck.configure(m_pconf);
    }
};

// A container which stores a pointer to the 3D object and its projected
// 2D shape from top view.
using ShapeData2D = std::vector<std::pair<Slic3r::ModelInstance*, Item>>;

ShapeData2D projectModelFromTop(const Slic3r::Model &model,
                                const WipeTowerInfo &wti,
                                double               tolerance)
{
    ShapeData2D ret;

    // Count all the items on the bin (all the object's instances)
    auto s = std::accumulate(model.objects.begin(), model.objects.end(),
                             size_t(0), [](size_t s, ModelObject* o)
    {
        return s + o->instances.size();
    });

    ret.reserve(s);

    for(ModelObject* objptr : model.objects) {
        if (! objptr->instances.empty()) {

            // TODO export the exact 2D projection. Cannot do it as libnest2d
            // does not support concave shapes (yet).
            ClipperLib::Path clpath;

            // Object instances should carry the same scaling and
            // x, y rotation that is why we use the first instance.
            {
                ModelInstance *finst       = objptr->instances.front();
                Vec3d          rotation    = finst->get_rotation();
                rotation.z()               = 0.;
                Transform3d trafo_instance = Geometry::assemble_transform(
                    Vec3d::Zero(),
                    rotation,
                    finst->get_scaling_factor(),
                    finst->get_mirror());
                Polygon p = objptr->convex_hull_2d(trafo_instance);
                
                assert(!p.points.empty());

                // this may happen for malformed models, see:
                // https://github.com/prusa3d/PrusaSlicer/issues/2209
                if (p.points.empty()) continue;
                
                if(tolerance > EPSILON) {
                    Polygons pp { p };
                    pp = p.simplify(scaled<double>(tolerance));
                    if (!pp.empty()) p = pp.front();
                }
                
                p.reverse();
                assert(!p.is_counter_clockwise());
                clpath = Slic3rMultiPoint_to_ClipperPath(p);
                auto firstp = clpath.front(); clpath.emplace_back(firstp);
            }

            Vec3d rotation0 = objptr->instances.front()->get_rotation();
            rotation0(2) = 0.;
            for(ModelInstance* objinst : objptr->instances) {
                ClipperLib::Polygon pn;
                pn.Contour = clpath;

                // Efficient conversion to item.
                Item item(std::move(pn));

                // Invalid geometries would throw exceptions when arranging
                if(item.vertexCount() > 3) {
                    item.rotation(Geometry::rotation_diff_z(rotation0, objinst->get_rotation()));
                    item.translation({
                        scaled<ClipperLib::cInt>(objinst->get_offset(X)),
                        scaled<ClipperLib::cInt>(objinst->get_offset(Y))
                    });
                    ret.emplace_back(objinst, item);
                }
            }
        }
    }

    // The wipe tower is a separate case (in case there is one), let's duplicate the code
    if (wti.is_wipe_tower) {
        Points pts;
        pts.emplace_back(coord_t(scale_(0.)), coord_t(scale_(0.)));
        pts.emplace_back(coord_t(scale_(wti.bb_size(0))), coord_t(scale_(0.)));
        pts.emplace_back(coord_t(scale_(wti.bb_size(0))), coord_t(scale_(wti.bb_size(1))));
        pts.emplace_back(coord_t(scale_(-0.)), coord_t(scale_(wti.bb_size(1))));
        pts.emplace_back(coord_t(scale_(-0.)), coord_t(scale_(0.)));
        Polygon p(std::move(pts));
        ClipperLib::Path clpath = Slic3rMultiPoint_to_ClipperPath(p);
        ClipperLib::Polygon pn;
        pn.Contour = clpath;
        // Efficient conversion to item.
        Item item(std::move(pn));
        item.rotation(wti.rotation),
        item.translation({
            scaled<ClipperLib::cInt>(wti.pos(0)),
            scaled<ClipperLib::cInt>(wti.pos(1))
        });
        ret.emplace_back(nullptr, item);
    }

    return ret;
}

// Apply the calculated translations and rotations (currently disabled) to the
// Model object instances.
void applyResult(
        IndexedPackGroup::value_type& group,
        Coord batch_offset,
        ShapeData2D& shapemap,
        WipeTowerInfo& wti)
{
    for(auto& r : group) {
        auto idx = r.first;     // get the original item index
        Item& item = r.second;  // get the item itself

        // Get the model instance from the shapemap using the index
        ModelInstance *inst_ptr = shapemap[idx].first;

            // Get the transformation data from the item object and scale it
            // appropriately
            auto off = item.translation();
            Radians rot = item.rotation();

            Vec3d foff(off.X*SCALING_FACTOR + batch_offset,
                       off.Y*SCALING_FACTOR,
                       inst_ptr ? inst_ptr->get_offset()(Z) : 0.);

        if (inst_ptr) {
            // write the transformation data into the model instance
            inst_ptr->set_rotation(Z, rot);
            inst_ptr->set_offset(foff);
        }
        else { // this is the wipe tower - we will modify the struct with the info
               // and leave it up to the called to actually move the wipe tower
            wti.pos = Vec2d(foff(0), foff(1));
            wti.rotation = rot;
        }
    }
}

// Get the type of bed geometry from a simple vector of points.
BedShapeHint bedShape(const Polyline &bed) {
    BedShapeHint ret;

    auto x = [](const Point& p) { return p(0); };
    auto y = [](const Point& p) { return p(1); };

    auto width = [x](const BoundingBox& box) {
        return x(box.max) - x(box.min);
    };

    auto height = [y](const BoundingBox& box) {
        return y(box.max) - y(box.min);
    };

    auto area = [&width, &height](const BoundingBox& box) {
        double w = width(box);
        double h = height(box);
        return w*h;
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

        Circle ret(center, avg_dist);
        for(auto el : vertex_distances)
        {
            if (std::abs(el - avg_dist) > 10 * SCALED_EPSILON) {
                ret = Circle();
                break;
            }
        }

        return ret;
    };

    auto parea = poly_area(bed);

    if( (1.0 - parea/area(bb)) < 1e-3 ) {
        ret.type = BedShapeType::BOX;
        ret.shape.box = bb;
    }
    else if(auto c = isCircle(bed)) {
        ret.type = BedShapeType::CIRCLE;
        ret.shape.circ = c;
    } else {
        ret.type = BedShapeType::IRREGULAR;
        ret.shape.polygon = bed;
    }

    // Determine the bed shape by hand
    return ret;
}

static const SLIC3R_CONSTEXPR double SIMPLIFY_TOLERANCE_MM = 0.1;

// The final client function to arrange the Model. A progress indicator and
// a stop predicate can be also be passed to control the process.
bool arrange(Model &model,              // The model with the geometries
             WipeTowerInfo& wti,        // Wipe tower info
             coord_t min_obj_distance,  // Has to be in scaled (clipper) measure
             const Polyline &bed,       // The bed geometry.
             BedShapeHint bedhint,      // Hint about the bed geometry type.
             bool first_bin_only,       // What to do is not all items fit.

             // Controlling callbacks.
             std::function<void (unsigned)> progressind,
             std::function<bool ()> stopcondition)
{
    bool ret = true;
    
    // Get the 2D projected shapes with their 3D model instance pointers
    auto shapemap = arr::projectModelFromTop(model, wti, SIMPLIFY_TOLERANCE_MM);

    // Copy the references for the shapes only as the arranger expects a
    // sequence of objects convertible to Item or ClipperPolygon
    std::vector<std::reference_wrapper<Item>> shapes;
    shapes.reserve(shapemap.size());
    std::for_each(shapemap.begin(), shapemap.end(),
                  [&shapes] (ShapeData2D::value_type& it)
    {
        shapes.push_back(std::ref(it.second));
    });

    IndexedPackGroup result;

    // If there is no hint about the shape, we will try to guess
    if(bedhint.type == BedShapeType::WHO_KNOWS) bedhint = bedShape(bed);

    BoundingBox bbb(bed);

    auto& cfn = stopcondition;
    
    // Integer ceiling the min distance from the bed perimeters
    coord_t md = min_obj_distance - SCALED_EPSILON;
    md = (md % 2) ? md / 2 + 1 : md / 2;

    auto binbb = Box({libnest2d::Coord{bbb.min(0)} - md,
                      libnest2d::Coord{bbb.min(1)} - md},
                     {libnest2d::Coord{bbb.max(0)} + md,
                      libnest2d::Coord{bbb.max(1)} + md});

    switch(bedhint.type) {
    case BedShapeType::BOX: {

        // Create the arranger for the box shaped bed
        AutoArranger<Box> arrange(binbb, min_obj_distance, progressind, cfn);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::CIRCLE: {

        auto c = bedhint.shape.circ;
        auto cc = to_lnCircle(c);

        AutoArranger<lnCircle> arrange(cc, min_obj_distance, progressind, cfn);
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::IRREGULAR:
    case BedShapeType::WHO_KNOWS: {

        using P = libnest2d::PolygonImpl;

        auto ctour = Slic3rMultiPoint_to_ClipperPath(bed);
        P irrbed = sl::create<PolygonImpl>(std::move(ctour));

        AutoArranger<P> arrange(irrbed, min_obj_distance, progressind, cfn);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    };

    if(result.empty() || stopcondition()) return false;

    if(first_bin_only) {
        applyResult(result.front(), 0, shapemap, wti);
    } else {

        const auto STRIDE_PADDING = 1.2;

        Coord stride = static_cast<Coord>(STRIDE_PADDING*
                                          binbb.width()*SCALING_FACTOR);
        Coord batch_offset = 0;

        for(auto& group : result) {
            applyResult(group, batch_offset, shapemap, wti);

            // Only the first pack group can be placed onto the print bed. The
            // other objects which could not fit will be placed next to the
            // print bed
            batch_offset += stride;
        }
    }

    for(auto objptr : model.objects) objptr->invalidate_bounding_box();

    return ret && result.size() == 1;
}

void find_new_position(const Model &model,
                       ModelInstancePtrs toadd,
                       coord_t min_obj_distance,
                       const Polyline &bed,
                       WipeTowerInfo& wti)
{    
    // Get the 2D projected shapes with their 3D model instance pointers
    auto shapemap = arr::projectModelFromTop(model, wti, SIMPLIFY_TOLERANCE_MM);

    // Copy the references for the shapes only as the arranger expects a
    // sequence of objects convertible to Item or ClipperPolygon
    PackGroup preshapes; preshapes.emplace_back();
    ItemGroup shapes;
    preshapes.front().reserve(shapemap.size());

    std::vector<ModelInstance*> shapes_ptr; shapes_ptr.reserve(toadd.size());
    IndexedPackGroup result;

    // If there is no hint about the shape, we will try to guess
    BedShapeHint bedhint = bedShape(bed);

    BoundingBox bbb(bed);
    
    // Integer ceiling the min distance from the bed perimeters
    coord_t md = min_obj_distance - SCALED_EPSILON;
    md = (md % 2) ? md / 2 + 1 : md / 2;
    
    auto binbb = Box({libnest2d::Coord{bbb.min(0)} - md,
                      libnest2d::Coord{bbb.min(1)} - md},
                     {libnest2d::Coord{bbb.max(0)} + md,
                      libnest2d::Coord{bbb.max(1)} + md});

    for(auto it = shapemap.begin(); it != shapemap.end(); ++it) {
        if(std::find(toadd.begin(), toadd.end(), it->first) == toadd.end()) {
           if(it->second.isInside(binbb)) // just ignore items which are outside
               preshapes.front().emplace_back(std::ref(it->second));
        }
        else {
            shapes_ptr.emplace_back(it->first);
            shapes.emplace_back(std::ref(it->second));
        }
    }

    auto try_first_to_center = [&shapes, &shapes_ptr, &binbb]
            (std::function<bool(const Item&)> is_colliding,
             std::function<void(Item&)> preload)
    {
        // Try to put the first item to the center, as the arranger will not
        // do this for us.
        auto shptrit = shapes_ptr.begin();
        for(auto shit = shapes.begin(); shit != shapes.end(); ++shit, ++shptrit)
        {
            // Try to place items to the center
            Item& itm = *shit;
            auto ibb = itm.boundingBox();
            auto d = binbb.center() - ibb.center();
            itm.translate(d);
            if(!is_colliding(itm)) {
                preload(itm);

                auto offset = itm.translation();
                Radians rot = itm.rotation();
                ModelInstance *minst = *shptrit;
                Vec3d foffset(offset.X*SCALING_FACTOR,
                              offset.Y*SCALING_FACTOR,
                              minst->get_offset()(Z));

                // write the transformation data into the model instance
                minst->set_rotation(Z, rot);
                minst->set_offset(foffset);

                shit = shapes.erase(shit);
                shptrit = shapes_ptr.erase(shptrit);
                break;
            }
        }
    };

    switch(bedhint.type) {
    case BedShapeType::BOX: {

        // Create the arranger for the box shaped bed
        AutoArranger<Box> arrange(binbb, min_obj_distance);

        if(!preshapes.front().empty()) { // If there is something on the plate
            arrange.preload(preshapes);
            try_first_to_center(
                [&arrange](const Item& itm) {return arrange.is_colliding(itm);},
                [&arrange](Item& itm) { arrange.preload({{itm}}); }
            );
        }

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::CIRCLE: {

        auto c = bedhint.shape.circ;
        auto cc = to_lnCircle(c);

        // Create the arranger for the box shaped bed
        AutoArranger<lnCircle> arrange(cc, min_obj_distance);

        if(!preshapes.front().empty()) { // If there is something on the plate
            arrange.preload(preshapes);
            try_first_to_center(
                [&arrange](const Item& itm) {return arrange.is_colliding(itm);},
                [&arrange](Item& itm) { arrange.preload({{itm}}); }
            );
        }

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::IRREGULAR:
    case BedShapeType::WHO_KNOWS: {
        using P = libnest2d::PolygonImpl;

        auto ctour = Slic3rMultiPoint_to_ClipperPath(bed);
        P irrbed = sl::create<PolygonImpl>(std::move(ctour));

        AutoArranger<P> arrange(irrbed, min_obj_distance);

        if(!preshapes.front().empty()) { // If there is something on the plate
            arrange.preload(preshapes);
            try_first_to_center(
                [&arrange](const Item& itm) {return arrange.is_colliding(itm);},
                [&arrange](Item& itm) { arrange.preload({{itm}}); }
            );
        }

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    };

    // Now we go through the result which will contain the fixed and the moving
    // polygons as well. We will have to search for our item.

    const auto STRIDE_PADDING = 1.2;
    Coord stride = Coord(STRIDE_PADDING*binbb.width()*SCALING_FACTOR);
    Coord batch_offset = 0;

    for(auto& group : result) {
        for(auto& r : group) if(r.first < shapes.size()) {
            Item& resultitem = r.second;
            unsigned idx = r.first;
            auto offset = resultitem.translation();
            Radians rot = resultitem.rotation();
            ModelInstance *minst = shapes_ptr[idx];
            Vec3d foffset(offset.X*SCALING_FACTOR + batch_offset,
                          offset.Y*SCALING_FACTOR,
                          minst->get_offset()(Z));

            // write the transformation data into the model instance
            minst->set_rotation(Z, rot);
            minst->set_offset(foffset);
        }
        batch_offset += stride;
    }
}

}


}
