#include "libslic3r/libslic3r.h"
#include "libslic3r/GCode/PreviewData.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "AppConfig.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "GLCanvas3DManager.hpp"
#include "GLCanvas3D.hpp"
#include "PresetBundle.hpp"
#include "wxExtensions.hpp"

#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/checkbox.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace Slic3r {
namespace GUI {

View3D::View3D(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, bed, camera, view_toolbar, model, config, process);
}

View3D::~View3D()
{
    if (m_canvas_widget != nullptr)
    {
        _3DScene::remove_canvas(m_canvas_widget);
        delete m_canvas_widget;
        m_canvas = nullptr;
    }
}

bool View3D::init(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    m_canvas_widget = GLCanvas3DManager::create_wxglcanvas(this);
    _3DScene::add_canvas(m_canvas_widget, bed, camera, view_toolbar);
    m_canvas = _3DScene::get_canvas(this->m_canvas_widget);

    m_canvas->allow_multisample(GLCanvas3DManager::can_multisample());
    // XXX: If have OpenGL
    m_canvas->enable_picking(true);
    m_canvas->enable_moving(true);
    // XXX: more config from 3D.pm
    m_canvas->set_model(model);
    m_canvas->set_process(process);
    m_canvas->set_config(config);
    m_canvas->enable_gizmos(true);
    m_canvas->enable_selection(true);
    m_canvas->enable_main_toolbar(true);
    m_canvas->enable_undoredo_toolbar(true);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    return true;
}

void View3D::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void View3D::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void View3D::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        m_canvas->select_view(direction);
}

void View3D::select_all()
{
    if (m_canvas != nullptr)
        m_canvas->select_all();
}

void View3D::deselect_all()
{
    if (m_canvas != nullptr)
        m_canvas->deselect_all();
}

void View3D::delete_selected()
{
    if (m_canvas != nullptr)
        m_canvas->delete_selected();
}

void View3D::mirror_selection(Axis axis)
{
    if (m_canvas != nullptr)
        m_canvas->mirror_selection(axis);
}

int View3D::check_volumes_outside_state() const
{
    return (m_canvas != nullptr) ? m_canvas->check_volumes_outside_state() : false;
}

bool View3D::is_layers_editing_enabled() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_enabled() : false;
}

bool View3D::is_layers_editing_allowed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_layers_editing_allowed() : false;
}

void View3D::enable_layers_editing(bool enable)
{
    if (m_canvas != nullptr)
        m_canvas->enable_layers_editing(enable);
}

bool View3D::is_dragging() const
{
    return (m_canvas != nullptr) ? m_canvas->is_dragging() : false;
}

bool View3D::is_reload_delayed() const
{
    return (m_canvas != nullptr) ? m_canvas->is_reload_delayed() : false;
}

void View3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if (m_canvas != nullptr)
        m_canvas->reload_scene(refresh_immediately, force_full_scene_refresh);
}

void View3D::render()
{
    if (m_canvas != nullptr)
        //m_canvas->render();
        m_canvas->set_as_dirty();
}

Preview::Preview(
    wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, 
    BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, std::function<void()> schedule_background_process_func)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
    , m_double_slider_sizer(nullptr)
    , m_label_view_type(nullptr)
    , m_choice_view_type(nullptr)
    , m_label_show_features(nullptr)
    , m_combochecklist_features(nullptr)
    , m_checkbox_travel(nullptr)
    , m_checkbox_retractions(nullptr)
    , m_checkbox_unretractions(nullptr)
    , m_checkbox_shells(nullptr)
    , m_checkbox_legend(nullptr)
    , m_config(config)
    , m_process(process)
    , m_gcode_preview_data(gcode_preview_data)
    , m_number_extruders(1)
    , m_preferred_color_mode("feature")
    , m_loaded(false)
    , m_enabled(false)
    , m_schedule_background_process(schedule_background_process_func)
#ifdef __linux__
    , m_volumes_cleanup_required(false)
#endif // __linux__
{
    if (init(parent, bed, camera, view_toolbar, model))
    {
        show_hide_ui_elements("none");
        load_print();
    }
}

bool Preview::init(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    m_canvas_widget = GLCanvas3DManager::create_wxglcanvas(this);
    _3DScene::add_canvas(m_canvas_widget, bed, camera, view_toolbar);
    m_canvas = _3DScene::get_canvas(this->m_canvas_widget);
    m_canvas->allow_multisample(GLCanvas3DManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->enable_legend_texture(true);
    m_canvas->enable_dynamic_background(true);

    m_double_slider_sizer = new wxBoxSizer(wxHORIZONTAL);
    create_double_slider();

    m_label_view_type = new wxStaticText(this, wxID_ANY, _(L("View")));

    m_choice_view_type = new wxChoice(this, wxID_ANY);
    m_choice_view_type->Append(_(L("Feature type")));
    m_choice_view_type->Append(_(L("Height")));
    m_choice_view_type->Append(_(L("Width")));
    m_choice_view_type->Append(_(L("Speed")));
    m_choice_view_type->Append(_(L("Fan speed")));
    m_choice_view_type->Append(_(L("Volumetric flow rate")));
    m_choice_view_type->Append(_(L("Tool")));
    m_choice_view_type->Append(_(L("Color Print")));
    m_choice_view_type->SetSelection(0);

    m_label_show_features = new wxStaticText(this, wxID_ANY, _(L("Show")));

    m_combochecklist_features = new wxComboCtrl();
    m_combochecklist_features->Create(this, wxID_ANY, _(L("Feature types")), wxDefaultPosition, wxSize(15 * wxGetApp().em_unit(), -1), wxCB_READONLY);
    std::string feature_text = GUI::into_u8(_(L("Feature types")));
    std::string feature_items = GUI::into_u8(
        _(L("Perimeter")) + "|" +
        _(L("External perimeter")) + "|" +
        _(L("Overhang perimeter")) + "|" +
        _(L("Internal infill")) + "|" +
        _(L("Solid infill")) + "|" +
        _(L("Top solid infill")) + "|" +
        _(L("Bridge infill")) + "|" +
        _(L("Gap fill")) + "|" +
        _(L("Skirt")) + "|" +
        _(L("Support material")) + "|" +
        _(L("Support material interface")) + "|" +
        _(L("Wipe tower")) + "|" +
        _(L("Custom"))
    );
    Slic3r::GUI::create_combochecklist(m_combochecklist_features, feature_text, feature_items, true);

    m_checkbox_travel = new wxCheckBox(this, wxID_ANY, _(L("Travel")));
    m_checkbox_retractions = new wxCheckBox(this, wxID_ANY, _(L("Retractions")));
    m_checkbox_unretractions = new wxCheckBox(this, wxID_ANY, _(L("Unretractions")));
    m_checkbox_shells = new wxCheckBox(this, wxID_ANY, _(L("Shells")));
    m_checkbox_legend = new wxCheckBox(this, wxID_ANY, _(L("Legend")));
    m_checkbox_legend->SetValue(true);

    wxBoxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);
    top_sizer->Add(m_double_slider_sizer, 0, wxEXPAND, 0);

    wxBoxSizer* bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_sizer->Add(m_label_view_type, 0, wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->Add(m_choice_view_type, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_label_show_features, 0, wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->Add(m_combochecklist_features, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(20);
    bottom_sizer->Add(m_checkbox_travel, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_retractions, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_unretractions, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_shells, 0, wxEXPAND | wxALL, 5);
    bottom_sizer->AddSpacer(20);
    bottom_sizer->Add(m_checkbox_legend, 0, wxEXPAND | wxALL, 5);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(top_sizer, 1, wxALL | wxEXPAND, 0);
    main_sizer->Add(bottom_sizer, 0, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();
    
    // sets colors for gcode preview extrusion roles
    std::vector<std::string> extrusion_roles_colors = {
        "Perimeter", "FFFF66",
        "External perimeter", "FFA500",
        "Overhang perimeter", "0000FF",
        "Internal infill", "B1302A",
        "Solid infill", "D732D7",
        "Top solid infill", "FF1A1A",
        "Bridge infill", "9999FF",
        "Gap fill", "FFFFFF",
        "Skirt", "845321",
        "Support material", "00FF00",
        "Support material interface", "008000",
        "Wipe tower", "B3E3AB",
        "Custom", "28CC94"
    };
    m_gcode_preview_data->set_extrusion_paths_colors(extrusion_roles_colors);

    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    if (m_canvas_widget != nullptr)
    {
		_3DScene::remove_canvas(m_canvas_widget);
        delete m_canvas_widget;
        m_canvas = nullptr;
    }
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
}

void Preview::set_number_extruders(unsigned int number_extruders)
{
    if (m_number_extruders != number_extruders)
    {
        m_number_extruders = number_extruders;
        int tool_idx = m_choice_view_type->FindString(_(L("Tool")));
        int type = (number_extruders > 1) ? tool_idx /* color by a tool number */  : 0; // color by a feature type
        m_choice_view_type->SetSelection(type);
        if ((0 <= type) && (type < (int)GCodePreviewData::Extrusion::Num_View_Types))
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;

        m_preferred_color_mode = (type == tool_idx) ? "tool_or_feature" : "feature";
    }
}

void Preview::set_canvas_as_dirty()
{
    m_canvas->set_as_dirty();
}

void Preview::set_enabled(bool enabled)
{
    m_enabled = enabled;
}

void Preview::bed_shape_changed()
{
    if (m_canvas != nullptr)
        m_canvas->bed_shape_changed();
}

void Preview::select_view(const std::string& direction)
{
    m_canvas->select_view(direction);
}

void Preview::set_drop_target(wxDropTarget* target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

void Preview::load_print(bool keep_z_range)
{
    PrinterTechnology tech = m_process->current_printer_technology();
    if (tech == ptFFF)
        load_print_as_fff(keep_z_range);
    else if (tech == ptSLA)
        load_print_as_sla();

    Layout();
}

void Preview::reload_print(bool keep_volumes)
{
#ifdef __linux__
    // We are getting mysterious crashes on Linux in gtk due to OpenGL context activation GH #1874 #1955.
    // So we are applying a workaround here: a delayed release of OpenGL vertex buffers.
    if (!IsShown())
    {
        m_volumes_cleanup_required = !keep_volumes;
        return;
    }
#endif /* __linux __ */
    if (
#ifdef __linux__
        m_volumes_cleanup_required || 
#endif /* __linux__ */
        !keep_volumes)
    {
        m_canvas->reset_volumes();
        m_canvas->reset_legend_texture();
        m_loaded = false;
#ifdef __linux__
        m_volumes_cleanup_required = false;
#endif /* __linux__ */
    }

    load_print();
}

void Preview::refresh_print()
{
    m_loaded = false;

    if (!IsShown())
        return;

    load_print(true);
}

void Preview::msw_rescale()
{
    // rescale slider
    if (m_slider) m_slider->msw_rescale();

    // rescale warning legend on the canvas
    get_canvas3d()->msw_rescale();

    // rescale legend
    refresh_print();
}

void Preview::move_double_slider(wxKeyEvent& evt)
{
    if (m_slider) 
        m_slider->OnKeyDown(evt);
}

void Preview::edit_double_slider(wxKeyEvent& evt)
{
    if (m_slider) 
        m_slider->OnChar(evt);
}

void Preview::bind_event_handlers()
{
    this->Bind(wxEVT_SIZE, &Preview::on_size, this);
    m_choice_view_type->Bind(wxEVT_CHOICE, &Preview::on_choice_view_type, this);
    m_combochecklist_features->Bind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_features, this);
    m_checkbox_travel->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_travel, this);
    m_checkbox_retractions->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_retractions, this);
    m_checkbox_unretractions->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_unretractions, this);
    m_checkbox_shells->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_shells, this);
    m_checkbox_legend->Bind(wxEVT_CHECKBOX, &Preview::on_checkbox_legend, this);
}

void Preview::unbind_event_handlers()
{
    this->Unbind(wxEVT_SIZE, &Preview::on_size, this);
    m_choice_view_type->Unbind(wxEVT_CHOICE, &Preview::on_choice_view_type, this);
    m_combochecklist_features->Unbind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_features, this);
    m_checkbox_travel->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_travel, this);
    m_checkbox_retractions->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_retractions, this);
    m_checkbox_unretractions->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_unretractions, this);
    m_checkbox_shells->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_shells, this);
    m_checkbox_legend->Unbind(wxEVT_CHECKBOX, &Preview::on_checkbox_legend, this);
}

void Preview::show_hide_ui_elements(const std::string& what)
{
    bool enable = (what == "full");
    m_label_show_features->Enable(enable);
    m_combochecklist_features->Enable(enable);
    m_checkbox_travel->Enable(enable); 
    m_checkbox_retractions->Enable(enable);
    m_checkbox_unretractions->Enable(enable);
    m_checkbox_shells->Enable(enable);
    m_checkbox_legend->Enable(enable);

    enable = (what != "none");
    m_label_view_type->Enable(enable);
    m_choice_view_type->Enable(enable);

    bool visible = (what != "none");
    m_label_show_features->Show(visible);
    m_combochecklist_features->Show(visible);
    m_checkbox_travel->Show(visible);
    m_checkbox_retractions->Show(visible);
    m_checkbox_unretractions->Show(visible);
    m_checkbox_shells->Show(visible);
    m_checkbox_legend->Show(visible);
    m_label_view_type->Show(visible);
    m_choice_view_type->Show(visible);
}

void Preview::reset_sliders(bool reset_all)
{
    m_enabled = false;
//    reset_double_slider();
    if (reset_all)
        m_double_slider_sizer->Hide((size_t)0);
    else
        m_double_slider_sizer->GetItem(size_t(0))->GetSizer()->Hide(1);
}

void Preview::update_sliders(const std::vector<double>& layers_z, bool keep_z_range)
{
    m_enabled = true;
    // update extruder selector
    if (wxGetApp().extruders_edited_cnt() != m_extruder_selector->GetCount()-1)
    {
        m_selected_extruder = m_extruder_selector->GetSelection();
        update_extruder_selector();
        if (m_selected_extruder >= m_extruder_selector->GetCount())
            m_selected_extruder = 0;
        m_extruder_selector->SetSelection(m_selected_extruder);
    }

    update_double_slider(layers_z, keep_z_range);
    m_double_slider_sizer->Show((size_t)0);
    if (m_slider->GetManipulationState() == DoubleSlider::msSingleExtruder)
        m_double_slider_sizer->GetItem(size_t(0))->GetSizer()->Hide((size_t)0);
    Layout();
}

void Preview::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    Refresh();
}

void Preview::on_choice_view_type(wxCommandEvent& evt)
{
    m_preferred_color_mode = (m_choice_view_type->GetStringSelection() == L("Tool")) ? "tool" : "feature";
    int selection = m_choice_view_type->GetCurrentSelection();
    if ((0 <= selection) && (selection < (int)GCodePreviewData::Extrusion::Num_View_Types))
        m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)selection;

    if (m_gcode_preview_data->extrusion.view_type != GCodePreviewData::Extrusion::ColorPrint)
        m_extruder_selector->SetSelection(0);

    reload_print();
}

void Preview::on_combochecklist_features(wxCommandEvent& evt)
{
    int flags = Slic3r::GUI::combochecklist_get_flags(m_combochecklist_features);
    m_gcode_preview_data->extrusion.role_flags = (unsigned int)flags;
    refresh_print();
}

void Preview::on_checkbox_travel(wxCommandEvent& evt)
{
    m_gcode_preview_data->travel.is_visible = m_checkbox_travel->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_retractions(wxCommandEvent& evt)
{
    m_gcode_preview_data->retraction.is_visible = m_checkbox_retractions->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_unretractions(wxCommandEvent& evt)
{
    m_gcode_preview_data->unretraction.is_visible = m_checkbox_unretractions->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_shells(wxCommandEvent& evt)
{
    m_gcode_preview_data->shell.is_visible = m_checkbox_shells->IsChecked();
    refresh_print();
}

void Preview::on_checkbox_legend(wxCommandEvent& evt)
{
    m_canvas->enable_legend_texture(m_checkbox_legend->IsChecked());
    m_canvas_widget->Refresh();
}

void Preview::update_view_type(bool slice_completed)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->project_config;

    /*
    // #ys_FIXME_COLOR
    const wxString& choice = !config.option<ConfigOptionFloats>("colorprint_heights")->values.empty() && 
                             wxGetApp().extruders_edited_cnt()==1 ? 
                                _(L("Color Print")) :
                                config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values.size() > 1 ?
                                    _(L("Tool")) : 
                                    _(L("Feature type"));
    */

    const wxString& choice = !wxGetApp().plater()->model().custom_gcode_per_height.empty() &&
                             (wxGetApp().extruders_edited_cnt()==1 || !slice_completed) ? 
                                _(L("Color Print")) :
                                config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values.size() > 1 ?
                                    _(L("Tool")) : 
                                    _(L("Feature type"));

    int type = m_choice_view_type->FindString(choice);
    if (m_choice_view_type->GetSelection() != type) {
        m_choice_view_type->SetSelection(type);
        if (0 <= type && type < (int)GCodePreviewData::Extrusion::Num_View_Types)
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;
        m_preferred_color_mode = "feature";
    }

    if (type != GCodePreviewData::Extrusion::EViewType::ColorPrint)
        m_extruder_selector->SetSelection(0);
}

void Preview::update_extruder_selector()
{
    apply_extruder_selector(&m_extruder_selector, this, L("Whole print"), wxDefaultPosition, wxDefaultSize, true);
}

void Preview::create_double_slider()
{
    m_slider = new DoubleSlider(this, wxID_ANY, 0, 0, 0, 100);
    // #ys_FIXME_COLOR
    // m_double_slider_sizer->Add(m_slider, 0, wxEXPAND, 0);

    update_extruder_selector();
    m_extruder_selector->SetSelection(0);
    m_extruder_selector->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt)
    {
        m_selected_extruder = m_extruder_selector->GetSelection();
        m_slider->SetExtruderID(m_selected_extruder);

        int type = m_choice_view_type->FindString(_(L("Color Print")));

        if (m_choice_view_type->GetSelection() != type) {
            m_choice_view_type->SetSelection(type);
            if (0 <= type && type < (int)GCodePreviewData::Extrusion::Num_View_Types)
                m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;
            m_preferred_color_mode = "feature";
        }
        reload_print();

        evt.StopPropagation();
    });

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_extruder_selector, 0, wxEXPAND, 0);
    sizer->Add(m_slider, 1, wxEXPAND, 0);

    m_double_slider_sizer->Add(sizer, 0, wxEXPAND, 0);

    // sizer, m_canvas_widget
    m_canvas_widget->Bind(wxEVT_KEY_DOWN, &Preview::update_double_slider_from_canvas, this);

    m_slider->Bind(wxEVT_SCROLL_CHANGED, &Preview::on_sliders_scroll_changed, this);


    Bind(wxCUSTOMEVT_TICKSCHANGED, [this](wxEvent&) {
        // #ys_FIXME_COLOR
        // wxGetApp().preset_bundle->project_config.option<ConfigOptionFloats>("colorprint_heights")->values = m_slider->GetTicksValues();

        Model& model = wxGetApp().plater()->model();
        model.custom_gcode_per_height = m_slider->GetTicksValues_();
        m_schedule_background_process();

        update_view_type(false);

        reload_print();
    });
}

// Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
// Returns -1 if there is no such member.
static int find_close_layer_idx(const std::vector<double>& zs, double &z, double eps)
{
    if (zs.empty())
        return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        -- it_l;
        if (z - *it_l < eps)
            return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps)
            return 0;
    } else {
        auto it_l = it_h;
        -- it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps) {
            return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin());
        }
    }
    return -1;
}

void Preview::check_slider_values(std::vector<Model::CustomGCode>& ticks_from_model,
                                  const std::vector<double>& layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                     [layers_z](Model::CustomGCode val)
        {
            auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val.height - DoubleSlider::epsilon());
            return it == layers_z.end();
        }),
        ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

void Preview::update_double_slider(const std::vector<double>& layers_z, bool keep_z_range)
{
    // Save the initial slider span.
    double z_low        = m_slider->GetLowerValueD();
    double z_high       = m_slider->GetHigherValueD();
    bool   was_empty    = m_slider->GetMaxValue() == 0;
    bool force_sliders_full_range = was_empty;
    if (!keep_z_range)
    {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_slider->GetMaxValueD()) > DoubleSlider::epsilon()/*1e-6*/;
        force_sliders_full_range |= span_changed;
    }
    bool   snap_to_min = force_sliders_full_range || m_slider->is_lower_at_min();
	bool   snap_to_max  = force_sliders_full_range || m_slider->is_higher_at_max();

    // #ys_FIXME_COLOR
    // std::vector<double> &ticks_from_config = (wxGetApp().preset_bundle->project_config.option<ConfigOptionFloats>("colorprint_heights"))->values;
    // check_slider_values(ticks_from_config, layers_z);
    std::vector<Model::CustomGCode> tmp_ticks_from_model;
    if (m_selected_extruder != 0)
        tmp_ticks_from_model = wxGetApp().plater()->model().custom_gcode_per_height;
    std::vector<Model::CustomGCode> &ticks_from_model = m_selected_extruder != 0 ? tmp_ticks_from_model :
                                                        wxGetApp().plater()->model().custom_gcode_per_height;
    check_slider_values(ticks_from_model, layers_z);

    m_slider->SetSliderValues(layers_z);
    assert(m_slider->GetMinValue() == 0);
    m_slider->SetMaxValue(layers_z.empty() ? 0 : layers_z.size() - 1);

    int idx_low  = 0;
    int idx_high = m_slider->GetMaxValue();
    if (! layers_z.empty()) {
        if (! snap_to_min) {
            int idx_new = find_close_layer_idx(layers_z, z_low, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (! snap_to_max) {
            int idx_new = find_close_layer_idx(layers_z, z_high, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }
    m_slider->SetSelectionSpan(idx_low, idx_high);

    // #ys_FIXME_COLOR
    // m_slider->SetTicksValues(ticks_from_config);
    m_slider->SetTicksValues_(ticks_from_model);

    bool color_print_enable = (wxGetApp().plater()->printer_technology() == ptFFF);
    //  #ys_FIXME_COLOR
    // if (color_print_enable) {
    //     const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    //     if (cfg.opt<ConfigOptionFloats>("nozzle_diameter")->values.size() > 1) 
    //         color_print_enable = false;
    // }
    // m_slider->EnableTickManipulation(color_print_enable);

    m_slider->EnableTickManipulation(color_print_enable);
    if (color_print_enable && wxGetApp().extruders_edited_cnt() > 1) {
        //bool is_detected_full_print = //wxGetApp().plater()->fff_print().extruders().size() == 1;
        m_slider->SetExtruderID(m_extruder_selector->GetSelection());
    }
    else
        m_slider->SetExtruderID(-1);

}
//  #ys_FIXME_COLOR
void Preview::check_slider_values(std::vector<double>& ticks_from_config,
                                 const std::vector<double> &layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_config.size();
    ticks_from_config.erase(std::remove_if(ticks_from_config.begin(), ticks_from_config.end(),
                                           [layers_z](double val)
    {
        auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val - DoubleSlider::epsilon());
        return it == layers_z.end();
    }),
                            ticks_from_config.end());
    if (ticks_from_config.size() != old_size)
        m_schedule_background_process();
}

void Preview::reset_double_slider()
{
    m_slider->SetHigherValue(0);
    m_slider->SetLowerValue(0);
}

void Preview::update_double_slider_from_canvas(wxKeyEvent& event)
{
    if (event.HasModifiers()) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();

    if (key == 'U' || key == 'D') {
        const int new_pos = key == 'U' ? m_slider->GetHigherValue() + 1 : m_slider->GetHigherValue() - 1;
        m_slider->SetHigherValue(new_pos);
		if (event.ShiftDown() || m_slider->is_one_layer()) m_slider->SetLowerValue(m_slider->GetHigherValue());
    }
    else if (key == 'L') {
        m_checkbox_legend->SetValue(!m_checkbox_legend->GetValue());
        auto evt = wxCommandEvent();
        on_checkbox_legend(evt);
    }
    else if (key == 'S')
        m_slider->ChangeOneLayerLock();
    else
        event.Skip();
}

void Preview::load_print_as_fff(bool keep_z_range)
{
    if (m_loaded || m_process->current_printer_technology() != ptFFF)
        return;

    // we require that there's at least one object and the posSlice step
    // is performed on all of them(this ensures that _shifted_copies was
    // populated and we know the number of layers)
    bool has_layers = false;
    const Print *print = m_process->fff_print();
    if (print->is_step_done(posSlice)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->layers().empty()) {
                has_layers = true;
                break;
            }
    }
	if (print->is_step_done(posSupportMaterial)) {
        for (const PrintObject* print_object : print->objects())
            if (! print_object->support_layers().empty()) {
                has_layers = true;
                break;
            }
    }

    if (! has_layers)
    {
        reset_sliders(true);
        m_canvas->reset_legend_texture();
        m_canvas_widget->Refresh();
        return;
    }

    if (m_preferred_color_mode == "tool_or_feature")
    {
        // It is left to Slic3r to decide whether the print shall be colored by the tool or by the feature.
        // Color by feature if it is a single extruder print.
        unsigned int number_extruders = (unsigned int)print->extruders().size();
        int tool_idx = m_choice_view_type->FindString(_(L("Tool")));
        int type = (number_extruders > 1) ? tool_idx /* color by a tool number */ : 0; // color by a feature type
        m_choice_view_type->SetSelection(type);
        if ((0 <= type) && (type < (int)GCodePreviewData::Extrusion::Num_View_Types))
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;
        // If the->SetSelection changed the following line, revert it to "decide yourself".
        m_preferred_color_mode = "tool_or_feature";
    }

    bool gcode_preview_data_valid = print->is_step_done(psGCodeExport) && ! m_gcode_preview_data->empty();
    // Collect colors per extruder.
    std::vector<std::string> colors;
    // #ys_FIXME_COLOR
    // std::vector<double> color_print_values = {};
    std::vector<Model::CustomGCode> color_print_values = {};
    // set color print values, if it si selected "ColorPrint" view type
    if (m_gcode_preview_data->extrusion.view_type == GCodePreviewData::Extrusion::ColorPrint)
    {
        colors = wxGetApp().plater()->get_extruder_colors_from_plater_config();
        color_print_values = wxGetApp().plater()->model().custom_gcode_per_height;

        for (const Model::CustomGCode& code : color_print_values)
            if (code.gcode == "M600")
                colors.push_back(code.color);
        colors.push_back("#808080"); // gray color for pause print or custom G-code 

        if (gcode_preview_data_valid)
            color_print_values.clear();
        /*
        if (! gcode_preview_data_valid) {
            // #ys_FIXME_COLOR
            // const auto& config = wxGetApp().preset_bundle->project_config;
            // color_print_values = config.option<ConfigOptionFloats>("colorprint_heights")->values;
            color_print_values = wxGetApp().plater()->model().custom_gcode_per_height;
        }
        */
    }
    else if (gcode_preview_data_valid || (m_gcode_preview_data->extrusion.view_type == GCodePreviewData::Extrusion::Tool) )
    {
        const ConfigOptionStrings* extruders_opt = dynamic_cast<const ConfigOptionStrings*>(m_config->option("extruder_colour"));
        const ConfigOptionStrings* filamemts_opt = dynamic_cast<const ConfigOptionStrings*>(m_config->option("filament_colour"));
        unsigned int colors_count = std::max((unsigned int)extruders_opt->values.size(), (unsigned int)filamemts_opt->values.size());

        unsigned char rgb[3];
        for (unsigned int i = 0; i < colors_count; ++i)
        {
            std::string color = m_config->opt_string("extruder_colour", i);
            if (!PresetBundle::parse_color(color, rgb))
            {
                color = m_config->opt_string("filament_colour", i);
                if (!PresetBundle::parse_color(color, rgb))
                    color = "#FFFFFF";
            }

            colors.emplace_back(color);
        }
        color_print_values.clear();
    }

    if (IsShown())
    {
        m_canvas->set_selected_extruder(m_selected_extruder);
        if (gcode_preview_data_valid) {
            // Load the real G-code preview.
            m_canvas->load_gcode_preview(*m_gcode_preview_data, colors);
            m_loaded = true;
        } else {
            // disable color change information for multi-material presets
            // if (wxGetApp().extruders_edited_cnt() > 1) // #ys_FIXME_COLOR
            //     color_print_values.clear();

            // Load the initial preview based on slices, not the final G-code.
            m_canvas->load_preview(colors, color_print_values);
        }
        show_hide_ui_elements(gcode_preview_data_valid ? "full" : "simple");
        // recalculates zs and update sliders accordingly
        std::vector<double> zs = m_canvas->get_current_print_zs(true);
        if (zs.empty()) {
            // all layers filtered out
            reset_sliders(m_selected_extruder==0);
            m_canvas_widget->Refresh();
        } else
            update_sliders(zs, keep_z_range);
    }
}

void Preview::load_print_as_sla()
{
    if (m_loaded || (m_process->current_printer_technology() != ptSLA))
        return;

    unsigned int n_layers = 0;
    const SLAPrint* print = m_process->sla_print();

    std::vector<double> zs;
    double initial_layer_height = print->material_config().initial_layer_height.value;
    for (const SLAPrintObject* obj : print->objects())
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty())
        {
            auto low_coord = obj->get_slice_index().front().print_level();
            for (auto& rec : obj->get_slice_index())
                zs.emplace_back(initial_layer_height + (rec.print_level() - low_coord) * SCALING_FACTOR);
        }
    sort_remove_duplicates(zs);

    m_canvas->reset_clipping_planes_cache();

    n_layers = (unsigned int)zs.size();
    if (n_layers == 0)
    {
        reset_sliders(true);
        m_canvas_widget->Refresh();
    }

    if (IsShown())
    {
        m_canvas->load_sla_preview();
        show_hide_ui_elements("none");

        if (n_layers > 0)
            update_sliders(zs);

        m_loaded = true;
    }
}

void Preview::on_sliders_scroll_changed(wxCommandEvent& event)
{
    if (IsShown())
    {
        PrinterTechnology tech = m_process->current_printer_technology();
        if (tech == ptFFF)
        {
            m_canvas->set_toolpaths_range(m_slider->GetLowerValueD() - 1e-6, m_slider->GetHigherValueD() + 1e-6);
            m_canvas->render();
            m_canvas->set_use_clipping_planes(false);
        }
        else if (tech == ptSLA)
        {
            m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -m_slider->GetLowerValueD()));
            m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), m_slider->GetHigherValueD()));
            m_canvas->set_use_clipping_planes(m_slider->GetHigherValue() != 0);
            m_canvas->render();
        }
    }
}


} // namespace GUI
} // namespace Slic3r
