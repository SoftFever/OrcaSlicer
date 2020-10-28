#include "../Layer.hpp"
#include "../MotionPlanner.hpp"
#include "../GCode.hpp"
#include "../MotionPlanner.hpp"
#include "../EdgeGrid.hpp"
#include "../Geometry.hpp"
#include "../ShortestPath.hpp"
#include "../Print.hpp"
#include "../Polygon.hpp"
#include "../ExPolygon.hpp"
#include "../ClipperUtils.hpp"
#include "AvoidCrossingPerimeters.hpp"

#include <memory>

namespace Slic3r {

void AvoidCrossingPerimeters::init_external_mp(const Print& print)
{
    m_external_mp = Slic3r::make_unique<MotionPlanner>(union_ex(this->collect_contours_all_layers(print.objects())));
}

// Plan a travel move while minimizing the number of perimeter crossings.
// point is in unscaled coordinates, in the coordinate system of the current active object
// (set by gcodegen.set_origin()).
Polyline AvoidCrossingPerimeters::travel_to(const GCode& gcodegen, const Point& point)
{
    // If use_external, then perform the path planning in the world coordinate system (correcting for the gcodegen offset).
    // Otherwise perform the path planning in the coordinate system of the active object.
    bool  use_external = this->use_external_mp || this->use_external_mp_once;
    Point scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    Polyline result = (use_external ? m_external_mp.get() : m_layer_mp.get())->
        shortest_path(gcodegen.last_pos() + scaled_origin, point + scaled_origin);
    if (use_external)
        result.translate(-scaled_origin);
    return result;
}

// Collect outer contours of all objects over all layers.
// Discard objects only containing thin walls (offset would fail on an empty polygon).
// Used by avoid crossing perimeters feature.
Polygons AvoidCrossingPerimeters::collect_contours_all_layers(const PrintObjectPtrs& objects)
{
    Polygons islands;
    for (const PrintObject* object : objects) {
        // Reducing all the object slices into the Z projection in a logarithimc fashion.
        // First reduce to half the number of layers.
        std::vector<Polygons> polygons_per_layer((object->layers().size() + 1) / 2);
        tbb::parallel_for(tbb::blocked_range<size_t>(0, object->layers().size() / 2),
                          [&object, &polygons_per_layer](const tbb::blocked_range<size_t>& range) {
                              for (size_t i = range.begin(); i < range.end(); ++i) {
                                  const Layer* layer1 = object->layers()[i * 2];
                                  const Layer* layer2 = object->layers()[i * 2 + 1];
                                  Polygons polys;
                                  polys.reserve(layer1->lslices.size() + layer2->lslices.size());
                                  for (const ExPolygon& expoly : layer1->lslices)
                                      //FIXME no holes?
                                      polys.emplace_back(expoly.contour);
                                  for (const ExPolygon& expoly : layer2->lslices)
                                      //FIXME no holes?
                                      polys.emplace_back(expoly.contour);
                                  polygons_per_layer[i] = union_(polys);
                              }
                          });
        if (object->layers().size() & 1) {
            const Layer* layer = object->layers().back();
            Polygons polys;
            polys.reserve(layer->lslices.size());
            for (const ExPolygon& expoly : layer->lslices)
                //FIXME no holes?
                polys.emplace_back(expoly.contour);
            polygons_per_layer.back() = union_(polys);
        }
        // Now reduce down to a single layer.
        size_t cnt = polygons_per_layer.size();
        while (cnt > 1) {
            tbb::parallel_for(tbb::blocked_range<size_t>(0, cnt / 2),
                              [&polygons_per_layer](const tbb::blocked_range<size_t>& range) {
                                  for (size_t i = range.begin(); i < range.end(); ++i) {
                                      Polygons polys;
                                      polys.reserve(polygons_per_layer[i * 2].size() + polygons_per_layer[i * 2 + 1].size());
                                      polygons_append(polys, polygons_per_layer[i * 2]);
                                      polygons_append(polys, polygons_per_layer[i * 2 + 1]);
                                      polygons_per_layer[i * 2] = union_(polys);
                                  }
                              });
            for (size_t i = 1; i < cnt / 2; ++i)
                polygons_per_layer[i] = std::move(polygons_per_layer[i * 2]);
            if (cnt & 1)
                polygons_per_layer[cnt / 2] = std::move(polygons_per_layer[cnt - 1]);
            cnt = (cnt + 1) / 2;
        }
        // And collect copies of the objects.
        for (const PrintInstance& instance : object->instances()) {
            // All the layers were reduced to the 1st item of polygons_per_layer.
            size_t i = islands.size();
            polygons_append(islands, polygons_per_layer.front());
            for (; i < islands.size(); ++i)
                islands[i].translate(instance.shift);
        }
    }
    return islands;
}

// Create a rotation matrix for projection on the given vector
static Matrix2d rotation_by_direction(const Point &direction)
{
    Matrix2d rotation;
    rotation.block<1, 2>(0, 0) = direction.cast<double>() / direction.cast<double>().norm();
    rotation(1, 0)             = -rotation(0, 1);
    rotation(1, 1)             = rotation(0, 0);

    return rotation;
}

static Point find_first_different_vertex(const Polygon &polygon, const size_t point_idx, const Point &point, bool forward)
{
    assert(point_idx < polygon.size());
    if (point != polygon.points[point_idx])
        return polygon.points[point_idx];

    int line_idx = int(point_idx);
    if (forward)
        for (; point == polygon.points[line_idx]; line_idx = (((line_idx + 1) < int(polygon.points.size())) ? (line_idx + 1) : 0));
    else
        for (; point == polygon.points[line_idx]; line_idx = (((line_idx - 1) >= 0) ? (line_idx - 1) : (int(polygon.points.size()) - 1)));
    return polygon.points[line_idx];
}

static Vec2d three_points_inward_normal(const Point &left, const Point &middle, const Point &right)
{
    assert(left != middle);
    assert(middle != right);

    Vec2d normal_1(-1 * (middle.y() - left.y()), middle.x() - left.x());
    Vec2d normal_2(-1 * (right.y() - middle.y()), right.x() - middle.x());
    normal_1.normalize();
    normal_2.normalize();

    return (normal_1 + normal_2).normalized();
}

// Compute normal of the polygon's vertex in an inward direction
static Vec2d get_polygon_vertex_inward_normal(const Polygon &polygon, const size_t point_idx)
{
    const size_t left_idx  = (point_idx <= 0) ? (polygon.size() - 1) : (point_idx - 1);
    const size_t right_idx = (point_idx >= (polygon.size() - 1)) ? 0 : (point_idx + 1);
    const Point &middle    = polygon.points[point_idx];
    const Point &left      = find_first_different_vertex(polygon, left_idx, middle, false);
    const Point &right     = find_first_different_vertex(polygon, right_idx, middle, true);
    return three_points_inward_normal(left, middle, right);
}

// Compute offset of point_idx of the polygon in a direction of inward normal
static Point get_polygon_vertex_offset(const Polygon &polygon, const size_t point_idx, const int offset)
{
    return polygon.points[point_idx] + (get_polygon_vertex_inward_normal(polygon, point_idx) * double(offset)).cast<coord_t>();
}

// Compute offset (in the direction of inward normal) of the point(passed on "middle") based on the nearest points laying on the polygon (left_idx and right_idx).
static Point get_middle_point_offset(const Polygon &polygon, const size_t left_idx, const size_t right_idx, const Point &middle, const coord_t offset)
{
    const Point &left  = find_first_different_vertex(polygon, left_idx, middle, false);
    const Point &right = find_first_different_vertex(polygon, right_idx, middle, true);
    return middle + (three_points_inward_normal(left, middle, right) * double(offset)).cast<coord_t>();
}

static bool check_if_could_cross_perimeters(const BoundingBox &bbox, const Point &start, const Point &end)
{
    bool start_out_of_bound = !bbox.contains(start), end_out_of_bound = !bbox.contains(end);
    // When both endpoints are out of the bounding box, it needs to check in more detail.
    if (start_out_of_bound && end_out_of_bound) {
        Point intersection;
        return bbox.polygon().intersection(Line(start, end), &intersection);
    }
    return true;
}

static std::pair<Point, Point> clamp_endpoints_by_bounding_box(const BoundingBox &bbox, const Point &start, const Point &end)
{
    bool   start_out_of_bound = !bbox.contains(start), end_out_of_bound = !bbox.contains(end);
    Point  start_clamped = start, end_clamped = end;
    Points intersections;
    if (start_out_of_bound || end_out_of_bound) {
        bbox.polygon().intersections(Line(start, end), &intersections);
        assert(intersections.size() <= 2);
    }

    if (start_out_of_bound && !end_out_of_bound && intersections.size() == 1) {
        start_clamped = intersections[0];
    } else if (!start_out_of_bound && end_out_of_bound && intersections.size() == 1) {
        end_clamped = intersections[0];
    } else if (start_out_of_bound && end_out_of_bound && intersections.size() == 2) {
        if ((intersections[0] - start).cast<double>().norm() < (intersections[1] - start).cast<double>().norm()) {
            start_clamped = intersections[0];
            end_clamped   = intersections[1];
        } else {
            start_clamped = intersections[1];
            end_clamped   = intersections[0];
        }
    }

    return std::make_pair(start_clamped, end_clamped);
}

static inline float get_default_perimeter_spacing(const Print &print)
{
    const std::vector<double> &nozzle_diameters = print.config().nozzle_diameter.values;
    return float(scale_(*std::max_element(nozzle_diameters.begin(), nozzle_diameters.end())));
}

static float get_perimeter_spacing(const Layer &layer)
{
    size_t regions_count     = 0;
    float  perimeter_spacing = 0.f;
    for (const LayerRegion *layer_region : layer.regions()) {
        perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
        ++regions_count;
    }

    assert(perimeter_spacing >= 0.f);
    if (regions_count != 0)
        perimeter_spacing /= float(regions_count);
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object()->print());
    return perimeter_spacing;
}

static float get_perimeter_spacing_external(const Layer &layer)
{
    size_t regions_count     = 0;
    float  perimeter_spacing = 0.f;
    for (const PrintObject *object : layer.object()->print()->objects())
        for (Layer *l : object->layers())
            if ((layer.print_z - EPSILON) <= l->print_z && l->print_z <= (layer.print_z + EPSILON))
                for (const LayerRegion *layer_region : l->regions()) {
                    perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
                    ++regions_count;
                }

    assert(perimeter_spacing >= 0.f);
    if (regions_count != 0)
        perimeter_spacing /= float(regions_count);
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object()->print());
    return perimeter_spacing;
}

// Check if anyone of ExPolygons contains whole travel.
template<class T> static bool any_expolygon_contains(const ExPolygons &ex_polygons, const T &travel)
{
    for (const ExPolygon &ex_polygon : ex_polygons)
        if (ex_polygon.contains(travel)) return true;

    return false;
}

static std::pair<Polygons, Polygons> split_expolygon(const ExPolygons &ex_polygons)
{
    Polygons contours, holes;
    contours.reserve(ex_polygons.size());
    holes.reserve(std::accumulate(ex_polygons.begin(), ex_polygons.end(), size_t(0),
                                  [](size_t sum, const ExPolygon &ex_poly) { return sum + ex_poly.holes.size(); }));
    for (const ExPolygon &ex_poly : ex_polygons) {
        contours.emplace_back(ex_poly.contour);
        append(holes, ex_poly.holes);
    }
    return std::make_pair(std::move(contours), std::move(holes));
}

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
static void export_travel_to_svg(const Polygons                                            &boundary,
                                 const Line                                                &original_travel,
                                 const Polyline                                            &result_travel,
                                 const std::vector<AvoidCrossingPerimeters2::Intersection> &intersections,
                                 const std::string                                         &path)
{
    BoundingBox   bbox = get_extents(boundary);
    ::Slic3r::SVG svg(path, bbox);
    svg.draw_outline(boundary, "green");
    svg.draw(original_travel, "blue");
    svg.draw(result_travel, "red");
    svg.draw(original_travel.a, "black");
    svg.draw(original_travel.b, "grey");

    for (const AvoidCrossingPerimeters2::Intersection &intersection : intersections)
        svg.draw(intersection.point, "lightseagreen");
}
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

ExPolygons AvoidCrossingPerimeters2::get_boundary(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    size_t      polygons_count    = 0;
    for (const LayerRegion *layer_region : layer.regions())
        polygons_count += layer_region->slices.surfaces.size();

    ExPolygons boundary;
    boundary.reserve(polygons_count);
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->slices.surfaces) boundary.emplace_back(surface.expolygon);

    boundary                      = union_ex(boundary);
    ExPolygons perimeter_boundary = offset_ex(boundary, -perimeter_offset);
    ExPolygons result_boundary;
    if (perimeter_boundary.size() != boundary.size()) {
        // If any part of the polygon is missing after shrinking, then for misisng parts are is used the boundary of the slice.
        ExPolygons missing_perimeter_boundary = offset_ex(diff_ex(boundary,
                                                                  offset_ex(perimeter_boundary, perimeter_offset + float(SCALED_EPSILON) / 2.f)),
                                                          perimeter_offset + float(SCALED_EPSILON));
        perimeter_boundary                    = offset_ex(perimeter_boundary, perimeter_offset);
        perimeter_boundary.reserve(perimeter_boundary.size() + missing_perimeter_boundary.size());
        perimeter_boundary.insert(perimeter_boundary.end(), missing_perimeter_boundary.begin(), missing_perimeter_boundary.end());
        // By calling intersection_ex some artifacts arose by previous operations are removed.
        result_boundary = union_ex(intersection_ex(offset_ex(perimeter_boundary, -perimeter_offset), boundary));
    } else {
        result_boundary = std::move(perimeter_boundary);
    }

    auto [contours, holes] = split_expolygon(boundary);
    // Add an outer boundary to avoid crossing perimeters from supports
    ExPolygons outer_boundary = union_ex(
        diff(static_cast<Polygons>(Geometry::convex_hull(offset(contours, 2.f * perimeter_spacing))),
             offset(contours, perimeter_spacing + perimeter_offset)));
    result_boundary.insert(result_boundary.end(), outer_boundary.begin(), outer_boundary.end());
    ExPolygons holes_boundary = offset_ex(holes, -perimeter_spacing);
    result_boundary.insert(result_boundary.end(), holes_boundary.begin(), holes_boundary.end());
    result_boundary = union_ex(result_boundary);

    // Collect all top layers that will not be crossed.
    polygons_count = 0;
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->fill_surfaces.surfaces)
            if (surface.is_top()) ++polygons_count;

    if (polygons_count > 0) {
        ExPolygons top_layer_polygons;
        top_layer_polygons.reserve(polygons_count);
        for (const LayerRegion *layer_region : layer.regions())
            for (const Surface &surface : layer_region->fill_surfaces.surfaces)
                if (surface.is_top()) top_layer_polygons.emplace_back(surface.expolygon);

        top_layer_polygons = union_ex(top_layer_polygons);
        return diff_ex(result_boundary, offset_ex(top_layer_polygons, -perimeter_offset));
    }

    return result_boundary;
}

ExPolygons AvoidCrossingPerimeters2::get_boundary_external(const Layer &layer)
{
    const float perimeter_spacing = get_perimeter_spacing_external(layer);
    const float perimeter_offset  = perimeter_spacing / 2.f;
    ExPolygons  boundary;
    // Collect all polygons for all printed objects and their instances, which will be printed at the same time as passed "layer".
    for (const PrintObject *object : layer.object()->print()->objects()) {
        ExPolygons polygons_per_obj;
        for (Layer *l : object->layers())
            if ((layer.print_z - EPSILON) <= l->print_z && l->print_z <= (layer.print_z + EPSILON))
                for (const LayerRegion *layer_region : l->regions())
                    for (const Surface &surface : layer_region->slices.surfaces)
                        polygons_per_obj.emplace_back(surface.expolygon);

        for (const PrintInstance &instance : object->instances()) {
            size_t boundary_idx = boundary.size();
            boundary.reserve(boundary.size() + polygons_per_obj.size());
            boundary.insert(boundary.end(), polygons_per_obj.begin(), polygons_per_obj.end());
            for (; boundary_idx < boundary.size(); ++boundary_idx) boundary[boundary_idx].translate(instance.shift.x(), instance.shift.y());
        }
    }
    boundary               = union_ex(boundary);
    auto [contours, holes] = split_expolygon(boundary);
    // Polygons in which is possible traveling without crossing perimeters of another object.
    // A convex hull allows removing unnecessary detour caused by following the boundary of the object.
    ExPolygons result_boundary = union_ex(
        diff(static_cast<Polygons>(Geometry::convex_hull(offset(contours, 2.f * perimeter_spacing))),
             offset(contours,  perimeter_spacing + perimeter_offset)));
    // All holes are extended for forcing travel around the outer perimeter of a hole when a hole is crossed.
    ExPolygons holes_boundary = union_ex(diff(offset(holes, perimeter_spacing), offset(holes, perimeter_offset)));
    result_boundary.reserve(result_boundary.size() + holes_boundary.size());
    result_boundary.insert(result_boundary.end(), holes_boundary.begin(), holes_boundary.end());
    result_boundary = union_ex(result_boundary);
    return result_boundary;
}

// Returns a direction of the shortest path along the polygon boundary
AvoidCrossingPerimeters2::Direction AvoidCrossingPerimeters2::get_shortest_direction(const Lines &lines,
                                                                                     const size_t start_idx,
                                                                                     const size_t end_idx,
                                                                                     const Point &intersection_first,
                                                                                     const Point &intersection_last)
{
    double total_length_forward  = (lines[start_idx].b - intersection_first).cast<double>().norm();
    double total_length_backward = (lines[start_idx].a - intersection_first).cast<double>().norm();

    auto cyclic_index = [&lines](int index) {
        if (index >= int(lines.size()))
            index = 0;
        else if (index < 0)
            index = int(lines.size()) - 1;

        return index;
    };

    for (int line_idx = cyclic_index(int(start_idx) + 1); line_idx != int(end_idx); line_idx = cyclic_index(line_idx + 1))
        total_length_forward += lines[line_idx].length();

    for (int line_idx = cyclic_index(int(start_idx) - 1); line_idx != int(end_idx); line_idx = cyclic_index(line_idx - 1))
        total_length_backward += lines[line_idx].length();

    total_length_forward += (lines[end_idx].a - intersection_last).cast<double>().norm();
    total_length_backward += (lines[end_idx].b - intersection_last).cast<double>().norm();

    return (total_length_forward < total_length_backward) ? Direction::Forward : Direction::Backward;
}

Polyline AvoidCrossingPerimeters2::simplify_travel(const EdgeGrid::Grid &edge_grid, const Polyline &travel)
{
    struct Visitor
    {
        Visitor(const EdgeGrid::Grid &grid) : grid(grid) {}

        bool operator()(coord_t iy, coord_t ix)
        {
            assert(pt_current != nullptr);
            assert(pt_next != nullptr);
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            this->intersect      = false;
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++it_contour_and_segment) {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, *pt_current, *pt_next)) {
                    this->intersect = true;
                    return false;
                }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid &grid;
        const Slic3r::Point  *pt_current = nullptr;
        const Slic3r::Point  *pt_next    = nullptr;
        bool                  intersect  = false;
    } visitor(edge_grid);

    Polyline simplified_path;
    simplified_path.points.reserve(travel.points.size());
    simplified_path.points.emplace_back(travel.points.front());

    // Try to skip some points in the path.
    for (size_t point_idx = 1; point_idx < travel.size(); ++point_idx) {
        const Point &current_point = travel.points[point_idx - 1];
        Point        next          = travel.points[point_idx];

        visitor.pt_current = &current_point;

        for (size_t point_idx_2 = point_idx + 1; point_idx_2 < travel.size(); point_idx_2++) {
            if (travel.points[point_idx_2] == current_point) {
                next      = travel.points[point_idx_2];
                point_idx = point_idx_2;
                continue;
            }

            visitor.pt_next = &travel.points[point_idx_2];
            edge_grid.visit_cells_intersecting_line(*visitor.pt_current, *visitor.pt_next, visitor);
            // Check if deleting point causes crossing a boundary
            if (!visitor.intersect) {
                next      = travel.points[point_idx_2];
                point_idx = point_idx_2;
            }
        }

        simplified_path.append(next);
    }

    return simplified_path;
}

size_t AvoidCrossingPerimeters2::avoid_perimeters(const Polygons       &boundaries,
                                                  const EdgeGrid::Grid &edge_grid,
                                                  const Point          &start,
                                                  const Point          &end,
                                                  Polyline             *result_out)
{
    const Point direction           = end - start;
    Matrix2d    transform_to_x_axis = rotation_by_direction(direction);

    const Line travel_line_orig(start, end);
    const Line travel_line((transform_to_x_axis * start.cast<double>()).cast<coord_t>(),
                           (transform_to_x_axis * end.cast<double>()).cast<coord_t>());

    std::vector<Intersection> intersections;
    {
        struct Visitor
        {
            Visitor(const EdgeGrid::Grid &     grid,
                    std::vector<Intersection> &intersections,
                    const Matrix2d &           transform_to_x_axis,
                    const Line &               travel_line)
                : grid(grid), intersections(intersections), transform_to_x_axis(transform_to_x_axis), travel_line(travel_line)
            {}

            bool operator()(coord_t iy, coord_t ix)
            {
                // Called with a row and colum of the grid cell, which is intersected by a line.
                auto cell_data_range = grid.cell_data_range(iy, ix);
                for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second;
                     ++it_contour_and_segment) {
                    // End points of the line segment and their vector.
                    auto segment = grid.segment(*it_contour_and_segment);

                    Point intersection_point;
                    if (travel_line.intersection(Line(segment.first, segment.second), &intersection_point) &&
                        intersection_set.find(*it_contour_and_segment) == intersection_set.end()) {
                        intersections.emplace_back(it_contour_and_segment->first, it_contour_and_segment->second,
                                                   (transform_to_x_axis * intersection_point.cast<double>()).cast<coord_t>(), intersection_point);
                        intersection_set.insert(*it_contour_and_segment);
                    }
                }
                // Continue traversing the grid along the edge.
                return true;
            }

            const EdgeGrid::Grid                                                                 &grid;
            std::vector<Intersection>                                                            &intersections;
            const Matrix2d                                                                       &transform_to_x_axis;
            const Line                                                                           &travel_line;
            std::unordered_set<std::pair<size_t, size_t>, boost::hash<std::pair<size_t, size_t>>> intersection_set;
        } visitor(edge_grid, intersections, transform_to_x_axis, travel_line_orig);

        edge_grid.visit_cells_intersecting_line(start, end, visitor);
    }

    std::sort(intersections.begin(), intersections.end());

    Polyline result;
    result.append(start);
    for (auto it_first = intersections.begin(); it_first != intersections.end(); ++it_first) {
        // The entry point to the boundary polygon
        const Intersection &intersection_first = *it_first;
        // Skip the it_first from the search for the farthest exit point from the boundary polygon
        auto it_last_item = std::make_reverse_iterator(it_first) - 1;
        // Search for the farthest intersection different from it_first but with the same border_idx
        auto it_second_r  = std::find_if(intersections.rbegin(), it_last_item, [&intersection_first](const Intersection &intersection) {
            return intersection_first.border_idx == intersection.border_idx;
        });

        // Append the first intersection into the path
        size_t left_idx  = intersection_first.line_idx;
        size_t right_idx = (intersection_first.line_idx >= (boundaries[intersection_first.border_idx].points.size() - 1)) ? 0 : (intersection_first.line_idx + 1);
        // Offset of the polygon's point using get_middle_point_offset is used to simplify the calculation of intersection between the
        // boundary and the travel. The appended point is translated in the direction of inward normal. This translation ensures that the
        // appended point will be inside the polygon and not on the polygon border.
        result.append(get_middle_point_offset(boundaries[intersection_first.border_idx], left_idx, right_idx, intersection_first.point, coord_t(SCALED_EPSILON)));

        // Check if intersection line also exit the boundary polygon
        if (it_second_r != it_last_item) {
            // Transform reverse iterator to forward
            auto it_second = (it_second_r.base() - 1);
            // The exit point from the boundary polygon
            const Intersection &intersection_second = *it_second;
            Lines               border_lines        = boundaries[intersection_first.border_idx].lines();

            Direction shortest_direction = get_shortest_direction(border_lines, intersection_first.line_idx, intersection_second.line_idx, intersection_first.point, intersection_second.point);
            // Append the path around the border into the path
            if (shortest_direction == Direction::Forward)
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                    line_idx      = (((line_idx + 1) < int(border_lines.size())) ? (line_idx + 1) : 0))
                    result.append(get_polygon_vertex_offset(boundaries[intersection_first.border_idx],
                                                            (line_idx + 1 == int(boundaries[intersection_first.border_idx].points.size())) ? 0 : (line_idx + 1), coord_t(SCALED_EPSILON)));
            else
                for (int line_idx = int(intersection_first.line_idx); line_idx != int(intersection_second.line_idx);
                    line_idx      = (((line_idx - 1) >= 0) ? (line_idx - 1) : (int(border_lines.size()) - 1)))
                    result.append(get_polygon_vertex_offset(boundaries[intersection_second.border_idx], line_idx + 0, coord_t(SCALED_EPSILON)));

            // Append the farthest intersection into the path
            left_idx  = intersection_second.line_idx;
            right_idx = (intersection_second.line_idx >= (boundaries[intersection_second.border_idx].points.size() - 1)) ? 0 : (intersection_second.line_idx + 1);
            result.append(get_middle_point_offset(boundaries[intersection_second.border_idx], left_idx, right_idx, intersection_second.point, coord_t(SCALED_EPSILON)));
            // Skip intersections in between
            it_first = it_second;
        }
    }

    result.append(end);

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, travel_line_orig, result, intersections,
                             debug_out_path("AvoidCrossingPerimeters-initial-%d.svg", iRun++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    if(!intersections.empty())
        result = simplify_travel(edge_grid, result);

#ifdef AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_travel_to_svg(boundaries, travel_line_orig, result, intersections,
                             debug_out_path("AvoidCrossingPerimeters-final-%d.svg", iRun++));
    }
#endif /* AVOID_CROSSING_PERIMETERS_DEBUG_OUTPUT */

    append(result_out->points, result.points);
    return intersections.size();
}

bool AvoidCrossingPerimeters2::needs_wipe(const GCode &   gcodegen,
                                          const Line &    original_travel,
                                          const Polyline &result_travel,
                                          const size_t    intersection_count)
{
    bool z_lift_enabled = gcodegen.config().retract_lift.get_at(gcodegen.writer().extruder()->id()) > 0.;
    bool wipe_needed    = false;

    // If the original unmodified path doesn't have any intersection with boundary, then it is entirely inside the object otherwise is entirely
    // outside the object.
    if (intersection_count > 0) {
        // The original layer is intersected with defined boundaries. Then it is necessary to make a detailed test.
        // If the z-lift is enabled, then a wipe is needed when the original travel leads above the holes.
        if (z_lift_enabled) {
            if (any_expolygon_contains(m_slice, original_travel)) {
                // Check if original_travel and are not same result_travel
                if (result_travel.size() == 2 && result_travel.first_point() == original_travel.a && result_travel.last_point() == original_travel.b) {
                    wipe_needed = false;
                } else {
                    wipe_needed = !any_expolygon_contains(m_slice, result_travel);
                }
            } else {
                wipe_needed = true;
            }
        } else {
            wipe_needed = !any_expolygon_contains(m_slice, result_travel);
        }
    }

    return wipe_needed;
}

// Plan travel, which avoids perimeter crossings by following the boundaries of the layer.
Polyline AvoidCrossingPerimeters2::travel_to(const GCode &gcodegen, const Point &point, bool *could_be_wipe_disabled)
{
    // If use_external, then perform the path planning in the world coordinate system (correcting for the gcodegen offset).
    // Otherwise perform the path planning in the coordinate system of the active object.
    bool     use_external  = this->use_external_mp || this->use_external_mp_once;
    Point    scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    Point    start         = gcodegen.last_pos() + scaled_origin;
    Point    end           = point + scaled_origin;
    Polyline result;
    size_t   travel_intersection_count = 0;
    if (!check_if_could_cross_perimeters(use_external ? m_bbox_external : m_bbox, start, end)) {
        result                    = Polyline({start, end});
        travel_intersection_count = 0;
    } else {
        auto [start_clamped, end_clamped] = clamp_endpoints_by_bounding_box(use_external ? m_bbox_external : m_bbox, start, end);
        if (use_external)
            travel_intersection_count = this->avoid_perimeters(m_boundaries_external, m_grid_external, start_clamped, end_clamped, &result);
        else
            travel_intersection_count = this->avoid_perimeters(m_boundaries, m_grid, start_clamped, end_clamped, &result);
    }

    result.points.front() = start;
    result.points.back()  = end;

    Line travel(start, end);
    double max_detour_length scale_(gcodegen.config().avoid_crossing_perimeters_max_detour);
    if ((max_detour_length > 0) && ((result.length() - travel.length()) > max_detour_length)) {
        result = Polyline({start, end});
    }
    if (use_external) {
        result.translate(-scaled_origin);
        *could_be_wipe_disabled = false;
    } else
        *could_be_wipe_disabled = !needs_wipe(gcodegen, travel, result, travel_intersection_count);

    return result;
}

void AvoidCrossingPerimeters2::init_layer(const Layer &layer)
{
    m_slice.clear();
    m_boundaries.clear();
    m_boundaries_external.clear();

    for (const LayerRegion *layer_region : layer.regions())
        append(m_slice, (ExPolygons) layer_region->slices);

    m_boundaries = to_polygons(get_boundary(layer));
    m_boundaries_external = to_polygons(get_boundary_external(layer));

    m_bbox = get_extents(m_boundaries);
    m_bbox.offset(SCALED_EPSILON);
    m_bbox_external = get_extents(m_boundaries_external);
    m_bbox_external.offset(SCALED_EPSILON);

    m_grid.set_bbox(m_bbox);
    m_grid.create(m_boundaries, coord_t(scale_(1.)));
    m_grid_external.set_bbox(m_bbox_external);
    m_grid_external.create(m_boundaries_external, coord_t(scale_(1.)));
}

} // namespace Slic3r
