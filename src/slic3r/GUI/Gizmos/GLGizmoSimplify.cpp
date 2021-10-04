#include "GLGizmoSimplify.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/QuadricEdgeCollapse.hpp"

namespace Slic3r::GUI {

GLGizmoSimplify::GLGizmoSimplify(GLCanvas3D &       parent,
                                 const std::string &icon_filename,
                                 unsigned int       sprite_id)
    : GLGizmoBase(parent, icon_filename, -1)
    , m_state(State::settings)
    , m_is_valid_result(false)
    , m_exist_preview(false)
    , m_progress(0)
    , m_volume(nullptr)
    , m_obj_index(0)
    , m_need_reload(false) 
    , m_show_wireframe(false)

    , tr_mesh_name(_u8L("Mesh name"))
    , tr_triangles(_u8L("Triangles"))
    , tr_preview(_u8L("Preview"))
    , tr_detail_level(_u8L("Detail level"))
    , tr_decimate_ratio(_u8L("Decimate ratio"))

    , m_wireframe_VBO_id(0)
    , m_wireframe_IBO_id(0)
{}

GLGizmoSimplify::~GLGizmoSimplify() { 
    m_state = State::canceling;
    if (m_worker.joinable()) m_worker.join();
    free_gpu();
}

bool GLGizmoSimplify::on_init()
{
    //m_grabbers.emplace_back();
    //m_shortcut_key = WXK_CONTROL_C;
    return true;
}

std::string GLGizmoSimplify::on_get_name() const
{
    return _u8L("Simplify");
}

void GLGizmoSimplify::on_render() { }

void GLGizmoSimplify::on_render_for_picking() {}

void GLGizmoSimplify::on_render_input_window(float x, float y, float bottom_limit)
{
    create_gui_cfg();
    const Selection &selection = m_parent.get_selection();
    int obj_index = selection.get_object_idx();
    ModelVolume *act_volume = get_volume(selection, wxGetApp().plater()->model());
    if (act_volume == nullptr) {
        switch (m_state) {
        case State::settings: close(); break;
        case State::canceling: break;
        default: m_state = State::canceling;
        }
        return;
    }

    // Check selection of new volume
    // Do not reselect object when processing 
    if (act_volume != m_volume && m_state == State::settings) {
        bool change_window_position = (m_volume == nullptr);
        // select different model
        if (m_volume != nullptr && m_original_its.has_value()) {
            set_its(*m_original_its);
        }

        // close suggestion notification
        auto notification_manager = wxGetApp().plater()->get_notification_manager();
        notification_manager->remove_simplify_suggestion_with_id(act_volume->get_object()->id());

        m_obj_index = obj_index; // to remember correct object
        m_volume = act_volume;
        m_original_its = {};
        m_configuration.decimate_ratio = 50.; // default value
        m_configuration.fix_count_by_ratio(m_volume->mesh().its.indices.size());
        m_is_valid_result = false;
        m_exist_preview   = false;
        init_wireframe();

        if (change_window_position) {
            ImVec2 pos = ImGui::GetMousePos();
            pos.x -= m_gui_cfg->window_offset_x;
            pos.y -= m_gui_cfg->window_offset_y;
            // minimal top left value
            ImVec2 tl(m_gui_cfg->window_padding, m_gui_cfg->window_padding);
            if (pos.x < tl.x) pos.x = tl.x;
            if (pos.y < tl.y) pos.y = tl.y;
            // maximal bottom right value
            auto parent_size = m_parent.get_canvas_size();
            ImVec2 br(
                parent_size.get_width() - (2 * m_gui_cfg->window_offset_x + m_gui_cfg->window_padding), 
                parent_size.get_height() - (2 * m_gui_cfg->window_offset_y + m_gui_cfg->window_padding));
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

    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, tr_mesh_name + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);
    std::string name = m_volume->name;
    if (name.length() > m_gui_cfg->max_char_in_name)
        name = name.substr(0, m_gui_cfg->max_char_in_name - 3) + "...";
    m_imgui->text(name);
    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, tr_triangles + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);
    m_imgui->text(std::to_string(triangle_count));
    /*
    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, tr_preview + ":");
    ImGui::SameLine(m_gui_cfg->top_left_width);
    if (m_exist_preview) {
        m_imgui->text(std::to_string(m_volume->mesh().its.indices.size()));
    } else {
        m_imgui->text("---");
    }*/

    ImGui::Separator();

    if(ImGui::RadioButton("##use_error", !m_configuration.use_count)) {
        m_is_valid_result         = false;
        m_configuration.use_count = !m_configuration.use_count;
    }
    ImGui::SameLine();
    m_imgui->disabled_begin(m_configuration.use_count);
    ImGui::Text("%s", tr_detail_level.c_str());
    std::vector<std::string> reduce_captions = {
        static_cast<std::string>(_u8L("Extra high")),
        static_cast<std::string>(_u8L("High")),
        static_cast<std::string>(_u8L("Medium")),
        static_cast<std::string>(_u8L("Low")),
        static_cast<std::string>(_u8L("Extra low"))
    };
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);
    static int reduction = 2;
    if(ImGui::SliderInt("##ReductionLevel", &reduction, 0, 4, reduce_captions[reduction].c_str())) {
        m_is_valid_result = false;
        if (reduction < 0) reduction = 0;
        if (reduction > 4) reduction = 4;
        switch (reduction) {
        case 0: m_configuration.max_error = 1e-3f; break;
        case 1: m_configuration.max_error = 1e-2f; break;
        case 2: m_configuration.max_error = 0.1f; break;
        case 3: m_configuration.max_error = 0.5f; break;
        case 4: m_configuration.max_error = 1.f; break;
        }
    }
    m_imgui->disabled_end(); // !use_count

    if (ImGui::RadioButton("##use_count", m_configuration.use_count)) {
        m_is_valid_result         = false;
        m_configuration.use_count = !m_configuration.use_count;
    }
    ImGui::SameLine();

    // show preview result triangle count (percent)
    if (m_need_reload && !m_configuration.use_count) {
        m_configuration.wanted_count = static_cast<uint32_t>(m_volume->mesh().its.indices.size());
        m_configuration.decimate_ratio = 
            (1.0f - (m_configuration.wanted_count / (float) triangle_count)) * 100.f;
    }

    m_imgui->disabled_begin(!m_configuration.use_count);
    ImGui::Text("%s", tr_decimate_ratio.c_str());
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::SetNextItemWidth(m_gui_cfg->input_width);
    const char * format = (m_configuration.decimate_ratio > 10)? "%.0f %%": 
        ((m_configuration.decimate_ratio > 1)? "%.1f %%":"%.2f %%");
    if (ImGui::SliderFloat("##decimate_ratio", &m_configuration.decimate_ratio, 0.f, 100.f, format)) {
        m_is_valid_result = false;
        if (m_configuration.decimate_ratio < 0.f)
            m_configuration.decimate_ratio = 0.01f;
        if (m_configuration.decimate_ratio > 100.f)
            m_configuration.decimate_ratio = 100.f;
        m_configuration.fix_count_by_ratio(triangle_count);
    }

    ImGui::NewLine();
    ImGui::SameLine(m_gui_cfg->bottom_left_width);
    ImGui::Text(_L("%d triangles").c_str(), m_configuration.wanted_count);
    m_imgui->disabled_end(); // use_count

    if (ImGui::Checkbox(_L("Show wireframe").c_str(), &m_show_wireframe)) {
        if (m_show_wireframe) init_wireframe();
        else free_gpu();
    }

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
            // simplify but not apply on mesh
            process();
        }
        ImGui::SameLine();
        if (m_imgui->button(_L("Apply"))) {
            if (!m_is_valid_result) {
                m_state = State::close_on_end;
                process();
            } else if (m_exist_preview) {
                // use preview and close
                after_apply();
            } else { // no changes made
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
        bool close_on_end = (m_state == State::close_on_end);
        // Reload visualization of mesh - change VBO, FBO on GPU
        m_parent.reload_scene(true);
        // set m_state must be before close() !!!
        m_state = State::settings;
        if (close_on_end) after_apply();
        else init_wireframe();
        // Fix warning icon in object list
        wxGetApp().obj_list()->update_item_error_icon(m_obj_index, -1);
    }
}

void GLGizmoSimplify::after_apply() {
    // set flag to NOT revert changes when switch GLGizmoBase::m_state
    m_exist_preview = false;
    // fix hollowing, sla support points, modifiers, ...
    auto plater = wxGetApp().plater();
    plater->changed_mesh(m_obj_index);
    close();
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
    m_worker = std::thread([this]() {
        // store original triangles        
        uint32_t triangle_count = (m_configuration.use_count) ? m_configuration.wanted_count : 0;
        float    max_error      = (!m_configuration.use_count) ? m_configuration.max_error : std::numeric_limits<float>::max();

        std::function<void(void)> throw_on_cancel = [&]() {
            if (m_state == State::canceling) {
                throw SimplifyCanceledException();
            }
        };

        int64_t last = 0;
        std::function<void(int)> statusfn = [this, &last](int percent) {
            m_progress = percent;

            // check max 4fps
            int64_t now = m_parent.timestamp_now();
            if ((now - last) < 250) return;
            last = now;

            request_rerender();
        };

        indexed_triangle_set collapsed = *m_original_its; // copy

        try {
            its_quadric_edge_collapse(collapsed, triangle_count, &max_error, throw_on_cancel, statusfn);
            set_its(collapsed);
            m_is_valid_result = true;
            m_exist_preview   = true;
        } catch (SimplifyCanceledException &) {
            // set state out of main thread
            m_state = State::settings; 
        }
        // need to render last status fn to change bar graph to buttons        
        request_rerender();
    });
}

void GLGizmoSimplify::set_its(indexed_triangle_set &its) {
    m_volume->set_mesh(its);
    m_volume->calculate_convex_hull();
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
        // can appear when delete objects
        bool empty_selection = m_parent.get_selection().is_empty();

        // cancel processing
        if (empty_selection && 
            m_state != State::settings &&
            m_state != State::canceling)  
            m_state = State::canceling;

        // refuse outgoing during simlification
        // object is not selected when it is deleted(cancel and close gizmo)
        if (m_state != State::settings && !empty_selection) {
            GLGizmoBase::m_state = GLGizmoBase::On;
            auto notification_manager = wxGetApp().plater()->get_notification_manager();
            notification_manager->push_notification(
                NotificationType::CustomNotification,
                NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                _u8L("ERROR: Wait until Simplification ends or Cancel process."));
            return;
        }

        // revert preview
        if (m_exist_preview) {
            m_exist_preview = false;
            if (exist_volume(m_volume)) {
                set_its(*m_original_its);
                m_parent.reload_scene(false);
                m_need_reload = false;
            }
        }

        // invalidate selected model
        m_volume = nullptr;
    } else if (GLGizmoBase::m_state == GLGizmoBase::On) {
        // when open by hyperlink it needs to show up
        request_rerender();
    }
}

void GLGizmoSimplify::create_gui_cfg() { 
    if (m_gui_cfg.has_value()) return;
    int space_size = m_imgui->calc_text_size(":MM").x;
    GuiCfg cfg;
    cfg.top_left_width = std::max(m_imgui->calc_text_size(tr_mesh_name).x,
                                  m_imgui->calc_text_size(tr_triangles).x) 
        + space_size;

    const float radio_size = ImGui::GetFrameHeight();
    cfg.bottom_left_width =
        std::max(m_imgui->calc_text_size(tr_detail_level).x,
                 m_imgui->calc_text_size(tr_decimate_ratio).x) +
        space_size + radio_size;

    cfg.input_width   = cfg.bottom_left_width * 1.5;
    cfg.window_offset_x = (cfg.bottom_left_width + cfg.input_width)/2;
    cfg.window_offset_y = ImGui::GetTextLineHeightWithSpacing() * 5;
    m_gui_cfg = cfg;
}

void GLGizmoSimplify::request_rerender() {
    wxGetApp().plater()->CallAfter([this]() {
        set_dirty();
        m_parent.schedule_extra_frame(0);
    });
}

bool GLGizmoSimplify::exist_volume(ModelVolume *volume) {
    auto objs = wxGetApp().plater()->model().objects;
    for (const auto &obj : objs) {
        const auto &vlms = obj->volumes;
        auto        item = std::find(vlms.begin(), vlms.end(), volume);
        if (item != vlms.end()) return true;
    }
    return false;
}

ModelVolume * GLGizmoSimplify::get_volume(const Selection &selection, Model &model)
{
    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    if (idxs.empty()) return nullptr;
    // only one selected volume
    if (idxs.size() != 1) return nullptr;
    const GLVolume *selected_volume = selection.get_volume(*idxs.begin());
    if (selected_volume == nullptr) return nullptr;

    const GLVolume::CompositeID &cid  = selected_volume->composite_id;
    const ModelObjectPtrs& objs = model.objects;
    if (cid.object_id < 0 || objs.size() <= static_cast<size_t>(cid.object_id))
        return nullptr;
    const ModelObject* obj = objs[cid.object_id];
    if (cid.volume_id < 0 || obj->volumes.size() <= static_cast<size_t>(cid.volume_id))
        return nullptr;    
    return obj->volumes[cid.volume_id];
}

const ModelVolume *GLGizmoSimplify::get_volume(const GLVolume::CompositeID &cid, const Model &model)
{
    const ModelObjectPtrs &objs = model.objects;
    if (cid.object_id < 0 || objs.size() <= static_cast<size_t>(cid.object_id))
        return nullptr;
    const ModelObject *obj = objs[cid.object_id];
    if (cid.volume_id < 0 || obj->volumes.size() <= static_cast<size_t>(cid.volume_id))
        return nullptr;
    return obj->volumes[cid.volume_id];
}

void GLGizmoSimplify::init_wireframe()
{
    if (!m_show_wireframe) return;
    const indexed_triangle_set &its = m_volume->mesh().its;
    free_gpu();
    if (its.indices.empty()) return;

    // vertices
    glsafe(::glGenBuffers(1, &m_wireframe_VBO_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_wireframe_VBO_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER,
                            its.vertices.size() * 3 * sizeof(float),
                            its.vertices.data(), GL_STATIC_DRAW));
    
    // indices
    std::vector<Vec2i> contour_indices;
    contour_indices.reserve((its.indices.size() * 3) / 2);
    for (const auto &triangle : its.indices) { 
        for (size_t ti1 = 0; ti1 < 3; ++ti1) { 
            size_t ti2 = (ti1 == 2) ? 0 : (ti1 + 1);
            if (triangle[ti1] > triangle[ti2]) continue;
            contour_indices.emplace_back(triangle[ti1], triangle[ti2]);
        }
    }
    glsafe(::glGenBuffers(1, &m_wireframe_IBO_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_wireframe_IBO_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER,
                          2*contour_indices.size() * sizeof(coord_t),
                          contour_indices.data(), GL_STATIC_DRAW));
    m_wireframe_IBO_size = contour_indices.size() * 2;
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLGizmoSimplify::render_wireframe() const
{
    // is initialized?
    if (m_wireframe_VBO_id == 0 || m_wireframe_IBO_id == 0) return;
    if (!m_show_wireframe) return;

    const auto& selection   = m_parent.get_selection();
    const auto& volume_idxs = selection.get_volume_idxs();
    if (volume_idxs.empty() || volume_idxs.size() != 1) return;
    const GLVolume *selected_volume = selection.get_volume(*volume_idxs.begin());
    
    // check that selected model is wireframe initialized
    if (m_volume != get_volume(selected_volume->composite_id, *m_parent.get_model()))
        return;

    const Transform3d trafo_matrix = selected_volume->world_matrix();
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(trafo_matrix.data()));

    auto *contour_shader = wxGetApp().get_shader("mm_contour");
    contour_shader->start_using();
    glsafe(::glDepthFunc(GL_LEQUAL));
    glsafe(::glLineWidth(1.0f));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_wireframe_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), nullptr));
    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_wireframe_IBO_id));
    glsafe(::glDrawElements(GL_LINES, m_wireframe_IBO_size, GL_UNSIGNED_INT, nullptr));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    glsafe(::glDepthFunc(GL_LESS));

    glsafe(::glPopMatrix()); // pop trafo
    contour_shader->stop_using(); 
}

void GLGizmoSimplify::free_gpu()
{
    if (m_wireframe_VBO_id != 0) {
        glsafe(::glDeleteBuffers(1, &m_wireframe_VBO_id));
        m_wireframe_VBO_id = 0;
    }

    if (m_wireframe_IBO_id != 0) {
        glsafe(::glDeleteBuffers(1, &m_wireframe_IBO_id));
        m_wireframe_IBO_id = 0;
    }
}

} // namespace Slic3r::GUI
