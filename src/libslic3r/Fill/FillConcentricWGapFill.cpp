#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../VariableWidth.hpp"
#include "../ShortestPath.hpp"

#include "FillConcentricWGapFill.hpp"

namespace Slic3r {

const float concentric_overlap_threshold = 0.02;

void FillConcentricWGapFill::fill_surface_extrusion(const Surface* surface, const FillParams& params, ExtrusionEntitiesPtr& out)
{
    //BBS: FillConcentricWGapFill.cpp is absolutely newly add by BBL for narrow internal solid infill area to reduce vibration
    // Because the area is narrow, we should not use the surface->expolygon which has overlap with perimeter, but
    // use no_overlap_expolygons instead to avoid overflow in narrow area.
    //Slic3r::ExPolygons expp = offset_ex(surface->expolygon, double(scale_(0 - 0.5 * this->spacing)));
    float min_spacing = this->spacing * (1 - concentric_overlap_threshold);
    Slic3r::ExPolygons expp = offset2_ex(this->no_overlap_expolygons, -double(scale_(0.5 * this->spacing + 0.5 * min_spacing) - 1),
                                         +double(scale_(0.5 * min_spacing) - 1));
    // Create the infills for each of the regions.
    Polylines polylines_out;
    for (size_t i = 0; i < expp.size(); ++i) {
        ExPolygon expolygon = expp[i];

        coord_t distance = scale_(this->spacing / params.density);
        if (params.density > 0.9999f && !params.dont_adjust) {
            distance = scale_(this->spacing);
        }

        ExPolygons gaps;
        Polygons loops = (Polygons)expolygon;
        ExPolygons last = { expolygon };
        bool first = true;
        while (!last.empty()) {
            ExPolygons next_onion = offset2_ex(last, -double(distance + scale_(this->spacing) / 2), +double(scale_(this->spacing) / 2));
            for (auto it = next_onion.begin(); it != next_onion.end(); it++) {
                Polygons temp_loops = (Polygons)(*it);
                loops.insert(loops.end(), temp_loops.begin(), temp_loops.end());
            }
            append(gaps, diff_ex(
                offset(last, -0.5f * distance),
                offset(next_onion, 0.5f * distance + 10)));  // 10 is safty offset
            last = next_onion;
            if (first && !this->no_overlap_expolygons.empty()) {
                gaps = intersection_ex(gaps, this->no_overlap_expolygons);
            }
            first = false;
        }

        ExtrusionRole good_role = params.extrusion_role;
        ExtrusionEntityCollection *coll_nosort = new ExtrusionEntityCollection();
        coll_nosort->no_sort = this->no_sort(); //can be sorted inside the pass
        extrusion_entities_append_loops(
            coll_nosort->entities, std::move(loops),
            good_role,
            params.flow.mm3_per_mm(),
            params.flow.width(),
            params.flow.height());

        //BBS: add internal gapfills between infill loops
        if (!gaps.empty() && params.density >= 1) {
            double min = 0.2 * distance * (1 - INSET_OVERLAP_TOLERANCE);
            double max = 2. * distance;
            ExPolygons gaps_ex = diff_ex(
                offset2_ex(gaps, -float(min / 2), float(min / 2)),
                offset2_ex(gaps, -float(max / 2), float(max / 2)),
                ApplySafetyOffset::Yes);
            //BBS: sort the gap_ex to avoid mess travel
            Points ordering_points;
            ordering_points.reserve(gaps_ex.size());
            ExPolygons gaps_ex_sorted;
            gaps_ex_sorted.reserve(gaps_ex.size());
            for (const ExPolygon &ex : gaps_ex)
                ordering_points.push_back(ex.contour.first_point());
            std::vector<Points::size_type> order = chain_points(ordering_points);
            for (size_t i : order)
                gaps_ex_sorted.emplace_back(std::move(gaps_ex[i]));

            ThickPolylines polylines;
            for (ExPolygon& ex : gaps_ex_sorted) {
                //BBS: medial axis algorithm can't handle duplicated points in expolygon.
                //Use DP simplify to avoid duplicated points and accelerate medial-axis calculation as well.
                ex.douglas_peucker(SCALED_RESOLUTION);
                ex.medial_axis(max, min, &polylines);
            }

            if (!polylines.empty() && !is_bridge(good_role)) {
                ExtrusionEntityCollection gap_fill;
                variable_width(polylines, erGapFill, params.flow, gap_fill.entities);
                coll_nosort->append(std::move(gap_fill.entities));
            }
        }

        if (!coll_nosort->entities.empty())
            out.push_back(coll_nosort);
        else
            delete coll_nosort;
    }

    //BBS: add external gapfill between perimeter and infill
    ExPolygons external_gaps = diff_ex(this->no_overlap_expolygons, offset_ex(expp, double(scale_(0.5 * this->spacing))), ApplySafetyOffset::Yes);
    external_gaps = union_ex(external_gaps);
    if (!this->no_overlap_expolygons.empty())
            external_gaps = intersection_ex(external_gaps, this->no_overlap_expolygons);

    if (!external_gaps.empty()) {
        double min = 0.4 * scale_(params.flow.nozzle_diameter()) * (1 - INSET_OVERLAP_TOLERANCE);
        double max = 2. * params.flow.scaled_width();
        //BBS: collapse, be sure we don't gapfill where the perimeters are already touching each other (negative spacing).
        min = std::max(min, (double)Flow::rounded_rectangle_extrusion_width_from_spacing((float)EPSILON, (float)params.flow.height()));
        ExPolygons external_gaps_collapsed = offset2_ex(external_gaps, double(-min / 2), double(+min / 2));

        ThickPolylines polylines;
        for (ExPolygon& ex : external_gaps_collapsed) {
            //BBS: medial axis algorithm can't handle duplicated points in expolygon.
            //Use DP simplify to avoid duplicated points and accelerate medial-axis calculation as well.
            ex.douglas_peucker(SCALED_RESOLUTION);
            ex.medial_axis(max, min, &polylines);
        }

        ExtrusionEntityCollection* coll_external_gapfill = new ExtrusionEntityCollection();
        coll_external_gapfill->no_sort = this->no_sort();
        if (!polylines.empty() && !is_bridge(params.extrusion_role)) {
            ExtrusionEntityCollection gap_fill;
            variable_width(polylines, erGapFill, params.flow, gap_fill.entities);
            coll_external_gapfill->append(std::move(gap_fill.entities));
        }
        if (!coll_external_gapfill->entities.empty())
            out.push_back(coll_external_gapfill);
        else
            delete coll_external_gapfill;
    }
}

}