#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ShortestPath.hpp"
#include "VariableWidth.hpp"
#include "CurveAnalyzer.hpp"
#include "Clipper2Utils.hpp"
#include "Arachne/WallToolPaths.hpp"

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

// Thanks Cura developers for this function.
static void fuzzy_extrusion_line(Arachne::ExtrusionLine& ext_lines, double fuzzy_skin_thickness, double fuzzy_skin_point_dist)
{
    const double min_dist_between_points = fuzzy_skin_point_dist * 3. / 4.; // hardcoded: the point distance may vary between 3/4 and 5/4 the supplied value
    const double range_random_point_dist = fuzzy_skin_point_dist / 2.;
    double       dist_left_over = double(rand()) * (min_dist_between_points / 2) / double(RAND_MAX); // the distance to be traversed on the line before making the first new point

    auto* p0 = &ext_lines.front();
    std::vector<Arachne::ExtrusionJunction> out;
    out.reserve(ext_lines.size());
    for (auto& p1 : ext_lines) {
        if (p0->p == p1.p) { // Connect endpoints.
            out.emplace_back(p1.p, p1.w, p1.perimeter_index);
            continue;
        }

        // 'a' is the (next) new point between p0 and p1
        Vec2d  p0p1 = (p1.p - p0->p).cast<double>();
        double p0p1_size = p0p1.norm();
        // so that p0p1_size - dist_last_point evaulates to dist_left_over - p0p1_size
        double dist_last_point = dist_left_over + p0p1_size * 2.;
        for (double p0pa_dist = dist_left_over; p0pa_dist < p0p1_size; p0pa_dist += min_dist_between_points + double(rand()) * range_random_point_dist / double(RAND_MAX)) {
            double r = double(rand()) * (fuzzy_skin_thickness * 2.) / double(RAND_MAX) - fuzzy_skin_thickness;
            out.emplace_back(p0->p + (p0p1 * (p0pa_dist / p0p1_size) + perp(p0p1).cast<double>().normalized() * r).cast<coord_t>(), p1.w, p1.perimeter_index);
            dist_last_point = p0pa_dist;
        }
        dist_left_over = p0p1_size - dist_last_point;
        p0 = &p1;
    }

    while (out.size() < 3) {
        size_t point_idx = ext_lines.size() - 2;
        out.emplace_back(ext_lines[point_idx].p, ext_lines[point_idx].w, ext_lines[point_idx].perimeter_index);
        if (point_idx == 0)
            break;
        --point_idx;
    }

    if (ext_lines.back().p == ext_lines.front().p) // Connect endpoints.
        out.front().p = out.back().p;

    if (out.size() >= 3)
        ext_lines.junctions = std::move(out);
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
            // prepare grown lower layer slices for overhang detection
            BoundingBox bbox(polygon.points);
            bbox.offset(SCALED_EPSILON);

            Polylines remain_polines;

            //BBS: don't calculate overhang degree when enable fuzzy skin. It's unmeaning
            if (perimeter_generator.config->enable_overhang_speed && perimeter_generator.config->fuzzy_skin == FuzzySkinType::None) {
                for (auto it = lower_polygons_series->begin();
                    it != lower_polygons_series->end(); it++)
                {
                    Polygons lower_polygons_series_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(it->second, bbox);

                    Polylines inside_polines = (it == lower_polygons_series->begin()) ? intersection_pl({polygon}, lower_polygons_series_clipped) :
                                                                                        intersection_pl(remain_polines, lower_polygons_series_clipped);
                    extrusion_paths_append(
                        paths,
                        std::move(inside_polines),
                        it->first,
                        int(0),
                        role,
                        extrusion_mm3_per_mm,
                        extrusion_width,
                        (float)perimeter_generator.layer_height);

                    remain_polines = (it == lower_polygons_series->begin()) ? diff_pl({polygon}, lower_polygons_series_clipped) :
                                                                              diff_pl(remain_polines, lower_polygons_series_clipped);

                    if (remain_polines.size() == 0)
                        break;
                }
            } else {
                auto it = lower_polygons_series->end();
                it--;
                Polygons lower_polygons_series_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(it->second, bbox);

                Polylines inside_polines = intersection_pl({polygon}, lower_polygons_series_clipped);
                extrusion_paths_append(
                    paths,
                    std::move(inside_polines),
                    int(0),
                    int(0),
                    role,
                    extrusion_mm3_per_mm,
                    extrusion_width,
                    (float)perimeter_generator.layer_height);

                remain_polines = diff_pl({polygon}, lower_polygons_series_clipped);
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

static ClipperLib_Z::Paths clip_extrusion(const ClipperLib_Z::Path& subject, const ClipperLib_Z::Paths& clip, ClipperLib_Z::ClipType clipType)
{
    ClipperLib_Z::Clipper clipper;
    clipper.ZFillFunction([](const ClipperLib_Z::IntPoint& e1bot, const ClipperLib_Z::IntPoint& e1top, const ClipperLib_Z::IntPoint& e2bot,
        const ClipperLib_Z::IntPoint& e2top, ClipperLib_Z::IntPoint& pt) {
            // The clipping contour may be simplified by clipping it with a bounding box of "subject" path.
            // The clipping function used may produce self intersections outside of the "subject" bounding box. Such self intersections are 
            // harmless to the result of the clipping operation,
            // Both ends of each edge belong to the same source: Either they are from subject or from clipping path.
            assert(e1bot.z() >= 0 && e1top.z() >= 0);
            assert(e2bot.z() >= 0 && e2top.z() >= 0);
            assert((e1bot.z() == 0) == (e1top.z() == 0));
            assert((e2bot.z() == 0) == (e2top.z() == 0));

            // Start & end points of the clipped polyline (extrusion path with a non-zero width).
            ClipperLib_Z::IntPoint start = e1bot;
            ClipperLib_Z::IntPoint end = e1top;
            if (start.z() <= 0 && end.z() <= 0) {
                start = e2bot;
                end = e2top;
            }

            if (start.z() <= 0 && end.z() <= 0) {
                // Self intersection on the source contour.
                assert(start.z() == 0 && end.z() == 0);
                pt.z() = 0;
            }
            else {
                // Interpolate extrusion line width.
                assert(start.z() > 0 && end.z() > 0);

                double length_sqr = (end - start).cast<double>().squaredNorm();
                double dist_sqr = (pt - start).cast<double>().squaredNorm();
                double t = std::sqrt(dist_sqr / length_sqr);

                pt.z() = start.z() + coord_t((end.z() - start.z()) * t);
            }
        });

    clipper.AddPath(subject, ClipperLib_Z::ptSubject, false);
    clipper.AddPaths(clip, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::PolyTree clipped_polytree;
    ClipperLib_Z::Paths    clipped_paths;
    clipper.Execute(clipType, clipped_polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);
    ClipperLib_Z::PolyTreeToPaths(clipped_polytree, clipped_paths);

    // Clipped path could contain vertices from the clip with a Z coordinate equal to zero.
    // For those vertices, we must assign value based on the subject.
    // This happens only in sporadic cases.
    for (ClipperLib_Z::Path& path : clipped_paths)
        for (ClipperLib_Z::IntPoint& c_pt : path)
            if (c_pt.z() == 0) {
                // Now we must find the corresponding line on with this point is located and compute line width (Z coordinate).
                if (subject.size() <= 2)
                    continue;

                const Point pt(c_pt.x(), c_pt.y());
                Point       projected_pt_min;
                auto        it_min = subject.begin();
                auto        dist_sqr_min = std::numeric_limits<double>::max();
                Point       prev(subject.front().x(), subject.front().y());
                for (auto it = std::next(subject.begin()); it != subject.end(); ++it) {
                    Point curr(it->x(), it->y());
                    Point projected_pt = pt.projection_onto(Line(prev, curr));
                    if (double dist_sqr = (projected_pt - pt).cast<double>().squaredNorm(); dist_sqr < dist_sqr_min) {
                        dist_sqr_min = dist_sqr;
                        projected_pt_min = projected_pt;
                        it_min = std::prev(it);
                    }
                    prev = curr;
                }

                assert(dist_sqr_min <= SCALED_EPSILON);
                assert(std::next(it_min) != subject.end());

                const Point  pt_a(it_min->x(), it_min->y());
                const Point  pt_b(std::next(it_min)->x(), std::next(it_min)->y());
                const double line_len = (pt_b - pt_a).cast<double>().norm();
                const double dist = (projected_pt_min - pt_a).cast<double>().norm();
                c_pt.z() = coord_t(double(it_min->z()) + (dist / line_len) * double(std::next(it_min)->z() - it_min->z()));
            }

    assert([&clipped_paths = std::as_const(clipped_paths)]() -> bool {
        for (const ClipperLib_Z::Path& path : clipped_paths)
            for (const ClipperLib_Z::IntPoint& pt : path)
                if (pt.z() <= 0)
                    return false;
        return true;
    }());

    return clipped_paths;
}

struct PerimeterGeneratorArachneExtrusion
{
    Arachne::ExtrusionLine* extrusion = nullptr;
    // Indicates if closed ExtrusionLine is a contour or a hole. Used it only when ExtrusionLine is a closed loop.
    bool is_contour = false;
    // Should this extrusion be fuzzyfied on path generation?
    bool fuzzify = false;
};


static void smooth_overhang_level(ExtrusionPaths &paths)
{
    const double threshold_length = scale_(0.8);
    const double       filter_range     = scale_(6.5);

    // 0.save old overhang series first which is input of filter
    const int path_num = paths.size();
    if (path_num < 2)
        // don't need to do filting if only has one path in vector
        return;
    std::vector<int> old_overhang_series;
    old_overhang_series.reserve(path_num);
    for (int i = 0; i < path_num; i++) old_overhang_series.push_back(paths[i].get_overhang_degree());

    for (int i = 0; i < path_num;) {
        if ((paths[i].role() != erPerimeter && paths[i].role() != erExternalPerimeter)) {
            i++;
            continue;
        }

        double current_length          = paths[i].length();
        int    current_overhang_degree = old_overhang_series[i];
        double total_lens              = current_length;
        int    pt                      = i + 1;

        for (; pt < path_num; pt++) {
            if (paths[pt].get_overhang_degree() != current_overhang_degree || (paths[pt].role() != erPerimeter && paths[pt].role() != erExternalPerimeter)) {
                break;
            }
            total_lens += paths[pt].length();
        }

        if (total_lens < threshold_length) {
            double left_total_length  = (filter_range - total_lens) / 2;
            double right_total_length = left_total_length;

            double                              temp_length;
            int                                 j = i - 1;
            int                                 index;
            std::vector<std::pair<double, int>> neighbor_path;
            while (left_total_length > 0) {
                index = (j < 0) ? path_num - 1 : j;
                if (paths[index].role() == erOverhangPerimeter) break;
                temp_length = paths[index].length();
                if (temp_length > left_total_length)
                    neighbor_path.emplace_back(std::pair<double, int>(left_total_length, old_overhang_series[index]));
                else
                    neighbor_path.emplace_back(std::pair<double, int>(temp_length, old_overhang_series[index]));
                left_total_length -= temp_length;
                j = index;
                j--;
            }

            j = pt;
            while (right_total_length > 0) {
                index = j % path_num;
                if (paths[index].role() == erOverhangPerimeter) break;
                temp_length = paths[index].length();
                if (temp_length > right_total_length)
                    neighbor_path.emplace_back(std::pair<double, int>(right_total_length, old_overhang_series[index]));
                else
                    neighbor_path.emplace_back(std::pair<double, int>(temp_length, old_overhang_series[index]));
                right_total_length -= temp_length;
                j++;
            }

            double sum        = 0;
            double length_sum = 0;
            for (auto it = neighbor_path.begin(); it != neighbor_path.end(); it++) {
                sum += (it->first * it->second);
                length_sum += it->first;
            }

            double average_overhang = (double) (total_lens * current_overhang_degree + sum) / (length_sum + total_lens);

            for (int idx=i; idx<pt;idx++)
                paths[idx].set_overhang_degree((int) average_overhang);
        }

        i = pt;
    }
}

static ExtrusionEntityCollection traverse_extrusions(const PerimeterGenerator& perimeter_generator, std::vector<PerimeterGeneratorArachneExtrusion>& pg_extrusions)
{
    ExtrusionEntityCollection extrusion_coll;
    for (PerimeterGeneratorArachneExtrusion& pg_extrusion : pg_extrusions) {
        Arachne::ExtrusionLine* extrusion = pg_extrusion.extrusion;
        if (extrusion->empty())
            continue;

        const bool    is_external = extrusion->inset_idx == 0;
        ExtrusionRole role = is_external ? erExternalPerimeter : erPerimeter;

        if (pg_extrusion.fuzzify)
            fuzzy_extrusion_line(*extrusion, scaled<float>(perimeter_generator.config->fuzzy_skin_thickness.value), scaled<float>(perimeter_generator.config->fuzzy_skin_point_distance.value));

        ExtrusionPaths paths;
        // detect overhanging/bridging perimeters
        if (perimeter_generator.config->detect_overhang_wall && perimeter_generator.layer_id > perimeter_generator.object_config->raft_layers
            && !((perimeter_generator.object_config->enable_support || perimeter_generator.object_config->enforce_support_layers > 0) &&
                perimeter_generator.object_config->support_top_z_distance.value == 0)) {
            ClipperLib_Z::Path extrusion_path;
            extrusion_path.reserve(extrusion->size());
            BoundingBox extrusion_path_bbox;
            for (const Arachne::ExtrusionJunction &ej : extrusion->junctions) {
                extrusion_path.emplace_back(ej.p.x(), ej.p.y(), ej.w);
                extrusion_path_bbox.merge(Point(ej.p.x(), ej.p.y()));
            }

            ClipperLib_Z::Paths lower_slices_paths;
            {
                lower_slices_paths.reserve(perimeter_generator.lower_slices_polygons().size());
                Points clipped;
                extrusion_path_bbox.offset(SCALED_EPSILON);
                for (const Polygon &poly : perimeter_generator.lower_slices_polygons()) {
                    clipped.clear();
                    ClipperUtils::clip_clipper_polygon_with_subject_bbox(poly.points, extrusion_path_bbox, clipped);
                    if (!clipped.empty()) {
                        lower_slices_paths.emplace_back();
                        ClipperLib_Z::Path &out = lower_slices_paths.back();
                        out.reserve(clipped.size());
                        for (const Point &pt : clipped)
                          out.emplace_back(pt.x(), pt.y(), 0);
                    }
                }
            }

            ExtrusionPaths temp_paths;
            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(temp_paths, clip_extrusion(extrusion_path, lower_slices_paths, ClipperLib_Z::ctIntersection), role,
                                   is_external ? perimeter_generator.ext_perimeter_flow : perimeter_generator.perimeter_flow);

            if (perimeter_generator.config->enable_overhang_speed && perimeter_generator.config->fuzzy_skin == FuzzySkinType::None) {

                Flow flow = is_external ? perimeter_generator.ext_perimeter_flow : perimeter_generator.perimeter_flow;
                std::map<double, std::vector<Polygons>> clipper_serise;

                std::map<double,ExtrusionPaths> recognization_paths;
                for (const ExtrusionPath &path : temp_paths) {
                    if (recognization_paths.count(path.width))
                        recognization_paths[path.width].emplace_back(std::move(path));
                    else
                        recognization_paths.insert(std::pair<double, ExtrusionPaths>(path.width, {std::move(path)}));
                }

                for (const auto &it : recognization_paths) {
                    Polylines be_clipped;

                    for (const ExtrusionPath &p : it.second) {
                        be_clipped.emplace_back(std::move(p.polyline));
                    }

                    BoundingBox extrusion_bboxs = get_extents(be_clipped);
                    //ExPolygons lower_slcier_chopped = *perimeter_generator.lower_slices;
                    Polygons lower_slcier_chopped=ClipperUtils::clip_clipper_polygons_with_subject_bbox(*perimeter_generator.lower_slices, extrusion_bboxs, true);

                    double start_pos = -it.first * 0.5;
                    double end_pos   = 0.5 * it.first;

                    Polylines             remain_polylines;
                    std::vector<Polygons> degree_polygons;
                    for (int j = 0; j < overhang_sampling_number; j++) {
                        Polygons  limiton_polygons = offset(lower_slcier_chopped, float(scale_(start_pos + (j + 0.5) * (end_pos - start_pos) / (overhang_sampling_number - 1))));

                        Polylines inside_polines   = j == 0 ? intersection_pl(be_clipped, limiton_polygons) : intersection_pl(remain_polylines, limiton_polygons);

                        remain_polylines           = j == 0 ? diff_pl(be_clipped, limiton_polygons) : diff_pl(remain_polylines, limiton_polygons);

                        extrusion_paths_append(paths, std::move(inside_polines), j, int(0), role, it.second.front().mm3_per_mm, it.second.front().width, it.second.front().height);

                        if (remain_polylines.size() == 0) break;
                    }

                    if (remain_polylines.size() != 0) {
                        extrusion_paths_append(paths, std::move(remain_polylines), overhang_sampling_number - 1, int(0), erOverhangPerimeter, it.second.front().mm3_per_mm, it.second.front().width, it.second.front().height);
                    }
                }

            } else {
                    paths = std::move(temp_paths);
            
            }
            // get overhang paths by checking what parts of this loop fall
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(paths, clip_extrusion(extrusion_path, lower_slices_paths, ClipperLib_Z::ctDifference), erOverhangPerimeter,
                perimeter_generator.overhang_flow);

            // Reapply the nearest point search for starting point.
            // We allow polyline reversal because Clipper may have randomly reversed polylines during clipping.
            // Arachne sometimes creates extrusion with zero-length (just two same endpoints);
            if (!paths.empty()) {
                Point start_point = paths.front().first_point();
                if (!extrusion->is_closed) {
                    // Especially for open extrusion, we need to select a starting point that is at the start
                    // or the end of the extrusions to make one continuous line. Also, we prefer a non-overhang
                    // starting point.
                    struct PointInfo
                    {
                        size_t occurrence = 0;
                        bool   is_overhang = false;
                    };
                    std::unordered_map<Point, PointInfo, PointHash> point_occurrence;
                    for (const ExtrusionPath& path : paths) {
                        ++point_occurrence[path.polyline.first_point()].occurrence;
                        ++point_occurrence[path.polyline.last_point()].occurrence;
                        if (path.role() == erOverhangPerimeter) {
                            point_occurrence[path.polyline.first_point()].is_overhang = true;
                            point_occurrence[path.polyline.last_point()].is_overhang = true;
                        }
                    }

                    // Prefer non-overhang point as a starting point.
                    for (const std::pair<Point, PointInfo> pt : point_occurrence)
                        if (pt.second.occurrence == 1) {
                            start_point = pt.first;
                            if (!pt.second.is_overhang) {
                                start_point = pt.first;
                                break;
                            }
                        }
                }

                chain_and_reorder_extrusion_paths(paths, &start_point);

                if (perimeter_generator.config->enable_overhang_speed && perimeter_generator.config->fuzzy_skin == FuzzySkinType::None) {
                    // BBS: filter the speed
                    smooth_overhang_level(paths);
                }

            }
        }
        else {
            extrusion_paths_append(paths, *extrusion, role, is_external ? perimeter_generator.ext_perimeter_flow : perimeter_generator.perimeter_flow);
        }

        // Append paths to collection.
        if (!paths.empty()) {
            if (extrusion->is_closed) {
                ExtrusionLoop extrusion_loop(std::move(paths));
                // Restore the orientation of the extrusion loop.
                if (pg_extrusion.is_contour)
                    extrusion_loop.make_counter_clockwise();
                else
                    extrusion_loop.make_clockwise();

                for (auto it = std::next(extrusion_loop.paths.begin()); it != extrusion_loop.paths.end(); ++it) {
                    assert(it->polyline.points.size() >= 2);
                    assert(std::prev(it)->polyline.last_point() == it->polyline.first_point());
                }
                assert(extrusion_loop.paths.front().first_point() == extrusion_loop.paths.back().last_point());

                extrusion_coll.append(std::move(extrusion_loop));
            }
            else {
                // Because we are processing one ExtrusionLine all ExtrusionPaths should form one connected path.
                // But there is possibility that due to numerical issue there is poss
                assert([&paths = std::as_const(paths)]() -> bool {
                    for (auto it = std::next(paths.begin()); it != paths.end(); ++it)
                        if (std::prev(it)->polyline.last_point() != it->polyline.first_point())
                            return false;
                    return true;
                }());
                ExtrusionMultiPath multi_path;
                multi_path.paths.emplace_back(std::move(paths.front()));

                for (auto it_path = std::next(paths.begin()); it_path != paths.end(); ++it_path) {
                    if (multi_path.paths.back().last_point() != it_path->first_point()) {
                        extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
                        multi_path = ExtrusionMultiPath();
                    }
                    multi_path.paths.emplace_back(std::move(*it_path));
                }

                extrusion_coll.append(ExtrusionMultiPath(std::move(multi_path)));
            }
        }
    }

    return extrusion_coll;
}



void PerimeterGenerator::process_classic()
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
    double surface_simplify_resolution = (print_config->enable_arc_fitting && this->config->fuzzy_skin == FuzzySkinType::None) ? 0.2 * m_scaled_resolution : m_scaled_resolution;
    //BBS: reorder the surface to reduce the travel time
    ExPolygons surface_exp;
    for (const Surface &surface : this->slices->surfaces)
        surface_exp.push_back(surface.expolygon);
    std::vector<size_t> surface_order = chain_expolygons(surface_exp);
    for (size_t order_idx = 0; order_idx < surface_order.size(); order_idx++) {
        const Surface &surface = this->slices->surfaces[surface_order[order_idx]];
        // detect how many perimeters must be generated for this island
        int        loop_number = this->config->wall_loops + surface.extra_perimeters - 1;  // 0-indexed loops
        //BBS: set the topmost and bottom most layer to be one wall
        if (loop_number > 0 && ((this->object_config->top_one_wall_type != TopOneWallType::None && this->upper_slices == nullptr) || (this->object_config->only_one_wall_first_layer && layer_id == 0)))
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
                            ex.medial_axis(min_width, ext_perimeter_width + ext_perimeter_spacing2, &thin_walls);
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
                    const bool fuzzify_contours = this->config->fuzzy_skin != FuzzySkinType::None && ((i == 0 && this->layer_id > 0) || this->config->fuzzy_skin == FuzzySkinType::AllWalls);
                    const bool fuzzify_holes = fuzzify_contours && (this->config->fuzzy_skin == FuzzySkinType::All || this->config->fuzzy_skin == FuzzySkinType::AllWalls);
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
                if (i == 0 && i != loop_number && this->object_config->top_one_wall_type == TopOneWallType::Alltop && this->upper_slices != NULL) {
                    //split the polygons with top/not_top
                    //get the offset from solid surface anchor
                    coord_t offset_top_surface = scale_(1.5 * (config->wall_loops.value == 0 ? 0. : unscaled(double(ext_perimeter_width + perimeter_spacing * int(int(config->wall_loops.value) - int(1))))));
                    // if possible, try to not push the extra perimeters inside the sparse infill
                    if (offset_top_surface > 0.9 * (config->wall_loops.value <= 1 ? 0. : (perimeter_spacing * (config->wall_loops.value - 1))))
                        offset_top_surface -= coord_t(0.9 * (config->wall_loops.value <= 1 ? 0. : (perimeter_spacing * (config->wall_loops.value - 1))));
                    else
                        offset_top_surface = 0;
                    //don't takes into account too thin areas
                    double min_width_top_surface = (this->object_config->top_area_threshold / 100) * std::max(double(ext_perimeter_spacing / 2 + 10), 1.0 * (double(perimeter_width)));

                    //BBS: get boungding box of last
                    BoundingBox last_box   = get_extents(last);
                    last_box.offset(SCALED_EPSILON);

                    // BBS: get the Polygons upper the polygon this layer
                    Polygons upper_polygons_series_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*this->upper_slices, last_box);
                    upper_polygons_series_clipped = offset(upper_polygons_series_clipped, min_width_top_surface);

                    //set the clip to a virtual "second perimeter"
                    fill_clip = offset_ex(last, -double(ext_perimeter_spacing));
                    // get the real top surface
                    ExPolygons grown_lower_slices;
                    ExPolygons bridge_checker;
                    // BBS: check whether surface be bridge or not
                    if (this->lower_slices != NULL) {
                        // BBS: get the Polygons below the polygon this layer
                        Polygons lower_polygons_series_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*this->lower_slices, last_box);

                        double bridge_offset = std::max(double(ext_perimeter_spacing), (double(perimeter_width)));
                        bridge_checker  = offset_ex(diff_ex(last, lower_polygons_series_clipped, ApplySafetyOffset::Yes), 1.5 * bridge_offset);
                    }
                    ExPolygons delete_bridge = diff_ex(last, bridge_checker, ApplySafetyOffset::Yes);

                    ExPolygons top_polygons = diff_ex(delete_bridge, upper_polygons_series_clipped, ApplySafetyOffset::Yes);
                    //get the not-top surface, from the "real top" but enlarged by external_infill_margin (and the min_width_top_surface we removed a bit before)
                    ExPolygons temp_gap        = diff_ex(top_polygons, fill_clip);
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
                    if (has_gap_fill)
                        last = union_ex(last,temp_gap);
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
                this->object_config->wall_sequence == WallSequence::OuterInner;
            if (is_outer_wall_first ||
                //BBS: always print outer wall first when there indeed has brim.
                (this->layer_id == 0 &&
                 this->object_config->brim_type == BrimType::btOuterOnly &&
                 this->object_config->brim_width.value > 0))
                entities.reverse();
            //BBS. adjust wall generate seq
            else if (this->object_config->wall_sequence == WallSequence::InnerOuterInner)
                if (entities.entities.size() > 1){
                    int              last_outer=0;
                    int              outer = 0;
                    for (; outer < entities.entities.size(); ++outer)
                        if (entities.entities[outer]->role() == erExternalPerimeter && outer - last_outer > 1) {
                            std::swap(entities.entities[outer], entities.entities[outer - 1]);
                            last_outer = outer;
                        }
                }
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
                ex.medial_axis(min, max, &polylines);
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
            // OrcaSlicer: filter out tiny gap fills
            polylines.erase(std::remove_if(polylines.begin(), polylines.end(), [&](const ThickPolyline &p) {
                return p.length()< scale_(this->config->filter_out_gap_fill.value);
            }), polylines.end());

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

//BBS:
void PerimeterGenerator::add_infill_contour_for_arachne( ExPolygons        infill_contour,
                                                         int                loops,
                                                         coord_t            ext_perimeter_spacing,
                                                         coord_t            perimeter_spacing,
                                                         coord_t            min_perimeter_infill_spacing,
                                                         coord_t            spacing,
                                                         bool               is_inner_part)
{
    if( offset_ex(infill_contour, -float(spacing / 2.)).empty() )
    {
        infill_contour.clear(); // Infill region is too small, so let's filter it out.
    }

    // create one more offset to be used as boundary for fill
    // we offset by half the perimeter spacing (to get to the actual infill boundary)
    // and then we offset back and forth by half the infill spacing to only consider the
    // non-collapsing regions
    coord_t insert = (loops < 0) ? 0: ext_perimeter_spacing;
    if (is_inner_part || loops > 0)
        insert = perimeter_spacing;

    insert = coord_t(scale_(this->config->infill_wall_overlap.get_abs_value(unscale<double>(insert))));

    Polygons inner_pp;
    for (ExPolygon &ex : infill_contour)
        ex.simplify_p(m_scaled_resolution, &inner_pp);

    this->fill_surfaces->append(offset2_ex(union_ex(inner_pp), float(-min_perimeter_infill_spacing / 2.), float(insert + min_perimeter_infill_spacing / 2.)), stInternal);

    append(*this->fill_no_overlap, offset2_ex(union_ex(inner_pp), float(-min_perimeter_infill_spacing / 2.), float(+min_perimeter_infill_spacing / 2.)));
}

// Thanks, Cura developers, for implementing an algorithm for generating perimeters with variable width (Arachne) that is based on the paper
// "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling"
void PerimeterGenerator::process_arachne()
{
    // other perimeters
    m_mm3_per_mm = this->perimeter_flow.mm3_per_mm();
    coord_t perimeter_spacing = this->perimeter_flow.scaled_spacing();

    // external perimeters
    m_ext_mm3_per_mm = this->ext_perimeter_flow.mm3_per_mm();
    coord_t ext_perimeter_width = this->ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing = this->ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2 = scaled<coord_t>(0.5f * (this->ext_perimeter_flow.spacing() + this->perimeter_flow.spacing()));

    // overhang perimeters
    m_mm3_per_mm_overhang = this->overhang_flow.mm3_per_mm();

    // solid infill
    coord_t solid_infill_spacing = this->solid_infill_flow.scaled_spacing();

    // prepare grown lower layer slices for overhang detection
    if (this->lower_slices != nullptr && this->config->detect_overhang_wall) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used
        // in the current layer
        double nozzle_diameter = this->print_config->nozzle_diameter.get_at(this->config->wall_filament - 1);
        m_lower_slices_polygons = offset(*this->lower_slices, float(scale_(+nozzle_diameter / 2)));
    }


    // BBS: don't simplify too much which influence arc fitting when export gcode if arc_fitting is enabled
    double surface_simplify_resolution = (print_config->enable_arc_fitting && this->config->fuzzy_skin == FuzzySkinType::None) ? 0.2 * m_scaled_resolution : m_scaled_resolution;
    // we need to process each island separately because we might have different
    // extra perimeters for each one
    for (const Surface& surface : this->slices->surfaces) {
        // detect how many perimeters must be generated for this island
        int loop_number = this->config->wall_loops + surface.extra_perimeters - 1; // 0-indexed loops
        if (loop_number > 0 && this->object_config->only_one_wall_first_layer && layer_id == 0 ||
            (this->object_config->top_one_wall_type == TopOneWallType::Topmost && this->upper_slices == nullptr))
            loop_number = 0;

        ExPolygons last = offset_ex(surface.expolygon.simplify_p(surface_simplify_resolution), -float(ext_perimeter_width / 2. - ext_perimeter_spacing / 2.));
        Polygons   last_p = to_polygons(last);

        double min_nozzle_diameter = *std::min_element(print_config->nozzle_diameter.values.begin(), print_config->nozzle_diameter.values.end());
        Arachne::WallToolPathsParams input_params;
        {
            if (const auto& min_feature_size_opt = object_config->min_feature_size)
                input_params.min_feature_size = min_feature_size_opt.value * 0.01 * min_nozzle_diameter;

            if (const auto& min_bead_width_opt = object_config->min_bead_width)
                input_params.min_bead_width = min_bead_width_opt.value * 0.01 * min_nozzle_diameter;

            if (const auto& wall_transition_filter_deviation_opt = object_config->wall_transition_filter_deviation)
                input_params.wall_transition_filter_deviation = wall_transition_filter_deviation_opt.value * 0.01 * min_nozzle_diameter;

            if (const auto& wall_transition_length_opt = object_config->wall_transition_length)
                input_params.wall_transition_length = wall_transition_length_opt.value * 0.01 * min_nozzle_diameter;

            input_params.wall_transition_angle = this->object_config->wall_transition_angle.value;
            input_params.wall_distribution_count = this->object_config->wall_distribution_count.value;
        }

        int remain_loops = -1;
        if (this->object_config->top_one_wall_type == TopOneWallType::Alltop) {
            if (this->upper_slices != nullptr)
                remain_loops = loop_number - 1;

            loop_number = 0;
        }

        Arachne::WallToolPaths wallToolPaths(last_p, ext_perimeter_spacing, perimeter_spacing, coord_t(loop_number + 1), 0, layer_height, input_params);
        std::vector<Arachne::VariableWidthLines> perimeters = wallToolPaths.getToolPaths();
        loop_number = int(perimeters.size()) - 1;

        //BBS: top one wall for arachne
        ExPolygons infill_contour = union_ex(wallToolPaths.getInnerContour());
        ExPolygons inner_infill_contour;

        if( remain_loops >= 0 )
        {
            ExPolygons the_layer_surface = infill_contour;
            // BBS: get boungding box of last
            BoundingBox infill_contour_box = get_extents(infill_contour);
            infill_contour_box.offset(SCALED_EPSILON);

            // BBS: get the Polygons upper the polygon this layer
            Polygons upper_polygons_series_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*this->upper_slices, infill_contour_box);

            infill_contour = diff_ex(infill_contour, upper_polygons_series_clipped);

            coord_t    perimeter_width       = this->perimeter_flow.scaled_width();
            //BBS: add bridge area
            if (this->lower_slices != nullptr) {
                BoundingBox infill_contour_box = get_extents(infill_contour);
                infill_contour_box.offset(SCALED_EPSILON);
                // BBS: get the Polygons below the polygon this layer
                Polygons lower_polygons_series_clipped = ClipperUtils::clip_clipper_polygons_with_subject_bbox(*this->lower_slices, infill_contour_box);

                ExPolygons bridge_area = offset_ex(diff_ex(infill_contour, lower_polygons_series_clipped), std::max(ext_perimeter_spacing, perimeter_width));
                infill_contour = diff_ex(infill_contour, bridge_area);
            }
            //BBS: filter small area and extend top surface a bit to hide the wall line
            double min_width_top_surface = (this->object_config->top_area_threshold / 100) * std::max(double(ext_perimeter_spacing / 4 + 10), double(perimeter_width / 4));
            infill_contour = offset2_ex(infill_contour, -min_width_top_surface, min_width_top_surface + perimeter_width);

            //BBS: get the inner surface that not export to top
            ExPolygons surface_not_export_to_top = diff_ex(the_layer_surface, infill_contour);

            //BBS: get real top surface
            infill_contour = intersection_ex(infill_contour, the_layer_surface);
            Polygons surface_not_export_to_top_p = to_polygons(surface_not_export_to_top);
            Arachne::WallToolPaths innerWallToolPaths(surface_not_export_to_top_p, perimeter_spacing, perimeter_spacing, coord_t(remain_loops + 1), 0, layer_height, input_params);

            std::vector<Arachne::VariableWidthLines> perimeters_inner = innerWallToolPaths.getToolPaths();
            remain_loops = int(perimeters_inner.size()) - 1;

            //BBS: set wall's perporsity
            if (!perimeters.empty()) {
                for (int perimeter_idx = 0; perimeter_idx < perimeters_inner.size(); perimeter_idx++) {
                    if (perimeters_inner[perimeter_idx].empty()) continue;

                    for (Arachne::ExtrusionLine &wall : perimeters_inner[perimeter_idx]) {
                        // BBS: 0 means outer wall
                        wall.inset_idx++;
                    }
                }
            }
            perimeters.insert(perimeters.end(), perimeters_inner.begin(), perimeters_inner.end());

            inner_infill_contour = union_ex(innerWallToolPaths.getInnerContour());
        }

#ifdef ARACHNE_DEBUG
        {
            static int iRun = 0;
            export_perimeters_to_svg(debug_out_path("arachne-perimeters-%d-%d.svg", layer_id, iRun++), to_polygons(last), perimeters, union_ex(wallToolPaths.getInnerContour()));
        }
#endif

        // All closed ExtrusionLine should have the same the first and the last point.
        // But in rare cases, Arachne produce ExtrusionLine marked as closed but without
        // equal the first and the last point.
        assert([&perimeters = std::as_const(perimeters)]() -> bool {
            for (const Arachne::VariableWidthLines& perimeter : perimeters)
                for (const Arachne::ExtrusionLine& el : perimeter)
                    if (el.is_closed && el.junctions.front().p != el.junctions.back().p)
                        return false;
            return true;
        }());

        int start_perimeter = int(perimeters.size()) - 1;
        int end_perimeter = -1;
        int direction = -1;

        bool is_outer_wall_first =
            this->object_config->wall_sequence == WallSequence::OuterInner || this->object_config->wall_sequence == WallSequence::InnerOuterInner;
        if (is_outer_wall_first) {
            start_perimeter = 0;
            end_perimeter = int(perimeters.size());
            direction = 1;
        }

        std::vector<Arachne::ExtrusionLine*> all_extrusions;
        for (int perimeter_idx = start_perimeter; perimeter_idx != end_perimeter; perimeter_idx += direction) {
            if (perimeters[perimeter_idx].empty())
                continue;
            for (Arachne::ExtrusionLine& wall : perimeters[perimeter_idx])
                all_extrusions.emplace_back(&wall);
        }

        // Find topological order with constraints from extrusions_constrains.
        std::vector<size_t>              blocked(all_extrusions.size(), 0); // Value indicating how many extrusions it is blocking (preceding extrusions) an extrusion.
        std::vector<std::vector<size_t>> blocking(all_extrusions.size());   // Each extrusion contains a vector of extrusions that are blocked by this extrusion.
        std::unordered_map<const Arachne::ExtrusionLine*, size_t> map_extrusion_to_idx;
        for (size_t idx = 0; idx < all_extrusions.size(); idx++)
            map_extrusion_to_idx.emplace(all_extrusions[idx], idx);

        auto extrusions_constrains = Arachne::WallToolPaths::getRegionOrder(all_extrusions, is_outer_wall_first);
        for (auto [before, after] : extrusions_constrains) {
            auto after_it = map_extrusion_to_idx.find(after);
            ++blocked[after_it->second];
            blocking[map_extrusion_to_idx.find(before)->second].emplace_back(after_it->second);
        }

        std::vector<bool> processed(all_extrusions.size(), false);          // Indicate that the extrusion was already processed.
        Point             current_position = all_extrusions.empty() ? Point::Zero() : all_extrusions.front()->junctions.front().p; // Some starting position.
        std::vector<PerimeterGeneratorArachneExtrusion> ordered_extrusions;         // To store our result in. At the end we'll std::swap.
        ordered_extrusions.reserve(all_extrusions.size());

        while (ordered_extrusions.size() < all_extrusions.size()) {
            size_t best_candidate = 0;
            double best_distance_sqr = std::numeric_limits<double>::max();
            bool   is_best_closed = false;

            std::vector<size_t> available_candidates;
            for (size_t candidate = 0; candidate < all_extrusions.size(); ++candidate) {
                if (processed[candidate] || blocked[candidate])
                    continue; // Not a valid candidate.
                available_candidates.push_back(candidate);
            }

            std::sort(available_candidates.begin(), available_candidates.end(), [&all_extrusions](const size_t a_idx, const size_t b_idx) -> bool {
                return all_extrusions[a_idx]->is_closed < all_extrusions[b_idx]->is_closed;
                });

            for (const size_t candidate_path_idx : available_candidates) {
                auto& path = all_extrusions[candidate_path_idx];

                if (path->junctions.empty()) { // No vertices in the path. Can't find the start position then or really plan it in. Put that at the end.
                    if (best_distance_sqr == std::numeric_limits<double>::max()) {
                        best_candidate = candidate_path_idx;
                        is_best_closed = path->is_closed;
                    }
                    continue;
                }

                const Point candidate_position = path->junctions.front().p;
                double      distance_sqr = (current_position - candidate_position).cast<double>().norm();
                if (distance_sqr < best_distance_sqr) { // Closer than the best candidate so far.
                    if (path->is_closed || (!path->is_closed && best_distance_sqr != std::numeric_limits<double>::max()) || (!path->is_closed && !is_best_closed)) {
                        best_candidate = candidate_path_idx;
                        best_distance_sqr = distance_sqr;
                        is_best_closed = path->is_closed;
                    }
                }
            }

            auto& best_path = all_extrusions[best_candidate];
            ordered_extrusions.push_back({ best_path, best_path->is_contour(), false });
            processed[best_candidate] = true;
            for (size_t unlocked_idx : blocking[best_candidate])
                blocked[unlocked_idx]--;

            if (!best_path->junctions.empty()) { //If all paths were empty, the best path is still empty. We don't upate the current position then.
                if (best_path->is_closed)
                    current_position = best_path->junctions[0].p; //We end where we started.
                else
                    current_position = best_path->junctions.back().p; //Pick the other end from where we started.
            }
        }

        if (this->layer_id > 0 && this->config->fuzzy_skin != FuzzySkinType::None) {
            std::vector<PerimeterGeneratorArachneExtrusion*> closed_loop_extrusions;
            for (PerimeterGeneratorArachneExtrusion& extrusion : ordered_extrusions)
                if (extrusion.extrusion->inset_idx == 0) {
                    if (extrusion.extrusion->is_closed && this->config->fuzzy_skin == FuzzySkinType::External) {
                        closed_loop_extrusions.emplace_back(&extrusion);
                    }
                    else {
                        extrusion.fuzzify = true;
                    }
                }

            if (this->config->fuzzy_skin == FuzzySkinType::External) {
                ClipperLib_Z::Paths loops_paths;
                loops_paths.reserve(closed_loop_extrusions.size());
                for (const auto& cl_extrusion : closed_loop_extrusions) {
                    assert(cl_extrusion->extrusion->junctions.front() == cl_extrusion->extrusion->junctions.back());
                    size_t             loop_idx = &cl_extrusion - &closed_loop_extrusions.front();
                    ClipperLib_Z::Path loop_path;
                    loop_path.reserve(cl_extrusion->extrusion->junctions.size() - 1);
                    for (auto junction_it = cl_extrusion->extrusion->junctions.begin(); junction_it != std::prev(cl_extrusion->extrusion->junctions.end()); ++junction_it)
                        loop_path.emplace_back(junction_it->p.x(), junction_it->p.y(), loop_idx);
                    loops_paths.emplace_back(loop_path);
                }

                ClipperLib_Z::Clipper clipper;
                clipper.AddPaths(loops_paths, ClipperLib_Z::ptSubject, true);
                ClipperLib_Z::PolyTree loops_polytree;
                clipper.Execute(ClipperLib_Z::ctUnion, loops_polytree, ClipperLib_Z::pftEvenOdd, ClipperLib_Z::pftEvenOdd);

                for (const ClipperLib_Z::PolyNode* child_node : loops_polytree.Childs) {
                    // The whole contour must have the same index.
                    coord_t polygon_idx = child_node->Contour.front().z();
                    bool    has_same_idx = std::all_of(child_node->Contour.begin(), child_node->Contour.end(),
                        [&polygon_idx](const ClipperLib_Z::IntPoint& point) -> bool { return polygon_idx == point.z(); });
                    if (has_same_idx)
                        closed_loop_extrusions[polygon_idx]->fuzzify = true;
                }
            }
        }
        // BBS. adjust wall generate seq
        if (this->object_config->wall_sequence == WallSequence::InnerOuterInner) {
            if (ordered_extrusions.size() > 2) { // 3 walls minimum needed to do inner outer inner ordering
                int position = 0; // index to run the re-ordering for multiple external perimeters in a single island.
                int arr_i = 0;    // index to run through the walls
                int outer, first_internal, second_internal; // allocate index values
                // run the re-ordering for all wall loops in the same island
                while (position < ordered_extrusions.size()) {
                    outer = first_internal = second_internal = -1; // initialise all index values to -1
                    // run through the walls to get the index values that need re-ordering until the first one for each
                    // is found. Start at "position" index to enable the for loop to iterate for multiple external
                    // perimeters in a single island
                    for (arr_i = position; arr_i < ordered_extrusions.size(); ++arr_i) {
                        switch (ordered_extrusions[arr_i].extrusion->inset_idx) {
                        case 0: // external perimeter
                            if (outer == -1)
                                outer = arr_i;
                            break;
                        case 1: // first internal wall
                            if (first_internal==-1 && arr_i>outer && outer!=-1)
                                first_internal = arr_i;
                            break;
                        case 2: // second internal wall
                            if (ordered_extrusions[arr_i].extrusion->inset_idx == 2 && second_internal == -1 &&
                                arr_i > first_internal && outer!=-1)
                                second_internal = arr_i;
                            break;
                        }
                        if (outer >-1 && first_internal>-1 && second_internal>-1)
                            break; // found all three perimeters to re-order
                    }
                    if (outer > -1 && first_internal > -1 && second_internal > -1) { // found perimeters to re-order?
                        const auto temp = ordered_extrusions[second_internal];
                        ordered_extrusions[second_internal] = ordered_extrusions[first_internal];
                        ordered_extrusions[first_internal] = ordered_extrusions[outer];
                        ordered_extrusions[outer] = temp;
                    } else
                        break; // did not find any more candidates to re-order, so stop the while loop early
                    // go to the next perimeter to continue scanning for external walls in the same island
                    position = arr_i + 1;
                }
            }
        }

        if (ExtrusionEntityCollection extrusion_coll = traverse_extrusions(*this, ordered_extrusions); !extrusion_coll.empty())
            this->loops->append(extrusion_coll);

        const coord_t spacing = (perimeters.size() == 1) ? ext_perimeter_spacing2 : perimeter_spacing;

        // collapse too narrow infill areas
        const auto    min_perimeter_infill_spacing = coord_t(solid_infill_spacing * (1. - INSET_OVERLAP_TOLERANCE));
        // append infill areas to fill_surfaces
        add_infill_contour_for_arachne(infill_contour, loop_number, ext_perimeter_spacing, perimeter_spacing, min_perimeter_infill_spacing, spacing, false);

        //BBS: add infill_contour of top one wall part
        if( !inner_infill_contour.empty() )
            add_infill_contour_for_arachne(inner_infill_contour, remain_loops, ext_perimeter_spacing, perimeter_spacing, min_perimeter_infill_spacing, spacing, true);

    }
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
