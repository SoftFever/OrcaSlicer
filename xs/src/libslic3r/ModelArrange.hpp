#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include "Model.hpp"
#include "SVG.hpp"
#include <libnest2d.h>

#include <numeric>
#include <ClipperUtils.hpp>

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
bool arrange(Model &model, coordf_t dist, const Slic3r::BoundingBoxf* bb,
             bool first_bin_only,
             std::function<void(unsigned)> progressind)
{
    using ArrangeResult = _IndexedPackGroup<PolygonImpl>;

    bool ret = true;

    // Create the arranger config
    auto min_obj_distance = static_cast<Coord>(dist/SCALING_FACTOR);

    // Get the 2D projected shapes with their 3D model instance pointers
    auto shapemap = arr::projectModelFromTop(model);

    bool hasbin = bb != nullptr && bb->defined;
    double area_max = 0;

    // Copy the references for the shapes only as the arranger expects a
    // sequence of objects convertible to Item or ClipperPolygon
    std::vector<std::reference_wrapper<Item>> shapes;
    shapes.reserve(shapemap.size());
    std::for_each(shapemap.begin(), shapemap.end(),
                  [&shapes, min_obj_distance, &area_max, hasbin]
                  (ShapeData2D::value_type& it)
    {
        shapes.push_back(std::ref(it.second));
    });

    Box bin;

    if(hasbin) {
        // Scale up the bounding box to clipper scale.
        BoundingBoxf bbb = *bb;
        bbb.scale(1.0/SCALING_FACTOR);

        bin = Box({
                    static_cast<libnest2d::Coord>(bbb.min.x),
                    static_cast<libnest2d::Coord>(bbb.min.y)
                },
                {
                    static_cast<libnest2d::Coord>(bbb.max.x),
                    static_cast<libnest2d::Coord>(bbb.max.y)
                });
    }

    // Will use the DJD selection heuristic with the BottomLeft placement
    // strategy
    using Arranger = Arranger<NfpPlacer, FirstFitSelection>;
    using PConf = Arranger::PlacementConfig;
    using SConf = Arranger::SelectionConfig;

    PConf pcfg;     // Placement configuration
    SConf scfg;     // Selection configuration

    // Align the arranged pile into the center of the bin
    pcfg.alignment = PConf::Alignment::CENTER;

    // Start placing the items from the center of the print bed
    pcfg.starting_point = PConf::Alignment::CENTER;

    // TODO cannot use rotations until multiple objects of same geometry can
    // handle different rotations
    // arranger.useMinimumBoundigBoxRotation();
    pcfg.rotations = { 0.0 };

    // The accuracy of optimization. Goes from 0.0 to 1.0 and scales performance
    pcfg.accuracy = 0.4f;

    // Magic: we will specify what is the goal of arrangement... In this case
    // we override the default object function to make the larger items go into
    // the center of the pile and smaller items orbit it so the resulting pile
    // has a circle-like shape. This is good for the print bed's heat profile.
    // We alse sacrafice a bit of pack efficiency for this to work. As a side
    // effect, the arrange procedure is a lot faster (we do not need to
    // calculate the convex hulls)
    pcfg.object_function = [bin, hasbin](
            NfpPlacer::Pile& pile,   // The currently arranged pile
            const Item &item,
            double /*area*/,        // Sum area of items (not needed)
            double norm,            // A norming factor for physical dimensions
            double penality)        // Min penality in case of bad arrangement
    {
        using pl = PointLike;

        static const double BIG_ITEM_TRESHOLD = 0.2;
        static const double GRAVITY_RATIO = 0.5;
        static const double DENSITY_RATIO = 1.0 - GRAVITY_RATIO;

        // We will treat big items (compared to the print bed) differently
        NfpPlacer::Pile bigs;
        bigs.reserve(pile.size());
        for(auto& p : pile) {
            auto pbb = ShapeLike::boundingBox(p);
            auto na = std::sqrt(pbb.width()*pbb.height())/norm;
            if(na > BIG_ITEM_TRESHOLD) bigs.emplace_back(p);
        }

        // Candidate item bounding box
        auto ibb = item.boundingBox();

        // Calculate the full bounding box of the pile with the candidate item
        pile.emplace_back(item.transformedShape());
        auto fullbb = ShapeLike::boundingBox(pile);
        pile.pop_back();

        // The bounding box of the big items (they will accumulate in the center
        // of the pile
        auto bigbb = bigs.empty()? fullbb : ShapeLike::boundingBox(bigs);

        // The size indicator of the candidate item. This is not the area,
        // but almost...
        auto itemnormarea = std::sqrt(ibb.width()*ibb.height())/norm;

        // Will hold the resulting score
        double score = 0;

        if(itemnormarea > BIG_ITEM_TRESHOLD) {
            // This branch is for the bigger items..
            // Here we will use the closest point of the item bounding box to
            // the already arranged pile. So not the bb center nor the a choosen
            // corner but whichever is the closest to the center. This will
            // prevent unwanted strange arrangements.

            auto minc = ibb.minCorner(); // bottom left corner
            auto maxc = ibb.maxCorner(); // top right corner

            // top left and bottom right corners
            auto top_left = PointImpl{getX(minc), getY(maxc)};
            auto bottom_right = PointImpl{getX(maxc), getY(minc)};

            auto cc = fullbb.center(); // The gravity center

            // Now the distnce of the gravity center will be calculated to the
            // five anchor points and the smallest will be chosen.
            std::array<double, 5> dists;
            dists[0] = pl::distance(minc, cc);
            dists[1] = pl::distance(maxc, cc);
            dists[2] = pl::distance(ibb.center(), cc);
            dists[3] = pl::distance(top_left, cc);
            dists[4] = pl::distance(bottom_right, cc);

            auto dist = *(std::min_element(dists.begin(), dists.end())) / norm;

            // Density is the pack density: how big is the arranged pile
            auto density = std::sqrt(fullbb.width()*fullbb.height()) / norm;

            // The score is a weighted sum of the distance from pile center
            // and the pile size
            score = GRAVITY_RATIO * dist + DENSITY_RATIO * density;

        } else if(itemnormarea < BIG_ITEM_TRESHOLD && bigs.empty()) {
            // If there are no big items, only small, we should consider the
            // density here as well to not get silly results
            auto bindist = pl::distance(ibb.center(), bin.center()) / norm;
            auto density = std::sqrt(fullbb.width()*fullbb.height()) / norm;
            score = GRAVITY_RATIO * bindist + DENSITY_RATIO * density;
        } else {
            // Here there are the small items that should be placed around the
            // already processed bigger items.
            // No need to play around with the anchor points, the center will be
            // just fine for small items
            score = pl::distance(ibb.center(), bigbb.center()) / norm;
        }

        // If it does not fit into the print bed we will beat it
        // with a large penality. If we would not do this, there would be only
        // one big pile that doesn't care whether it fits onto the print bed.
        if(!NfpPlacer::wouldFit(fullbb, bin)) score = 2*penality - score;

        return score;
    };

    // Create the arranger object
    Arranger arranger(bin, min_obj_distance, pcfg, scfg);

    // Set the progress indicator for the arranger.
    arranger.progressIndicator(progressind);

    // Arrange and return the items with their respective indices within the
    // input sequence.
    auto result = arranger.arrangeIndexed(shapes.begin(), shapes.end());

    auto applyResult = [&shapemap](ArrangeResult::value_type& group,
            Coord batch_offset)
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
    };

    if(first_bin_only) {
        applyResult(result.front(), 0);
    } else {

        const auto STRIDE_PADDING = 1.2;

        Coord stride = static_cast<Coord>(STRIDE_PADDING*
                                          bin.width()*SCALING_FACTOR);
        Coord batch_offset = 0;

        for(auto& group : result) {
            applyResult(group, batch_offset);

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
