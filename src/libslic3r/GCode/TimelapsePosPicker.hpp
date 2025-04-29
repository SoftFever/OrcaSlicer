#ifndef TIMELAPSE_POS_PICKER_HPP
#define TIMELAPSE_POS_PICKER_HPP

#include <vector>
#include "libslic3r/Point.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

    const Point DefaultTimelapsePos = Point(0, 0);
    const Point DefaultCameraPos = Point(0, 0);

    class Layer;
    class Print;

    struct PosPickCtx
    {
        Point curr_pos;
        const Layer* curr_layer;
        int picture_extruder_id; // the extruder id to take picture
        int curr_extruder_id;
        bool based_on_all_layer; //  whether to calculate the safe position based all layers
        PrintSequence print_sequence; // print sequence: by layer or by object
        std::optional<std::set<const PrintObject*>> printed_objects; // printed objects, only have value in by object mode
        std::optional<int> liftable_extruder_id; // extruder id that can be lifted, cause bed height to change
        std::optional<int> extruder_height_gap; // the height gap caused by extruder lift
    };

    // data are stored without plate offset
    class TimelapsePosPicker
    {
    public:
        TimelapsePosPicker() = default;
        ~TimelapsePosPicker() = default;

        Point pick_pos(const PosPickCtx& ctx);
        void init(const Print* print, const Point& plate_offset);
        void reset();
    private:
        void construct_printable_area_by_printer();

        Point pick_pos_for_curr_layer(const PosPickCtx& ctx);
        Point pick_pos_for_all_layer(const PosPickCtx& ctx);

        ExPolygons collect_object_slices_data(const Layer* curr_layer, float height_range, const std::vector<const PrintObject*>& object_list);
        Polygons collect_limit_areas_for_camera(const std::vector<const PrintObject*>& object_list);

        Polygon expand_object_projection(const Polygon& poly);

        Point pick_nearest_object_center(const Point& curr_pos, const std::vector<const PrintObject*>& object_list);
        Point get_objects_center(const std::vector<const PrintObject*>& object_list);

        Polygon get_limit_area_for_camera(const PrintObject* obj);
        std::vector<const PrintObject*> get_object_list(const std::optional<std::set<const PrintObject*>>& printed_objects);

        double get_raft_height(const PrintObject* obj);
        BoundingBoxf3 get_real_instance_bbox(const PrintInstance& instance);
        Point get_object_center(const PrintObject* obj);
    private:
        const Print* print{ nullptr };
        std::vector<ExPolygons> m_extruder_printable_area;
        Polygon m_bed_polygon;
        Point m_plate_offset;

        std::unordered_map<const PrintInstance*, BoundingBoxf3> bbox_cache;

        std::optional<Point> m_all_layer_pos;
    };
}

#endif