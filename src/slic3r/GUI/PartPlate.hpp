#ifndef __part_plate_hpp_
#define __part_plate_hpp_

#include <vector>
#include <set>
#include <array>
#include <thread>
#include <mutex>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Arrange.hpp"
#include "Plater.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "GLCanvas3D.hpp"
#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"
#include "3DBed.hpp"

class GLUquadric;
typedef class GLUquadric GLUquadricObject;


// use PLATE_CURRENT_IDX stands for using current plate
// and use PLATE_ALL_IDX
#define PLATE_CURRENT_IDX   -1
#define PLATE_ALL_IDX       -2

#define MAX_PLATE_COUNT     36

inline int compute_colum_count(int count)
{
    float value = sqrt((float)count);
    float round_value = round(value);
    int cols;

    if (value > round_value)
        cols = round_value +1;
    else
        cols = round_value;

    return cols;
}

namespace Slic3r {

class Model;
class ModelObject;
class ModelInstance;
class Print;
class SLAPrint;

namespace GUI {
class Plater;
class GLCanvas3D;
struct Camera;
class PartPlateList;

using GCodeResult = GCodeProcessorResult;

class PartPlate : public ObjectBase
{
public:
    enum HeightLimitMode{
        HEIGHT_LIMIT_NONE,
        HEIGHT_LIMIT_BOTTOM,
        HEIGHT_LIMIT_TOP,
        HEIGHT_LIMIT_BOTH
    };

private:
    PartPlateList* m_partplate_list {nullptr };
    Plater* m_plater; //Plater reference, not own it
    Model* m_model; //Model reference, not own it
    PrinterTechnology  printer_technology;

    std::set<std::pair<int, int>> obj_to_instance_set;
    std::set<std::pair<int, int>> instance_outside_set;
    int m_plate_index;
    Vec3d m_origin;
    int m_width;
    int m_depth;
    int m_height;
    float m_height_to_lid;
    float m_height_to_rod;
    bool m_printable;
    bool m_locked;
    bool m_ready_for_slice;
    bool m_slice_result_valid;
    bool m_apply_invalid {false};
    float m_slice_percent;

    Print *m_print; //Print reference, not own it, no need to serialize
    GCodeProcessorResult *m_gcode_result;
    std::vector<FilamentInfo> slice_filaments_info;
    int m_print_index;

    std::string m_tmp_gcode_path;       //use a temp path to store the gcode
    std::string m_temp_config_3mf_path; //use a temp path to store the config 3mf
    std::string m_gcode_path_from_3mf;  //use a path to store the gcode loaded from 3mf

    friend class PartPlateList;

    Pointfs m_shape;
    Pointfs m_exclude_area;
    BoundingBoxf3 m_bounding_box;
    BoundingBoxf3 m_extended_bounding_box;
    mutable std::vector<BoundingBoxf3> m_exclude_bounding_box;
    mutable BoundingBoxf3 m_grabber_box;
    Transform3d m_grabber_trans_matrix;
    Slic3r::Geometry::Transformation position;
    std::vector<Vec3f> positions;
    unsigned int m_vbo_id{ 0 };
    GeometryBuffer m_triangles;
    GeometryBuffer m_exclude_triangles;
    GeometryBuffer m_logo_triangles;
    GeometryBuffer m_gridlines;
    GeometryBuffer m_gridlines_bolder;
    GeometryBuffer m_height_limit_common;
    GeometryBuffer m_height_limit_bottom;
    GeometryBuffer m_height_limit_top;
    GeometryBuffer m_del_icon;
    //GeometryBuffer m_del_and_background_icon;
    mutable unsigned int m_del_vbo_id{ 0 };
    GeometryBuffer m_arrange_icon;
    mutable unsigned int m_arrange_vbo_id{ 0 };
    GeometryBuffer m_orient_icon;
    mutable unsigned int m_orient_vbo_id{ 0 };
    GeometryBuffer m_lock_icon;
    mutable unsigned int m_lock_vbo_id{ 0 };
    GeometryBuffer m_plate_settings_icon;
    mutable unsigned int m_plate_settings_vbo_id{ 0 };
    GeometryBuffer m_plate_idx_icon;
    mutable unsigned int m_plate_idx_vbo_id{ 0 };
    GLTexture m_texture;

    mutable float m_grabber_color[4];
    float m_scale_factor{ 1.0f };
    GLUquadricObject* m_quadric;
    int m_hover_id;
    bool m_selected;

    // BBS
    DynamicPrintConfig m_config;

    // SoftFever
    // part plate name
    std::string m_name;
    GeometryBuffer m_plate_name_icon;
    mutable unsigned int m_plate_name_vbo_id{ 0 };
    GLTexture m_name_texture;
    wxCoord m_name_texture_width;
    wxCoord m_name_texture_height;

    void init();
    bool valid_instance(int obj_id, int instance_id);
    void generate_print_polygon(ExPolygon &print_polygon);
    void generate_exclude_polygon(ExPolygon &exclude_polygon);
    void generate_logo_polygon(ExPolygon &logo_polygon);
    void calc_bounding_boxes() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_exclude_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& pp_bbox);
    void calc_height_limit();
    void calc_vertex_for_number(int index, bool one_number, GeometryBuffer &buffer);
    void calc_vertex_for_icons(int index, GeometryBuffer &buffer);
    void calc_vertex_for_icons_background(int icon_count, GeometryBuffer &buffer);
    //void calc_vertex_for_name_tex(GeometryBuffer &buffer);

    void render_background(bool force_default_color = false) const;
    void render_logo(bool bottom) const;
    void render_logo_texture(GLTexture& logo_texture, const GeometryBuffer& logo_buffer, bool bottom, unsigned int vbo_id) const;
    void render_exclude_area(bool force_default_color) const;
    //void render_background_for_picking(const float* render_color) const;
    void render_grid(bool bottom) const;
    void render_height_limit(PartPlate::HeightLimitMode mode = HEIGHT_LIMIT_BOTH) const;
    void render_label(GLCanvas3D& canvas) const;
    void render_grabber(const float* render_color, bool use_lighting) const;
    void render_face(float x_size, float y_size) const;
    void render_arrows(const float* render_color, bool use_lighting) const;
    void render_left_arrow(const float* render_color, bool use_lighting) const;
    void render_right_arrow(const float* render_color, bool use_lighting) const;
    void render_icon_texture(int position_id, int tex_coords_id, const GeometryBuffer &buffer, GLTexture &texture, unsigned int &vbo_id) const;
    void render_icons(bool bottom, bool only_name = false, int hover_id = -1);
    void render_only_numbers(bool bottom) const;
    void render_plate_name_texture(int position_id, int tex_coords_id);
    void render_rectangle_for_picking(const GeometryBuffer &buffer, const float* render_color) const;
    void on_render_for_picking() const;
    std::array<float, 4> picking_color_component(int idx) const;
    void release_opengl_resource();

public:
    static const unsigned int PLATE_BASE_ID = 255 * 255 * 253;
    static const unsigned int GRABBER_COUNT = 6;

    static std::array<float, 4> SELECT_COLOR;
    static std::array<float, 4> UNSELECT_COLOR;
    static std::array<float, 4> UNSELECT_DARK_COLOR;
    static std::array<float, 4> DEFAULT_COLOR;
    static std::array<float, 4> LINE_BOTTOM_COLOR;
    static std::array<float, 4> LINE_TOP_COLOR;
    static std::array<float, 4> LINE_TOP_DARK_COLOR;
    static std::array<float, 4> LINE_TOP_SEL_COLOR;
    static std::array<float, 4> LINE_TOP_SEL_DARK_COLOR;
    static std::array<float, 4> HEIGHT_LIMIT_BOTTOM_COLOR;
    static std::array<float, 4> HEIGHT_LIMIT_TOP_COLOR;

    static void update_render_colors();
    static void load_render_colors();

    PartPlate();
    PartPlate(PartPlateList *partplate_list, Vec3d origin, int width, int depth, int height, Plater* platerObj, Model* modelObj, bool printable=true, PrinterTechnology tech = ptFFF);
    ~PartPlate();

    bool operator<(PartPlate&) const;

    //clear alll the instances in plate
    void clear(bool clear_sliced_result = true);

    BedType get_bed_type() const;
    void set_bed_type(BedType bed_type);
    void reset_bed_type();
    DynamicPrintConfig* config() { return &m_config; }

    // set print sequence per plate
    //bool print_seq_same_global = true;
    void set_print_seq(PrintSequence print_seq = PrintSequence::ByDefault);
    PrintSequence get_print_seq() const;
    // Get the real effective print sequence of current plate.
    // If curr_plate's print_seq is ByDefault, use the global sequence
    // @return PrintSequence::{ByLayer,ByObject}
    PrintSequence get_real_print_seq() const;


    //static const int plate_x_offset = 20; //mm
    //static const double plate_x_gap = 0.2;
    ThumbnailData thumbnail_data;
    static const int plate_thumbnail_width = 512;
    static const int plate_thumbnail_height = 512;

    ThumbnailData cali_thumbnail_data;
    PlateBBoxData cali_bboxes_data;
    static const int cali_thumbnail_width = 2560;
    static const int cali_thumbnail_height = 2560;

    //set the plate's index
    void set_index(int index);

    //get the plate's index
    int get_index() { return m_plate_index; }

    // SoftFever
    //get the plate's name
    std::string get_plate_name() { return m_name; }
    void generate_plate_name_texture();
    //set the plate's name
    void set_plate_name(const std::string& name);

    //get the print's object, result and index
    void get_print(PrintBase **print, GCodeResult **result, int *index);

    //set the print object, result and it's index
    void set_print(PrintBase *print, GCodeResult* result = nullptr, int index = -1);

    //get gcode filename
    std::string get_gcode_filename();

    bool is_valid_gcode_file();

    //get the plate's center point origin
    Vec3d get_center_origin();
    /* size and position related functions*/
    //set position and size
    void set_pos_and_size(Vec3d& origin, int width, int depth, int height, bool with_instance_move);

    // BBS
    ModelObjectPtrs get_objects() { return m_model->objects; }
    ModelInstance* get_instance(int obj_id, int instance_id);

    Vec3d get_origin() { return m_origin; }
    Vec3d estimate_wipe_tower_size(const double w, const double wipe_volume) const;
    std::vector<int> get_extruders(bool conside_custom_gcode = false) const;
    std::vector<int> get_used_extruders();

    /* instance related operations*/
    //judge whether instance is bound in plate or not
    bool contain_instance(int obj_id, int instance_id);
    bool contain_instance_totally(ModelObject* object, int instance_id) const;
    //judge whether instance is totally included in plate or not
    bool contain_instance_totally(int obj_id, int instance_id) const;

    //judge whether the plate's origin is at the left of instance or not
    bool is_left_top_of(int obj_id, int instance_id);

    //check whether instance is outside the plate or not
    bool check_outside(int obj_id, int instance_id, BoundingBoxf3* bounding_box = nullptr);

    //judge whether instance is intesected with plate or not
    bool intersect_instance(int obj_id, int instance_id, BoundingBoxf3* bounding_box = nullptr);

    //add an instance into plate
    int add_instance(int obj_id, int instance_id, bool move_position, BoundingBoxf3* bounding_box = nullptr);

    //remove instance from plate
    int remove_instance(int obj_id, int instance_id);

    //translate instance on the plate
    void translate_all_instance(Vec3d position);

    //update instance exclude state
    void update_instance_exclude_status(int obj_id, int instance_id, BoundingBoxf3* bounding_box = nullptr);

    //update object's index caused by original object deleted
    void update_object_index(int obj_idx_removed, int obj_idx_max);

    //whether it is empty
    bool empty() { return obj_to_instance_set.empty(); }

    int printable_instance_size();

    //whether it is has printable instances
    bool has_printable_instances();

    //move instances to left or right PartPlate
    void move_instances_to(PartPlate& left_plate, PartPlate& right_plate, BoundingBoxf3* bounding_box = nullptr);

    /*rendering related functions*/
    const Pointfs& get_shape() const { return m_shape; }
    bool set_shape(const Pointfs& shape, const Pointfs& exclude_areas, Vec2d position, float height_to_lid, float height_to_rod);
    bool contains(const Vec3d& point) const;
    bool contains(const GLVolume& v) const;
    bool contains(const BoundingBoxf3& bb) const;
    bool intersects(const BoundingBoxf3& bb) const;

    void render(bool bottom, bool only_body = false, bool force_background_color = false, HeightLimitMode mode = HEIGHT_LIMIT_NONE, int hover_id = -1);
    void render_for_picking() const { on_render_for_picking(); }
    void set_selected();
    void set_unselected();
    void set_hover_id(int id) { m_hover_id = id; }
    const BoundingBoxf3& get_bounding_box(bool extended = false) { return extended ? m_extended_bounding_box : m_bounding_box; }
    const BoundingBox get_bounding_box_crd();
    BoundingBoxf3 get_build_volume()
    {
        Vec3d up_point(m_origin.x() + m_width, m_origin.y() + m_depth, m_origin.z() + m_height);
        Vec3d low_point(m_origin.x(), m_origin.y(), m_origin.z());
        BoundingBoxf3 plate_box(low_point, up_point);
        return plate_box;
    }

    const std::vector<BoundingBoxf3>& get_exclude_areas() { return m_exclude_bounding_box; }


    /*status related functions*/
    //update status
    void update_states();

    //is locked or not
    bool is_locked() const { return m_locked; }
    void lock(bool state) { m_locked = state; }

    //is a printable plate or not
    bool is_printable() const { return m_printable; }

    //can be sliced or not
    bool can_slice() const
    {
        return m_ready_for_slice && !m_apply_invalid;
    }
    void update_slice_ready_status(bool ready_slice)
    {
        m_ready_for_slice = ready_slice;
    }

    //bedtype mismatch or not
    bool is_apply_result_invalid() const
    {
        return m_apply_invalid;
    }
    void update_apply_result_invalid(bool invalid)
    {
        m_apply_invalid = invalid;
    }

    //is slice result valid or not
    bool is_slice_result_valid() const
    {
        return m_slice_result_valid;
    }

    //is slice result ready for print
    bool is_slice_result_ready_for_print() const
    {
        bool result = m_slice_result_valid;
        if (result)
            result = m_gcode_result ? (!m_gcode_result->toolpath_outside) : false;
        return result;
    }

    //invalid sliced result
    void update_slice_result_valid_state(bool valid = false);

    void update_slicing_percent(float percent)
    {
        m_slice_percent = percent;
    }

    float get_slicing_percent() { return m_slice_percent; }

    /*slice related functions*/
    //update current slice context into backgroud slicing process
    void update_slice_context(BackgroundSlicingProcess& process);
    //return the fff print object
    Print* fff_print() { return m_print; }
    //return the slice result
    GCodeProcessorResult* get_slice_result() { return m_gcode_result; }

    std::string           get_tmp_gcode_path();
    std::string           get_temp_config_3mf_path();
    //this API should only be used for command line usage
    void set_tmp_gcode_path(std::string new_path)
    {
        m_tmp_gcode_path = new_path;
    }
    //load gcode from file
    int load_gcode_from_file(const std::string& filename);
    //load thumbnail data from file
    int load_thumbnail_data(std::string filename);
    //load pattern thumbnail data from file
    int load_pattern_thumbnail_data(std::string filename);
    //load pattern box data from file
    int load_pattern_box_data(std::string filename);

    void print() const;

    friend class cereal::access;
    friend class UndoRedo::StackImpl;

    template<class Archive> void load(Archive& ar) {
        std::vector<std::pair<int, int>>	objects_and_instances;
        std::vector<std::pair<int, int>>	instances_outside;

        ar(m_plate_index, m_print_index, m_origin, m_width, m_depth, m_height, m_locked, m_selected, m_ready_for_slice, m_slice_result_valid, m_apply_invalid, m_printable, m_tmp_gcode_path, objects_and_instances, instances_outside, m_config);

        for (std::vector<std::pair<int, int>>::iterator it = objects_and_instances.begin(); it != objects_and_instances.end(); ++it)
            obj_to_instance_set.insert(std::pair(it->first, it->second));

        for (std::vector<std::pair<int, int>>::iterator it = instances_outside.begin(); it != instances_outside.end(); ++it)
            instance_outside_set.insert(std::pair(it->first, it->second));
    }
    template<class Archive> void save(Archive& ar) const {
        std::vector<std::pair<int, int>>	objects_and_instances;
        std::vector<std::pair<int, int>>	instances_outside;

        for (std::set<std::pair<int, int>>::iterator it = instance_outside_set.begin(); it != instance_outside_set.end(); ++it)
            instances_outside.emplace_back(it->first, it->second);

        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);

        ar(m_plate_index, m_print_index, m_origin, m_width, m_depth, m_height, m_locked, m_selected, m_ready_for_slice, m_slice_result_valid, m_apply_invalid, m_printable, m_tmp_gcode_path, objects_and_instances, instances_outside, m_config);
    }
    /*template<class Archive> void serialize(Archive& ar)
    {
        std::vector<std::pair<int, int>> objects_and_instances;
        for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
            objects_and_instances.emplace_back(it->first, it->second);
        ar(m_plate_index, m_origin, m_width, m_depth, m_height, m_locked, m_ready_for_slice, m_printable, objects_and_instances);
    }*/
};

class PartPlateList : public ObjectBase
{
    Plater* m_plater; //Plater reference, not own it
    Model* m_model; //Model reference, not own it
    PrinterTechnology  printer_technology;

    std::vector<PartPlate*> m_plate_list;
    std::map<int, PrintBase*> m_print_list;
    std::map<int, GCodeResult*> m_gcode_result_list;
    std::mutex m_plates_mutex;
    int m_plate_count;
    int m_plate_cols;
    int m_current_plate;
    int m_print_index;

    int m_plate_width;
    int m_plate_depth;
    int m_plate_height;

    float m_height_to_lid;
    float m_height_to_rod;
    PartPlate::HeightLimitMode m_height_limit_mode{PartPlate::HEIGHT_LIMIT_BOTH};

    PartPlate unprintable_plate;
    Pointfs m_shape;
    Pointfs m_exclude_areas;
    BoundingBoxf3 m_bounding_box;
    bool m_intialized;
    std::string m_logo_texture_filename;
    GLTexture m_logo_texture;
    GLTexture m_del_texture;
    GLTexture m_del_hovered_texture;
    GLTexture m_arrange_texture;
    GLTexture m_arrange_hovered_texture;
    GLTexture m_orient_texture;
    GLTexture m_orient_hovered_texture;
    GLTexture m_locked_texture;
    GLTexture m_locked_hovered_texture;
    GLTexture m_lockopen_texture;
    GLTexture m_lockopen_hovered_texture;
    GLTexture m_plate_settings_texture;
    GLTexture m_plate_settings_changed_texture;
    GLTexture m_plate_settings_hovered_texture;
    GLTexture m_plate_settings_changed_hovered_texture;
    GLTexture m_idx_textures[MAX_PLATE_COUNT];
    // set render option
    bool render_bedtype_logo = true;
    bool render_plate_settings = true;

    bool m_is_dark = false;

    void init();
    //compute the origin for printable plate with index i
    Vec3d compute_origin(int index, int column_count);
    //compute the origin for unprintable plate
    Vec3d compute_origin_for_unprintable();
    //compute shape position
    Vec2d compute_shape_position(int index, int cols);
    //generate icon textures
    void generate_icon_textures();
    void release_icon_textures();

    friend class cereal::access;
    friend class UndoRedo::StackImpl;
    friend class PartPlate;

public:
    class BedTextureInfo {
    public:
        class TexturePart {
        public:
            // position
            int x;
            int y;
            int w;
            int h;
            unsigned int vbo_id;
            std::string filename;
            GLTexture* texture { nullptr };
            Vec2d offset;
            GeometryBuffer* buffer { nullptr };
            TexturePart(int xx, int yy, int ww, int hh, std::string file) {
                x = xx; y = yy;
                w = ww; h = hh;
                filename = file;
                texture = nullptr;
                buffer = nullptr;
                vbo_id = 0;
                offset = Vec2d(0, 0);
            }

            TexturePart(const TexturePart& part) {
                this->x = part.x;
                this->y = part.y;
                this->w = part.w;
                this->h = part.h;
                this->offset = part.offset;
                this->buffer    = part.buffer;
                this->filename  = part.filename;
                this->texture   = part.texture;
                this->vbo_id    = part.vbo_id;
            }

            void update_buffer();
        };
        std::vector<TexturePart> parts;
    };

    static const unsigned int MAX_PLATES_COUNT = MAX_PLATE_COUNT;
    static GLTexture bed_textures[(unsigned int)btCount];
    static bool is_load_bedtype_textures;

    PartPlateList(int width, int depth, int height, Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);
    PartPlateList(Plater* platerObj, Model* modelObj, PrinterTechnology tech = ptFFF);
    ~PartPlateList();

    //this may be happened after machine changed
    void reset_size(int width, int depth, int height, bool reload_objects = true, bool update_shapes = false);
    //clear all the instances in the plate, but keep the plates
    void clear(bool delete_plates = false, bool release_print_list = false, bool except_locked = false, int plate_index = -1);
    //clear all the instances in the plate, and delete the plates, only keep the first default plate
    void reset(bool do_init);
    //compute the origin for printable plate with index i using new width
    Vec3d compute_origin_using_new_size(int i, int new_width, int new_depth);

    //reset partplate to init states
    void reinit();

    //get the plate stride
    double plate_stride_x();
    double plate_stride_y();
    void get_plate_size(int& width, int& depth, int& height) {
        width = m_plate_width;
        depth = m_plate_depth;
        height = m_plate_height;
    }

    /*basic plate operations*/
    //create an empty plate and return its index
    int create_plate(bool adjust_position = true);

    //destroy print which has the index of print_index
    int destroy_print(int print_index);

    //delete a plate by index
    int delete_plate(int index);

    //delete a plate by pointer
    //int delete_plate(PartPlate* plate);
    void delete_selected_plate();

    //get a plate pointer by index
    PartPlate* get_plate(int index);

    void get_height_limits(float& height_to_lid, float& height_to_rod)
    {
        height_to_lid = m_height_to_lid;
        height_to_rod = m_height_to_rod;
    }

    void set_height_limits_mode(PartPlate::HeightLimitMode mode)
    {
        m_height_limit_mode = mode;
    }

    int get_curr_plate_index() const { return m_current_plate; }
    PartPlate* get_curr_plate() { return m_plate_list[m_current_plate]; }

    std::vector<PartPlate*>& get_plate_list() { return m_plate_list; };

    PartPlate* get_selected_plate();

    std::vector<PartPlate*> get_nonempty_plate_list();

    std::vector<const GCodeProcessorResult*> get_nonempty_plates_slice_results();

    Vec3d get_current_plate_origin() { return compute_origin(m_current_plate, m_plate_cols); }
    Vec2d get_current_shape_position() { return compute_shape_position(m_current_plate, m_plate_cols); }
    Pointfs get_exclude_area() { return m_exclude_areas; }

    //select plate
    int select_plate(int index);

    //get the plate counts, not including the invalid plate
    int get_plate_count() const;

    //update the plate cols due to plate count change
    void update_plate_cols();

    void update_all_plates_pos_and_size(bool adjust_position = true, bool with_unprintable_move = true);

    //get the plate cols
    int get_plate_cols() { return m_plate_cols; }

    //move the plate to position index
    int move_plate_to_index(int old_index, int new_index);

    //lock plate
    int lock_plate(int index, bool state);

    //is locked
    bool is_locked(int index) { return m_plate_list[index]->is_locked();}

    //find plate by print index, return -1 if not found
    int find_plate_by_print_index(int index);

    /*instance related operations*/
    //find instance in which plate, return -1 when not found
    //this function only judges whether it is intersect with plate
    int find_instance(int obj_id, int instance_id);
    int find_instance(BoundingBoxf3& bounding_box);

    //find instance belongs to which plate
    //this function not only judges whether it is intersect with plate, but also judges whether it is fully included in plate
    //returns -1 when can not find any plate
    int find_instance_belongs(int obj_id, int instance_id);

    //notify instance's update, need to refresh the instance in plates
    int notify_instance_update(int obj_id, int instance_id);

    //notify instance is removed
    int notify_instance_removed(int obj_id, int instance_id);

    //add instance to special plate, need to remove from the original plate
    int add_to_plate(int obj_id, int instance_id, int plate_id);

    //reload all objects
    int reload_all_objects(bool except_locked = false, int plate_index = -1);

    //reload objects for newly created plate
    int construct_objects_list_for_new_plate(int plate_index);

    /* arrangement related functions */
    //compute the plate index
    int compute_plate_index(arrangement::ArrangePolygon& arrange_polygon);
    //preprocess an arrangement::ArrangePolygon, return true if it is in a locked plate
    bool preprocess_arrange_polygon(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected);
    bool preprocess_arrange_polygon_other_locked(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected);
    bool preprocess_exclude_areas(arrangement::ArrangePolygons& unselected, int num_plates = 16, float inflation = 0);
    bool preprocess_nonprefered_areas(arrangement::ArrangePolygons& regions, int num_plates = 1, float inflation=0);

    void postprocess_bed_index_for_selected(arrangement::ArrangePolygon& arrange_polygon);
    void postprocess_bed_index_for_unselected(arrangement::ArrangePolygon& arrange_polygon);
    void postprocess_bed_index_for_current_plate(arrangement::ArrangePolygon& arrange_polygon);

    //postprocess an arrangement:;ArrangePolygon
    void postprocess_arrange_polygon(arrangement::ArrangePolygon& arrange_polygon, bool selected);

    /*rendering related functions*/
    void on_change_color_mode(bool is_dark) { m_is_dark = is_dark; }
    void render(bool bottom,    bool only_current = false, bool only_body = false, int hover_id = -1);
    void render_for_picking_pass();
    void set_render_option(bool bedtype_texture, bool plate_settings);
    BoundingBoxf3& get_bounding_box() { return m_bounding_box; }
    //int select_plate_by_hover_id(int hover_id);
    int select_plate_by_obj(int obj_index, int instance_index);
    void calc_bounding_boxes();
    void select_plate_view();
    bool set_shapes(const Pointfs& shape, const Pointfs& exclude_areas, const std::string& custom_texture, float height_to_lid, float height_to_rod);
    void set_hover_id(int id);
    void reset_hover_id();
    bool intersects(const BoundingBoxf3 &bb);
    bool contains(const BoundingBoxf3 &bb);

    /*slice related functions*/
    //update current slice context into backgroud slicing process
    void update_slice_context_to_current_plate(BackgroundSlicingProcess& process);
    //return the current fff print object
    Print& get_current_fff_print() const;
    //return the slice result
    GCodeProcessorResult* get_current_slice_result() const;
    //will create a plate and load gcode, return the plate index
    int create_plate_from_gcode_file(const std::string& filename);

    //invalid all the plater's slice result
    void invalid_all_slice_result();
    //set current plater's slice result to valid
    void update_current_slice_result_state(bool valid) { m_plate_list[m_current_plate]->update_slice_result_valid_state(valid); }
    //is slice result valid or not
    bool is_all_slice_results_valid() const;
    bool is_all_slice_results_ready_for_print() const;
    bool is_all_plates_ready_for_slice() const;
    void print() const;

    //get the all the sliced result
    void get_sliced_result(std::vector<bool>& sliced_result, std::vector<std::string>& gcode_paths);
    //retruct plates structures after de-serialize
    int rebuild_plates_after_deserialize(std::vector<bool>& previous_sliced_result, std::vector<std::string>& previous_gcode_paths);

    //retruct plates structures after auto-arrangement
    int rebuild_plates_after_arrangement(bool recycle_plates = true, bool except_locked = false, int plate_index = -1);

    /* load/store releted functions, with_gcode = true and plate_idx = -1, export all gcode
    * if with_gcode = true and specify plate_idx, export plate_idx gcode only
    */
    int store_to_3mf_structure(PlateDataPtrs& plate_data_list, bool with_slice_info = true, int plate_idx = -1);
    int load_from_3mf_structure(PlateDataPtrs& plate_data_list);
    //load gcode files
    int load_gcode_files();

    template<class Archive> void serialize(Archive& ar)
    {
        //ar(cereal::base_class<ObjectBase>(this));
        ar(m_shape, m_plate_width, m_plate_depth, m_plate_height, m_height_to_lid, m_height_to_rod, m_height_limit_mode, m_plate_count, m_current_plate, m_plate_list, unprintable_plate);
        //ar(m_plate_width, m_plate_depth, m_plate_height, m_plate_count, m_current_plate);
    }

    void init_bed_type_info();
    void load_bedtype_textures();

    BedTextureInfo bed_texture_info[btCount];
};

} // namespace GUI
} // namespace Slic3r

namespace cereal
{
    template <class Archive> struct specialize<Archive, Slic3r::GUI::PartPlate, cereal::specialization::member_load_save> {};
}
#endif //__part_plate_hpp_
