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
    }

    size_t triangle_count = volume->mesh().its.indices.size();
    // already reduced mesh
    if (original_its.has_value())
        triangle_count = original_its->indices.size();

    int flag = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
               ImGuiWindowFlags_NoCollapse;
    m_imgui->begin(_L("Simplify mesh ") + volume->name, flag);
    std::string description = "Reduce amout of triangle in mesh( " +
                              volume->name + " has " +
                              std::to_string(triangle_count) + " triangles)";
    ImGui::Text(description.c_str());
    // First initialization + fix triangle count
    if (c.wanted_count > triangle_count) c.update_percent(triangle_count);
    if (m_imgui->checkbox(_L("Until triangle count is less than "), c.use_count)) {
        if (!c.use_count) c.use_error = true;
        is_valid_result = false;
    }

    m_imgui->disabled_begin(!c.use_count);
    ImGui::SameLine();
    int wanted_count = c.wanted_count;
    if (ImGui::SliderInt("triangles", &wanted_count, 0,
                         triangle_count, "%d")) {
        c.wanted_count = static_cast<uint32_t>(wanted_count);
        c.update_count(triangle_count);
        is_valid_result = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    const char * precision = (c.wanted_percent > 10)? "%.0f": ((c.wanted_percent > 1)? "%.1f":"%.2f");
    float step = (c.wanted_percent > 10)? 1.f: ((c.wanted_percent > 1)? 0.1f : 0.01f);
    if (ImGui::InputFloat("%", &c.wanted_percent, step, 10*step, precision)) {
        if (c.wanted_percent < 0.f) c.wanted_percent = 0.f;
        if (c.wanted_percent > 100.f) c.wanted_percent = 100.f;
        c.update_percent(triangle_count);
        is_valid_result = false;
    }
    m_imgui->disabled_end(); // use_count

    if (m_imgui->checkbox(_L("Until error"), c.use_error)) {
        if (!c.use_error) c.use_count = true;
        is_valid_result = false;
    }
    ImGui::SameLine();
    m_imgui->disabled_begin(!c.use_error);
    if (ImGui::InputFloat("(maximal quadric error)", &c.max_error, 0.01f, .1f, "%.2f")) {
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
        ImGui::SameLine();
        if (m_imgui->button(_L("Preview"))) {
            state = State::simplifying;
            // simplify but not aply on mesh
            process();
        }
        ImGui::SameLine();
        if (m_imgui->button(_L("Aply"))) {
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

        ImGui::SameLine();
        // draw progress bar
        ImGui::Text("Processing %c \t %d / 100",
                    "|/-\\"[(int) (ImGui::GetTime() / 0.05f) & 3], progress);
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
        uint32_t                  triangle_count  = (c.use_count)? c.wanted_count : 0;
        float*                    max_error       = (c.use_error)? &c.max_error : nullptr;
        std::function<void(void)> throw_on_cancel = [&]() { if ( state == State::canceling) throw SimplifyCanceledException(); };    
        std::function<void(int)> statusfn = [&](int percent) { progress = percent; };
        indexed_triangle_set collapsed = original_its.value(); // copy
        try {
            its_quadric_edge_collapse(collapsed, triangle_count, max_error, throw_on_cancel, statusfn);
            set_its(collapsed);
            is_valid_result = true;
        } catch (SimplifyCanceledException &) {
            is_valid_result = false;
        }
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
