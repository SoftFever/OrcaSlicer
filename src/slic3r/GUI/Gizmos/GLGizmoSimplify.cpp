// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoSimplify.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

namespace Slic3r::GUI {

GLGizmoSimplify::GLGizmoSimplify(GLCanvas3D &       parent,
                                 const std::string &icon_filename,
                                 unsigned int       sprite_id)
    : GLGizmoBase(parent, icon_filename, -1)
    , state(State::settings)
    , is_valid_result(false)
    , progress(0)
    , volume(nullptr)
    , obj_index(0)
    , need_reload(false)
{}

GLGizmoSimplify::~GLGizmoSimplify() { 
    state = State::canceling;
    if (worker.joinable()) worker.join();
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

void GLGizmoSimplify::on_render() const{}
void GLGizmoSimplify::on_render_for_picking() const{}

void GLGizmoSimplify::on_render_input_window(float x, float y, float bottom_limit)
{
    const int min_triangle_count = 4; // tetrahedron
    // GUI size constants
    // TODO: calculate from translation text sizes
    const int top_left_width    = 100;
    const int bottom_left_width = 100;
    const int input_width       = 100;
    const int input_small_width = 80;
    const int window_offset = 100;
    const int max_char_in_name = 25;

    int flag = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoCollapse;
    m_imgui->begin(on_get_name(), flag);

    const Selection &selection = m_parent.get_selection();
    int object_idx = selection.get_object_idx();
    ModelObject *obj = wxGetApp().plater()->model().objects[object_idx];
    ModelVolume *act_volume = obj->volumes.front();

    // Check selection of new volume
    // Do not reselect object when processing 
    if (act_volume != volume && state == State::settings) {
        obj_index = object_idx; // to remember correct object
        volume = act_volume;
        original_its = {};
        const TriangleMesh &tm = volume->mesh();
        c.wanted_percent = 50.;  // default value
        c.update_percent(tm.its.indices.size());
        is_valid_result = false;
        // set window position
        ImVec2 pos = ImGui::GetMousePos();
        pos.x -= window_offset;
        pos.y -= window_offset;
        ImGui::SetWindowPos(pos, ImGuiCond_Always);
    }

    size_t triangle_count = volume->mesh().its.indices.size();
    // already reduced mesh
    if (original_its.has_value())
        triangle_count = original_its->indices.size();

    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _L("Mesh name") + ":");
    ImGui::SameLine(top_left_width);
    std::string name = volume->name;
    if (name.length() > max_char_in_name)
        name = name.substr(0, max_char_in_name-3) + "...";
    m_imgui->text(name);
    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _L("Triangles") + ":");
    ImGui::SameLine(top_left_width);
    m_imgui->text(std::to_string(triangle_count));

    ImGui::Separator();

    ImGui::Text(_L("Limit by triangles").c_str());
    ImGui::SameLine(bottom_left_width);
    // First initialization + fix triangle count
    if (m_imgui->checkbox("##UseCount", c.use_count)) {
        if (!c.use_count) c.use_error = true;
        is_valid_result = false;
    }

    m_imgui->disabled_begin(!c.use_count);
    ImGui::Text(_L("Triangle count").c_str());
    ImGui::SameLine(bottom_left_width);
    int wanted_count = c.wanted_count;
    ImGui::SetNextItemWidth(input_width);
    if (ImGui::SliderInt("##triangle_count", &wanted_count, min_triangle_count, triangle_count, "%d")) {
        c.wanted_count = static_cast<uint32_t>(wanted_count);
        if (c.wanted_count < min_triangle_count)
            c.wanted_count = min_triangle_count;
        if (c.wanted_count > triangle_count) 
            c.wanted_count = triangle_count;
        c.update_count(triangle_count);
        is_valid_result = false;
    }
    ImGui::Text(_L("Ratio").c_str());
    ImGui::SameLine(bottom_left_width);
    ImGui::SetNextItemWidth(input_small_width);
    const char * precision = (c.wanted_percent > 10)? "%.0f": ((c.wanted_percent > 1)? "%.1f":"%.2f");
    float step = (c.wanted_percent > 10)? 1.f: ((c.wanted_percent > 1)? 0.1f : 0.01f);
    if (ImGui::InputFloat("%", &c.wanted_percent, step, 10*step, precision)) {
        if (c.wanted_percent > 100.f) c.wanted_percent = 100.f;
        c.update_percent(triangle_count);
        if (c.wanted_count < min_triangle_count) {
            c.wanted_count = min_triangle_count;
            c.update_count(triangle_count);
        }
        is_valid_result = false;
    }
    m_imgui->disabled_end(); // use_count

    ImGui::NewLine();
    ImGui::Text(_L("Limit by error").c_str());
    ImGui::SameLine(bottom_left_width);
    if (m_imgui->checkbox("##UseError", c.use_error)) {
        if (!c.use_error) c.use_count = true;
        is_valid_result = false;
    }

    m_imgui->disabled_begin(!c.use_error);
    ImGui::Text(_L("Max. error").c_str());
    ImGui::SameLine(bottom_left_width);
    ImGui::SetNextItemWidth(input_small_width);
    if (ImGui::InputFloat("##maxError", &c.max_error, 0.01f, .1f, "%.2f")) {
        if (c.max_error < 0.f) c.max_error = 0.f;
        is_valid_result = false;
    }
    m_imgui->disabled_end(); // use_error


    if (state == State::settings) {
        if (m_imgui->button(_L("Cancel"))) {
            if (original_its.has_value()) { 
                set_its(*original_its);
                state = State::close_on_end;
            } else {
                close();
            }
        }
        ImGui::SameLine(bottom_left_width);
        if (m_imgui->button(_L("Preview"))) {
            state = State::simplifying;
            // simplify but not aply on mesh
            process();
        }
        ImGui::SameLine();
        if (m_imgui->button(_L("Apply"))) {
            if (!is_valid_result) {
                state = State::close_on_end;
                process();
            } else {
                // use preview and close
                close();
            }
        }
    } else {        
        m_imgui->disabled_begin(state == State::canceling);
        if (m_imgui->button(_L("Cancel"))) state = State::canceling;
        m_imgui->disabled_end(); 

        ImGui::SameLine(bottom_left_width);
        // draw progress bar
        char buf[32];
        sprintf(buf, L("Process %d / 100"), progress);
        ImGui::ProgressBar(progress / 100., ImVec2(input_width, 0.f), buf);
    }
    m_imgui->end();

    if (need_reload) { 
        need_reload = false;

        // Reload visualization of mesh - change VBO, FBO on GPU
        m_parent.reload_scene(true); // deactivate gizmo??
        GLGizmosManager &gizmos_mgr = m_parent.get_gizmos_manager();
        gizmos_mgr.open_gizmo(GLGizmosManager::EType::Simplify);

        if (state == State::close_on_end) {
            // fix hollowing, sla support points, modifiers, ...
            auto plater = wxGetApp().plater();
            plater->changed_mesh(obj_index); // deactivate gizmo??
            // changed_mesh cause close();
            //close(); 
        }

        // change from simplifying | aply
        state = State::settings;        
    }
}

void GLGizmoSimplify::close() {
    volume = nullptr;
    
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

    if (!original_its.has_value())
        original_its = volume->mesh().its; // copy

    auto plater = wxGetApp().plater();
    plater->take_snapshot(_L("Simplify ") + volume->name);
    plater->clear_before_change_mesh(obj_index);
    progress = 0;
    if (worker.joinable()) worker.join();
    worker = std::thread([&]() {
        // store original triangles        
        uint32_t triangle_count = (c.use_count) ? c.wanted_count : 0;
        float    max_error      = (c.use_error) ? c.max_error : std::numeric_limits<float>::max();

        std::function<void(void)> throw_on_cancel = [&]() {
            if (state == State::canceling) {
                throw SimplifyCanceledException();
            }
        };    
        std::function<void(int)> statusfn = [&](int percent) { 
            progress = percent; 
            wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
        };

        indexed_triangle_set collapsed;
        if (last_error.has_value()) {
            // is chance to continue with last reduction
            const indexed_triangle_set &its = volume->mesh().its;
            uint32_t last_triangle_count = static_cast<uint32_t>(its.indices.size());
            if ((!c.use_count || triangle_count <= last_triangle_count) && 
                (!c.use_error || c.max_error <= *last_error)) {
                collapsed = its; // small copy
            } else {
                collapsed = *original_its; // copy
            }
        } else {
            collapsed = *original_its; // copy
        }

        try {
            its_quadric_edge_collapse(collapsed, triangle_count, &max_error, throw_on_cancel, statusfn);
            set_its(collapsed);
            is_valid_result = true;
            last_error = max_error;
        } catch (SimplifyCanceledException &) {
            state = State::settings;
        }
        wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
    });
}

void GLGizmoSimplify::set_its(indexed_triangle_set &its) {
    auto tm = std::make_unique<TriangleMesh>(its);
    tm->repair();
    volume->set_mesh(std::move(tm));
    volume->set_new_unique_id();
    volume->get_object()->invalidate_bounding_box();
    need_reload = true;
}

bool GLGizmoSimplify::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

} // namespace Slic3r::GUI
