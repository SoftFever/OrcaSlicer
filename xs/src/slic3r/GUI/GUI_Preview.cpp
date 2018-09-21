#include "../../libslic3r/libslic3r.h"
#include "GUI_Preview.hpp"
#include "GUI.hpp"
#include "AppConfig.hpp"
#include "3DScene.hpp"
#include "../../libslic3r/GCode/PreviewData.hpp"
#include "PresetBundle.hpp"

#include <wx/notebook.h>
#include <wx/glcanvas.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <wx/combo.h>
#include <wx/checkbox.h>

// this include must follow the wxWidgets ones or it won't compile on Windows -> see http://trac.wxwidgets.org/ticket/2421
#include "../../libslic3r/Print.hpp"

namespace Slic3r {
namespace GUI {

Preview::Preview(wxNotebook* notebook, DynamicPrintConfig* config, Print* print, GCodePreviewData* gcode_preview_data)
    : m_canvas(nullptr)
    , m_double_slider_sizer(nullptr)
    , m_label_view_type(nullptr)
    , m_choice_view_type(nullptr)
    , m_label_show_features(nullptr)
    , m_combochecklist_features(nullptr)
    , m_checkbox_travel(nullptr)
    , m_checkbox_retractions(nullptr)
    , m_checkbox_unretractions(nullptr)
    , m_checkbox_shells(nullptr)
    , m_config(config)
    , m_print(print)
    , m_gcode_preview_data(gcode_preview_data)
    , m_number_extruders(1)
    , m_preferred_color_mode("feature")
    , m_loaded(false)
    , m_enabled(false)
    , m_force_sliders_full_range(false)
{
    if (init(notebook, config, print, gcode_preview_data))
    {
        notebook->AddPage(this, _(L("Preview")));
        show_hide_ui_elements("none");
        load_print();
    }
}

bool Preview::init(wxNotebook* notebook, DynamicPrintConfig* config, Print* print, GCodePreviewData* gcode_preview_data)
{
    if ((notebook == nullptr) || (config == nullptr) || (print == nullptr) || (gcode_preview_data == nullptr))
        return false;

    // creates this panel add append it to the given notebook as a new page
    if (!Create(notebook, wxID_ANY, wxDefaultPosition, wxDefaultSize))
        return false;

    int attribList[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24, WX_GL_SAMPLE_BUFFERS, GL_TRUE, WX_GL_SAMPLES, 4, 0 };

    int wxVersion = wxMAJOR_VERSION * 10000 + wxMINOR_VERSION * 100 + wxRELEASE_NUMBER;
    const AppConfig* app_config = GUI::get_app_config();
    bool enable_multisample = (app_config != nullptr) && (app_config->get("use_legacy_opengl") != "1") && (wxVersion >= 30003);

    // if multisample is not enabled or supported by the graphic card, remove it from the attributes list
    bool can_multisample = enable_multisample && wxGLCanvas::IsExtensionSupported("WGL_ARB_multisample");
//    bool can_multisample = enable_multisample && wxGLCanvas::IsDisplaySupported(attribList); // <<< Alternative method: but IsDisplaySupported() seems not to work
    if (!can_multisample)
        attribList[4] = 0;

    m_canvas = new wxGLCanvas(this, wxID_ANY, attribList);
    if (m_canvas == nullptr)
        return false;

    _3DScene::add_canvas(m_canvas);
    _3DScene::allow_multisample(m_canvas, can_multisample);
    _3DScene::enable_shader(m_canvas, true);
    _3DScene::set_config(m_canvas, m_config);
    _3DScene::set_print(m_canvas, m_print);
    _3DScene::enable_legend_texture(m_canvas, true);
    _3DScene::enable_dynamic_background(m_canvas, true);

    m_double_slider_sizer = new wxBoxSizer(wxHORIZONTAL);
    create_double_slider(this, m_double_slider_sizer, m_canvas);

    m_label_view_type = new wxStaticText(this, wxID_ANY, _(L("View")));

    m_choice_view_type = new wxChoice(this, wxID_ANY);
    m_choice_view_type->Append(_(L("Feature type")));
    m_choice_view_type->Append(_(L("Height")));
    m_choice_view_type->Append(_(L("Width")));
    m_choice_view_type->Append(_(L("Speed")));
    m_choice_view_type->Append(_(L("Volumetric flow rate")));
    m_choice_view_type->Append(_(L("Tool")));
    m_choice_view_type->SetSelection(0);

    m_label_show_features = new wxStaticText(this, wxID_ANY, _(L("Show")));

    m_combochecklist_features = new wxComboCtrl();
    m_combochecklist_features->Create(this, wxID_ANY, _(L("Feature types")), wxDefaultPosition, wxSize(200, -1), wxCB_READONLY);
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

    wxBoxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(m_canvas, 1, wxALL | wxEXPAND, 0);
    top_sizer->Add(m_double_slider_sizer, 0, wxEXPAND, 0);

    wxBoxSizer* bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
    bottom_sizer->Add(m_label_view_type, 0, wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->Add(m_choice_view_type, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_label_show_features, 0, wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->Add(m_combochecklist_features, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->AddSpacer(20);
    bottom_sizer->Add(m_checkbox_travel, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_retractions, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_unretractions, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
    bottom_sizer->AddSpacer(10);
    bottom_sizer->Add(m_checkbox_shells, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);

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

    if (m_canvas != nullptr)
    {
        _3DScene::remove_canvas(m_canvas);
        delete m_canvas;
    }
}

void Preview::register_on_viewport_changed_callback(void* callback)
{
    if ((m_canvas != nullptr) && (callback != nullptr))
        _3DScene::register_on_viewport_changed_callback(m_canvas, callback);
}

void Preview::set_number_extruders(unsigned int number_extruders)
{
    if (m_number_extruders != number_extruders)
    {
        m_number_extruders = number_extruders;
        int type = 0; // color by a feature type
        if (number_extruders > 1)
        {
            int tool_idx = m_choice_view_type->FindString(_(L("Tool")));
            int type = (number_extruders > 1) ? tool_idx /* color by a tool number */  : 0; // color by a feature type
            m_choice_view_type->SetSelection(type);
            if ((0 <= type) && (type < (int)GCodePreviewData::Extrusion::Num_View_Types))
                m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;

            m_preferred_color_mode = (type == tool_idx) ? "tool_or_feature" : "feature";
        }
    }
}

void Preview::reset_gcode_preview_data()
{
    m_gcode_preview_data->reset();
    _3DScene::reset_legend_texture();
}

void Preview::set_canvas_as_dirty()
{
    if (m_canvas != nullptr)
        _3DScene::set_as_dirty(m_canvas);
}

void Preview::set_enabled(bool enabled)
{
    m_enabled = enabled;
}

void Preview::set_bed_shape(const Pointfs& shape)
{
    if (m_canvas != nullptr)
        _3DScene::set_bed_shape(m_canvas, shape);
}

void Preview::select_view(const std::string& direction)
{
    if (m_canvas != nullptr)
        _3DScene::select_view(m_canvas, direction);
}

void Preview::set_viewport_from_scene(wxGLCanvas* canvas)
{
    if ((m_canvas != nullptr) && (canvas != nullptr))
        _3DScene::set_viewport_from_scene(m_canvas, canvas);
}

void Preview::set_viewport_into_scene(wxGLCanvas* canvas)
{
    if ((m_canvas != nullptr) && (canvas != nullptr))
        _3DScene::set_viewport_from_scene(canvas, m_canvas);
}

void Preview::set_drop_target(wxDropTarget* target)
{
    if (target != nullptr)
        SetDropTarget(target);
}

void Preview::load_print()
{
    if (m_loaded)
        return;

    // we require that there's at least one object and the posSlice step
    // is performed on all of them(this ensures that _shifted_copies was
    // populated and we know the number of layers)
    unsigned int n_layers = 0;
    if (m_print->is_step_done(posSlice))
    {
        std::set<float> zs;
        for (const PrintObject* print_object : m_print->objects())
        {
            const LayerPtrs& layers = print_object->layers();
            const SupportLayerPtrs& support_layers = print_object->support_layers();
            for (const Layer* layer : layers)
            {
                zs.insert(layer->print_z);
            }
            for (const SupportLayer* layer : support_layers)
            {
                zs.insert(layer->print_z);
            }
        }

        n_layers = (unsigned int)zs.size();
    }

    if (n_layers == 0)
    {
        reset_sliders();
        _3DScene::reset_legend_texture();
        if (m_canvas)
            m_canvas->Refresh();

        return;
    }

    if (m_preferred_color_mode == "tool_or_feature")
    {
        // It is left to Slic3r to decide whether the print shall be colored by the tool or by the feature.
        // Color by feature if it is a single extruder print.
        unsigned int number_extruders = (unsigned int)m_print->extruders().size();
        int tool_idx = m_choice_view_type->FindString(_(L("Tool")));
        int type = (number_extruders > 1) ? tool_idx /* color by a tool number */ : 0; // color by a feature type
        m_choice_view_type->SetSelection(type);
        if ((0 <= type) && (type < (int)GCodePreviewData::Extrusion::Num_View_Types))
            m_gcode_preview_data->extrusion.view_type = (GCodePreviewData::Extrusion::EViewType)type;
        // If the->SetSelection changed the following line, revert it to "decide yourself".
        m_preferred_color_mode = "tool_or_feature";
    }

    // Collect colors per extruder.
    std::vector<std::string> colors;
    if (!m_gcode_preview_data->empty() || (m_gcode_preview_data->extrusion.view_type == GCodePreviewData::Extrusion::Tool))
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

            colors.push_back(color);
        }
    }

    if (IsShown() && (m_canvas != nullptr))
    {
        // used to set the sliders to the extremes of the current zs range
        m_force_sliders_full_range = false;

        if (m_gcode_preview_data->empty())
        {
            // load skirt and brim
            _3DScene::load_preview(m_canvas, colors);
            show_hide_ui_elements("simple");
        }
        else
        {
            m_force_sliders_full_range = (_3DScene::get_volumes_count(m_canvas) == 0);
            _3DScene::load_gcode_preview(m_canvas, m_gcode_preview_data, colors);
            show_hide_ui_elements("full");

            // recalculates zs and update sliders accordingly
            n_layers = (unsigned int)_3DScene::get_current_print_zs(m_canvas, true).size();
            if (n_layers == 0)
            {
                // all layers filtered out
                reset_sliders();
                m_canvas->Refresh();
            }
        }

        if (n_layers > 0)
            update_sliders();

        m_loaded = true;
    }
}

void Preview::reload_print(bool force)
{
    _3DScene::reset_volumes(m_canvas);
    m_loaded = false;

    if (!IsShown() && !force)
        return;

    load_print();
}

void Preview::refresh_print()
{
    m_loaded = false;

    if (!IsShown())
        return;

    load_print();
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

    enable = (what != "none");
    m_label_view_type->Enable(enable);
    m_choice_view_type->Enable(enable);
}

void Preview::reset_sliders()
{
    m_enabled = false;
    reset_double_slider();
    m_double_slider_sizer->Hide((size_t)0);
}

void Preview::update_sliders()
{
    m_enabled = true;
    update_double_slider(m_force_sliders_full_range);
    m_double_slider_sizer->Show((size_t)0);
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

} // namespace GUI
} // namespace Slic3r
