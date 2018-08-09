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

std::tuple<double /*score*/, Box /*farthest point from bin center*/>
objfunc(const PointImpl& bincenter,
        double /*bin_area*/,
        ShapeLike::Shapes<PolygonImpl>& pile,   // The currently arranged pile
        double /*pile_area*/,
        const Item &item,
        double norm,            // A norming factor for physical dimensions
        std::vector<double>& areacache, // pile item areas will be cached
        // a spatial index to quickly get neighbors of the candidate item
        SpatIndex& spatindex
        )
{
    using pl = PointLike;
    using sl = ShapeLike;

    static const double BIG_ITEM_TRESHOLD = 0.2;
    static const double ROUNDNESS_RATIO = 0.5;
    static const double DENSITY_RATIO = 1.0 - ROUNDNESS_RATIO;

    // We will treat big items (compared to the print bed) differently
    auto normarea = [norm](double area) { return std::sqrt(area)/norm; };

    // If a new bin has been created:
    if(pile.size() < areacache.size()) {
        areacache.clear();
        spatindex.clear();
    }

    // We must fill the caches:
    int idx = 0;
    for(auto& p : pile) {
        if(idx == areacache.size()) {
            areacache.emplace_back(sl::area(p));
            if(normarea(areacache[idx]) > BIG_ITEM_TRESHOLD)
                spatindex.insert({sl::boundingBox(p), idx});
        }

        idx++;
    }

    // Candidate item bounding box
    auto ibb = item.boundingBox();

    // Calculate the full bounding box of the pile with the candidate item
    pile.emplace_back(item.transformedShape());
    auto fullbb = ShapeLike::boundingBox(pile);
    pile.pop_back();

    // The bounding box of the big items (they will accumulate in the center
    // of the pile
    Box bigbb;
    if(spatindex.empty()) bigbb = fullbb;
    else {
        auto boostbb = spatindex.bounds();
        boost::geometry::convert(boostbb, bigbb);
    }

    // The size indicator of the candidate item. This is not the area,
    // but almost...
    double item_normarea = normarea(item.area());

    // Will hold the resulting score
    double score = 0;

    if(item_normarea > BIG_ITEM_TRESHOLD) {
        // This branch is for the bigger items..
        // Here we will use the closest point of the item bounding box to
        // the already arranged pile. So not the bb center nor the a choosen
        // corner but whichever is the closest to the center. This will
        // prevent some unwanted strange arrangements.

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

        // Density is the pack density: how big is the arranged pile
        auto density = std::sqrt(fullbb.width()*fullbb.height()) / norm;

        // Prepare a variable for the alignment score.
        // This will indicate: how well is the candidate item aligned with
        // its neighbors. We will check the aligment with all neighbors and
        // return the score for the best alignment. So it is enough for the
        // candidate to be aligned with only one item.
        auto alignment_score = std::numeric_limits<double>::max();

        auto& trsh =  item.transformedShape();

        auto querybb = item.boundingBox();

        // Query the spatial index for the neigbours
        std::vector<SpatElement> result;
        spatindex.query(bgi::intersects(querybb), std::back_inserter(result));

        for(auto& e : result) { // now get the score for the best alignment
            auto idx = e.second;
            auto& p = pile[idx];
            auto parea = areacache[idx];
            auto bb = sl::boundingBox(sl::Shapes<PolygonImpl>{p, trsh});
            auto bbarea = bb.area();
            auto ascore = 1.0 - (item.area() + parea)/bbarea;

            if(ascore < alignment_score) alignment_score = ascore;
        }

        // The final mix of the score is the balance between the distance
        // from the full pile center, the pack density and the
        // alignment with the neigbours
        auto C = 0.33;
        score = C * dist +  C * density + C * alignment_score;

    } else if( item_normarea < BIG_ITEM_TRESHOLD && spatindex.empty()) {
        // If there are no big items, only small, we should consider the
        // density here as well to not get silly results
        auto bindist = pl::distance(ibb.center(), bincenter) / norm;
        auto density = std::sqrt(fullbb.width()*fullbb.height()) / norm;
        score = ROUNDNESS_RATIO * bindist + DENSITY_RATIO * density;
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
    pcfg.accuracy = 0.6f;
}

template<class TBin>
class AutoArranger {};

template<class TBin>
class _ArrBase {
protected:
    using Placer = strategies::_NofitPolyPlacer<PolygonImpl, TBin>;
    using Selector = FirstFitSelection;
    using Packer = Arranger<Placer, Selector>;
    using PConfig = typename Packer::PlacementConfig;
    using Distance = TCoord<PointImpl>;
    using Pile = ShapeLike::Shapes<PolygonImpl>;

    Packer pck_;
    PConfig pconf_; // Placement configuration
    double bin_area_;
    std::vector<double> areacache_;
    SpatIndex rtree_;
public:

    _ArrBase(const TBin& bin, Distance dist,
             std::function<void(unsigned)> progressind):
       pck_(bin, dist), bin_area_(ShapeLike::area<PolygonImpl>(bin))
    {
        fillConfig(pconf_);
        pck_.progressIndicator(progressind);
    }

    template<class...Args> inline IndexedPackGroup operator()(Args&&...args) {
        areacache_.clear();
        return pck_.arrangeIndexed(std::forward<Args>(args)...);
    }
};

template<>
class AutoArranger<Box>: public _ArrBase<Box> {
public:

    AutoArranger(const Box& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<Box>(bin, dist, progressind)
    {
        pconf_.object_function = [this, bin] (
                    Pile& pile,
                    const Item &item,
                    double pile_area,
                    double norm,
                    double /*penality*/) {

            auto result = objfunc(bin.center(), bin_area_, pile,
                                  pile_area, item, norm, areacache_, rtree_);
            double score = std::get<0>(result);
            auto& fullbb = std::get<1>(result);

            auto wdiff = fullbb.width() - bin.width();
            auto hdiff = fullbb.height() - bin.height();
            if(wdiff > 0) score += std::pow(wdiff, 2) / norm;
            if(hdiff > 0) score += std::pow(hdiff, 2) / norm;

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
        pconf_.object_function = [this, &bin] (
                    Pile& pile,
                    const Item &item,
                    double pile_area,
                    double norm,
                    double /*penality*/) {

            auto binbb = ShapeLike::boundingBox(bin);
            auto result = objfunc(binbb.center(), bin_area_, pile,
                                  pile_area, item, norm, areacache_, rtree_);
            double score = std::get<0>(result);

            pile.emplace_back(item.transformedShape());
            auto chull = ShapeLike::convexHull(pile);
            pile.pop_back();

            // If it does not fit into the print bed we will beat it with a
            // large penality. If we would not do this, there would be only one
            // big pile that doesn't care whether it fits onto the print bed.
            if(!Placer::wouldFit(chull, bin)) score += norm;

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
        this->pconf_.object_function = [this] (
                    Pile& pile,
                    const Item &item,
                    double pile_area,
                    double norm,
                    double /*penality*/) {

            auto result = objfunc({0, 0}, 0, pile, pile_area,
                                  item, norm, areacache_, rtree_);
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

enum BedShapeHint {
    BOX,
    CIRCLE,
    IRREGULAR,
    WHO_KNOWS
};

BedShapeHint bedShape(const Slic3r::Polyline& /*bed*/) {
    // Determine the bed shape by hand
    return BOX;
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
    BoundingBox bbb(bed.points);

    auto binbb = Box({
                         static_cast<libnest2d::Coord>(bbb.min.x),
                         static_cast<libnest2d::Coord>(bbb.min.y)
                     },
                     {
                         static_cast<libnest2d::Coord>(bbb.max.x),
                         static_cast<libnest2d::Coord>(bbb.max.y)
                     });

    switch(bedhint) {
    case BOX: {

        // Create the arranger for the box shaped bed
        AutoArranger<Box> arrange(binbb, min_obj_distance, progressind);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case CIRCLE:
        break;
    case IRREGULAR:
    case WHO_KNOWS: {
        using P = libnest2d::PolygonImpl;

        auto ctour = Slic3rMultiPoint_to_ClipperPath(bed);
        P irrbed = ShapeLike::create<PolygonImpl>(std::move(ctour));

//        std::cout << ShapeLike::toString(irrbed) << std::endl;

        AutoArranger<P> arrange(irrbed, min_obj_distance, progressind);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    };

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
