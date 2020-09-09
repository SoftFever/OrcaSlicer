#include "SeamPlacer.hpp"

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {



static float extrudate_overlap_penalty(float nozzle_r, float weight_zero, float overlap_distance)
{
    // The extrudate is not fully supported by the lower layer. Fit a polynomial penalty curve.
    // Solved by sympy package:
/*
from sympy import *
(x,a,b,c,d,r,z)=symbols('x a b c d r z')
p = a + b*x + c*x*x + d*x*x*x
p2 = p.subs(solve([p.subs(x, -r), p.diff(x).subs(x, -r), p.diff(x,x).subs(x, -r), p.subs(x, 0)-z], [a, b, c, d]))
from sympy.plotting import plot
plot(p2.subs(r,0.2).subs(z,1.), (x, -1, 3), adaptive=False, nb_of_points=400)
*/
    if (overlap_distance < - nozzle_r) {
        // The extrudate is fully supported by the lower layer. This is the ideal case, therefore zero penalty.
        return 0.f;
    } else {
        float x  = overlap_distance / nozzle_r;
        float x2 = x * x;
        float x3 = x2 * x;
        return weight_zero * (1.f + 3.f * x + 3.f * x2 + x3);
    }
}



// Return a value in <0, 1> of a cubic B-spline kernel centered around zero.
// The B-spline is re-scaled so it has value 1 at zero.
static inline float bspline_kernel(float x)
{
    x = std::abs(x);
    if (x < 1.f) {
        return 1.f - (3.f / 2.f) * x * x + (3.f / 4.f) * x * x * x;
    }
    else if (x < 2.f) {
        x -= 1.f;
        float x2 = x * x;
        float x3 = x2 * x;
        return (1.f / 4.f) - (3.f / 4.f) * x + (3.f / 4.f) * x2 - (1.f / 4.f) * x3;
    }
    else
        return 0;
}



static Points::const_iterator project_point_to_polygon_and_insert(Polygon &polygon, const Point &pt, double eps)
{
    assert(polygon.points.size() >= 2);
    if (polygon.points.size() <= 1)
    if (polygon.points.size() == 1)
        return polygon.points.begin();

    Point  pt_min;
    double d_min = std::numeric_limits<double>::max();
    size_t i_min = size_t(-1);

    for (size_t i = 0; i < polygon.points.size(); ++ i) {
        size_t j = i + 1;
        if (j == polygon.points.size())
            j = 0;
        const Point &p1 = polygon.points[i];
        const Point &p2 = polygon.points[j];
        const Slic3r::Point v_seg = p2 - p1;
        const Slic3r::Point v_pt  = pt - p1;
        const int64_t l2_seg = int64_t(v_seg(0)) * int64_t(v_seg(0)) + int64_t(v_seg(1)) * int64_t(v_seg(1));
        int64_t t_pt = int64_t(v_seg(0)) * int64_t(v_pt(0)) + int64_t(v_seg(1)) * int64_t(v_pt(1));
        if (t_pt < 0) {
            // Closest to p1.
            double dabs = sqrt(int64_t(v_pt(0)) * int64_t(v_pt(0)) + int64_t(v_pt(1)) * int64_t(v_pt(1)));
            if (dabs < d_min) {
                d_min  = dabs;
                i_min  = i;
                pt_min = p1;
            }
        }
        else if (t_pt > l2_seg) {
            // Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the next step.
            continue;
        } else {
            // Closest to the segment.
            assert(t_pt >= 0 && t_pt <= l2_seg);
            int64_t d_seg = int64_t(v_seg(1)) * int64_t(v_pt(0)) - int64_t(v_seg(0)) * int64_t(v_pt(1));
            double d = double(d_seg) / sqrt(double(l2_seg));
            double dabs = std::abs(d);
            if (dabs < d_min) {
                d_min  = dabs;
                i_min  = i;
                // Evaluate the foot point.
                pt_min = p1;
                double linv = double(d_seg) / double(l2_seg);
                pt_min(0) = pt(0) - coord_t(floor(double(v_seg(1)) * linv + 0.5));
                pt_min(1) = pt(1) + coord_t(floor(double(v_seg(0)) * linv + 0.5));
                assert(Line(p1, p2).distance_to(pt_min) < scale_(1e-5));
            }
        }
    }

    assert(i_min != size_t(-1));
    if ((pt_min - polygon.points[i_min]).cast<double>().norm() > eps) {
        // Insert a new point on the segment i_min, i_min+1.
        return polygon.points.insert(polygon.points.begin() + (i_min + 1), pt_min);
    }
    return polygon.points.begin() + i_min;
}



static std::vector<float> polygon_angles_at_vertices(const Polygon &polygon, const std::vector<float> &lengths, float min_arm_length)
{
    assert(polygon.points.size() + 1 == lengths.size());
    if (min_arm_length > 0.25f * lengths.back())
        min_arm_length = 0.25f * lengths.back();

    // Find the initial prev / next point span.
    size_t idx_prev = polygon.points.size();
    size_t idx_curr = 0;
    size_t idx_next = 1;
    while (idx_prev > idx_curr && lengths.back() - lengths[idx_prev] < min_arm_length)
        -- idx_prev;
    while (idx_next < idx_prev && lengths[idx_next] < min_arm_length)
        ++ idx_next;

    std::vector<float> angles(polygon.points.size(), 0.f);
    for (; idx_curr < polygon.points.size(); ++ idx_curr) {
        // Move idx_prev up until the distance between idx_prev and idx_curr is lower than min_arm_length.
        if (idx_prev >= idx_curr) {
            while (idx_prev < polygon.points.size() && lengths.back() - lengths[idx_prev] + lengths[idx_curr] > min_arm_length)
                ++ idx_prev;
            if (idx_prev == polygon.points.size())
                idx_prev = 0;
        }
        while (idx_prev < idx_curr && lengths[idx_curr] - lengths[idx_prev] > min_arm_length)
            ++ idx_prev;
        // Move idx_prev one step back.
        if (idx_prev == 0)
            idx_prev = polygon.points.size() - 1;
        else
            -- idx_prev;
        // Move idx_next up until the distance between idx_curr and idx_next is greater than min_arm_length.
        if (idx_curr <= idx_next) {
            while (idx_next < polygon.points.size() && lengths[idx_next] - lengths[idx_curr] < min_arm_length)
                ++ idx_next;
            if (idx_next == polygon.points.size())
                idx_next = 0;
        }
        while (idx_next < idx_curr && lengths.back() - lengths[idx_curr] + lengths[idx_next] < min_arm_length)
            ++ idx_next;
        // Calculate angle between idx_prev, idx_curr, idx_next.
        const Point &p0 = polygon.points[idx_prev];
        const Point &p1 = polygon.points[idx_curr];
        const Point &p2 = polygon.points[idx_next];
        const Point  v1 = p1 - p0;
        const Point  v2 = p2 - p1;
        int64_t dot   = int64_t(v1(0))*int64_t(v2(0)) + int64_t(v1(1))*int64_t(v2(1));
        int64_t cross = int64_t(v1(0))*int64_t(v2(1)) - int64_t(v1(1))*int64_t(v2(0));
        float angle = float(atan2(double(cross), double(dot)));
        angles[idx_curr] = angle;
    }

    return angles;
}



void SeamPlacer::init(const Print& print)
{
    m_enforcers.clear();
    m_blockers.clear();
    m_last_seam_position.clear();

   for (const PrintObject* po : print.objects()) {
       po->project_and_append_custom_facets(true, EnforcerBlockerType::ENFORCER, m_enforcers);
       po->project_and_append_custom_facets(true, EnforcerBlockerType::BLOCKER, m_blockers);
   }
   const std::vector<double>& nozzle_dmrs = print.config().nozzle_diameter.values;
   float max_nozzle_dmr = *std::max_element(nozzle_dmrs.begin(), nozzle_dmrs.end());
   for (ExPolygons& explgs : m_enforcers)
       explgs = Slic3r::offset_ex(explgs, scale_(max_nozzle_dmr));
   for (ExPolygons& explgs : m_blockers)
       explgs = Slic3r::offset_ex(explgs, scale_(max_nozzle_dmr));
}



Point SeamPlacer::get_seam(const size_t layer_idx, const SeamPosition seam_position,
               const ExtrusionLoop& loop, Point last_pos, coordf_t nozzle_dmr,
               const PrintObject* po, bool was_clockwise, const EdgeGrid::Grid* lower_layer_edge_grid)
{
    if (seam_position == spNearest || seam_position == spAligned || seam_position == spRear) {
            Polygon        polygon    = loop.polygon();
            const coord_t  nozzle_r   = coord_t(scale_(0.5 * nozzle_dmr) + 0.5);

            if (this->is_custom(layer_idx)) {
                // Seam enf/blockers can begin and end in between the original vertices.
                // Let add extra points in between and update the leghths.
                polygon.densify(scale_(0.2f));
            }

            // Retrieve the last start position for this object.
            float last_pos_weight = 1.f;

            if (seam_position == spAligned) {
                // Seam is aligned to the seam at the preceding layer.
                if (po != nullptr && m_last_seam_position.count(po) > 0) {
                    last_pos = m_last_seam_position[po];
                    last_pos_weight = 1.f;
                }
            }
            else if (seam_position == spRear) {
                // Object is centered around (0,0) in its current coordinate system.
                last_pos.x() = 0;
                last_pos.y() += coord_t(3. * po->bounding_box().radius());
                last_pos_weight = 5.f;
            }

            // Insert a projection of last_pos into the polygon.
            size_t last_pos_proj_idx;
            {
                auto it = project_point_to_polygon_and_insert(polygon, last_pos, 0.1 * nozzle_r);
                last_pos_proj_idx = it - polygon.points.begin();
            }

            // Parametrize the polygon by its length.
            std::vector<float> lengths = polygon.parameter_by_length();

            // For each polygon point, store a penalty.
            // First calculate the angles, store them as penalties. The angles are caluculated over a minimum arm length of nozzle_r.
            std::vector<float> penalties = polygon_angles_at_vertices(polygon, lengths, float(nozzle_r));
            // No penalty for reflex points, slight penalty for convex points, high penalty for flat surfaces.
            const float penaltyConvexVertex = 1.f;
            const float penaltyFlatSurface  = 5.f;
            const float penaltyOverhangHalf = 10.f;
            // Penalty for visible seams.
           for (size_t i = 0; i < polygon.points.size(); ++ i) {
                float ccwAngle = penalties[i];
                if (was_clockwise)
                    ccwAngle = - ccwAngle;
                float penalty = 0;
                if (ccwAngle <- float(0.6 * PI))
                    // Sharp reflex vertex. We love that, it hides the seam perfectly.
                    penalty = 0.f;
                else if (ccwAngle > float(0.6 * PI))
                    // Seams on sharp convex vertices are more visible than on reflex vertices.
                    penalty = penaltyConvexVertex;
                else if (ccwAngle < 0.f) {
                    // Interpolate penalty between maximum and zero.
                    penalty = penaltyFlatSurface * bspline_kernel(ccwAngle * float(PI * 2. / 3.));
                } else {
                    assert(ccwAngle >= 0.f);
                    // Interpolate penalty between maximum and the penalty for a convex vertex.
                    penalty = penaltyConvexVertex + (penaltyFlatSurface - penaltyConvexVertex) * bspline_kernel(ccwAngle * float(PI * 2. / 3.));
                }
                // Give a negative penalty for points close to the last point or the prefered seam location.
                float dist_to_last_pos_proj = (i < last_pos_proj_idx) ?
                    std::min(lengths[last_pos_proj_idx] - lengths[i], lengths.back() - lengths[last_pos_proj_idx] + lengths[i]) :
                    std::min(lengths[i] - lengths[last_pos_proj_idx], lengths.back() - lengths[i] + lengths[last_pos_proj_idx]);
                float dist_max = 0.1f * lengths.back(); // 5.f * nozzle_dmr
                penalty -= last_pos_weight * bspline_kernel(dist_to_last_pos_proj / dist_max);
                penalties[i] = std::max(0.f, penalty);
            }

            // Penalty for overhangs.
            if (lower_layer_edge_grid) {
                // Use the edge grid distance field structure over the lower layer to calculate overhangs.
                coord_t nozzle_r = coord_t(std::floor(scale_(0.5 * nozzle_dmr) + 0.5));
                coord_t search_r = coord_t(std::floor(scale_(0.8 * nozzle_dmr) + 0.5));
                for (size_t i = 0; i < polygon.points.size(); ++ i) {
                    const Point &p = polygon.points[i];
                    coordf_t dist;
                    // Signed distance is positive outside the object, negative inside the object.
                    // The point is considered at an overhang, if it is more than nozzle radius
                    // outside of the lower layer contour.
                    [[maybe_unused]] bool found = lower_layer_edge_grid->signed_distance(p, search_r, dist);
                    // If the approximate Signed Distance Field was initialized over lower_layer_edge_grid,
                    // then the signed distnace shall always be known.
                    assert(found);
                    penalties[i] += extrudate_overlap_penalty(float(nozzle_r), penaltyOverhangHalf, float(dist));
                }
            }

            // Penalty according to custom seam selection. This one is huge compared to
            // the others so that points outside enforcers/inside blockers never win.
            this->penalize_polygon(polygon, penalties, lengths, layer_idx);

            // Find a point with a minimum penalty.
            size_t idx_min = std::min_element(penalties.begin(), penalties.end()) - penalties.begin();

            // For all (aligned, nearest, rear) seams:
            {
                // Very likely the weight of idx_min is very close to the weight of last_pos_proj_idx.
                // In that case use last_pos_proj_idx instead.
                float penalty_aligned  = penalties[last_pos_proj_idx];
                float penalty_min      = penalties[idx_min];
                float penalty_diff_abs = std::abs(penalty_min - penalty_aligned);
                float penalty_max      = std::max(penalty_min, penalty_aligned);
                float penalty_diff_rel = (penalty_max == 0.f) ? 0.f : penalty_diff_abs / penalty_max;
                // printf("Align seams, penalty aligned: %f, min: %f, diff abs: %f, diff rel: %f\n", penalty_aligned, penalty_min, penalty_diff_abs, penalty_diff_rel);
                if (std::abs(penalty_diff_rel) < 0.05) {
                    // Penalty of the aligned point is very close to the minimum penalty.
                    // Align the seams as accurately as possible.
                    idx_min = last_pos_proj_idx;
                }
                m_last_seam_position[po] = polygon.points[idx_min];
            }


            // Export the contour into a SVG file.
            #if 0
            {
                static int iRun = 0;
                SVG svg(debug_out_path("GCode_extrude_loop-%d.svg", iRun ++));
                if (m_layer->lower_layer != NULL)
                    svg.draw(m_layer->lower_layer->slices);
                for (size_t i = 0; i < loop.paths.size(); ++ i)
                    svg.draw(loop.paths[i].as_polyline(), "red");
                Polylines polylines;
                for (size_t i = 0; i < loop.paths.size(); ++ i)
                    polylines.push_back(loop.paths[i].as_polyline());
                Slic3r::Polygons polygons;
                coordf_t nozzle_dmr = EXTRUDER_CONFIG(nozzle_diameter);
                coord_t delta = scale_(0.5*nozzle_dmr);
                Slic3r::offset(polylines, &polygons, delta);
    //            for (size_t i = 0; i < polygons.size(); ++ i) svg.draw((Polyline)polygons[i], "blue");
                svg.draw(last_pos, "green", 3);
                svg.draw(polygon.points[idx_min], "yellow", 3);
                svg.Close();
            }
            #endif
            return polygon.points[idx_min];

        } else { // spRandom
            if (loop.loop_role() == elrContourInternalPerimeter) {
                // This loop does not contain any other loop. Set a random position.
                // The other loops will get a seam close to the random point chosen
                // on the inner most contour.
                //FIXME This works correctly for inner contours first only.
                //FIXME Better parametrize the loop by its length.
                Polygon polygon = loop.polygon();
                Point centroid = polygon.centroid();
                last_pos = Point(polygon.bounding_box().max(0), centroid(1));
                last_pos.rotate(fmod((float)rand()/16.0, 2.0*PI), centroid);
            }
            return last_pos;
        }
}




void SeamPlacer::get_indices(size_t layer_id,
                             const Polygon& polygon,
                             std::vector<size_t>& enforcers_idxs,
                             std::vector<size_t>& blockers_idxs) const
{
    enforcers_idxs.clear();
    blockers_idxs.clear();

    // FIXME: This is quadratic and it should be improved, maybe by building
    // an AABB tree (or at least utilize bounding boxes).
    for (size_t i=0; i<polygon.points.size(); ++i) {

        if (! m_enforcers.empty()) {
            assert(layer_id < m_enforcers.size());
            for (const ExPolygon& explg : m_enforcers[layer_id]) {
                if (explg.contains(polygon.points[i]))
                    enforcers_idxs.push_back(i);
            }
        }

        if (! m_blockers.empty()) {
            assert(layer_id < m_blockers.size());
            for (const ExPolygon& explg : m_blockers[layer_id]) {
                if (explg.contains(polygon.points[i]))
                    blockers_idxs.push_back(i);
            }
        }
    }
}


// Go through the polygon, identify points inside support enforcers and return
// indices of points in the middle of each enforcer (measured along the contour).
static std::vector<size_t> find_enforcer_centers(const Polygon& polygon,
                                                 const std::vector<float>& lengths,
                                                 const std::vector<size_t>& enforcers_idxs)
{
    std::vector<size_t> out;
    assert(polygon.points.size()+1 == lengths.size());
    assert(std::is_sorted(enforcers_idxs.begin(), enforcers_idxs.end()));
    if (polygon.size() < 2 || enforcers_idxs.empty())
        return out;

    auto get_center_idx = [&polygon, &lengths](size_t start_idx, size_t end_idx) -> size_t {
        assert(end_idx >= start_idx);
        if (start_idx == end_idx)
            return start_idx;
        float t_c = lengths[start_idx] + 0.5f * (lengths[end_idx] - lengths[start_idx]);
        auto it = std::lower_bound(lengths.begin() + start_idx, lengths.begin() + end_idx, t_c);
        int ret = it - lengths.begin();
        return ret;
    };

    int last_enforcer_start_idx = enforcers_idxs.front();
    bool last_pt_in_list = enforcers_idxs.back() == polygon.points.size() - 1;

    for (size_t i=0; i<enforcers_idxs.size()-1; ++i) {
        if ((i == enforcers_idxs.size() - 1)
         || enforcers_idxs[i+1] != enforcers_idxs[i] + 1) {
            // i is last point of current enforcer
            out.push_back(get_center_idx(last_enforcer_start_idx, enforcers_idxs[i]));
            last_enforcer_start_idx = enforcers_idxs[i+1];
        }
    }

    if (last_pt_in_list) {
        // last point is an enforcer - not yet accounted for.
        if (enforcers_idxs.front() != 0) {
            size_t center_idx = get_center_idx(last_enforcer_start_idx, enforcers_idxs.back());
            out.push_back(center_idx);
        } else {
            // Wrap-around. Update first center already found.
            if (out.empty()) {
                // Probably an enforcer around the whole contour. Return nothing.
                return out;
            }

            // find last point of the enforcer at the beginning:
            size_t idx = 0;
            while (enforcers_idxs[idx]+1 == enforcers_idxs[idx+1])
                ++idx;

            float t_s = lengths[last_enforcer_start_idx];
            float t_e = lengths[idx];
            float half_dist = 0.5f * (t_e + lengths.back() - t_s);
            float t_c = (half_dist > t_e) ? t_s + half_dist : t_e - half_dist;

            auto it = std::lower_bound(lengths.begin(), lengths.end(), t_c);
            out[0] = it - lengths.begin();
            if (out[0] == lengths.size() - 1)
                --out[0];
            assert(out[0] < lengths.size() - 1);
        }
    }
    return out;
}



void SeamPlacer::penalize_polygon(const Polygon& polygon,
                                  std::vector<float>& penalties,
                                  const std::vector<float>& lengths,
                                  int layer_id) const
{
    std::vector<size_t> enforcers_idxs;
    std::vector<size_t> blockers_idxs;
    this->get_indices(layer_id, polygon, enforcers_idxs, blockers_idxs);

    for (size_t i : enforcers_idxs) {
        assert(i < penalties.size());
        penalties[i] -= float(ENFORCER_BLOCKER_PENALTY);
    }
    for (size_t i : blockers_idxs) {
        assert(i < penalties.size());
        penalties[i] += float(ENFORCER_BLOCKER_PENALTY);
    }
    std::vector<size_t> enf_centers = find_enforcer_centers(polygon, lengths, enforcers_idxs);
    for (size_t idx : enf_centers) {
        assert(idx < penalties.size());
        penalties[idx] -= 1000.f;
    }

//    //////////////////////
//            std::ostringstream os;
//            os << std::setw(3) << std::setfill('0') << layer_id;
//            int a = scale_(20.);
//            SVG svg("custom_seam" + os.str() + ".svg", BoundingBox(Point(-a, -a), Point(a, a)));
//            /*if (! m_enforcers.empty())
//                svg.draw(m_enforcers[layer_id], "blue");
//            if (! m_blockers.empty())
//                svg.draw(m_blockers[layer_id], "red");*/

//            size_t min_idx = std::min_element(penalties.begin(), penalties.end()) - penalties.begin();

//            //svg.draw(polygon.points[idx_min], "red", 6e5);
//            for (size_t i=0; i<polygon.points.size(); ++i) {
//                std::string fill;
//                coord_t size = 0;
//                if (min_idx == i) {
//                    fill = "yellow";
//                    size = 5e5;
//                } else {
//                    fill = (std::find(enforcers_idxs.begin(), enforcers_idxs.end(), i) != enforcers_idxs.end() ? "green" : "black");
//                    if (std::find(enf_centers.begin(), enf_centers.end(), i) != enf_centers.end()) {
//                        size = 5e5;
//                        fill = "blue";
//                    }
//                }
//                if (i != 0)
//                    svg.draw(polygon.points[i], fill, size);
//                else
//                    svg.draw(polygon.points[i], "red", 5e5);
//            }
//    ////////////////////

}


}
