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
    if (point != polygon.points[point_idx])
        return polygon.points[point_idx];

    int line_idx = point_idx;
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
};

static Vec2d get_polygon_vertex_inward_normal(const Polygon &polygon, const size_t point_idx)
{
    const size_t left_idx  = (point_idx <= 0) ? (polygon.size() - 1) : (point_idx - 1);
    const size_t right_idx = (point_idx >= (polygon.size() - 1)) ? 0 : (point_idx + 1);
    const Point &middle    = polygon.points[point_idx];
    const Point &left      = find_first_different_vertex(polygon, left_idx, middle, false);
    const Point &right     = find_first_different_vertex(polygon, right_idx, middle, true);
    return three_points_inward_normal(left, middle, right);
}

// Compute offset of polygon's in a direction inward normal
static Point get_polygon_vertex_offset(const Polygon &polygon, const size_t point_idx, const int offset)
{
    return polygon.points[point_idx] + (get_polygon_vertex_inward_normal(polygon, point_idx) * double(offset)).cast<coord_t>();
}

static Point get_middle_point_offset(const Polygon &polygon, const size_t left_idx, const size_t right_idx, const Point &middle, const int offset)
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

static inline coord_t get_default_perimeter_spacing(const Print &print)
{
    const std::vector<double> &nozzle_diameters = print.config().nozzle_diameter.values;
    return scale_(*std::max_element(nozzle_diameters.begin(), nozzle_diameters.end()));
}

static coord_t get_perimeter_spacing(const Layer &layer)
{
    size_t  regions_count     = 0;
    coord_t perimeter_spacing = 0;
    for (const LayerRegion *layer_region : layer.regions()) {
        perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
        ++regions_count;
    }

    assert(perimeter_spacing >= 0);
    if (regions_count != 0)
        perimeter_spacing /= regions_count;
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object()->print());
    return perimeter_spacing;
}

static coord_t get_perimeter_spacing_external(const Layer &layer)
{
    size_t  regions_count     = 0;
    coord_t perimeter_spacing = 0;
    for (const PrintObject *object : layer.object()->print()->objects())
        for (Layer *l : object->layers())
            if ((layer.print_z - EPSILON) <= l->print_z && l->print_z <= (layer.print_z + EPSILON))
                for (const LayerRegion *layer_region : l->regions()) {
                    perimeter_spacing += layer_region->flow(frPerimeter).scaled_spacing();
                    ++regions_count;
                }

    assert(perimeter_spacing >= 0);
    if (regions_count != 0)
        perimeter_spacing /= regions_count;
    else
        perimeter_spacing = get_default_perimeter_spacing(*layer.object()->print());
    return perimeter_spacing;
}

ExPolygons AvoidCrossingPerimeters2::get_boundary(const Layer &layer)
{
    const coord_t perimeter_spacing = get_perimeter_spacing(layer);
    const coord_t offset            = perimeter_spacing / 2;
    size_t        polygons_count    = 0;
    for (const LayerRegion *layer_region : layer.regions())
        polygons_count += layer_region->slices.surfaces.size();

    ExPolygons boundary;
    boundary.reserve(polygons_count);
    for (const LayerRegion *layer_region : layer.regions())
        for (const Surface &surface : layer_region->slices.surfaces) boundary.emplace_back(surface.expolygon);

    boundary                      = union_ex(boundary);
    ExPolygons perimeter_boundary = offset_ex(boundary, -offset);
    ExPolygons final_boundary;
    if (perimeter_boundary.size() != boundary.size()) {
        // If any part of the polygon is missing after shrinking, the boundary of slice is used instead.
        ExPolygons missing_perimeter_boundary = offset_ex(diff_ex(boundary, offset_ex(perimeter_boundary, offset + SCALED_EPSILON / 2)),
                                                          offset + SCALED_EPSILON);
        perimeter_boundary                    = offset_ex(perimeter_boundary, offset);
        perimeter_boundary.insert(perimeter_boundary.begin(), missing_perimeter_boundary.begin(), missing_perimeter_boundary.end());
        final_boundary = union_ex(intersection_ex(offset_ex(perimeter_boundary, -offset), boundary));
    } else {
        final_boundary = std::move(perimeter_boundary);
    }

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
        return diff_ex(final_boundary, offset_ex(top_layer_polygons, -offset));
    }

    return final_boundary;
}

ExPolygons AvoidCrossingPerimeters2::get_boundary_external(const Layer &layer)
{
    ExPolygons boundary;
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

    const coord_t perimeter_spacing = get_perimeter_spacing_external(layer);
    const coord_t perimeter_offset  = perimeter_spacing / 2;

    Polygons contours;
    Polygons holes;
    for (ExPolygon &poly : boundary) {
        contours.emplace_back(poly.contour);
        append(holes, poly.holes);
    }

    ExPolygons final_boundary = union_ex(diff(offset(contours, perimeter_spacing * 3), offset(contours, 3 * perimeter_spacing - perimeter_offset)));
    ExPolygons holes_boundary = union_ex(diff(offset(holes, perimeter_spacing), offset(holes, perimeter_offset)));
    final_boundary.reserve(final_boundary.size() + holes_boundary.size());
    final_boundary.insert(final_boundary.end(), holes_boundary.begin(), holes_boundary.end());
    final_boundary = union_ex(final_boundary);
    return final_boundary;
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
            index = lines.size() - 1;

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

    Polyline optimized_comb_path;
    optimized_comb_path.points.reserve(travel.points.size());
    optimized_comb_path.points.emplace_back(travel.points.front());

    // Try to skip some points in the path.
    for (size_t point_idx = 1; point_idx < travel.size(); point_idx++) {
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

        optimized_comb_path.append(next);
    }

    return optimized_comb_path;
}

Polyline AvoidCrossingPerimeters2::avoid_perimeters(const Polygons       &boundaries,
                                                    const EdgeGrid::Grid &edge_grid,
                                                    const Point          &start,
                                                    const Point          &end)
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
        const Intersection &intersection_first = *it_first;
        for (auto it_second = it_first + 1; it_second != intersections.end(); ++it_second) {
            const Intersection &intersection_second = *it_second;
            if (intersection_first.border_idx == intersection_second.border_idx) {
                Lines border_lines = boundaries[intersection_first.border_idx].lines();
                // Append the nearest intersection into the path
                size_t left_idx  = intersection_first.line_idx;
                size_t right_idx = (intersection_first.line_idx >= (boundaries[intersection_first.border_idx].points.size() - 1)) ? 0 : (intersection_first.line_idx + 1);
                result.append(get_middle_point_offset(boundaries[intersection_first.border_idx], left_idx, right_idx, intersection_first.point, SCALED_EPSILON));

                Direction shortest_direction = get_shortest_direction(border_lines, intersection_first.line_idx, intersection_second.line_idx,
                                                                      intersection_first.point, intersection_second.point);
                // Append the path around the border into the path
                // Offset of the polygon's point is used to simplify calculation of intersection between boundary
                if (shortest_direction == Direction::Forward)
                    for (int line_idx = intersection_first.line_idx; line_idx != int(intersection_second.line_idx);
                         line_idx     = (((line_idx + 1) < int(border_lines.size())) ? (line_idx + 1) : 0))
                        result.append(get_polygon_vertex_offset(boundaries[intersection_first.border_idx],
                                                                (line_idx + 1 == int(boundaries[intersection_first.border_idx].points.size())) ? 0 : (line_idx + 1), SCALED_EPSILON));
                else
                    for (int line_idx = intersection_first.line_idx; line_idx != int(intersection_second.line_idx);
                         line_idx     = (((line_idx - 1) >= 0) ? (line_idx - 1) : (int(border_lines.size()) - 1)))
                        result.append(get_polygon_vertex_offset(boundaries[intersection_second.border_idx], line_idx + 0, SCALED_EPSILON));

                // Append the farthest intersection into the path
                left_idx  = intersection_second.line_idx;
                right_idx = (intersection_second.line_idx >= (boundaries[intersection_second.border_idx].points.size() - 1)) ? 0 : (intersection_second.line_idx + 1);
                result.append(get_middle_point_offset(boundaries[intersection_second.border_idx], left_idx, right_idx, intersection_second.point, SCALED_EPSILON));
                // Skip intersections in between
                it_first = (it_second - 1);
                break;
            }
        }
    }

    result.append(end);
    return simplify_travel(edge_grid, result);
}

Polyline AvoidCrossingPerimeters2::travel_to(const GCode &gcodegen, const Point &point)
{
    // If use_external, then perform the path planning in the world coordinate system (correcting for the gcodegen offset).
    // Otherwise perform the path planning in the coordinate system of the active object.
    bool     use_external  = this->use_external_mp || this->use_external_mp_once;
    Point    scaled_origin = use_external ? Point::new_scale(gcodegen.origin()(0), gcodegen.origin()(1)) : Point(0, 0);
    Point    start         = gcodegen.last_pos() + scaled_origin;
    Point    end           = point + scaled_origin;
    Polyline result;
    if (!check_if_could_cross_perimeters(use_external ? m_bbox_external : m_bbox, start, end)) {
        result = Polyline({start, end});
    } else {
        auto [start_clamped, end_clamped] = clamp_endpoints_by_bounding_box(use_external ? m_bbox_external : m_bbox, start, end);
        if (use_external)
            result = this->avoid_perimeters(m_boundaries_external, m_grid_external, start_clamped, end_clamped);
        else
            result = this->avoid_perimeters(m_boundaries, m_grid, start_clamped, end_clamped);
    }

    result.points.front() = start;
    result.points.back()  = end;

    Line travel(start, end);
    double max_detour_length scale_(gcodegen.config().avoid_crossing_perimeters_max_detour);
    if ((max_detour_length > 0) && ((result.length() - travel.length()) > max_detour_length)) {
        result = Polyline({start, end});
    }
    if (use_external)
        result.translate(-scaled_origin);
    return result;
}

void AvoidCrossingPerimeters2::init_layer(const Layer &layer)
{
    m_boundaries.clear();
    m_boundaries_external.clear();

    ExPolygons boundaries          = get_boundary(layer);
    ExPolygons boundaries_external = get_boundary_external(layer);

    m_bbox = get_extents(boundaries);
    m_bbox.offset(SCALED_EPSILON);
    m_bbox_external = get_extents(boundaries_external);
    m_bbox_external.offset(SCALED_EPSILON);

    for (const ExPolygon &ex_poly : boundaries) {
        m_boundaries.emplace_back(ex_poly.contour);
        append(m_boundaries, ex_poly.holes);
    }
    for (const ExPolygon &ex_poly : boundaries_external) {
        m_boundaries_external.emplace_back(ex_poly.contour);
        append(m_boundaries_external, ex_poly.holes);
    }

    m_grid.set_bbox(m_bbox);
    m_grid.create(m_boundaries, scale_(1.));
    m_grid_external.set_bbox(m_bbox_external);
    m_grid_external.create(m_boundaries_external, scale_(1.));
}

} // namespace Slic3r
