#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#if ENABLE_GCODE_VIEWER
#include "libslic3r/Print.hpp"
#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#if ENABLE_GCODE_VIEWER
#include "GUI_Utils.hpp"
#include "DoubleSlider.hpp"
#include "GLCanvas3D.hpp"
#include "libslic3r/Model.hpp"
#endif // ENABLE_GCODE_VIEWER

#include <GL/glew.h>
#include <boost/log/trivial.hpp>

#include <array>
#include <algorithm>
#include <chrono>

namespace Slic3r {
namespace GUI {

static unsigned char buffer_id(GCodeProcessor::EMoveType type) {
    return static_cast<unsigned char>(type) - static_cast<unsigned char>(GCodeProcessor::EMoveType::Retract);
}

static GCodeProcessor::EMoveType buffer_type(unsigned char id) {
    return static_cast<GCodeProcessor::EMoveType>(static_cast<unsigned char>(GCodeProcessor::EMoveType::Retract) + id);
}

std::vector<std::array<float, 3>> decode_colors(const std::vector<std::string>& colors) {
    static const float INV_255 = 1.0f / 255.0f;

    std::vector<std::array<float, 3>> output(colors.size(), {0.0f, 0.0f, 0.0f} );
    for (size_t i = 0; i < colors.size(); ++i)
    {
        const std::string& color = colors[i];
        const char* c = color.data() + 1;
        if ((color.size() == 7) && (color.front() == '#')) {
            for (size_t j = 0; j < 3; ++j) {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if ((digit1 == -1) || (digit2 == -1))
                    break;

                output[i][j] = float(digit1 * 16 + digit2) * INV_255;
            }
        }
    }
    return output;
}

void GCodeViewer::VBuffer::reset()
{
    // release gpu memory
    if (vbo_id > 0) {
        glsafe(::glDeleteBuffers(1, &vbo_id));
        vbo_id = 0;
    }

    vertices_count = 0;
}

bool GCodeViewer::Path::is_path_visible(const Path& path, unsigned int flags) {
    return Extrusions::is_role_visible(flags, path.role);
};

bool GCodeViewer::Path::is_path_in_z_range(const Path& path, const std::array<double, 2>& z_range)
{
    auto in_z_range = [z_range](double z) {
        return z > z_range[0] - EPSILON && z < z_range[1] + EPSILON;
    };
    
    return in_z_range(path.first_z) || in_z_range(path.last_z);
}

void GCodeViewer::IBuffer::reset()
{
    // release gpu memory
    if (ibo_id > 0) {
        glsafe(::glDeleteBuffers(1, &ibo_id));
        ibo_id = 0;
    }

    // release cpu memory
    data = std::vector<unsigned int>();
    data_size = 0;
    paths = std::vector<Path>();
}

bool GCodeViewer::IBuffer::init_shader(const std::string& vertex_shader_src, const std::string& fragment_shader_src)
{
    if (!shader.init(vertex_shader_src, fragment_shader_src)) {
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize toolpaths shader: please, check that the files " << vertex_shader_src << " and " << fragment_shader_src << " are available";
        return false;
    }

    return true;
}

void GCodeViewer::IBuffer::add_path(const GCodeProcessor::MoveVertex& move)
{
    unsigned int id = static_cast<unsigned int>(data.size());
    double z = static_cast<double>(move.position[2]);
    paths.push_back({ move.type, move.extrusion_role, id, id, z, z, move.delta_extruder, move.height, move.width, move.feedrate, move.fan_speed, move.volumetric_rate(), move.extruder_id, move.cp_color_id });
}

std::array<float, 3> GCodeViewer::Extrusions::Range::get_color_at(float value) const
{
    // Input value scaled to the colors range
    const float step = step_size();
    const float global_t = (step != 0.0f) ? std::max(0.0f, value - min) / step : 0.0f; // lower limit of 0.0f

    const size_t color_max_idx = Range_Colors.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const size_t color_low_idx = std::clamp<size_t>(static_cast<size_t>(global_t), 0, color_max_idx);
    const size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Compute how far the value is between the low and high colors so that they can be interpolated
    const float local_t = std::clamp(global_t - static_cast<float>(color_low_idx), 0.0f, 1.0f);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    std::array<float, 3> ret;
    for (unsigned int i = 0; i < 3; ++i) {
        ret[i] = lerp(Range_Colors[color_low_idx][i], Range_Colors[color_high_idx][i], local_t);
    }
    return ret;
}

const std::vector<std::array<float, 3>> GCodeViewer::Extrusion_Role_Colors {{
    { 0.50f, 0.50f, 0.50f },   // erNone
    { 1.00f, 1.00f, 0.40f },   // erPerimeter
    { 1.00f, 0.65f, 0.00f },   // erExternalPerimeter
    { 0.00f, 0.00f, 1.00f },   // erOverhangPerimeter
    { 0.69f, 0.19f, 0.16f },   // erInternalInfill
    { 0.84f, 0.20f, 0.84f },   // erSolidInfill
    { 1.00f, 0.10f, 0.10f },   // erTopSolidInfill
    { 0.60f, 0.60f, 1.00f },   // erBridgeInfill
    { 1.00f, 1.00f, 1.00f },   // erGapFill
    { 0.52f, 0.48f, 0.13f },   // erSkirt
    { 0.00f, 1.00f, 0.00f },   // erSupportMaterial
    { 0.00f, 0.50f, 0.00f },   // erSupportMaterialInterface
    { 0.70f, 0.89f, 0.67f },   // erWipeTower
    { 0.16f, 0.80f, 0.58f },   // erCustom
    { 0.00f, 0.00f, 0.00f }    // erMixed
}};

const std::vector<std::array<float, 3>> GCodeViewer::Travel_Colors {{
    { 0.0f, 0.0f, 0.5f }, // Move
    { 0.0f, 0.5f, 0.0f }, // Extrude
    { 0.5f, 0.0f, 0.0f }  // Retract
}};

const std::vector<std::array<float, 3>> GCodeViewer::Range_Colors {{
    { 0.043f, 0.173f, 0.478f }, // bluish
    { 0.075f, 0.349f, 0.522f },
    { 0.110f, 0.533f, 0.569f },
    { 0.016f, 0.839f, 0.059f },
    { 0.667f, 0.949f, 0.000f },
    { 0.988f, 0.975f, 0.012f },
    { 0.961f, 0.808f, 0.039f },
    { 0.890f, 0.533f, 0.125f },
    { 0.820f, 0.408f, 0.188f },
    { 0.761f, 0.322f, 0.235f }  // reddish
}};

void GCodeViewer::load(const GCodeProcessor::Result& gcode_result, const Print& print, bool initialized)
{
    // avoid processing if called with the same gcode_result
    if (m_last_result_id == gcode_result.id)
        return;

    m_last_result_id = gcode_result.id;

    // release gpu memory, if used
    reset();

    load_toolpaths(gcode_result);
    load_shells(print, initialized);
}

void GCodeViewer::refresh(const GCodeProcessor::Result& gcode_result, const std::vector<std::string>& str_tool_colors)
{
    if (m_vertices.vertices_count == 0)
        return;

    // update tool colors
    m_tool_colors = decode_colors(str_tool_colors);

    // update ranges for coloring / legend
    m_extrusions.reset_ranges();
    for (size_t i = 0; i < m_vertices.vertices_count; ++i)
    {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        switch (curr.type)
        {
        case GCodeProcessor::EMoveType::Extrude:
        {
            m_extrusions.ranges.height.update_from(curr.height);
            m_extrusions.ranges.width.update_from(curr.width);
            m_extrusions.ranges.fan_speed.update_from(curr.fan_speed);
            m_extrusions.ranges.volumetric_rate.update_from(curr.volumetric_rate());
            [[fallthrough]];
        }
        case GCodeProcessor::EMoveType::Travel:
        {
            if (m_buffers[buffer_id(curr.type)].visible)
                m_extrusions.ranges.feedrate.update_from(curr.feedrate);

            break;
        }
        default: { break; }
        }
    }
}

void GCodeViewer::reset()
{
    m_vertices.reset();

    for (IBuffer& buffer : m_buffers) {
        buffer.reset();
    }

    m_bounding_box = BoundingBoxf3();
    m_tool_colors = std::vector<std::array<float, 3>>();
    m_extruder_ids = std::vector<unsigned char>();
    m_extrusions.reset_role_visibility_flags();
    m_extrusions.reset_ranges();
    m_shells.volumes.clear();
    m_layers_zs = std::vector<double>();
    m_layers_z_range = { 0.0, 0.0 };
    m_roles = std::vector<ExtrusionRole>();
}

void GCodeViewer::render() const
{
    glsafe(::glEnable(GL_DEPTH_TEST));
    render_toolpaths();
    render_shells();
    render_overlay();
}

bool GCodeViewer::is_toolpath_move_type_visible(GCodeProcessor::EMoveType type) const
{
    size_t id = static_cast<size_t>(buffer_id(type));
    return (id < m_buffers.size()) ? m_buffers[id].visible : false;
}

void GCodeViewer::set_toolpath_move_type_visible(GCodeProcessor::EMoveType type, bool visible)
{
    size_t id = static_cast<size_t>(buffer_id(type));
    if (id < m_buffers.size())
        m_buffers[id].visible = visible;
}

void GCodeViewer::set_options_visibility_from_flags(unsigned int flags)
{
    auto is_flag_set = [flags](unsigned int flag) {
        return (flags& (1 << flag)) != 0;
    };

    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Travel, is_flag_set(0));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Retract, is_flag_set(1));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Unretract, is_flag_set(2));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Tool_change, is_flag_set(3));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Color_change, is_flag_set(4));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Pause_Print, is_flag_set(5));
    set_toolpath_move_type_visible(GCodeProcessor::EMoveType::Custom_GCode, is_flag_set(6));
    m_shells.visible = is_flag_set(7);
    enable_legend(is_flag_set(8));
}

bool GCodeViewer::init_shaders()
{
    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    for (unsigned char i = begin_id; i < end_id; ++i)
    {
        std::string vertex_shader;
        std::string fragment_shader;

        switch (buffer_type(i))
        {
        case GCodeProcessor::EMoveType::Tool_change:  { vertex_shader = "toolchanges.vs"; fragment_shader = "toolchanges.fs"; break; }
        case GCodeProcessor::EMoveType::Color_change: { vertex_shader = "colorchanges.vs"; fragment_shader = "colorchanges.fs"; break; }
        case GCodeProcessor::EMoveType::Pause_Print:  { vertex_shader = "pauses.vs"; fragment_shader = "pauses.fs"; break; }
        case GCodeProcessor::EMoveType::Custom_GCode: { vertex_shader = "customs.vs"; fragment_shader = "customs.fs"; break; }
        case GCodeProcessor::EMoveType::Retract:      { vertex_shader = "retractions.vs"; fragment_shader = "retractions.fs"; break; }
        case GCodeProcessor::EMoveType::Unretract:    { vertex_shader = "unretractions.vs"; fragment_shader = "unretractions.fs"; break; }
        case GCodeProcessor::EMoveType::Extrude:      { vertex_shader = "extrusions.vs"; fragment_shader = "extrusions.fs"; break; }
        case GCodeProcessor::EMoveType::Travel:       { vertex_shader = "travels.vs"; fragment_shader = "travels.fs"; break; }
        default: { break; }
        }

        if (vertex_shader.empty() || fragment_shader.empty() || !m_buffers[i].init_shader(vertex_shader, fragment_shader))
            return false;
    }

    if (!m_shells.shader.init("shells.vs", "shells.fs")) {
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize shells shader: please, check that the files shells.vs and shells.fs are available";
        return false;
    }

    return true;
}

void GCodeViewer::load_toolpaths(const GCodeProcessor::Result& gcode_result)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    // vertex data
    m_vertices.vertices_count = gcode_result.moves.size();
    if (m_vertices.vertices_count == 0)
        return;

    // vertex data / bounding box -> extract from result
    std::vector<float> vertices_data;
    for (const GCodeProcessor::MoveVertex& move : gcode_result.moves) {
        for (int j = 0; j < 3; ++j) {
            vertices_data.insert(vertices_data.end(), move.position[j]);
            m_bounding_box.merge(move.position.cast<double>());
        }
    }

    // vertex data -> send to gpu
    glsafe(::glGenBuffers(1, &m_vertices.vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, vertices_data.size() * sizeof(float), vertices_data.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // vertex data -> free ram
    vertices_data = std::vector<float>();

    // indices data -> extract from result
    for (size_t i = 0; i < m_vertices.vertices_count; ++i)
    {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessor::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessor::MoveVertex& curr = gcode_result.moves[i];

        IBuffer& buffer = m_buffers[buffer_id(curr.type)];

        switch (curr.type)
        {
        case GCodeProcessor::EMoveType::Tool_change:
        case GCodeProcessor::EMoveType::Color_change:
        case GCodeProcessor::EMoveType::Pause_Print:
        case GCodeProcessor::EMoveType::Custom_GCode:
        case GCodeProcessor::EMoveType::Retract:
        case GCodeProcessor::EMoveType::Unretract:
        {
            buffer.add_path(curr);
            buffer.data.push_back(static_cast<unsigned int>(i));
            break;
        }
        case GCodeProcessor::EMoveType::Extrude:
        case GCodeProcessor::EMoveType::Travel:
        {
            if (prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                buffer.add_path(curr);
                buffer.data.push_back(static_cast<unsigned int>(i - 1));
            }
            
            buffer.paths.back().last = static_cast<unsigned int>(buffer.data.size());
            buffer.data.push_back(static_cast<unsigned int>(i));
            break;
        }
        default:
        {
            continue;
        }
        }
    }

    // indices data -> send data to gpu
    for (IBuffer& buffer : m_buffers)
    {
        buffer.data_size = buffer.data.size();
        if (buffer.data_size > 0) {
            glsafe(::glGenBuffers(1, &buffer.ibo_id));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo_id));
            glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, buffer.data_size * sizeof(unsigned int), buffer.data.data(), GL_STATIC_DRAW));
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

            // indices data -> free ram
            buffer.data = std::vector<unsigned int>();
        }
    }

    // layers zs / roles / extruder ids / cp color ids -> extract from result
    for (const GCodeProcessor::MoveVertex& move : gcode_result.moves) {
        if (move.type == GCodeProcessor::EMoveType::Extrude)
            m_layers_zs.emplace_back(static_cast<double>(move.position[2]));

        m_roles.emplace_back(move.extrusion_role);
        m_extruder_ids.emplace_back(move.extruder_id);
    }

    // layers zs -> replace intervals of layers with similar top positions with their average value.
    std::sort(m_layers_zs.begin(), m_layers_zs.end());
    int n = int(m_layers_zs.size());
    int k = 0;
    for (int i = 0; i < n;) {
        int j = i + 1;
        double zmax = m_layers_zs[i] + EPSILON;
        for (; j < n && m_layers_zs[j] <= zmax; ++j);
        m_layers_zs[k++] = (j > i + 1) ? (0.5 * (m_layers_zs[i] + m_layers_zs[j - 1])) : m_layers_zs[i];
        i = j;
    }
    if (k < n)
        m_layers_zs.erase(m_layers_zs.begin() + k, m_layers_zs.end());

    // set layers z range
    m_layers_z_range = { m_layers_zs.front(), m_layers_zs.back() };

    // roles -> remove duplicates
    std::sort(m_roles.begin(), m_roles.end());
    m_roles.erase(std::unique(m_roles.begin(), m_roles.end()), m_roles.end());

    // extruder ids -> remove duplicates
    std::sort(m_extruder_ids.begin(), m_extruder_ids.end());
    m_extruder_ids.erase(std::unique(m_extruder_ids.begin(), m_extruder_ids.end()), m_extruder_ids.end());

    auto end_time = std::chrono::high_resolution_clock::now();
    std::cout << "toolpaths generation time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms \n";
}

void GCodeViewer::load_shells(const Print& print, bool initialized)
{
    if (print.objects().empty())
        // no shells, return
        return;

    // adds objects' volumes 
    int object_id = 0;
    for (const PrintObject* obj : print.objects())
    {
        const ModelObject* model_obj = obj->model_object();

        std::vector<int> instance_ids(model_obj->instances.size());
        for (int i = 0; i < (int)model_obj->instances.size(); ++i) {
            instance_ids[i] = i;
        }

        m_shells.volumes.load_object(model_obj, object_id, instance_ids, "object", initialized);

        ++object_id;
    }

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF) {
        // adds wipe tower's volume
        double max_z = print.objects()[0]->model_object()->get_model()->bounding_box().max(2);
        const PrintConfig& config = print.config();
        size_t extruders_count = config.nozzle_diameter.size();
        if ((extruders_count > 1) && config.wipe_tower && !config.complete_objects) {
            const DynamicPrintConfig& print_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            double layer_height = print_config.opt_float("layer_height");
            double first_layer_height = print_config.get_abs_value("first_layer_height", layer_height);
            double nozzle_diameter = print.config().nozzle_diameter.values[0];
            float depth = print.wipe_tower_data(extruders_count, first_layer_height, nozzle_diameter).depth;
            float brim_width = print.wipe_tower_data(extruders_count, first_layer_height, nozzle_diameter).brim_width;

            m_shells.volumes.load_wipe_tower_preview(1000, config.wipe_tower_x, config.wipe_tower_y, config.wipe_tower_width, depth, max_z, config.wipe_tower_rotation_angle,
                !print.is_step_done(psWipeTower), brim_width, initialized);
        }
    }

    for (GLVolume* volume : m_shells.volumes.volumes)
    {
        volume->zoom_to_volumes = false;
        volume->color[3] = 0.25f;
        volume->force_native_color = true;
        volume->set_render_color();
    }
}

void GCodeViewer::render_toolpaths() const
{
    auto extrusion_color = [this](const Path& path) {
        std::array<float, 3> color;
        switch (m_view_type)
        {
        case EViewType::FeatureType:    { color = Extrusion_Role_Colors[static_cast<unsigned int>(path.role)]; break; }
        case EViewType::Height:         { color = m_extrusions.ranges.height.get_color_at(path.height); break; }
        case EViewType::Width:          { color = m_extrusions.ranges.width.get_color_at(path.width); break; }
        case EViewType::Feedrate:       { color = m_extrusions.ranges.feedrate.get_color_at(path.feedrate); break; }
        case EViewType::FanSpeed:       { color = m_extrusions.ranges.fan_speed.get_color_at(path.fan_speed); break; }
        case EViewType::VolumetricRate: { color = m_extrusions.ranges.volumetric_rate.get_color_at(path.volumetric_rate); break; }
        case EViewType::Tool:           { color = m_tool_colors[path.extruder_id]; break; }
        case EViewType::ColorPrint:     { color = m_tool_colors[path.cp_color_id]; break; }
        default:                        { color = { 1.0f, 1.0f, 1.0f }; break; }
        }
        return color;
    };

    auto travel_color = [this](const Path& path) {
        return (path.delta_extruder < 0.0f) ? Travel_Colors[2] /* Retract */ :
              ((path.delta_extruder > 0.0f) ? Travel_Colors[1] /* Extrude */ :
                Travel_Colors[0] /* Move */);
    };

    auto set_color = [](GLint current_program_id, const std::array<float, 3>& color) {
        if (current_program_id > 0) {
            GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
            if (color_id >= 0) {
                glsafe(::glUniform3fv(color_id, 1, (const GLfloat*)color.data()));
                return;
            }
        }
        BOOST_LOG_TRIVIAL(error) << "Unable to find uniform_color uniform";
    };

    glsafe(::glCullFace(GL_BACK));
    glsafe(::glLineWidth(3.0f));

    unsigned char begin_id = buffer_id(GCodeProcessor::EMoveType::Retract);
    unsigned char end_id = buffer_id(GCodeProcessor::EMoveType::Count);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vertices.vbo_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, VBuffer::vertex_size_bytes(), (const void*)0));
    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    for (unsigned char i = begin_id; i < end_id; ++i) {
        const IBuffer& buffer = m_buffers[i];
        if (buffer.ibo_id == 0)
            continue;
        
        if (!buffer.visible)
            continue;

        if (buffer.shader.is_initialized()) {
            GCodeProcessor::EMoveType type = buffer_type(i);

            buffer.shader.start_using();
            
            GLint current_program_id;
            glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.ibo_id));

            switch (type)
            {
            case GCodeProcessor::EMoveType::Tool_change:
            {
                std::array<float, 3> color = { 1.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                    glsafe(::glDrawElements(GL_POINTS, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                    glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Color_change:
            {
                std::array<float, 3> color = { 1.0f, 0.0f, 0.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                    glsafe(::glDrawElements(GL_POINTS, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                    glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Pause_Print:
            {
                std::array<float, 3> color = { 0.0f, 1.0f, 0.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                    glsafe(::glDrawElements(GL_POINTS, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                    glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Custom_GCode:
            {
                std::array<float, 3> color = { 0.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                    glsafe(::glDrawElements(GL_POINTS, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                    glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Retract:
            {
                std::array<float, 3> color = { 1.0f, 0.0f, 1.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                    glsafe(::glDrawElements(GL_POINTS, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                    glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Unretract:
            {
                std::array<float, 3> color = { 0.0f, 1.0f, 1.0f };
                set_color(current_program_id, color);
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    glsafe(::glEnable(GL_PROGRAM_POINT_SIZE));
                    glsafe(::glDrawElements(GL_POINTS, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                    glsafe(::glDisable(GL_PROGRAM_POINT_SIZE));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Extrude:
            {
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_visible(path, m_extrusions.role_visibility_flags) || !Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    set_color(current_program_id, extrusion_color(path));
                    glsafe(::glDrawElements(GL_LINE_STRIP, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                }
                break;
            }
            case GCodeProcessor::EMoveType::Travel:
            {
                for (const Path& path : buffer.paths) {
                    if (!Path::is_path_in_z_range(path, m_layers_z_range))
                        continue;

                    set_color(current_program_id, (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool || m_view_type == EViewType::ColorPrint) ? extrusion_color(path) : travel_color(path));
                    glsafe(::glDrawElements(GL_LINE_STRIP, GLsizei(path.last - path.first + 1), GL_UNSIGNED_INT, (const void*)(path.first * sizeof(GLuint))));
                }
                break;
            }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            buffer.shader.stop_using();
        }
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GCodeViewer::render_shells() const
{
    if (!m_shells.visible || m_shells.volumes.empty() || !m_shells.shader.is_initialized())
        return;

//    glsafe(::glDepthMask(GL_FALSE));

    m_shells.shader.start_using();
    m_shells.volumes.render(GLVolumeCollection::Transparent, true, wxGetApp().plater()->get_camera().get_view_matrix());
    m_shells.shader.stop_using();

//    glsafe(::glDepthMask(GL_TRUE));
}

void GCodeViewer::render_overlay() const
{
    static const ImVec4 ORANGE(1.0f, 0.49f, 0.22f, 1.0f);
    static const float ICON_BORDER_SIZE = 25.0f;
    static const ImU32 ICON_BORDER_COLOR = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    static const float GAP_ICON_TEXT = 7.5f;

    if (!m_legend_enabled || m_roles.empty())
        return;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    imgui.set_next_window_pos(0, 0, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    imgui.begin(std::string("Legend"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    if (ImGui::IsWindowAppearing())
        // force an extra farme
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    auto add_item = [draw_list, &imgui](const std::array<float, 3>& color, const std::string& label) {
        // draw icon
        ImVec2 pos(ImGui::GetCursorPosX() + 2.0f, ImGui::GetCursorPosY() + 2.0f);
        draw_list->AddRect({ pos.x, pos.y }, { pos.x + ICON_BORDER_SIZE, pos.y + ICON_BORDER_SIZE }, ICON_BORDER_COLOR, 0.0f, 0);
        draw_list->AddRectFilled({ pos.x + 1.0f, pos.y + 1.0f },
                                 { pos.x + ICON_BORDER_SIZE - 1.0f, pos.y + ICON_BORDER_SIZE - 1.0f },
                                 ImGui::GetColorU32({ color[0], color[1], color[2], 1.0f }));
        // draw text
        ImGui::SetCursorPos({ pos.x + ICON_BORDER_SIZE + GAP_ICON_TEXT, pos.y + 0.5f * (ICON_BORDER_SIZE - ImGui::GetTextLineHeight()) });
        imgui.text(label);
    };

    auto add_range = [this, draw_list, &imgui, add_item](const Extrusions::Range& range, unsigned int decimals) {
        auto add_range_item = [this, draw_list, &imgui, add_item](int i, float value, unsigned int decimals) {
            char buf[1024];
            ::sprintf(buf, "%.*f", decimals, value);
            add_item(Range_Colors[i], buf);
        };

        float step_size = range.step_size();
        if (step_size == 0.0f)
            // single item use case
            add_range_item(0, range.min, decimals);
        else
        {
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                add_range_item(i, range.min + static_cast<float>(i) * step_size, decimals);
            }
        }
    };

    // extrusion paths -> title
    ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
    switch (m_view_type)
    {
    case EViewType::FeatureType:    { imgui.text(I18N::translate_utf8(L("Feature type"))); break; }
    case EViewType::Height:         { imgui.text(I18N::translate_utf8(L("Height (mm)"))); break; }
    case EViewType::Width:          { imgui.text(I18N::translate_utf8(L("Width (mm)"))); break; }
    case EViewType::Feedrate:       { imgui.text(I18N::translate_utf8(L("Speed (mm/s)"))); break; }
    case EViewType::FanSpeed:       { imgui.text(I18N::translate_utf8(L("Fan Speed (%%)"))); break; }
    case EViewType::VolumetricRate: { imgui.text(I18N::translate_utf8(L("Volumetric flow rate (mm³/s)"))); break; }
    case EViewType::Tool:           { imgui.text(I18N::translate_utf8(L("Tool"))); break; }
    case EViewType::ColorPrint:     { imgui.text(I18N::translate_utf8(L("Color Print"))); break; }
    default:                        { break; }
    }
    ImGui::PopStyleColor();
    ImGui::Separator();

    // extrusion paths -> items
    switch (m_view_type)
    {
    case EViewType::FeatureType:
    {
        for (ExtrusionRole role : m_roles) {
            bool visible = m_extrusions.is_role_visible(role);
            if (!visible)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3333f);

            add_item(Extrusion_Role_Colors[static_cast<unsigned int>(role)], I18N::translate_utf8(ExtrusionEntity::role_to_string(role)));
            if (!visible)
                ImGui::PopStyleVar();
        }
        break;
    }
    case EViewType::Height:         { add_range(m_extrusions.ranges.height, 3); break; }
    case EViewType::Width:          { add_range(m_extrusions.ranges.width, 3); break; }
    case EViewType::Feedrate:       { add_range(m_extrusions.ranges.feedrate, 1); break; }
    case EViewType::FanSpeed:       { add_range(m_extrusions.ranges.fan_speed, 0); break; }
    case EViewType::VolumetricRate: { add_range(m_extrusions.ranges.volumetric_rate, 3); break; }
    case EViewType::Tool:
    {
        size_t tools_count = m_tool_colors.size();
        for (size_t i = 0; i < tools_count; ++i) {
            // shows only extruders actually used
            auto it = std::find(m_extruder_ids.begin(), m_extruder_ids.end(), static_cast<unsigned char>(i));
            if (it == m_extruder_ids.end())
                continue;

            add_item(m_tool_colors[i], (boost::format(I18N::translate_utf8(L("Extruder %d"))) % (i + 1)).str());
        }
        break;
    }
    case EViewType::ColorPrint:
    {
        const std::vector<CustomGCode::Item>& custom_gcode_per_print_z = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
        const int extruders_count = wxGetApp().extruders_edited_cnt();
        if (extruders_count == 1) { // single extruder use case
            if (custom_gcode_per_print_z.empty())
                // no data to show
                add_item(m_tool_colors.front(), I18N::translate_utf8(L("Default print color")));
            else {
                std::vector<std::pair<double, double>> cp_values;
                cp_values.reserve(custom_gcode_per_print_z.size());

                for (auto custom_code : custom_gcode_per_print_z) {
                    if (custom_code.gcode != ColorChangeCode)
                        continue;

                    auto lower_b = std::lower_bound(m_layers_zs.begin(), m_layers_zs.end(), custom_code.print_z - Slic3r::DoubleSlider::epsilon());

                    if (lower_b == m_layers_zs.end())
                        continue;

                    double current_z = *lower_b;
                    double previous_z = lower_b == m_layers_zs.begin() ? 0.0 : *(--lower_b);

                    // to avoid duplicate values, check adding values
                    if (cp_values.empty() || !(cp_values.back().first == previous_z && cp_values.back().second == current_z))
                        cp_values.emplace_back(std::make_pair(previous_z, current_z));
                }

                const int items_cnt = static_cast<int>(cp_values.size());
                if (items_cnt == 0) { // There is no one color change, but there are some pause print or custom Gcode
                    add_item(m_tool_colors.front(), I18N::translate_utf8(L("Default print color")));
                }
                else {
                    for (int i = items_cnt; i >= 0; --i) {
                        // create label for color change item
                        std::string id_str = " (" + std::to_string(i + 1) + ")";

                        if (i == 0) {
                            add_item(m_tool_colors[i], (boost::format(I18N::translate_utf8(L("up to %.2f mm"))) % cp_values.front().first).str() + id_str);
                            break;
                        }
                        else if (i == items_cnt) {
                            add_item(m_tool_colors[i], (boost::format(I18N::translate_utf8(L("above %.2f mm"))) % cp_values[i - 1].second).str() + id_str);
                            continue;
                        }
                        add_item(m_tool_colors[i], (boost::format(I18N::translate_utf8(L("%.2f - %.2f mm"))) % cp_values[i - 1].second % cp_values[i].first).str() + id_str);
                    }
                }
            }
        }
        else // multi extruder use case
        {
            // extruders
            for (unsigned int i = 0; i < (unsigned int)extruders_count; ++i) {
                add_item(m_tool_colors[i], (boost::format(I18N::translate_utf8(L("Extruder %d"))) % (i + 1)).str());
            }

            // color changes
            int color_change_idx = 1 + static_cast<int>(m_tool_colors.size()) - extruders_count;
            size_t last_color_id = m_tool_colors.size() - 1;
            for (int i = static_cast<int>(custom_gcode_per_print_z.size()) - 1; i >= 0; --i) {
                if (custom_gcode_per_print_z[i].gcode == ColorChangeCode) {
                    // create label for color change item
                    std::string id_str = " (" + std::to_string(color_change_idx--) + ")";

                    add_item(m_tool_colors[last_color_id--],
                             (boost::format(I18N::translate_utf8(L("Color change for Extruder %d at %.2f mm"))) % custom_gcode_per_print_z[i].extruder % custom_gcode_per_print_z[i].print_z).str() + id_str);
                }
            }
        }

        break;
    }
    default: { break; }
    }

    // travel paths
    if (m_buffers[buffer_id(GCodeProcessor::EMoveType::Travel)].visible)
    {
        switch (m_view_type)
        {
        case EViewType::Feedrate:
        case EViewType::Tool:
        case EViewType::ColorPrint:
        {
            break;
        }
        default:
        {
            // title
            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
            imgui.text(I18N::translate_utf8(L("Travel")));
            ImGui::PopStyleColor();
            ImGui::Separator();

            // items
            add_item(Travel_Colors[0], I18N::translate_utf8(L("Movement")));
            add_item(Travel_Colors[1], I18N::translate_utf8(L("Extrusion")));
            add_item(Travel_Colors[2], I18N::translate_utf8(L("Retraction")));

            break;
        }
        }
    }

    imgui.end();
    ImGui::PopStyleVar();
}

} // namespace GUI
} // namespace Slic3r

#endif // ENABLE_GCODE_VIEWER
