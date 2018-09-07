#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include "Model.hpp"
#include "SVG.hpp"
#include <libnest2d.h>

#include <numeric>
#include <ClipperUtils.hpp>

#include <boost/geometry/index/rtree.hpp>

namespace Slic3r {
namespace arr {

using namespace libnest2d;

std::string toString(const Model& model, bool holes = true) {
    std::stringstream  ss;

    ss << "{\n";

    for(auto objptr : model.objects) {
        if(!objptr) continue;

        auto rmesh = objptr->raw_mesh();

        for(auto objinst : objptr->instances) {
            if(!objinst) continue;

            Slic3r::TriangleMesh tmpmesh = rmesh;
            tmpmesh.scale(objinst->scaling_factor);
            objinst->transform_mesh(&tmpmesh);
            ExPolygons expolys = tmpmesh.horizontal_projection();
            for(auto& expoly_complex : expolys) {

                auto tmp = expoly_complex.simplify(1.0/SCALING_FACTOR);
                if(tmp.empty()) continue;
                auto expoly = tmp.front();
                expoly.contour.make_clockwise();
                for(auto& h : expoly.holes) h.make_counter_clockwise();

                ss << "\t{\n";
                ss << "\t\t{\n";

                for(auto v : expoly.contour.points) ss << "\t\t\t{"
                                                    << v.x << ", "
                                                    << v.y << "},\n";
                {
                    auto v = expoly.contour.points.front();
                    ss << "\t\t\t{" << v.x << ", " << v.y << "},\n";
                }
                ss << "\t\t},\n";

                // Holes:
                ss << "\t\t{\n";
                if(holes) for(auto h : expoly.holes) {
                    ss << "\t\t\t{\n";
                    for(auto v : h.points) ss << "\t\t\t\t{"
                                           << v.x << ", "
                                           << v.y << "},\n";
                    {
                        auto v = h.points.front();
                        ss << "\t\t\t\t{" << v.x << ", " << v.y << "},\n";
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

void toSVG(SVG& svg, const Model& model) {
    for(auto objptr : model.objects) {
        if(!objptr) continue;

        auto rmesh = objptr->raw_mesh();

        for(auto objinst : objptr->instances) {
            if(!objinst) continue;

            Slic3r::TriangleMesh tmpmesh = rmesh;
            tmpmesh.scale(objinst->scaling_factor);
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

std::tuple<double /*score*/, Box /*farthest point from bin center*/>
objfunc(const PointImpl& bincenter,
        const shapelike::Shapes<PolygonImpl>& merged_pile,
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
    using Coord = TCoord<PointImpl>;

    static const double ROUNDNESS_RATIO = 0.5;
    static const double DENSITY_RATIO = 1.0 - ROUNDNESS_RATIO;

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

template<class TBin>
class AutoArranger {};

template<class TBin>
class _ArrBase {
protected:

    using Placer = TPacker<TBin>;
    using Selector = FirstFitSelection;
    using Packer = Nester<Placer, Selector>;
    using PConfig = typename Packer::PlacementConfig;
    using Distance = TCoord<PointImpl>;
    using Pile = sl::Shapes<PolygonImpl>;

    Packer pck_;
    PConfig pconf_; // Placement configuration
    double bin_area_;
    SpatIndex rtree_;
    SpatIndex smallsrtree_;
    double norm_;
    Pile merged_pile_;
    Box pilebb_;
    ItemGroup remaining_;
    ItemGroup items_;
public:

    _ArrBase(const TBin& bin, Distance dist,
             std::function<void(unsigned)> progressind):
       pck_(bin, dist), bin_area_(sl::area(bin)),
       norm_(std::sqrt(sl::area(bin)))
    {
        fillConfig(pconf_);

        pconf_.before_packing =
        [this](const Pile& merged_pile,            // merged pile
               const ItemGroup& items,             // packed items
               const ItemGroup& remaining)         // future items to be packed
        {
            items_ = items;
            merged_pile_ = merged_pile;
            remaining_ = remaining;

            pilebb_ = sl::boundingBox(merged_pile);

            rtree_.clear();
            smallsrtree_.clear();

            // We will treat big items (compared to the print bed) differently
            auto isBig = [this](double a) {
                return a/bin_area_ > BIG_ITEM_TRESHOLD ;
            };

            for(unsigned idx = 0; idx < items.size(); ++idx) {
                Item& itm = items[idx];
                if(isBig(itm.area())) rtree_.insert({itm.boundingBox(), idx});
                smallsrtree_.insert({itm.boundingBox(), idx});
            }
        };

        pck_.progressIndicator(progressind);
    }

    template<class...Args> inline IndexedPackGroup operator()(Args&&...args) {
        rtree_.clear();
        return pck_.executeIndexed(std::forward<Args>(args)...);
    }
};

template<>
class AutoArranger<Box>: public _ArrBase<Box> {
public:

    AutoArranger(const Box& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<Box>(bin, dist, progressind)
    {

        pconf_.object_function = [this, bin] (const Item &item) {

            auto result = objfunc(bin.center(),
                                  merged_pile_,
                                  pilebb_,
                                  items_,
                                  item,
                                  bin_area_,
                                  norm_,
                                  rtree_,
                                  smallsrtree_,
                                  remaining_);

            double score = std::get<0>(result);
            auto& fullbb = std::get<1>(result);

            double miss = Placer::overfit(fullbb, bin);
            miss = miss > 0? miss : 0;
            score += miss*miss;

            return score;
        };

        pck_.configure(pconf_);
    }
};

using lnCircle = libnest2d::_Circle<libnest2d::PointImpl>;

template<>
class AutoArranger<lnCircle>: public _ArrBase<lnCircle> {
public:

    AutoArranger(const lnCircle& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<lnCircle>(bin, dist, progressind) {

        pconf_.object_function = [this, &bin] (const Item &item) {

            auto result = objfunc(bin.center(),
                                  merged_pile_,
                                  pilebb_,
                                  items_,
                                  item,
                                  bin_area_,
                                  norm_,
                                  rtree_,
                                  smallsrtree_,
                                  remaining_);

            double score = std::get<0>(result);

            auto isBig = [this](const Item& itm) {
                return itm.area()/bin_area_ > BIG_ITEM_TRESHOLD ;
            };

            if(isBig(item)) {
                auto mp = merged_pile_;
                mp.push_back(item.transformedShape());
                auto chull = sl::convexHull(mp);
                double miss = Placer::overfit(chull, bin);
                if(miss < 0) miss = 0;
                score += miss*miss;
            }

            return score;
        };

        pck_.configure(pconf_);
    }
};

template<>
class AutoArranger<PolygonImpl>: public _ArrBase<PolygonImpl> {
public:
    AutoArranger(const PolygonImpl& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<PolygonImpl>(bin, dist, progressind)
    {
        pconf_.object_function = [this, &bin] (const Item &item) {

            auto binbb = sl::boundingBox(bin);
            auto result = objfunc(binbb.center(),
                                  merged_pile_,
                                  pilebb_,
                                  items_,
                                  item,
                                  bin_area_,
                                  norm_,
                                  rtree_,
                                  smallsrtree_,
                                  remaining_);
            double score = std::get<0>(result);

            return score;
        };

        pck_.configure(pconf_);
    }
};

template<> // Specialization with no bin
class AutoArranger<bool>: public _ArrBase<Box> {
public:

    AutoArranger(Distance dist, std::function<void(unsigned)> progressind):
        _ArrBase<Box>(Box(0, 0), dist, progressind)
    {
        this->pconf_.object_function = [this] (const Item &item) {

            auto result = objfunc({0, 0},
                                  merged_pile_,
                                  pilebb_,
                                  items_,
                                  item,
                                  0,
                                  norm_,
                                  rtree_,
                                  smallsrtree_,
                                  remaining_);
            return std::get<0>(result);
        };

        this->pck_.configure(pconf_);
    }
};

// A container which stores a pointer to the 3D object and its projected
// 2D shape from top view.
using ShapeData2D =
    std::vector<std::pair<Slic3r::ModelInstance*, Item>>;

ShapeData2D projectModelFromTop(const Slic3r::Model &model) {
    ShapeData2D ret;

    auto s = std::accumulate(model.objects.begin(), model.objects.end(), 0,
                    [](size_t s, ModelObject* o){
        return s + o->instances.size();
    });

    ret.reserve(s);

    for(auto objptr : model.objects) {
        if(objptr) {

            auto rmesh = objptr->raw_mesh();

            for(auto objinst : objptr->instances) {
                if(objinst) {
                    Slic3r::TriangleMesh tmpmesh = rmesh;
                    ClipperLib::PolygonImpl pn;

                    tmpmesh.scale(objinst->scaling_factor);

                    // TODO export the exact 2D projection
                    auto p = tmpmesh.convex_hull();

                    p.make_clockwise();
                    p.append(p.first_point());
                    pn.Contour = Slic3rMultiPoint_to_ClipperPath( p );

                    // Efficient conversion to item.
                    Item item(std::move(pn));

                    // Invalid geometries would throw exceptions when arranging
                    if(item.vertexCount() > 3) {
                        item.rotation(objinst->rotation);
                        item.translation( {
                            ClipperLib::cInt(objinst->offset.x/SCALING_FACTOR),
                            ClipperLib::cInt(objinst->offset.y/SCALING_FACTOR)
                        });
                        ret.emplace_back(objinst, item);
                    }
                }
            }
        }
    }

    return ret;
}

class Circle {
    Point center_;
    double radius_;
public:

    inline Circle(): center_(0, 0), radius_(std::nan("")) {}
    inline Circle(const Point& c, double r): center_(c), radius_(r) {}

    inline double radius() const { return radius_; }
    inline const Point& center() const { return center_; }
    inline operator bool() { return !std::isnan(radius_); }
};

enum class BedShapeType {
    BOX,
    CIRCLE,
    IRREGULAR,
    WHO_KNOWS
};

struct BedShapeHint {
    BedShapeType type;
    /*union*/ struct {  // I know but who cares...
        Circle circ;
        BoundingBox box;
        Polyline polygon;
    } shape;
};

BedShapeHint bedShape(const Polyline& bed) {
    static const double E = 10/SCALING_FACTOR;

    BedShapeHint ret;

    auto width = [](const BoundingBox& box) {
        return box.max.x - box.min.x;
    };

    auto height = [](const BoundingBox& box) {
        return box.max.y - box.min.y;
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


    auto bb = bed.bounding_box();

    auto isCircle = [bb](const Polyline& polygon) {
        auto center = bb.center();
        std::vector<double> vertex_distances;
        double avg_dist = 0;
        for (auto pt: polygon.points)
        {
            double distance = center.distance_to(pt);
            vertex_distances.push_back(distance);
            avg_dist += distance;
        }

        avg_dist /= vertex_distances.size();

        Circle ret(center, avg_dist);
        for (auto el: vertex_distances)
        {
            if (abs(el - avg_dist) > 10 * SCALED_EPSILON)
                ret = Circle();
            break;
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

void applyResult(
        IndexedPackGroup::value_type& group,
        Coord batch_offset,
        ShapeData2D& shapemap)
{
    for(auto& r : group) {
        auto idx = r.first;     // get the original item index
        Item& item = r.second;  // get the item itself

        // Get the model instance from the shapemap using the index
        ModelInstance *inst_ptr = shapemap[idx].first;

        // Get the tranformation data from the item object and scale it
        // appropriately
        auto off = item.translation();
        Radians rot = item.rotation();
        Pointf foff(off.X*SCALING_FACTOR + batch_offset,
                    off.Y*SCALING_FACTOR);

        // write the tranformation data into the model instance
        inst_ptr->rotation = rot;
        inst_ptr->offset = foff;
    }
}


/**
 * \brief Arranges the model objects on the screen.
 *
 * The arrangement considers multiple bins (aka. print beds) for placing all
 * the items provided in the model argument. If the items don't fit on one
 * print bed, the remaining will be placed onto newly created print beds.
 * The first_bin_only parameter, if set to true, disables this behaviour and
 * makes sure that only one print bed is filled and the remaining items will be
 * untouched. When set to false, the items which could not fit onto the
 * print bed will be placed next to the print bed so the user should see a
 * pile of items on the print bed and some other piles outside the print
 * area that can be dragged later onto the print bed as a group.
 *
 * \param model The model object with the 3D content.
 * \param dist The minimum distance which is allowed for any pair of items
 * on the print bed  in any direction.
 * \param bb The bounding box of the print bed. It corresponds to the 'bin'
 * for bin packing.
 * \param first_bin_only This parameter controls whether to place the
 * remaining items which do not fit onto the print area next to the print
 * bed or leave them untouched (let the user arrange them by hand or remove
 * them).
 */
bool arrange(Model &model, coordf_t min_obj_distance,
             const Slic3r::Polyline& bed,
             BedShapeHint bedhint,
             bool first_bin_only,
             std::function<void(unsigned)> progressind)
{
    using ArrangeResult = _IndexedPackGroup<PolygonImpl>;

    bool ret = true;

    // Get the 2D projected shapes with their 3D model instance pointers
    auto shapemap = arr::projectModelFromTop(model);

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

    if(bedhint.type == BedShapeType::WHO_KNOWS) bedhint = bedShape(bed);

    BoundingBox bbb(bed);

    auto binbb = Box({
                         static_cast<libnest2d::Coord>(bbb.min.x),
                         static_cast<libnest2d::Coord>(bbb.min.y)
                     },
                     {
                         static_cast<libnest2d::Coord>(bbb.max.x),
                         static_cast<libnest2d::Coord>(bbb.max.y)
                     });

    switch(bedhint.type) {
    case BedShapeType::BOX: {

        // Create the arranger for the box shaped bed
        AutoArranger<Box> arrange(binbb, min_obj_distance, progressind);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::CIRCLE: {

        auto c = bedhint.shape.circ;
        auto cc = lnCircle({c.center().x, c.center().y} , c.radius());

        AutoArranger<lnCircle> arrange(cc, min_obj_distance, progressind);
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::IRREGULAR:
    case BedShapeType::WHO_KNOWS: {

        using P = libnest2d::PolygonImpl;

        auto ctour = Slic3rMultiPoint_to_ClipperPath(bed);
        P irrbed = sl::create<PolygonImpl>(std::move(ctour));

        AutoArranger<P> arrange(irrbed, min_obj_distance, progressind);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    };

    if(result.empty()) return false;

    if(first_bin_only) {
        applyResult(result.front(), 0, shapemap);
    } else {

        const auto STRIDE_PADDING = 1.2;

        Coord stride = static_cast<Coord>(STRIDE_PADDING*
                                          binbb.width()*SCALING_FACTOR);
        Coord batch_offset = 0;

        for(auto& group : result) {
            applyResult(group, batch_offset, shapemap);

            // Only the first pack group can be placed onto the print bed. The
            // other objects which could not fit will be placed next to the
            // print bed
            batch_offset += stride;
        }
    }

    for(auto objptr : model.objects) objptr->invalidate_bounding_box();

    return ret && result.size() == 1;
}

}
}
#endif // MODELARRANGE_HPP
