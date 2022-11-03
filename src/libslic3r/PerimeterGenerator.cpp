#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ShortestPath.hpp"
#include "VariableWidth.hpp"
#include "CurveAnalyzer.hpp"
#include "Clipper2Utils.hpp"

#include <cmath>
#include <cassert>

static const int overhang_sampling_number = 6;
static const double narrow_loop_length_threshold = 10;
//BBS: when the width of expolygon is smaller than
//ext_perimeter_width + ext_perimeter_spacing  * (1 - SMALLER_EXT_INSET_OVERLAP_TOLERANCE),
//we think it's small detail area and will generate smaller line width for it
static constexpr double SMALLER_EXT_INSET_OVERLAP_TOLERANCE = 0.22;

namespace Slic3r {

// Hierarchy of perimeters.
class PerimeterGeneratorLoop {
public:
    // Polygon of this contour.
    Polygon                             polygon;
    // Is it a contour or a hole?
    // Contours are CCW oriented, holes are CW oriented.
    bool                                is_contour;
    // BBS: is perimeter using smaller width
    bool is_smaller_width_perimeter;
    // Depth in the hierarchy. External perimeter has depth = 0. An external perimeter could be both a contour and a hole.
    unsigned short                      depth;
    // Should this contur be fuzzyfied on path generation?
    bool                                fuzzify;
    // Children contour, may be both CCW and CW oriented (outer contours or holes).
    std::vector<PerimeterGeneratorLoop> children;
    
    PerimeterGeneratorLoop(const Polygon &polygon, unsigned short depth, bool is_contour, bool fuzzify, bool is_small_width_perimeter = false) :
        polygon(polygon), is_contour(is_contour), is_smaller_width_perimeter(is_small_width_perimeter), depth(depth), fuzzify(fuzzify) {}
    // External perimeter. It may be CCW or CW oriented (outer contour or hole contour).
    bool is_external() const { return this->depth == 0; }
    // An island, which may have holes, but it does not have another internal island.
    bool is_internal_contour() const;
};

// Thanks Cura developers for this function.
static void fuzzy_polygon(Polygon &poly, double fuzzy_skin_thickness, double fuzzy_skin_point_distance)
{
    const double min_dist_between_points = fuzzy_skin_point_distance * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_distance / 2.;
    double dist_left_over = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point
    Point* p0 = &poly.points.back();
    Points out;
    out.reserve(poly.points.size());
    for (Point &p1 : poly.points)
    { // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1      = (p1 - *p0).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size;
            p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX))
        {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(*p0 + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>());
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0 = &p1;
    }
    while (out.size() < 3) {
        size_t point_idx = poly.size() - 2;
        out.emplace_back(poly[point_idx]);
        if (point_idx == 0)
            break;
        -- point_idx;
    }
    if (out.size() >= 3)
        poly.points = std::move(out);
}

using PerimeterGeneratorLoops = std::vector<PerimeterGeneratorLoop>;

static void lowpass_filter_by_paths_overhang_degree(ExtrusionPaths& paths) {
    const double filter_range = scale_(6.5);
    const double threshold_length = scale_(1.2);

    //0.save old overhang series first which is input of filter
    const int path_num = paths.size();
    if (path_num < 2)
        //don't need to do filting if only has one path in vector
        return;
    std::vector<int> old_overhang_series;
    old_overhang_series.reserve(path_num);
    for (int i = 0; i < path_num; i++)
        old_overhang_series.push_back(paths[i].get_overhang_degree());

    //1.lowpass filter
    for (int i = 0; i < path_num; i++) {
        double current_length = paths[i].length();
        int current_overhang_degree = old_overhang_series[i];
        if (current_length < threshold_length &&
            (paths[i].role() == erPerimeter || paths[i].role() == erExternalPerimeter)) {
            double left_total_length = (filter_range - current_length) / 2;
            double right_total_length = left_total_length;

            double temp_length;
            int j = i - 1;
            int index;
            std::vector<std::pair<double, int>> neighbor_path;
            while (left_total_length > 0) {
                index = (j < 0) ? path_num - 1 : j;
                if (paths[index].role() == erOverhangPerimeter)
                    break;
                temp_length = paths[index].length();
                if (temp_length > left_total_length)
                    neighbor_path.emplace_back(std::pair<double, int>(left_total_length, old_overhang_series[index]));
                else
                    neighbor_path.emplace_back(std::pair<double, int>(temp_length, old_overhang_series[index]));
                left_total_length -= temp_length;
                j = index;
                j--;
            }

            j = i + 1;
            while (right_total_length > 0) {
                index = j % path_num;
                if (paths[index].role() == erOverhangPerimeter)
                    break;
                temp_length = paths[index].length();
                if (temp_length > right_total_length) 
                    neighbor_path.emplace_back(std::pair<double, int>(right_total_length, old_overhang_series[index]));
                else
                    neighbor_path.emplace_back(std::pair<double, int>(temp_length, old_overhang_series[index]));
                right_total_length -= temp_length;
                j++;
            }

            double sum = 0;
            double length_sum = 0;
            for (auto it = neighbor_path.begin(); it != neighbor_path.end(); it++) {
                sum += (it->first * it->second);
                length_sum += it->first;
            }

            double average_overhang = (double)(current_length * current_overhang_degree + sum) / (length_sum + current_length);
            paths[i].set_overhang_degree((int)average_overhang);
        }
    }

    //2.merge path if have same overhang degree. from back to front to avoid data copy
    int last_overhang = paths[0].get_overhang_degree();
    auto it = paths.begin() + 1;
    while (it != paths.end())
    {
        if (last_overhang == it->get_overhang_degree()) {
            //BBS: don't need to append duplicated points, remove the last point
            if ((it-1)->polyline.last_point() == it->polyline.first_point())
                (it-1)->polyline.points.pop_back();
            (it-1)->polyline.append(std::move(it->polyline));
            it = paths.erase(it);
        } else {
            last_overhang = it->get_overhang_degree();
            it++;
        }
    }
}

static ExtrusionEntityCollection traverse_loops(const PerimeterGenerator &perimeter_generator, const PerimeterGeneratorLoops &loops, ThickPolylines &thin_walls)
{
    // loops is an arrayref of ::Loop objects
    // turn each one into an ExtrusionLoop object
    ExtrusionEntityCollection   coll;
    Polygon                     fuzzified;
    for (const PerimeterGeneratorLoop &loop : loops) {
        bool is_external = loop.is_external();
        bool is_small_width = loop.is_smaller_width_perimeter;
        
        ExtrusionRole role;
        ExtrusionLoopRole loop_role;
        role = is_external ? erExternalPerimeter : erPerimeter;
        if (loop.is_internal_contour()) {
            // Note that we set loop role to ContourInternalPerimeter
            // also when loop is both internal and external (i.e.
            // there's only one contour loop).
            loop_role = elrContourInternalPerimeter;
        } else {
            loop_role = elrDefault;
        }
        
        // detect overhanging/bridging perimeters
        ExtrusionPaths paths;

        // BBS: get lower polygons series, width, mm3_per_mm
        const std::map<int, Polygons> *lower_polygons_series;
        double extrusion_mm3_per_mm;
        double extrusion_width;
        if (is_external) {
            if (is_small_width) {
                //BBS: smaller width external perimeter
                lower_polygons_series = &perimeter_generator.m_smaller_external_lower_polygons_series;
                extrusion_mm3_per_mm = perimeter_generator.smaller_width_ext_mm3_per_mm();
                extrusion_width = perimeter_generator.smaller_ext_perimeter_flow.width();
            } else {
                //BBS: normal external perimeter
                lower_polygons_series = &perimeter_generator.m_external_lower_polygons_series;
                extrusion_mm3_per_mm = perimeter_generator.ext_mm3_per_mm();
                extrusion_width = perimeter_generator.ext_perimeter_flow.width();
            }
        } else {
            //BBS: normal perimeter
            lower_polygons_series = &perimeter_generator.m_lower_polygons_series;
            extrusion_mm3_per_mm = perimeter_generator.mm3_per_mm();
            extrusion_width = perimeter_generator.perimeter_flow.width();
        }


        const Polygon &polygon = loop.fuzzify ? fuzzified : loop.polygon;
        if (loop.fuzzify) {
            fuzzified = loop.polygon;
            fuzzy_polygon(fuzzified, scaled<float>(perimeter_generator.config->fuzzy_skin_thickness.value), scaled<float>(perimeter_generator.config->fuzzy_skin_point_distance.value));
        }
        if (perimeter_generator.config->detect_overhang_wall && perimeter_generator.layer_id > perimeter_generator.object_config->raft_layers) {
            // get non 100% overhang paths by intersecting this loop with the grown lower slices
            Polylines remain_polines;

            if (perimeter_generator.config->enable_overhang_speed) {
                for (auto it = lower_polygons_series->begin();
                    it != lower_polygons_series->end(); it++)
                {

                    Polylines inside_polines = (it == lower_polygons_series->begin()) ?
                        intersection_pl({ polygon }, it->second) :
                        intersection_pl_2(remain_polines, it->second);
                    extrusion_paths_append(
                        paths,
                        std::move(inside_polines),
                        it->first,
                        int(0),
                        role,
                        extrusion_mm3_per_mm,
                        extrusion_width,
                        (float)perimeter_generator.layer_height);

                    remain_polines = (it == lower_polygons_series->begin()) ?
                        diff_pl({ polygon }, it->second) :
                        diff_pl_2(remain_polines, it->second);

                    if (remain_polines.size() == 0)
                        break;
                }
            } else {
                auto it = lower_polygons_series->end();
                it--;
                Polylines inside_polines = intersection_pl({ polygon }, it->second);
                extrusion_paths_append(
                    paths,
                    std::move(inside_polines),
                    int(0),
                    int(0),
                    role,
                    extrusion_mm3_per_mm,
                    extrusion_width,
                    (float)perimeter_generator.layer_height);

                remain_polines = diff_pl({ polygon }, it->second);
            }

            // get 100% overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            if (remain_polines.size() != 0) {
                if (!((perimeter_generator.object_config->enable_support || perimeter_generator.object_config->enforce_support_layers > 0)
                    && perimeter_generator.object_config->support_top_z_distance.value == 0)) {
                    extrusion_paths_append(
                        paths,
                        std::move(remain_polines),
                        overhang_sampling_number - 1,
                        int(0),
                        erOverhangPerimeter,
                        perimeter_generator.mm3_per_mm_overhang(),
                        perimeter_generator.overhang_flow.width(),
                        perimeter_generator.overhang_flow.height());
                } else {
                    extrusion_paths_append(
                    paths,
                    std::move(remain_polines),
                    overhang_sampling_number - 1,
                    int(0),
                    role,
                    extrusion_mm3_per_mm,
                    extrusion_width,
                    (float)perimeter_generator.layer_height);
                }

            }
            
            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            chain_and_reorder_extrusion_paths(paths, &paths.front().first_point());
            // smothing the overhang degree
            // merge small path between paths which have same overhang degree
            lowpass_filter_by_paths_overhang_degree(paths);
        } else {
            ExtrusionPath path(role);
            //BBS.
            path.polyline = polygon.split_at_first_point();
            path.overhang_degree = 0;
            path.curve_degree = 0;
            path.mm3_per_mm = extrusion_mm3_per_mm;
            path.width = extrusion_width;
            path.height     = (float)perimeter_generator.layer_height;
            paths.emplace_back(std::move(path));
        }

        coll.append(ExtrusionLoop(std::move(paths), loop_role));
    }
    
    // Append thin walls to the nearest-neighbor search (only for first iteration)
    if (! thin_walls.empty()) {
        variable_width(thin_walls, erExternalPerimeter, perimeter_generator.ext_perimeter_flow, coll.entities);
        thin_walls.clear();
    }
    
    // Traverse children and build the final collection.
	Point zero_point(0, 0);
	std::vector<std::pair<size_t, bool>> chain = chain_extrusion_entities(coll.entities, &zero_point);
    ExtrusionEntityCollection out;
    for (const std::pair<size_t, bool> &idx : chain) {
		assert(coll.entities[idx.first] != nullptr);
        if (idx.first >= loops.size()) {
            // This is a thin wall.
			out.entities.reserve(out.entities.size() + 1);
            out.entities.emplace_back(coll.entities[idx.first]);
			coll.entities[idx.first] = nullptr;
            if (idx.second)
				out.entities.back()->reverse();
        } else {
            const PerimeterGeneratorLoop &loop = loops[idx.first];
            assert(thin_walls.empty());
            ExtrusionEntityCollection children = traverse_loops(perimeter_generator, loop.children, thin_walls);
            out.entities.reserve(out.entities.size() + children.entities.size() + 1);
            ExtrusionLoop *eloop = static_cast<ExtrusionLoop*>(coll.entities[idx.first]);
            coll.entities[idx.first] = nullptr;
            if (loop.is_contour) {
                eloop->make_counter_clockwise();
                out.append(std::move(children.entities));
                out.entities.emplace_back(eloop);
            } else {
                eloop->make_clockwise();
                out.entities.emplace_back(eloop);
                out.append(std::move(children.entities));
            }
        }
    }
    return out;
}

void PerimeterGenerator::process()
{
    // other perimeters
    m_mm3_per_mm               		= this->perimeter_flow.mm3_per_mm();
    coord_t perimeter_width         = this->perimeter_flow.scaled_width();
    coord_t perimeter_spacing       = this->perimeter_flow.scaled_spacing();
    
    // external perimeters
    m_ext_mm3_per_mm           		= this->ext_perimeter_flow.mm3_per_mm();
    coord_t ext_perimeter_width     = this->ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing   = this->ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2  = scaled<coord_t>(0.5f * (this->ext_perimeter_flow.spacing() + this->perimeter_flow.spacing()));
    
    // overhang perimeters
    m_mm3_per_mm_overhang      		= this->overhang_flow.mm3_per_mm();
    
    // solid infill
    coord_t solid_infill_spacing    = this->solid_infill_flow.scaled_spacing();
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_perimeter_spacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_perimeter_spacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = coord_t(perimeter_spacing      * (1 - INSET_OVERLAP_TOLERANCE));
    coord_t ext_min_spacing     = coord_t(ext_perimeter_spacing  * (1 - INSET_OVERLAP_TOLERANCE));
    bool    has_gap_fill 		= this->config->gap_infill_speed.value > 0;

    // BBS: this flow is for smaller external perimeter for small area
    coord_t ext_min_spacing_smaller = coord_t(ext_perimeter_spacing * (1 - SMALLER_EXT_INSET_OVERLAP_TOLERANCE));
    this->smaller_ext_perimeter_flow = this->ext_perimeter_flow;
    // BBS: to be checked
    this->smaller_ext_perimeter_flow = this->smaller_ext_perimeter_flow.with_width(SCALING_FACTOR *
        (ext_perimeter_width - 0.5 * SMALLER_EXT_INSET_OVERLAP_TOLERANCE * ext_perimeter_spacing));
    m_ext_mm3_per_mm_smaller_width = this->smaller_ext_perimeter_flow.mm3_per_mm();

    // prepare grown lower layer slices for overhang detection
    m_lower_polygons_series = generate_lower_polygons_series(this->perimeter_flow.width());
    if (ext_perimeter_width == perimeter_width)
        m_external_lower_polygons_series = m_lower_polygons_series;
    else
        m_external_lower_polygons_series = generate_lower_polygons_series(this->ext_perimeter_flow.width());
    m_smaller_external_lower_polygons_series = generate_lower_polygons_series(this->smaller_ext_perimeter_flow.width());

    // we need to process each island separately because we might have different
    // extra perimeters for each one

    // BBS: don't simplify too much which influence arc fitting when export gcode if arc_fitting is enabled
    double surface_simplify_resolution = (print_config->enable_arc_fitting) ? 0.1 * m_scaled_resolution : m_scaled_resolution;
    for (const Surface &surface : this->slices->surfaces) {
        // detect how many perimeters must be generated for this island
        int        loop_number = this->config->wall_loops + surface.extra_perimeters - 1;  // 0-indexed loops
        //BBS: set the topmost layer to be one wall
        if (loop_number > 0 && config->only_one_wall_top && this->upper_slices == nullptr)
            loop_number = 0;

        ExPolygons last        = union_ex(surface.expolygon.simplify_p(surface_simplify_resolution));
        ExPolygons gaps;
        ExPolygons top_fills;
        ExPolygons fill_clip;
        if (loop_number >= 0) {
            // In case no perimeters are to be generated, loop_number will equal to -1.
            std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
            std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
            ThickPolylines thin_walls;
            // we loop one time more than needed in order to find gaps after the last perimeter was applied
            for (int i = 0;; ++ i) {  // outer loop is 0
                // Calculate next onion shell of perimeters.
                ExPolygons offsets;
                ExPolygons offsets_with_smaller_width;
                if (i == 0) {
                    // look for thin walls
                    if (this->config->detect_thin_wall) {
                        // the minimum thickness of a single loop is:
                        // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                        offsets = offset2_ex(last,
                            -float(ext_perimeter_width / 2. + ext_min_spacing / 2. - 1),
                            +float(ext_min_spacing / 2. - 1));
                        // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                        // (actually, something larger than that still may exist due to mitering or other causes)
                        coord_t min_width = coord_t(scale_(this->ext_perimeter_flow.nozzle_diameter() / 3));
                        ExPolygons expp = opening_ex(
                            // medial axis requires non-overlapping geometry
                            diff_ex(last, offset(offsets, float(ext_perimeter_width / 2.) + ClipperSafetyOffset)),
                            float(min_width / 2.));
                        // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                        for (ExPolygon &ex : expp)
                            ex.medial_axis(ext_perimeter_width + ext_perimeter_spacing2, min_width, &thin_walls);
                    } else {
                        coord_t ext_perimeter_smaller_width = this->smaller_ext_perimeter_flow.scaled_width();
                        for (const ExPolygon& expolygon : last) {
                            // BBS: judge whether it's narrow but not too long island which is hard to place two line
                            ExPolygons expolys;
                            expolys.push_back(expolygon);
                            ExPolygons offset_result = offset2_ex(expolys,
                                -float(ext_perimeter_width / 2. + ext_min_spacing_smaller / 2.),
                                +float(ext_min_spacing_smaller / 2.));
                            if (offset_result.empty() &&
                                expolygon.area() < (double)(ext_perimeter_width + ext_min_spacing_smaller) * scale_(narrow_loop_length_threshold)) {
                                // BBS: for narrow external loop, use smaller line width
                                ExPolygons temp_result = offset_ex(expolygon, -float(ext_perimeter_smaller_width / 2.));
                                offsets_with_smaller_width.insert(offsets_with_smaller_width.end(), temp_result.begin(), temp_result.end());
                            }
                            else {
                                //BBS: for not narrow loop, use normal external perimeter line width
                                ExPolygons temp_result = offset_ex(expolygon, -float(ext_perimeter_width / 2.));
                                offsets.insert(offsets.end(), temp_result.begin(), temp_result.end());
                            }
                        }
                    }
                    if (m_spiral_vase && (offsets.size() > 1 || offsets_with_smaller_width.size() > 1)) {
                        // Remove all but the largest area polygon.
                        keep_largest_contour_only(offsets);
                        //BBS
                        if (offsets.empty())
                            //BBS: only have small width loop, then keep the largest in spiral vase mode
                            keep_largest_contour_only(offsets_with_smaller_width);
                        else
                            //BBS: have large area, clean the small width loop
                            offsets_with_smaller_width.clear();
                    }
                } else {
                    //FIXME Is this offset correct if the line width of the inner perimeters differs
                    // from the line width of the infill?
                    coord_t distance = (i == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
                    //BBS
                    //offsets = this->config->thin_walls ?
                        // This path will ensure, that the perimeters do not overfill, as in 
                        // prusa3d/Slic3r GH #32, but with the cost of rounding the perimeters
                        // excessively, creating gaps, which then need to be filled in by the not very 
                        // reliable gap fill algorithm.
                        // Also the offset2(perimeter, -x, x) may sometimes lead to a perimeter, which is larger than
                        // the original.
                        //offset2_ex(last,
                        //        - float(distance + min_spacing / 2. - 1.),
                        //        float(min_spacing / 2. - 1.)) :
                        // If "detect thin walls" is not enabled, this paths will be entered, which 
                        // leads to overflows, as in prusa3d/Slic3r GH #32
                        //offset_ex(last, - float(distance));

                    //BBS: For internal perimeter, we should "enable" thin wall strategy in which offset2 is used to
                    // remove too closed line, so that gap fill can be used for such internal narrow area in following
                    // handling.
                    offsets = offset2_ex(last,
                        -float(distance + min_spacing / 2. - 1.),
                        float(min_spacing / 2. - 1.));
                    // look for gaps
                    if (has_gap_fill)
                        // not using safety offset here would "detect" very narrow gaps
                        // (but still long enough to escape the area threshold) that gap fill
                        // won't be able to fill but we'd still remove from infill area
                        append(gaps, diff_ex(
                            offset(last,    - float(0.5 * distance)),
                            offset(offsets,   float(0.5 * distance + 10))));  // safety offset
                }
                if (offsets.empty() && offsets_with_smaller_width.empty()) {
                    // Store the number of loops actually generated.
                    loop_number = i - 1;
                    // No region left to be filled in.
                    last.clear();
                    break;
                } else if (i > loop_number) {
                    // If i > loop_number, we were looking just for gaps.
                    break;
                }
                {
                    const bool fuzzify_contours = this->config->fuzzy_skin != FuzzySkinType::None && i == 0 && this->layer_id > 0;
                    const bool fuzzify_holes = fuzzify_contours && this->config->fuzzy_skin == FuzzySkinType::All;
                    for (const ExPolygon& expolygon : offsets) {
                        // Outer contour may overlap with an inner contour,
                        // inner contour may overlap with another inner contour,
                        // outer contour may overlap with itself.
                        //FIXME evaluate the overlaps, annotate each point with an overlap depth,
                        // compensate for the depth of intersection.
                        contours[i].emplace_back(expolygon.contour, i, true, fuzzify_contours);

                        if (!expolygon.holes.empty()) {
                            holes[i].reserve(holes[i].size() + expolygon.holes.size());
                            for (const Polygon& hole : expolygon.holes)
                                holes[i].emplace_back(hole, i, false, fuzzify_holes);
                        }
                    }

                    //BBS: save perimeter loop which use smaller width
                    if (i == 0) {
                        for (const ExPolygon& expolygon : offsets_with_smaller_width) {
                            contours[i].emplace_back(PerimeterGeneratorLoop(expolygon.contour, i, true, fuzzify_contours, true));
                            if (!expolygon.holes.empty()) {
                                holes[i].reserve(holes[i].size() + expolygon.holes.size());
                                for (const Polygon& hole : expolygon.holes)
                                    holes[i].emplace_back(PerimeterGeneratorLoop(hole, i, false, fuzzify_contours, true));
                            }
                        }
                    }
                }

                last = std::move(offsets);

                //BBS: refer to superslicer
                //store surface for top infill if only_one_wall_top
                if (i == 0 && config->only_one_wall_top && this->upper_slices != NULL) {
                    //split the polygons with top/not_top
                    //get the offset from solid surface anchor
                    coord_t offset_top_surface = scale_(1.5 * (config->wall_loops.value == 0 ? 0. : unscaled(double(ext_perimeter_width + perimeter_spacing * int(int(config->wall_loops.value) - int(1))))));
                    // if possible, try to not push the extra perimeters inside the sparse infill
                    if (offset_top_surface > 0.9 * (config->wall_loops.value <= 1 ? 0. : (perimeter_spacing * (config->wall_loops.value - 1))))
                        offset_top_surface -= coord_t(0.9 * (config->wall_loops.value <= 1 ? 0. : (perimeter_spacing * (config->wall_loops.value - 1))));
                    else
                        offset_top_surface = 0;
                    //don't takes into account too thin areas
                    double min_width_top_surface = std::max(double(ext_perimeter_spacing / 2 + 10), 1.0 * (double(perimeter_width)));
                    ExPolygons grown_upper_slices = offset_ex(*this->upper_slices, min_width_top_surface);
                    //set the clip to a virtual "second perimeter"
                    fill_clip = offset_ex(last, -double(ext_perimeter_spacing));
                    // get the real top surface
                    ExPolygons grown_lower_slices;
                    ExPolygons bridge_checker;
                    // BBS: check whether surface be bridge or not
                    if (this->lower_slices != NULL) {
                        grown_lower_slices =*this->lower_slices;
                        double bridge_offset = std::max(double(ext_perimeter_spacing), (double(perimeter_width)));
                        bridge_checker       = offset_ex(diff_ex(last, grown_lower_slices, ApplySafetyOffset::Yes), 1.5 * bridge_offset);
                    }
                    ExPolygons delete_bridge = diff_ex(last, bridge_checker, ApplySafetyOffset::Yes);

                    ExPolygons top_polygons = diff_ex(delete_bridge, grown_upper_slices, ApplySafetyOffset::Yes);

                    //get the not-top surface, from the "real top" but enlarged by external_infill_margin (and the min_width_top_surface we removed a bit before)
                    ExPolygons inner_polygons = diff_ex(last,
                                                        offset_ex(top_polygons, offset_top_surface + min_width_top_surface - double(ext_perimeter_spacing / 2)),
                                                        ApplySafetyOffset::Yes);
                    // get the enlarged top surface, by using inner_polygons instead of upper_slices, and clip it for it to be exactly the polygons to fill.
                    top_polygons = diff_ex(fill_clip, inner_polygons, ApplySafetyOffset::Yes);
                    // increase by half peri the inner space to fill the frontier between last and stored.
                    top_fills = union_ex(top_fills, top_polygons);
                    //set the clip to the external wall but go back inside by infill_extrusion_width/2 to be sure the extrusion won't go outside even with a 100% overlap.
                    double infill_spacing_unscaled = this->config->sparse_infill_line_width.value;
                    fill_clip = offset_ex(last, double(ext_perimeter_spacing / 2) - scale_(infill_spacing_unscaled / 2));
                    last = intersection_ex(inner_polygons, last);
                    //{
                    //    std::stringstream stri;
                    //    stri << this->layer->id() << "_1_"<< i <<"_only_one_peri"<< ".svg";
                    //    SVG svg(stri.str());
                    //    svg.draw(to_polylines(top_fills), "green");
                    //    svg.draw(to_polylines(inner_polygons), "yellow");
                    //    svg.draw(to_polylines(top_polygons), "cyan");
                    //    svg.draw(to_polylines(oldLast), "orange");
                    //    svg.draw(to_polylines(last), "red");
                    //    svg.Close();
                    //}
                }

                if (i == loop_number && (! has_gap_fill || this->config->sparse_infill_density.value == 0)) {
                	// The last run of this loop is executed to collect gaps for gap fill.
                	// As the gap fill is either disabled or not 
                	break;
                }
            }

            // nest loops: holes first
            for (int d = 0; d <= loop_number; ++ d) {
                PerimeterGeneratorLoops &holes_d = holes[d];
                // loop through all holes having depth == d
                for (int i = 0; i < (int)holes_d.size(); ++ i) {
                    const PerimeterGeneratorLoop &loop = holes_d[i];
                    // find the hole loop that contains this one, if any
                    for (int t = d + 1; t <= loop_number; ++ t) {
                        for (int j = 0; j < (int)holes[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = holes[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                -- i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    // if no hole contains this hole, find the contour loop that contains it
                    for (int t = loop_number; t >= 0; -- t) {
                        for (int j = 0; j < (int)contours[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                -- i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    NEXT_LOOP: ;
                }
            }
            // nest contour loops
            for (int d = loop_number; d >= 1; -- d) {
                PerimeterGeneratorLoops &contours_d = contours[d];
                // loop through all contours having depth == d
                for (int i = 0; i < (int)contours_d.size(); ++ i) {
                    const PerimeterGeneratorLoop &loop = contours_d[i];
                    // find the contour loop that contains it
                    for (int t = d - 1; t >= 0; -- t) {
                        for (size_t j = 0; j < contours[t].size(); ++ j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                contours_d.erase(contours_d.begin() + i);
                                -- i;
                                goto NEXT_CONTOUR;
                            }
                        }
                    }
                    NEXT_CONTOUR: ;
                }
            }
            // at this point, all loops should be in contours[0]
            ExtrusionEntityCollection entities = traverse_loops(*this, contours.front(), thin_walls);
            // if brim will be printed, reverse the order of perimeters so that
            // we continue inwards after having finished the brim
            // TODO: add test for perimeter order
            bool is_outer_wall_first = 
                this->print_config->wall_infill_order == WallInfillOrder::OuterInnerInfill ||
                this->print_config->wall_infill_order == WallInfillOrder::InfillOuterInner;
            if (is_outer_wall_first ||
                //BBS: always print outer wall first when there indeed has brim.
                (this->layer_id == 0 &&
                 this->object_config->brim_type == BrimType::btOuterOnly &&
                 this->object_config->brim_width.value > 0))
                entities.reverse();
            // append perimeters for this slice as a collection
            if (! entities.empty())
                this->loops->append(entities);
        } // for each loop of an island

        // fill gaps
        if (! gaps.empty()) {
            // collapse 
            double min = 0.2 * perimeter_width * (1 - INSET_OVERLAP_TOLERANCE);
            double max = 2. * perimeter_spacing;
            ExPolygons gaps_ex = diff_ex(
                //FIXME offset2 would be enough and cheaper.
                opening_ex(gaps, float(min / 2.)),
                offset2_ex(gaps, - float(max / 2.), float(max / 2. + ClipperSafetyOffset)));
            ThickPolylines polylines;
            for (ExPolygon& ex : gaps_ex) {
                //BBS: Use DP simplify to avoid duplicated points and accelerate medial-axis calculation as well.
                ex.douglas_peucker(surface_simplify_resolution);
                ex.medial_axis(max, min, &polylines);
            }

#ifdef GAPS_OF_PERIMETER_DEBUG_TO_SVG
            {
                static int irun = 0;
                BoundingBox bbox_svg;
                bbox_svg.merge(get_extents(gaps_ex));
                {
                    std::stringstream stri;
                    stri << "debug_gaps_ex_" << irun << ".svg";
                    SVG svg(stri.str(), bbox_svg);
                    svg.draw(to_polylines(gaps_ex), "blue", 0.5);
                    svg.Close();
                }
                ++ irun;
            }
#endif

            if (! polylines.empty()) {
				ExtrusionEntityCollection gap_fill;
				variable_width(polylines, erGapFill, this->solid_infill_flow, gap_fill.entities);
                /*  Make sure we don't infill narrow parts that are already gap-filled
                    (we only consider this surface's gaps to reduce the diff() complexity).
                    Growing actual extrusions ensures that gaps not filled by medial axis
                    are not subtracted from fill surfaces (they might be too short gaps
                    that medial axis skips but infill might join with other infill regions
                    and use zigzag).  */
                //FIXME Vojtech: This grows by a rounded extrusion width, not by line spacing,
                // therefore it may cover the area, but no the volume.
                last = diff_ex(last, gap_fill.polygons_covered_by_width(10.f));
				this->gap_fill->append(std::move(gap_fill.entities));
			}
        }

        // create one more offset to be used as boundary for fill
        // we offset by half the perimeter spacing (to get to the actual infill boundary)
        // and then we offset back and forth by half the infill spacing to only consider the
        // non-collapsing regions
        coord_t inset = 
            (loop_number < 0) ? 0 :
            (loop_number == 0) ?
                // one loop
                ext_perimeter_spacing / 2 :
                // two or more loops?
                perimeter_spacing / 2;
        // only apply infill overlap if we actually have one perimeter
        coord_t infill_peri_overlap = 0;
        if (inset > 0) {
            infill_peri_overlap = coord_t(scale_(this->config->infill_wall_overlap.get_abs_value(unscale<double>(inset + solid_infill_spacing / 2))));
            inset -= infill_peri_overlap;
        }
        // simplify infill contours according to resolution
        Polygons pp;
        for (ExPolygon &ex : last)
            ex.simplify_p(m_scaled_resolution, &pp);
        ExPolygons not_filled_exp = union_ex(pp);
        // collapse too narrow infill areas
        coord_t min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));

        ExPolygons infill_exp = offset2_ex(
            not_filled_exp,
            float(-inset - min_perimeter_infill_spacing / 2.),
            float(min_perimeter_infill_spacing / 2.));
        // append infill areas to fill_surfaces
        //if any top_fills, grow them by ext_perimeter_spacing/2 to have the real un-anchored fill
        ExPolygons top_infill_exp = intersection_ex(fill_clip, offset_ex(top_fills, double(ext_perimeter_spacing / 2)));
        if (!top_fills.empty()) {
            infill_exp = union_ex(infill_exp, offset_ex(top_infill_exp, double(infill_peri_overlap)));
        }
        this->fill_surfaces->append(infill_exp, stInternal);

        // BBS: get the no-overlap infill expolygons
        {
            ExPolygons polyWithoutOverlap;
            if (min_perimeter_infill_spacing / 2 > infill_peri_overlap)
                polyWithoutOverlap = offset2_ex(
                    not_filled_exp,
                    float(-inset - min_perimeter_infill_spacing / 2.),
                    float(min_perimeter_infill_spacing / 2 - infill_peri_overlap));
            else
                polyWithoutOverlap = offset_ex(
                    not_filled_exp,
                    double(-inset - infill_peri_overlap));
            if (!top_fills.empty())
                polyWithoutOverlap = union_ex(polyWithoutOverlap, top_infill_exp);
            this->fill_no_overlap->insert(this->fill_no_overlap->end(), polyWithoutOverlap.begin(), polyWithoutOverlap.end());
        }

    } // for each island
}

bool PerimeterGeneratorLoop::is_internal_contour() const
{
    // An internal contour is a contour containing no other contours
    if (! this->is_contour)
        return false;
    for (const PerimeterGeneratorLoop &loop : this->children)
        if (loop.is_contour)
            return false;
    return true;
}

std::map<int, Polygons> PerimeterGenerator::generate_lower_polygons_series(float width)
{
    float nozzle_diameter = print_config->nozzle_diameter.get_at(config->wall_filament - 1);
    float start_offset = -0.5 * width;
    float end_offset = 0.5 * nozzle_diameter;

    assert(overhang_sampling_number >= 3);
    // generate offsets
    std::vector<float> offset_series;
    offset_series.reserve(overhang_sampling_number - 1);
    for (int i = 0; i < overhang_sampling_number - 1; i++) {
        offset_series.push_back(start_offset + (i + 0.5) * (end_offset - start_offset) / (overhang_sampling_number - 1));
    }
    // BBS: increase start_offset a little to avoid to calculate 90 degree as overhang
    offset_series[0] = start_offset + 0.5 * (end_offset - start_offset) / (overhang_sampling_number - 1);
    offset_series[overhang_sampling_number - 2] = end_offset;

    std::map<int, Polygons> lower_polygons_series;
    if (this->lower_slices == NULL) {
        return lower_polygons_series;
    }

    // offset expolygon to generate series of polygons
    for (int i = 0; i < offset_series.size(); i++) {
        lower_polygons_series.insert(std::pair<int, Polygons>(i, offset(*this->lower_slices, float(scale_(offset_series[i])))));
    }
    return lower_polygons_series;
}

}
