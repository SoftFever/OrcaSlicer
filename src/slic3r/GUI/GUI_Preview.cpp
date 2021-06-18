//#include "stdlib.h"
#include "libslic3r/libslic3r.h"
#include "libslic3r/Layer.hpp"
#include "GUI_Preview.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "OpenGLManager.hpp"
#include "GLCanvas3D.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "DoubleSlider.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"

#include <wx/listbook.h>
#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "NotificationManager.hpp"

#ifdef _WIN32
#include "BitmapComboBox.hpp"
#endif

namespace Slic3r {
namespace GUI {

View3D::View3D(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : m_canvas_widget(nullptr)
    , m_canvas(nullptr)
{
    init(parent, model, config, process);
}

View3D::~View3D()
{
    delete m_canvas;
    delete m_canvas_widget;
}

bool View3D::init(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));

    m_canvas->allow_multisample(OpenGLManager::can_multisample());
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
    m_canvas->enable_labels(true);
    m_canvas->enable_slope(true);

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
    wxWindow* parent, Model* model, DynamicPrintConfig* config,
    BackgroundSlicingProcess* process, GCodeProcessor::Result* gcode_result, std::function<void()> schedule_background_process_func)
    : m_config(config)
    , m_process(process)
    , m_gcode_result(gcode_result)
    , m_schedule_background_process(schedule_background_process_func)
{
    if (init(parent, model))
        load_print();
}

bool Preview::init(wxWindow* parent, Model* model)
{
    if (!Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 /* disable wxTAB_TRAVERSAL */))
        return false;

    // to match the background of the sliders
#ifdef _WIN32 
    wxGetApp().UpdateDarkUI(this);
#else
    SetBackgroundColour(GetParent()->GetBackgroundColour());
#endif // _WIN32 

    m_canvas_widget = OpenGLManager::create_wxglcanvas(*this);
    if (m_canvas_widget == nullptr)
        return false;

    m_canvas = new GLCanvas3D(m_canvas_widget);
    m_canvas->set_context(wxGetApp().init_glcontext(*m_canvas_widget));
    m_canvas->allow_multisample(OpenGLManager::can_multisample());
    m_canvas->set_config(m_config);
    m_canvas->set_model(model);
    m_canvas->set_process(m_process);
    m_canvas->enable_legend_texture(true);
    m_canvas->enable_dynamic_background(true);

    m_layers_slider_sizer = create_layers_slider_sizer();

    wxGetApp().UpdateDarkUI(m_bottom_toolbar_panel = new wxPanel(this));
    m_label_view_type = new wxStaticText(m_bottom_toolbar_panel, wxID_ANY, _L("View"));
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(m_choice_view_type = new BitmapComboBox(m_bottom_toolbar_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY));
#else
    m_choice_view_type = new wxComboBox(m_bottom_toolbar_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, NULL, wxCB_READONLY);
#endif
    m_choice_view_type->Append(_L("Feature type"));
    m_choice_view_type->Append(_L("Height"));
    m_choice_view_type->Append(_L("Width"));
    m_choice_view_type->Append(_L("Speed"));
    m_choice_view_type->Append(_L("Fan speed"));
    m_choice_view_type->Append(_L("Temperature"));
    m_choice_view_type->Append(_L("Volumetric flow rate"));
    m_choice_view_type->Append(_L("Tool"));
    m_choice_view_type->Append(_L("Color Print"));
    m_choice_view_type->SetSelection(0);

    m_label_show = new wxStaticText(m_bottom_toolbar_panel, wxID_ANY, _L("Show"));

#ifdef _WIN32
    long combo_style = wxCB_READONLY | wxBORDER_SIMPLE; //set border allows use default color instead of theme color wich is allways light under MSW
#else
    long combo_style = wxCB_READONLY;
#endif
    m_combochecklist_features = new wxComboCtrl();
    m_combochecklist_features->Create(m_bottom_toolbar_panel, wxID_ANY, _L("Feature types"), wxDefaultPosition, wxDefaultSize, combo_style);
    std::string feature_items = GUI::into_u8(
        _L("Unknown") + "|1|" +
        _L("Perimeter") + "|1|" +
        _L("External perimeter") + "|1|" +
        _L("Overhang perimeter") + "|1|" +
        _L("Internal infill") + "|1|" +
        _L("Solid infill") + "|1|" +
        _L("Top solid infill") + "|1|" +
        _L("Ironing") + "|1|" +
        _L("Bridge infill") + "|1|" +
        _L("Gap fill") + "|1|" +
        _L("Skirt/Brim") + "|1|" +
        _L("Support material") + "|1|" +
        _L("Support material interface") + "|1|" +
        _L("Wipe tower") + "|1|" +
        _L("Custom") + "|1"
    );
    Slic3r::GUI::create_combochecklist(m_combochecklist_features, GUI::into_u8(_L("Feature types")), feature_items);

    m_combochecklist_options = new wxComboCtrl();
    m_combochecklist_options->Create(m_bottom_toolbar_panel, wxID_ANY, _L("Options"), wxDefaultPosition, wxDefaultSize, combo_style);
    std::string options_items = GUI::into_u8(
        get_option_type_string(OptionType::Travel) + "|0|" +
        get_option_type_string(OptionType::Wipe) + "|0|" +
        get_option_type_string(OptionType::Retractions) + "|0|" +
        get_option_type_string(OptionType::Unretractions) + "|0|" +
#if ENABLE_SEAMS_VISUALIZATION
        get_option_type_string(OptionType::Seams) + "|0|" +
#endif // ENABLE_SEAMS_VISUALIZATION
        get_option_type_string(OptionType::ToolChanges) + "|0|" +
        get_option_type_string(OptionType::ColorChanges) + "|0|" +
        get_option_type_string(OptionType::PausePrints) + "|0|" +
        get_option_type_string(OptionType::CustomGCodes) + "|0|" +
        get_option_type_string(OptionType::Shells) + "|0|" +
        get_option_type_string(OptionType::ToolMarker) + "|1|" +
        get_option_type_string(OptionType::Legend) + "|1"
    );
    Slic3r::GUI::create_combochecklist(m_combochecklist_options, GUI::into_u8(_L("Options")), options_items);

    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_left_sizer->Add(m_canvas_widget, 1, wxALL | wxEXPAND, 0);

    wxBoxSizer* right_sizer = new wxBoxSizer(wxVERTICAL);
    right_sizer->Add(m_layers_slider_sizer, 1, wxEXPAND, 0);

    m_moves_slider = new DoubleSlider::Control(m_bottom_toolbar_panel, wxID_ANY, 0, 0, 0, 100, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    m_moves_slider->SetDrawMode(DoubleSlider::dmSequentialGCodeView);

    wxBoxSizer* bottom_toolbar_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_toolbar_sizer->AddSpacer(5);
    bottom_toolbar_sizer->Add(m_label_view_type, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    bottom_toolbar_sizer->Add(m_choice_view_type, 0, wxALIGN_CENTER_VERTICAL, 0);
    bottom_toolbar_sizer->AddSpacer(5);
    bottom_toolbar_sizer->Add(m_label_show, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    bottom_toolbar_sizer->Add(m_combochecklist_options, 0, wxALIGN_CENTER_VERTICAL, 0);
    // change the following number if editing the layout of the bottom toolbar sizer. It is used into update_bottom_toolbar()
    m_combochecklist_features_pos = 6;
    bottom_toolbar_sizer->Add(m_combochecklist_features, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    bottom_toolbar_sizer->Hide(m_combochecklist_features);
    bottom_toolbar_sizer->AddSpacer(5);
    bottom_toolbar_sizer->Add(m_moves_slider, 1, wxALL | wxEXPAND, 0);
    m_bottom_toolbar_panel->SetSizer(bottom_toolbar_sizer);

    m_left_sizer->Add(m_bottom_toolbar_panel, 0, wxALL | wxEXPAND, 0);
    m_left_sizer->Hide(m_bottom_toolbar_panel);

    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);
    main_sizer->Add(m_left_sizer, 1, wxALL | wxEXPAND, 0);
    main_sizer->Add(right_sizer, 0, wxALL | wxEXPAND, 0);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    GetSizer()->SetSizeHints(this);

    bind_event_handlers();
    
    return true;
}

Preview::~Preview()
{
    unbind_event_handlers();

    if (m_canvas != nullptr)
        delete m_canvas;

    if (m_canvas_widget != nullptr)
        delete m_canvas_widget;
}

void Preview::set_as_dirty()
{
    if (m_canvas != nullptr)
        m_canvas->set_as_dirty();
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

    update_bottom_toolbar();
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
#endif /* __linux__ */
    if (
#ifdef __linux__
        m_volumes_cleanup_required || 
#endif /* __linux__ */
        !keep_volumes)
    {
        m_canvas->reset_volumes();
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
    if (m_layers_slider != nullptr) m_layers_slider->msw_rescale();
    if (m_moves_slider != nullptr) m_moves_slider->msw_rescale();

    // rescale warning legend on the canvas
    get_canvas3d()->msw_rescale();

    // rescale legend
    refresh_print();
}

void Preview::sys_color_changed()
{
#ifdef _WIN32
    wxWindowUpdateLocker noUpdates(this);

    wxGetApp().UpdateAllStaticTextDarkUI(m_bottom_toolbar_panel);
    wxGetApp().UpdateDarkUI(m_choice_view_type);
    wxGetApp().UpdateDarkUI(m_combochecklist_features);
    wxGetApp().UpdateDarkUI(static_cast<wxCheckListBoxComboPopup*>(m_combochecklist_features->GetPopupControl()));
    wxGetApp().UpdateDarkUI(m_combochecklist_options);
    wxGetApp().UpdateDarkUI(static_cast<wxCheckListBoxComboPopup*>(m_combochecklist_options->GetPopupControl()));
#endif

    if (m_layers_slider != nullptr)
        m_layers_slider->sys_color_changed();
}

void Preview::jump_layers_slider(wxKeyEvent& evt)
{
    if (m_layers_slider) m_layers_slider->OnChar(evt);
}

void Preview::move_layers_slider(wxKeyEvent& evt)
{
    if (m_layers_slider != nullptr) m_layers_slider->OnKeyDown(evt);
}

void Preview::edit_layers_slider(wxKeyEvent& evt)
{
    if (m_layers_slider != nullptr) m_layers_slider->OnChar(evt);
}

void Preview::bind_event_handlers()
{
    this->Bind(wxEVT_SIZE, &Preview::on_size, this);
    m_choice_view_type->Bind(wxEVT_COMBOBOX, &Preview::on_choice_view_type, this);
    m_combochecklist_features->Bind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_features, this);
    m_combochecklist_options->Bind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_options, this);
    m_moves_slider->Bind(wxEVT_SCROLL_CHANGED, &Preview::on_moves_slider_scroll_changed, this);
}

void Preview::unbind_event_handlers()
{
    this->Unbind(wxEVT_SIZE, &Preview::on_size, this);
    m_choice_view_type->Unbind(wxEVT_COMBOBOX, &Preview::on_choice_view_type, this);
    m_combochecklist_features->Unbind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_features, this);
    m_combochecklist_options->Unbind(wxEVT_CHECKLISTBOX, &Preview::on_combochecklist_options, this);
    m_moves_slider->Unbind(wxEVT_SCROLL_CHANGED, &Preview::on_moves_slider_scroll_changed, this);
}

void Preview::move_moves_slider(wxKeyEvent& evt)
{
    if (m_moves_slider != nullptr) m_moves_slider->OnKeyDown(evt);
}

void Preview::hide_layers_slider()
{
    m_layers_slider_sizer->Hide((size_t)0);
    Layout();
}

void Preview::on_size(wxSizeEvent& evt)
{
    evt.Skip();
    Refresh();
}

void Preview::on_choice_view_type(wxCommandEvent& evt)
{
    int selection = m_choice_view_type->GetCurrentSelection();
    if (0 <= selection && selection < static_cast<int>(GCodeViewer::EViewType::Count)) {
        m_canvas->set_toolpath_view_type(static_cast<GCodeViewer::EViewType>(selection));
        m_keep_current_preview_type = true;
    }
    refresh_print();
}

void Preview::on_combochecklist_features(wxCommandEvent& evt)
{
    unsigned int flags = Slic3r::GUI::combochecklist_get_flags(m_combochecklist_features);
    m_canvas->set_toolpath_role_visibility_flags(flags);
    refresh_print();
}

void Preview::on_combochecklist_options(wxCommandEvent& evt)
{
    unsigned int curr_flags = m_canvas->get_gcode_options_visibility_flags();
    unsigned int new_flags = Slic3r::GUI::combochecklist_get_flags(m_combochecklist_options);
    if (curr_flags == new_flags)
        return;

    m_canvas->set_gcode_options_visibility_from_flags(new_flags);
    m_canvas->refresh_gcode_preview_render_paths();
    update_moves_slider();
}

void Preview::update_bottom_toolbar()
{
    combochecklist_set_flags(m_combochecklist_features, m_canvas->get_toolpath_role_visibility_flags());
    combochecklist_set_flags(m_combochecklist_options, m_canvas->get_gcode_options_visibility_flags());

    // updates visibility of features combobox
    if (m_bottom_toolbar_panel->IsShown()) {
        wxSizer* sizer = m_bottom_toolbar_panel->GetSizer();
        bool show = !m_canvas->is_gcode_legend_enabled() || m_canvas->get_gcode_view_type() != GCodeViewer::EViewType::FeatureType;

        if (show) {
            if (sizer->GetItem(m_combochecklist_features) == nullptr) {
                sizer->Insert(m_combochecklist_features_pos, m_combochecklist_features, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
                sizer->Show(m_combochecklist_features);
                sizer->Layout();
                Refresh();
            }
        }
        else {
            if (sizer->GetItem(m_combochecklist_features) != nullptr) {
                sizer->Hide(m_combochecklist_features);
                sizer->Detach(m_combochecklist_features);
                sizer->Layout();
                Refresh();
            }
        }
    }
}

wxBoxSizer* Preview::create_layers_slider_sizer()
{
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    m_layers_slider = new DoubleSlider::Control(this, wxID_ANY, 0, 0, 0, 100);

    m_layers_slider->SetDrawMode(wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA,
        wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects"));
    m_layers_slider->enable_action_icon(wxGetApp().is_editor());

    sizer->Add(m_layers_slider, 0, wxEXPAND, 0);

    // sizer, m_canvas_widget
    m_canvas_widget->Bind(wxEVT_KEY_DOWN, &Preview::update_layers_slider_from_canvas, this);
    m_canvas_widget->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_SHIFT)
            m_layers_slider->UseDefaultColors(true);
        event.Skip();
        });

    m_layers_slider->Bind(wxEVT_SCROLL_CHANGED, &Preview::on_layers_slider_scroll_changed, this);

    Bind(DoubleSlider::wxCUSTOMEVT_TICKSCHANGED, [this](wxEvent&) {
        Model& model = wxGetApp().plater()->model();
        model.custom_gcode_per_print_z = m_layers_slider->GetTicksValues();
        m_schedule_background_process();

        m_keep_current_preview_type = false;
        reload_print(false);
        });

    return sizer;
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

void Preview::check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model, const std::vector<double>& layers_z)
{
    // All ticks that would end up outside the slider range should be erased.
    // TODO: this should be placed into more appropriate part of code,
    // this function is e.g. not called when the last object is deleted
    unsigned int old_size = ticks_from_model.size();
    ticks_from_model.erase(std::remove_if(ticks_from_model.begin(), ticks_from_model.end(),
                     [layers_z](CustomGCode::Item val)
        {
            auto it = std::lower_bound(layers_z.begin(), layers_z.end(), val.print_z - DoubleSlider::epsilon());
            return it == layers_z.end();
        }),
        ticks_from_model.end());
    if (ticks_from_model.size() != old_size)
        m_schedule_background_process();
}

void Preview::update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range)
{
    // Save the initial slider span.
    double z_low = m_layers_slider->GetLowerValueD();
    double z_high = m_layers_slider->GetHigherValueD();
    bool   was_empty = m_layers_slider->GetMaxValue() == 0;

    bool force_sliders_full_range = was_empty;
    if (!keep_z_range)
    {
        bool span_changed = layers_z.empty() || std::abs(layers_z.back() - m_layers_slider->GetMaxValueD()) > DoubleSlider::epsilon()/*1e-6*/;
        force_sliders_full_range |= span_changed;
    }
    bool   snap_to_min = force_sliders_full_range || m_layers_slider->is_lower_at_min();
    bool   snap_to_max = force_sliders_full_range || m_layers_slider->is_higher_at_max();

    // Detect and set manipulation mode for double slider
    update_layers_slider_mode();

    Plater* plater = wxGetApp().plater();
    CustomGCode::Info& ticks_info_from_model = plater->model().custom_gcode_per_print_z;
    check_layers_slider_values(ticks_info_from_model.gcodes, layers_z);

    //first of all update extruder colors to avoid crash, when we are switching printer preset from MM to SM
    m_layers_slider->SetExtruderColors(plater->get_extruder_colors_from_plater_config());

    m_layers_slider->SetSliderValues(layers_z);
    assert(m_layers_slider->GetMinValue() == 0);
    m_layers_slider->SetMaxValue(layers_z.empty() ? 0 : layers_z.size() - 1);

    int idx_low = 0;
    int idx_high = m_layers_slider->GetMaxValue();
    if (!layers_z.empty()) {
        if (!snap_to_min) {
            int idx_new = find_close_layer_idx(layers_z, z_low, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_low = idx_new;
        }
        if (!snap_to_max) {
            int idx_new = find_close_layer_idx(layers_z, z_high, DoubleSlider::epsilon()/*1e-6*/);
            if (idx_new != -1)
                idx_high = idx_new;
        }
    }
    m_layers_slider->SetSelectionSpan(idx_low, idx_high);
    m_layers_slider->SetTicksValues(ticks_info_from_model);

    bool sla_print_technology = plater->printer_technology() == ptSLA;
    bool sequential_print = wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_bool("complete_objects");
    m_layers_slider->SetDrawMode(sla_print_technology, sequential_print);
    if (sla_print_technology)
        m_layers_slider->SetLayersTimes(plater->sla_print().print_statistics().layers_times);
    else {
        auto print_mode_stat = m_gcode_result->print_statistics.modes.front();
        m_layers_slider->SetLayersTimes(print_mode_stat.layers_times, print_mode_stat.time);
    }

    // Suggest the auto color change, if model looks like sign
    if (m_layers_slider->IsNewPrint())
    {
        const Print& print = wxGetApp().plater()->fff_print();
        double delta_area = scale_(scale_(25)); // equal to 25 mm2

        //bool is_possible_auto_color_change = false;
        for (auto object : print.objects()) {
            double object_x = double(object->size().x());
            double object_y = double(object->size().y());

            // if it's sign, than object have not to be a too height
            double height = object->height();
            coord_t longer_side = std::max(object_x, object_y);
            auto   num_layers = int(object->layers().size());
            if (height / longer_side > 0.3 || num_layers < 2)
                continue;

            const ExPolygons& bottom = object->get_layer(0)->lslices;
            double bottom_area = area(bottom);

            // at least 30% of object's height have to be a solid 
            int  i;
            for (i = 1; i < int(0.3 * num_layers); ++ i) {
                double cur_area = area(object->get_layer(i)->lslices);
                if (cur_area != bottom_area && fabs(cur_area - bottom_area) > scale_(scale_(1)))
                    break;
            }
            if (i < int(0.3 * num_layers))
                continue;

            // bottom layer have to be a biggest, so control relation between bottom layer and object size
            double prev_area = area(object->get_layer(i)->lslices);
            for ( i++; i < num_layers; i++) {
                double cur_area = area(object->get_layer(i)->lslices);
                if (cur_area > prev_area && prev_area - cur_area > scale_(scale_(1)))
                    break;
                prev_area = cur_area;
            }
            if (i < num_layers)
                continue;

            double top_area = area(object->get_layer(int(object->layers().size()) - 1)->lslices);
            if( bottom_area - top_area > delta_area) {
                NotificationManager* notif_mngr = wxGetApp().plater()->get_notification_manager();
                notif_mngr->push_notification(
                    NotificationType::SignDetected, NotificationManager::NotificationLevel::RegularNotification,
                    _u8L("NOTE:") + "\n" + _u8L("Sliced object looks like the sign") + "\n",
                    _u8L("Apply auto color change to print"),
                    [this](wxEvtHandler*) {
                        m_layers_slider->auto_color_change();
                        return true;
                    });

                notif_mngr->apply_in_preview();

                break;
            }
        }
    }

    m_layers_slider_sizer->Show((size_t)0);
    Layout();
}

void Preview::update_layers_slider_mode()
{
    //    true  -> single-extruder printer profile OR 
    //             multi-extruder printer profile , but whole model is printed by only one extruder
    //    false -> multi-extruder printer profile , and model is printed by several extruders
    bool    one_extruder_printed_model = true;

    // extruder used for whole model for multi-extruder printer profile
    int     only_extruder = -1; 

    if (wxGetApp().extruders_edited_cnt() > 1)
    {
        const ModelObjectPtrs& objects = wxGetApp().plater()->model().objects;

        // check if whole model uses just only one extruder
        if (!objects.empty())
        {
            const int extruder = objects[0]->config.has("extruder") ?
                                 objects[0]->config.option("extruder")->getInt() : 0;

            auto is_one_extruder_printed_model = [objects, extruder]()
            {
                for (ModelObject* object : objects)
                {
                    if (object->config.has("extruder") &&
                        object->config.option("extruder")->getInt() != extruder)
                        return false;

                    if (object->volumes.size() > 1)
                        for (ModelVolume* volume : object->volumes)
                            if (volume->config.has("extruder") &&
                                volume->config.option("extruder")->getInt() != extruder)
                                return false;

                    for (const auto& range : object->layer_config_ranges)
                        if (range.second.has("extruder") &&
                            range.second.option("extruder")->getInt() != extruder)
                            return false;
                }
                return true;
            };

            if (is_one_extruder_printed_model())
                only_extruder = extruder;
            else
                one_extruder_printed_model = false;
        }
    }

    m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder);
}

void Preview::reset_layers_slider()
{
    m_layers_slider->SetHigherValue(0);
    m_layers_slider->SetLowerValue(0);
}

void Preview::update_layers_slider_from_canvas(wxKeyEvent& event)
{
    if (event.HasModifiers()) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();

    if (key == 'S' || key == 'W') {
        const int new_pos = key == 'W' ? m_layers_slider->GetHigherValue() + 1 : m_layers_slider->GetHigherValue() - 1;
        m_layers_slider->SetHigherValue(new_pos);
        if (event.ShiftDown() || m_layers_slider->is_one_layer()) m_layers_slider->SetLowerValue(m_layers_slider->GetHigherValue());
    }
    else if (key == 'A' || key == 'D') {
        const int new_pos = key == 'D' ? m_moves_slider->GetHigherValue() + 1 : m_moves_slider->GetHigherValue() - 1;
        m_moves_slider->SetHigherValue(new_pos);
        if (event.ShiftDown() || m_moves_slider->is_one_layer()) m_moves_slider->SetLowerValue(m_moves_slider->GetHigherValue());
    }
    else if (key == 'X')
        m_layers_slider->ChangeOneLayerLock();
    else if (key == WXK_SHIFT)
        m_layers_slider->UseDefaultColors(false);
    else
        event.Skip();
}

void Preview::update_moves_slider()
{
    const GCodeViewer::SequentialView& view = m_canvas->get_gcode_sequential_view();
    // this should not be needed, but it is here to try to prevent rambling crashes on Mac Asan
    if (view.endpoints.last < view.endpoints.first)
        return;

    std::vector<double> values(view.endpoints.last - view.endpoints.first + 1);
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    std::vector<double> alternate_values(view.endpoints.last - view.endpoints.first + 1);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    unsigned int count = 0;
    for (unsigned int i = view.endpoints.first; i <= view.endpoints.last; ++i) {
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        values[count] = static_cast<double>(i + 1);
        if (view.gcode_ids[i] > 0)
            alternate_values[count] = static_cast<double>(view.gcode_ids[i]);
        ++count;
#else
        values[count++] = static_cast<double>(i + 1);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    }

    m_moves_slider->SetSliderValues(values);
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    m_moves_slider->SetSliderAlternateValues(alternate_values);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    m_moves_slider->SetMaxValue(view.endpoints.last - view.endpoints.first);
    m_moves_slider->SetSelectionSpan(view.current.first - view.endpoints.first, view.current.last - view.endpoints.first);
}

void Preview::enable_moves_slider(bool enable)
{
    bool render_as_disabled = !enable;
    if (m_moves_slider != nullptr && m_moves_slider->is_rendering_as_disabled() != render_as_disabled) {
        m_moves_slider->set_render_as_disabled(render_as_disabled);
        m_moves_slider->Refresh();
    }
}

void Preview::load_print_as_fff(bool keep_z_range)
{
    if (wxGetApp().mainframe == nullptr || wxGetApp().is_recreating_gui())
        // avoid processing while mainframe is being constructed
        return;

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

    if (wxGetApp().is_editor() && !has_layers) {
        hide_layers_slider();
        m_left_sizer->Hide(m_bottom_toolbar_panel);
        m_left_sizer->Layout();
        Refresh();
        m_canvas_widget->Refresh();
        return;
    }

    GCodeViewer::EViewType gcode_view_type = m_canvas->get_gcode_view_preview_type();
    bool gcode_preview_data_valid = !m_gcode_result->moves.empty();
    // Collect colors per extruder.
    std::vector<std::string> colors;
    std::vector<CustomGCode::Item> color_print_values = {};
    // set color print values, if it si selected "ColorPrint" view type
    if (gcode_view_type == GCodeViewer::EViewType::ColorPrint) {
        colors = wxGetApp().plater()->get_colors_for_color_print(m_gcode_result);

        if (!gcode_preview_data_valid) {
            color_print_values = wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes;
            colors.push_back("#808080"); // gray color for pause print or custom G-code 
        }
    }
    else if (gcode_preview_data_valid || gcode_view_type == GCodeViewer::EViewType::Tool) {
        colors = wxGetApp().plater()->get_extruder_colors_from_plater_config(m_gcode_result);
        color_print_values.clear();
    }

    if (IsShown()) {
        std::vector<double> zs;

        m_canvas->set_selected_extruder(0);
        if (gcode_preview_data_valid) {
            // Load the real G-code preview.
            m_canvas->load_gcode_preview(*m_gcode_result);
            m_canvas->refresh_gcode_preview(*m_gcode_result, colors);
            m_left_sizer->Show(m_bottom_toolbar_panel);
            m_left_sizer->Layout();
            Refresh();
            zs = m_canvas->get_gcode_layers_zs();
            m_loaded = true;
        } else {
            // Load the initial preview based on slices, not the final G-code.
            m_canvas->load_preview(colors, color_print_values);
            m_left_sizer->Hide(m_bottom_toolbar_panel);
            m_left_sizer->Layout();
            Refresh();
            zs = m_canvas->get_volumes_print_zs(true);
        }
        if (zs.empty()) {
            // all layers filtered out
            hide_layers_slider();
            m_canvas_widget->Refresh();
        } else
            update_layers_slider(zs, keep_z_range);
    }

    unsigned int number_extruders = (unsigned int)print->extruders().size();

    if (!m_keep_current_preview_type) {
        const wxString choice = !wxGetApp().plater()->model().custom_gcode_per_print_z.gcodes.empty() ?
            _L("Color Print") :
            (number_extruders > 1) ? _L("Tool") : _L("Feature type");

        int type = m_choice_view_type->FindString(choice);
        if (m_choice_view_type->GetSelection() != type) {
            if (0 <= type && type < static_cast<int>(GCodeViewer::EViewType::Count)) {
                m_choice_view_type->SetSelection(type);
                m_canvas->set_gcode_view_preview_type(static_cast<GCodeViewer::EViewType>(type));
            }
        }
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
        if (obj->is_step_done(slaposSliceSupports) && !obj->get_slice_index().empty()) {
            auto low_coord = obj->get_slice_index().front().print_level();
            for (auto& rec : obj->get_slice_index())
                zs.emplace_back(initial_layer_height + (rec.print_level() - low_coord) * SCALING_FACTOR);
        }
    sort_remove_duplicates(zs);

    m_canvas->reset_clipping_planes_cache();
    m_canvas->set_use_clipping_planes(true);

    n_layers = (unsigned int)zs.size();
    if (n_layers == 0) {
        hide_layers_slider();
        m_canvas_widget->Refresh();
    }

    if (IsShown()) {
        m_canvas->load_sla_preview();
        m_left_sizer->Hide(m_bottom_toolbar_panel);
        m_left_sizer->Hide(m_bottom_toolbar_panel);
        m_left_sizer->Layout();
        Refresh();

        if (n_layers > 0)
            update_layers_slider(zs);

        m_loaded = true;
    }
}

void Preview::on_layers_slider_scroll_changed(wxCommandEvent& event)
{
    if (IsShown()) {
        PrinterTechnology tech = m_process->current_printer_technology();
        if (tech == ptFFF) {
            m_canvas->set_volumes_z_range({ m_layers_slider->GetLowerValueD(), m_layers_slider->GetHigherValueD() });
            m_canvas->set_toolpaths_z_range({ static_cast<unsigned int>(m_layers_slider->GetLowerValue()), static_cast<unsigned int>(m_layers_slider->GetHigherValue()) });
            m_canvas->set_as_dirty();
        }
        else if (tech == ptSLA) {
            m_canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), -m_layers_slider->GetLowerValueD()));
            m_canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), m_layers_slider->GetHigherValueD()));
            m_canvas->render();
        }
    }
}

void Preview::on_moves_slider_scroll_changed(wxCommandEvent& event)
{
    m_canvas->update_gcode_sequential_view_current(static_cast<unsigned int>(m_moves_slider->GetLowerValueD() - 1.0), static_cast<unsigned int>(m_moves_slider->GetHigherValueD() - 1.0));
    m_canvas->render();
}

wxString Preview::get_option_type_string(OptionType type) const
{
    switch (type)
    {
    case OptionType::Travel:        { return _L("Travel"); }
    case OptionType::Wipe:          { return _L("Wipe"); }
    case OptionType::Retractions:   { return _L("Retractions"); }
    case OptionType::Unretractions: { return _L("Deretractions"); }
#if ENABLE_SEAMS_VISUALIZATION
    case OptionType::Seams:         { return _L("Seams"); }
#endif // ENABLE_SEAMS_VISUALIZATION
    case OptionType::ToolChanges:   { return _L("Tool changes"); }
    case OptionType::ColorChanges:  { return _L("Color changes"); }
    case OptionType::PausePrints:   { return _L("Print pauses"); }
    case OptionType::CustomGCodes:  { return _L("Custom G-codes"); }
    case OptionType::Shells:        { return _L("Shells"); }
    case OptionType::ToolMarker:    { return _L("Tool marker"); }
    case OptionType::Legend:        { return _L("Legend/Estimated printing time"); }
    default:                        { return ""; }
    }
}

} // namespace GUI
} // namespace Slic3r
