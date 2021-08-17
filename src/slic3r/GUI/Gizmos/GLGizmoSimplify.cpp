// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoSimplify.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

#include <chrono>
#include <thread>

namespace Slic3r::GUI {

GLGizmoSimplify::GLGizmoSimplify(GLCanvas3D &       parent,
                                 const std::string &icon_filename,
                                 unsigned int       sprite_id)
    : GLGizmoBase(parent, icon_filename, -1)
    , m_state(State::settings)
    , m_is_valid_result(false)
    , m_progress(0)
    , m_volume(nullptr)
    , m_obj_index(0)
    , m_need_reload(false)
{}

GLGizmoSimplify::~GLGizmoSimplify() { 
    m_state = State::canceling;
    if (m_worker.joinable()) m_worker.join();
}

bool GLGizmoSimplify::on_init()
{
    //m_grabbers.emplace_back();
    //m_shortcut_key = WXK_CONTROL_C;
    return true;
}


std::string GLGizmoSimplify::on_get_name() const
{
    return (_L("Simplify")).ToUTF8().data();
}

void GLGizmoSimplify::on_render() {}
void GLGizmoSimplify::on_render_for_picking() {}

void GLGizmoSimplify::on_render_input_window(float x, float y, float bottom_limit)
{
    const int min_triangle_count = 4; // tetrahedron
    const int max_char_in_name = 25;
    create_gui_cfg();

    const Selection &selection = m_parent.get_selection();
    int object_idx = selection.get_object_idx();
    ModelObject *obj = wxGetApp().plater()->model().objects[object_idx];
    ModelVolume *act_volume = obj->volumes.front();

    // Check selection of new volume
    // Do not reselect object when processing 
    if (act_volume != m_volume && m_state == State::settings) {
        bool change_window_position = (m_volume == nullptr);
        // select different model
        if (m_volume != nullptr && m_original_its.has_value()) {
            set_its(*m_original_its);
        }

        m_obj_index = object_idx; // to remember correct object
        m_volume = act_volume;
        m_original_its = {};
        const TriangleMesh &tm = m_volume->mesh();
        m_configuration.wanted_percent = 50.; // default value
        m_configuration.update_percent(tm.its.indices.size());
        m_is_valid_result = false;

        if (change_window_position) {
            ImVec2 pos = ImGui::GetMousePos();
            pos.x -= m_gui_cfg->window_offset;
            pos.y -= m_gui_cfg->window_offset;
            // minimal top left value
            ImVec2 tl(m_gui_cfg->window_padding, m_gui_cfg->window_padding);
            if (pos.x < tl.x) pos.x = tl.x;
            if (pos.y < tl.y) pos.y = tl.y;
            // maximal bottom right value
            auto parent_size = m_parent.get_canvas_size();
            ImVec2 br(
                parent_size.get_width() - (2 * m_gui_cfg->window_offset + m_gui_cfg->window_padding), 
                parent_size.get_height() - (2 * m_gui_cfg->window_offset + m_gui_cfg->window_padding));
            if (pos.x > br.x) pos.x = br.x;
            if (pos.y > br.y) pos.y = br.y;
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        }
    }

    int flag = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoCollapse;
    m_imgui->begin(on_get_name(), flag);

    size_t triangle_count = m_volume->mesh().its.indices.size();
    // already reduced mesh
    if (m_original_its.has_value())
        triangle_count = m_original_its->indices.size();

    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _L("Mesh name") + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);
    std::string name = m_volume->name;
    if (name.length() > max_char_in_name)
        name = name.substr(0, max_char_in_name-3) + "...";
    m_imgui->text(name);
    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _L("Triangles") + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);
    m_imgui->text(std::to_string(triangle_count));

    ImGui::Separator();

    ImGui::Text(_L("Limit by triangles").c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    // First initialization + fix triangle count
    if (m_imgui->checkbox("##UseCount", m_configuration.use_count)) {
        if (!m_configuration.use_count) m_configuration.use_error = true;
        m_is_valid_result = false;
    }

    m_imgui->disabled_begin(!m_configuration.use_count);
    ImGui::Text(_L("Triangle count").c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    int wanted_count = m_configuration.wanted_count;
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);
    if (ImGui::SliderInt("##triangle_count", &wanted_count, min_triangle_count, triangle_count, "%d")) {
        m_configuration.wanted_count = static_cast<uint32_t>(wanted_count);
        if (m_configuration.wanted_count < min_triangle_count)
            m_configuration.wanted_count = min_triangle_count;
        if (m_configuration.wanted_count > triangle_count) 
            m_configuration.wanted_count = triangle_count;
        m_configuration.update_count(triangle_count);
        m_is_valid_result = false;
    }
    ImGui::Text(_L("Ratio").c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::SetNextItemWidth(m_gui_cfg->input_small_width);
    const char * precision = (m_configuration.wanted_percent > 10)? "%.0f": 
        ((m_configuration.wanted_percent > 1)? "%.1f":"%.2f");
    float step = (m_configuration.wanted_percent > 10)? 1.f: 
        ((m_configuration.wanted_percent > 1)? 0.1f : 0.01f);
    if (ImGui::InputFloat("%", &m_configuration.wanted_percent, step, 10*step, precision)) {
        if (m_configuration.wanted_percent > 100.f) m_configuration.wanted_percent = 100.f;
        m_configuration.update_percent(triangle_count);
        if (m_configuration.wanted_count < min_triangle_count) {
            m_configuration.wanted_count = min_triangle_count;
            m_configuration.update_count(triangle_count);
        }
        m_is_valid_result = false;
    }
    m_imgui->disabled_end(); // use_count

    ImGui::NewLine();
    ImGui::Text(_L("Limit by error").c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    if (m_imgui->checkbox("##UseError", m_configuration.use_error)) {
        if (!m_configuration.use_error) m_configuration.use_count = true;
        m_is_valid_result = false;
    }

    m_imgui->disabled_begin(!m_configuration.use_error);
    ImGui::Text(_L("Max. error").c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::SetNextItemWidth(m_gui_cfg->input_small_width);
    if (ImGui::InputFloat("##maxError", &m_configuration.max_error, 0.01f, .1f, "%.2f")) {
        if (m_configuration.max_error < 0.f) m_configuration.max_error = 0.f;
        m_is_valid_result = false;
    }
    m_imgui->disabled_end(); // use_error

    if (m_state == State::settings) {
        if (m_imgui->button(_L("Cancel"))) {
            if (m_original_its.has_value()) { 
                set_its(*m_original_its);
                m_state = State::close_on_end;
            } else {
                close();
            }
        }
        ImGui::SameLine(m_gui_cfg->bottom_left_width);
        if (m_imgui->button(_L("Preview"))) {
            m_state = State::preview;
            // simplify but not aply on mesh
            process();
        }
        ImGui::SameLine();
        if (m_imgui->button(_L("Apply"))) {
            if (!m_is_valid_result) {
                m_state = State::close_on_end;
                process();
            } else {
                // use preview and close
                close();
            }
        }
    } else {        
        m_imgui->disabled_begin(m_state == State::canceling);
        if (m_imgui->button(_L("Cancel"))) m_state = State::canceling;
        m_imgui->disabled_end(); 

        ImGui::SameLine(m_gui_cfg->bottom_left_width);
        // draw progress bar
        char buf[32];
        sprintf(buf, L("Process %d / 100"), m_progress);
        ImGui::ProgressBar(m_progress / 100., ImVec2(m_gui_cfg->input_width, 0.f), buf);
    }
    m_imgui->end();

    if (m_need_reload) { 
        m_need_reload = false;

        // Reload visualization of mesh - change VBO, FBO on GPU
        m_parent.reload_scene(true);
        if (m_state == State::close_on_end) {
            // fix hollowing, sla support points, modifiers, ...
            auto plater = wxGetApp().plater();
            plater->changed_mesh(m_obj_index);
            close(); 
        }

        // change from simplifying | apply
        m_state = State::settings;           
        
        // Fix warning icon in object list
        wxGetApp().obj_list()->update_item_error_icon(m_obj_index, -1);
    }
}

void GLGizmoSimplify::close() {
    // close gizmo == open it again
    GLGizmosManager &gizmos_mgr = m_parent.get_gizmos_manager();
    gizmos_mgr.open_gizmo(GLGizmosManager::EType::Simplify);
}


void GLGizmoSimplify::process()
{
    class SimplifyCanceledException : public std::exception {
    public:
       const char* what() const throw() { return L("Model simplification has been canceled"); }
    };

    if (!m_original_its.has_value())
        m_original_its = m_volume->mesh().its; // copy

    auto plater = wxGetApp().plater();
    plater->take_snapshot(_L("Simplify ") + m_volume->name);
    plater->clear_before_change_mesh(m_obj_index);
    m_progress = 0;
    if (m_worker.joinable()) m_worker.join();
    m_worker = std::thread([&]() {
        // store original triangles        
        uint32_t triangle_count = (m_configuration.use_count) ? m_configuration.wanted_count : 0;
        float    max_error      = (m_configuration.use_error) ? 
            m_configuration.max_error : std::numeric_limits<float>::max();

        std::function<void(void)> throw_on_cancel = [&]() {
            if (m_state == State::canceling) {
                throw SimplifyCanceledException();
            }
        };
        std::function<void(int)> statusfn = [&](int percent) { 
            m_progress = percent;
            m_parent.schedule_extra_frame(0);
        };

        indexed_triangle_set collapsed;
        if (m_last_error.has_value() && m_last_count.has_value() &&             
            (!m_configuration.use_count || triangle_count <= *m_last_count) && 
            (!m_configuration.use_error || m_configuration.max_error <= *m_last_error)) {
            // continue from last reduction - speed up
            collapsed = m_volume->mesh().its; // small copy
        } else {
            collapsed = *m_original_its; // copy
        }

        try {
            its_quadric_edge_collapse(collapsed, triangle_count, &max_error, throw_on_cancel, statusfn);
            set_its(collapsed);
            m_is_valid_result = true;
            m_last_count = triangle_count; // need to store last requirement, collapsed count could be count-1
            m_last_error = max_error;
        } catch (SimplifyCanceledException &) {
            // set state out of main thread
            m_last_error = {};
            m_state = State::settings; 
        }
        // need to render last status fn to change bar graph to buttons
        m_parent.schedule_extra_frame(0);
    });
}

void GLGizmoSimplify::set_its(indexed_triangle_set &its) {
    auto tm = std::make_unique<TriangleMesh>(its);
    tm->repair();
    m_volume->set_mesh(std::move(tm));
    m_volume->set_new_unique_id();
    m_volume->get_object()->invalidate_bounding_box();
    m_need_reload = true;
}

bool GLGizmoSimplify::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoSimplify::on_set_state() 
{ 
    // Closing gizmo. e.g. selecting another one
    if (GLGizmoBase::m_state == GLGizmoBase::Off) {
        m_volume = nullptr;            
    }
}

void GLGizmoSimplify::create_gui_cfg() { 
    if (m_gui_cfg.has_value()) return;

    int space_size = m_imgui->calc_text_size(":MM").x;
    GuiCfg cfg;
    cfg.top_left_width = std::max(m_imgui->calc_text_size(_L("Mesh name")).x,
                                  m_imgui->calc_text_size(_L("Triangles")).x) 
        + space_size;

    cfg.bottom_left_width =
        std::max(
            std::max(m_imgui->calc_text_size(_L("Limit by triangles")).x,
                     std::max(m_imgui->calc_text_size(_L("Triangle count")).x,
                              m_imgui->calc_text_size(_L("Ratio")).x)),
            std::max(m_imgui->calc_text_size(_L("Limit by error")).x,
                     m_imgui->calc_text_size(_L("Max. error")).x)) + space_size;
    cfg.input_width       = cfg.bottom_left_width;
    cfg.input_small_width = cfg.input_width * 0.8;
    cfg.window_offset     = cfg.input_width;
    m_gui_cfg = cfg;
}

} // namespace Slic3r::GUI
