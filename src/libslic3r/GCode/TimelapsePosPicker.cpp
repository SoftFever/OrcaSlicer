#include "TimelapsePosPicker.hpp"
#include "Layer.hpp"

namespace Slic3r {
    void TimelapsePosPicker::init(const Print* print_, const Point& plate_offset)
    {
        reset();
        m_plate_offset = plate_offset;
        print = print_;
        construct_printable_area_by_printer();
    }

    void TimelapsePosPicker::reset()
    {
        print = nullptr;
        m_bed_polygon.clear();
        m_extruder_printable_area.clear();
        m_all_layer_pos = std::nullopt;
    }

    /**
     * @brief Retrieves a list of print objects based on the provided optional set of printed objects.
     * 
     * If the optional set of printed objects is provided, it converts the set into a vector.
     * Otherwise, it retrieves all objects from the print instance.
     */
    std::vector<const PrintObject*> TimelapsePosPicker::get_object_list(const std::optional<std::set<const PrintObject*>>& printed_objects)
    {
        std::vector<const PrintObject*> object_list;
        if (printed_objects.has_value()) {
            object_list = std::vector<const PrintObject*>(printed_objects->begin(), printed_objects->end());
        }
        else {
            object_list = std::vector<const PrintObject*>(print->objects().begin(), print->objects().end());
        }
        return object_list;
    }

    /**
     * @brief Constructs the printable area based on printer configuration.
     *
     * This function initializes the bed polygon, excludes specific areas, accounts for wipe towers,
     * and calculates the printable area for each extruder.
     */
    void TimelapsePosPicker::construct_printable_area_by_printer()
    {
        auto config = print->config();
        size_t extruder_count = config.nozzle_diameter.size();
        m_extruder_printable_area.clear();
        m_extruder_printable_area.resize(extruder_count);

        for (size_t idx = 0; idx < config.printable_area.values.size(); ++idx)
            m_bed_polygon.points.emplace_back(coord_t(scale_(config.printable_area.values[idx].x())), coord_t(scale_(config.printable_area.values[idx].y())));

        Polygon bed_exclude_area;
        for (size_t idx = 0; idx < config.bed_exclude_area.values.size(); ++idx)
            bed_exclude_area.points.emplace_back(coord_t(scale_(config.bed_exclude_area.values[idx].x())), coord_t(scale_(config.bed_exclude_area.values[idx].y())));

        Point base_wp_pt = print->get_fake_wipe_tower().pos.cast<coord_t>();
        base_wp_pt = Point{ scale_(base_wp_pt.x()),scale_(base_wp_pt.y()) };

        auto transform_wt_pt = [base_wp_pt](const Point& pt) -> Point {
            Point out =pt;
            out += base_wp_pt;
            return out;
            };
        auto wt_box = print->wipe_tower_data().bbx;
        Polygon wipe_tower_area{
            {transform_wt_pt({scale_(wt_box.min.x()),scale_(wt_box.min.y())})},
            {transform_wt_pt({scale_(wt_box.max.x()),scale_(wt_box.min.y())})},
            {transform_wt_pt({scale_(wt_box.max.x()),scale_(wt_box.max.y())})},
            {transform_wt_pt({scale_(wt_box.min.x()),scale_(wt_box.max.y())})}
        };

        for (size_t idx = 0; idx < extruder_count; ++idx) {
            ExPolygons printable_area = diff_ex(diff(m_bed_polygon, bed_exclude_area), { wipe_tower_area });
            if (idx < config.extruder_printable_area.size()) {
                Polygon extruder_printable_area;
                for (size_t j = 0; j < config.extruder_printable_area.values[idx].size(); ++j)
                    extruder_printable_area.points.emplace_back(coord_t(scale_(config.extruder_printable_area.values[idx][j].x())), coord_t(scale_(config.extruder_printable_area.values[idx][j].y())));
                printable_area = intersection_ex(printable_area, Polygons{ extruder_printable_area });
            }
            m_extruder_printable_area[idx] = printable_area;
        }
    }

    /**
     * @brief Collects object slice data within a specified height range for a given layer.
     *
     * @param layer The layer for which slices are being collected.
     * @param height_range The height range to consider for collecting slices.
     * @param object_list List of print objects to process.
     * @return ExPolygons representing the collected slice data.
     */
    ExPolygons TimelapsePosPicker::collect_object_slices_data(const Layer* layer, float height_range, const std::vector<const PrintObject*>& object_list)
    {
        auto range_intersect = [](int left1, int right1, int left2, int right2) {
            if (left1 <= left2 && left2 <= right1)
                return true;
            if (left2 <= left1 && left1 <= right2)
                return true;
            return false;
            };
        ExPolygons ret;
        float z_target = layer->print_z;
        float z_low = height_range < 0 ? layer->print_z + height_range : layer->print_z;
        float z_high = height_range < 0 ? layer->print_z : layer->print_z + height_range;
        if (z_low <= 0)
            return to_expolygons({ m_bed_polygon });

        for (auto& obj : object_list) {
            for (auto& instance : obj->instances()) {
                auto instance_bbox = get_real_instance_bbox(instance);
                if(range_intersect(instance_bbox.min.z(), instance_bbox.max.z(), layer->print_z + height_range, layer->print_z)){
                    ExPolygon expoly;
                    expoly.contour = {
                        {scale_(instance_bbox.min.x()), scale_(instance_bbox.min.y())},
                        {scale_(instance_bbox.max.x()), scale_(instance_bbox.min.y())},
                        {scale_(instance_bbox.max.x()), scale_(instance_bbox.max.y())},
                        {scale_(instance_bbox.min.x()), scale_(instance_bbox.max.y())}
                    };
                    expoly.contour = expand_object_projection(expoly.contour);
                    ret.emplace_back(std::move(expoly));
                }
            }
        }
        ret = union_ex(ret);
        return ret;
    }


    Polygons TimelapsePosPicker::collect_limit_areas_for_camera(const std::vector<const PrintObject*>& object_list)
    {
        Polygons ret;
        for (auto& obj : object_list)
            ret.emplace_back(get_limit_area_for_camera(obj));
        ret = union_(ret);
        return ret;
    }

    // expand the object expolygon by safe distance
    Polygon TimelapsePosPicker::expand_object_projection(const Polygon& poly)
    {
        // the input poly is bounding box, so we get the first offseted polygon is ok
        float radius = scale_(print->config().extruder_clearance_radius.value / 2);
        return offset(poly, radius)[0];
    }

    double TimelapsePosPicker::get_raft_height(const PrintObject* obj)
    {
        if (!obj || !obj->has_raft())
            return 0;
        auto slice_params = obj->slicing_parameters();
        int base_raft_layers = slice_params.base_raft_layers;
        double base_raft_height = slice_params.base_raft_layer_height;
        int interface_raft_layers = slice_params.interface_raft_layers;
        double interface_raft_height = slice_params.interface_raft_layer_height;
        double contact_raft_layer_height = slice_params.contact_raft_layer_height;

        double ret = print->config().initial_layer_print_height;
        if (base_raft_layers - 1 > 0)
            ret += (base_raft_layers - 1) * base_raft_height;
        if (interface_raft_layers - 1 > 0)
            ret += (interface_raft_layers - 1) * interface_raft_height;
        if (obj->config().raft_layers > 1)
            ret += contact_raft_layer_height;

        return ret + slice_params.gap_raft_object;
    }

    // get the real instance bounding box, remove the plate offset and add raft height
    BoundingBoxf3 TimelapsePosPicker::get_real_instance_bbox(const PrintInstance& instance)
    {
        auto bbox = instance.get_bounding_box();
        double raft_height =get_raft_height(instance.print_object);
        bbox.max.z() += raft_height;
        // remove plate offset
        bbox.min.x() -= m_plate_offset.x();
        bbox.max.x() -= m_plate_offset.x();
        bbox.min.y() -= m_plate_offset.y();
        bbox.max.y() -= m_plate_offset.y();
        return bbox;
    }

    Polygon TimelapsePosPicker::get_limit_area_for_camera(const PrintObject* obj)
    {
        if (!obj)
            return {};
        auto bbox = get_real_instance_bbox(obj->instances().front());
        float radius = print->config().extruder_clearance_radius.value / 2;

        auto offset_bbox = bbox.inflated(sqrt(2) * radius);
        Polygon ret = {
            DefaultCameraPos,
            {scale_(offset_bbox.max.x()),scale_(offset_bbox.min.y())},
            {scale_(offset_bbox.max.x()),scale_(offset_bbox.max.y())},
            {scale_(offset_bbox.min.x()),scale_(offset_bbox.max.y())}
        };
        return ret;
    }

    /**
     * @brief Selects the nearest position within the given safe areas relative to the current position.
     *
     * This function determines the closest point in the safe areas to the provided current position.
     * If the current position is already inside a safe area, it returns the current position.
     * If no safe areas are defined, return default timelapse position.
     *
     * @param curr_pos The reference point representing the current position.
     * @param safe_areas A collection of extended polygons defining the safe areas.
     * @return Point The nearest point within the safe areas or the default timelapse position if no safe areas exist.
     */
    Point pick_pos_internal(const Point& curr_pos, const ExPolygons& safe_areas)
    {
        if (std::any_of(safe_areas.begin(), safe_areas.end(), [&curr_pos](const ExPolygon& p) { return p.contains(curr_pos);}))
            return curr_pos;

        if (safe_areas.empty())
            return DefaultTimelapsePos;

        double min_distance = std::numeric_limits<double>::max();
        Point nearest_point =DefaultTimelapsePos;
#if 0
        for (const auto& expoly : safe_areas) {
            Polygons polys = to_polygons(expoly);
            for (auto& poly : polys) {
                auto nearest_point_ptr = poly.closest_point(curr_pos);
                if (nearest_point_ptr) {
                    double dist = (*nearest_point_ptr - curr_pos).cast<double>().norm();
                    if (min_distance > dist) {
                        min_distance = dist;
                        nearest_point = *nearest_point_ptr;
                    }
                }
            }
        }
#else
        for (const auto& expoly : safe_areas) {
            Polygons polys = to_polygons(expoly);
            for (auto& poly : polys) {
                for (size_t idx = 0; idx < poly.points.size(); ++idx) {
                    Line line(poly.points[idx], poly.points[next_idx_modulo(idx, poly.points)]);
                    Point candidate;
                    double dist = line.distance_to_squared(curr_pos, &candidate);
                    if (min_distance > dist) {
                        min_distance = dist;
                        nearest_point = candidate;
                    }
                }
            }
        }
#endif
        return nearest_point;
    }

    Point TimelapsePosPicker::pick_pos(const PosPickCtx& ctx)
    {
        Point res;
        if (ctx.based_on_all_layer)
            res = pick_pos_for_all_layer(ctx);
        else
            res = pick_pos_for_curr_layer(ctx);

        return { unscale_(res.x()), unscale_(res.y()) };
    }

    // get center point of curr object
    Point TimelapsePosPicker::get_object_center(const PrintObject* obj)
    {
        if (!obj)
            return {};
        // in bambu studio, each object only has one instance
        auto instance = obj->instances().front();
        auto instance_bbox = get_real_instance_bbox(instance);
        Point min_p{ instance_bbox.min.x(),instance_bbox.min.y() };
        Point max_p{ instance_bbox.max.x(),instance_bbox.max.y() };

        return { scale_((min_p.x() + max_p.x()) / 2),scale_(min_p.y() + max_p.y() / 2) };
    }

    Point TimelapsePosPicker::pick_nearest_object_center(const Point& curr_pos, const  std::vector<const PrintObject*>& object_list)
    {
        if (object_list.empty())
            return {};
        const PrintObject* ptr = object_list.front();
        double distance = std::numeric_limits<double>::max();
        for (auto& obj : object_list) {
            Point obj_center = get_object_center(obj);
            double dist = (obj_center - curr_pos).cast<double>().norm();
            if (distance > dist) {
                distance = dist;
                ptr = obj;
            }
        }
        return get_object_center(ptr);
    }

    Point TimelapsePosPicker::pick_pos_for_curr_layer(const PosPickCtx& ctx)
    {
        float height_gap = 0;
        if (ctx.curr_extruder_id != ctx.picture_extruder_id) {
            if (ctx.liftable_extruder_id.has_value() && ctx.picture_extruder_id != ctx.liftable_extruder_id && ctx.extruder_height_gap.has_value())
                height_gap = -*ctx.extruder_height_gap;
        }

        std::vector<const PrintObject*> object_list = get_object_list(ctx.printed_objects);

        ExPolygons layer_slices = collect_object_slices_data(ctx.curr_layer,height_gap, object_list);
        Polygons camera_limit_areas = collect_limit_areas_for_camera(object_list);
        ExPolygons unplacable_area = union_ex(layer_slices, camera_limit_areas);
        ExPolygons extruder_printable_area = m_extruder_printable_area[ctx.picture_extruder_id];

        ExPolygons safe_area = diff_ex(extruder_printable_area, unplacable_area);
        Point objs_center = get_objects_center(object_list);
        return pick_pos_internal(objs_center, safe_area);
    }

    /**
     * @brief Calculates the center of multiple objects.
     *
     * This function computes the average center of all instances of the provided objects.
     *
     * @param object_list A vector of pointers to PrintObject instances.
     * @return Point The average center of all objects.
     */
    Point TimelapsePosPicker::get_objects_center(const std::vector<const PrintObject*>& object_list)
    {
        if (object_list.empty())
            return Point(0,0);
        double sum_x = 0.0;
        double sum_y = 0.0;
        size_t total_instances = 0;
        for (auto& obj : object_list) {
            for (auto& instance : obj->instances()) {
                const auto& bbox = get_real_instance_bbox(instance);
                Point min_p{ bbox.min.x(),bbox.min.y() };
                Point max_p{ bbox.max.x(),bbox.max.y() };
                double center_x = (min_p.x() + max_p.x()) / 2.f;
                double center_y = (min_p.y() + max_p.y()) / 2.f;
                sum_x += center_x;
                sum_y += center_y;
                total_instances += 1;
            }
        }
        return Point{ coord_t(scale_(sum_x / total_instances)),coord_t(scale_(sum_y / total_instances)) };
    }

    Point TimelapsePosPicker::pick_pos_for_all_layer(const PosPickCtx& ctx)
    {
        float height_gap = 0;
        if (ctx.curr_extruder_id != ctx.picture_extruder_id) {
            if (ctx.liftable_extruder_id.has_value() && ctx.picture_extruder_id != ctx.liftable_extruder_id && ctx.extruder_height_gap.has_value())
                height_gap = *ctx.extruder_height_gap;
        }
        if (ctx.curr_layer->print_z < height_gap)
            return DefaultTimelapsePos;
        if (m_all_layer_pos)
            return *m_all_layer_pos;

        Polygons object_projections;

        auto object_list = get_object_list(std::nullopt);

        for (auto& obj : object_list) {
            for (auto& instance : obj->instances()) {
                const auto& bbox = get_real_instance_bbox(instance);
                Point min_p{ scale_(bbox.min.x()),scale_(bbox.min.y()) };
                Point max_p{ scale_(bbox.max.x()),scale_(bbox.max.y()) };
                Polygon obj_proj{ { min_p.x(),min_p.y() },
                    { max_p.x(),min_p.y() },
                    { max_p.x(),max_p.y() },
                    { min_p.x(),max_p.y() }
                };
                object_projections.emplace_back(expand_object_projection(obj_proj));
            }
        };

        object_projections = union_(object_projections);
        Polygons camera_limit_areas = collect_limit_areas_for_camera(object_list);
        Polygons unplacable_area = union_(object_projections, camera_limit_areas);

        ExPolygons extruder_printable_area;
        if (m_extruder_printable_area.size() > 1)
            extruder_printable_area = intersection_ex(m_extruder_printable_area[0], m_extruder_printable_area[1]);
        else if (m_extruder_printable_area.size() == 1)
            extruder_printable_area = m_extruder_printable_area.front();

        ExPolygons safe_area = diff_ex(extruder_printable_area, unplacable_area);

        Point starting_pos = get_objects_center(object_list);

        m_all_layer_pos = pick_pos_internal(starting_pos, safe_area);
        return *m_all_layer_pos;
    }

}