#include "EditGCodeDialog.hpp"

#include <vector>
#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/wupdlock.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "BitmapCache.hpp"
#include "ExtraRenderers.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"

#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Print.hpp"

#define BTN_GAP  FromDIP(20)
#define BTN_SIZE wxSize(FromDIP(58), FromDIP(24))

namespace Slic3r {
namespace GUI {

ConfigOption* get_new_option(const ConfigOptionType type)
{
    switch (type) {
    case coFloat:
        return new ConfigOptionFloat(0.);
    case coFloats:
        return new ConfigOptionFloats({ 0. });
    case coInt:
        return new ConfigOptionInt(0);
    case coInts:
        return new ConfigOptionInts({ 0 });
    case coString:
        return new ConfigOptionString("");
    case coStrings:
        return new ConfigOptionStrings({ ""});
    case coPercent:
        return new ConfigOptionPercent(0);
    case coPercents:
        return new ConfigOptionPercents({ 0});
    case coFloatOrPercent:
        return new ConfigOptionFloatOrPercent();
    case coFloatsOrPercents:
        return new ConfigOptionFloatsOrPercents();
    case coPoint:
        return new ConfigOptionPoint(Vec2d(100, 100));
    case coPoints:
        return new ConfigOptionPoints({ Vec2d(100,100) });
    case coPoint3:
        return new ConfigOptionPoint3();
    case coBool:
        return new ConfigOptionBool(true);
    case coBools:
        return new ConfigOptionBools({ true });
    case coEnum:
        return new ConfigOptionEnum<InfillPattern>();
    default:
        return nullptr;
    }
}

namespace fs = boost::filesystem;
namespace pt = boost::property_tree;
static std::vector<std::string> get_params_from_file(const std::string& file_name, DynamicConfig& out_config)
{
    const fs::path file_path = fs::path(custom_gcodes_dir() +
#ifdef _WIN32
                                        "\\"
#else
                                        "/"
#endif
                                        + file_name);

    if (!fs::exists(file_path))
        return {};

    const std::string file = file_path.string();

    // Load the preset file, apply preset values on top of defaults.
    try {
        DynamicConfig config;

        try {
            pt::ptree tree;
            boost::nowide::ifstream ifs(file);
            pt::read_ini(ifs, tree);
            for (const pt::ptree::value_type& v : tree) {
                try {
                    t_config_option_key opt_key = v.first;
                    const std::string type_str = v.second.get_value<std::string>();
                    const ConfigOptionType type = ConfigOptionType(std::atoi(type_str.c_str()));
                    if (ConfigOption* opt = get_new_option(type))
                        config.set_key_value(opt_key, std::move(opt));
                }
                catch (UnknownOptionException& err) {
                    throw RuntimeError(format("Some option from %1% cannot be loaded:\n\tReason: %2%", file, err.what()));
                }
            }
        }
        catch (const ConfigurationError& e) {
            throw ConfigurationError(format("Failed loading configuration file \"%1%\": \n\t%2%", file, e.what()));
        }

        out_config += config;
        return config.keys();
    }
    catch (const std::ifstream::failure& err) {
        throw RuntimeError(format("The %1% cannot be loaded:\n\tReason: %2%", file, err.what()));
    }
    catch (const std::runtime_error& err) {
        throw RuntimeError(format("Failed loading the custom_gcode_placeholders file: \"%1%\"\n\tReason: %2%", file , err.what()));
    }
}

//------------------------------------------
//          EditGCodeDialog
//------------------------------------------

EditGCodeDialog::EditGCodeDialog(wxWindow* parent, const std::string& key, const std::string& value) :
    DPIDialog(parent, wxID_ANY, format_wxstr(_L("Edit Custom G-code (%1%)"), key), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDlgDarkUI(this);

    int border = 10;
    int em = em_unit();

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Built-in placeholders (Double click item to add to G-code)") + ":");

    auto* grid_sizer = new wxFlexGridSizer(1, 3, 5, 15);
    grid_sizer->SetFlexibleDirection(wxBOTH);

    m_params_list = new ParamsViewCtrl(this, wxSize(em * 30, em * 70));
    m_params_list->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_params_list);

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_copies");
    m_add_btn->SetToolTip(_L("Add selected placeholder to G-code"));

    m_gcode_editor = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxSize(em * 75, em * 70), wxTE_MULTILINE
#ifdef _WIN32
    | wxBORDER_SIMPLE
#endif
    );
    m_gcode_editor->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_gcode_editor);

    grid_sizer->Add(m_params_list,  1, wxEXPAND);
    grid_sizer->Add(m_add_btn,      0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(m_gcode_editor, 2, wxEXPAND);

    grid_sizer->AddGrowableRow(0, 1);
    grid_sizer->AddGrowableCol(0, 1);
    grid_sizer->AddGrowableCol(2, 1);

    m_param_label = new wxStaticText(this, wxID_ANY, _L("Select placeholder"));
    m_param_label->SetFont(wxGetApp().bold_font());

    m_param_description = new wxStaticText(this, wxID_ANY, wxEmptyString);

    //Orca: use custom buttons
    auto btn_sizer = create_btn_sizer(wxOK | wxCANCEL);
    for(auto btn : m_button_list)
        wxGetApp().UpdateDarkUI(btn.second);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top           , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(grid_sizer          , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_param_label       , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_param_description , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(btn_sizer                , 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->Fit();
    this->Layout();

    this->CenterOnScreen();

    init_params_list(key);
    bind_list_and_button();
}

std::string EditGCodeDialog::get_edited_gcode() const
{
    return into_u8(m_gcode_editor->GetValue());
}

void EditGCodeDialog::init_params_list(const std::string& custom_gcode_name)
{
    const std::vector<std::string> read_write_params = get_params_from_file("rw_slicing_state", m_read_write_config);
    const std::vector<std::string> universal_params  = get_params_from_file("universal", m_universal_config);
    const std::vector<std::string> specific_params   = get_params_from_file(custom_gcode_name, m_specific_config);

    m_print_statistics_config = PrintStatistics::placeholders();

    auto get_type = [](const std::string& opt_key, const DynamicConfig& config) {
        return config.optptr(opt_key)->is_scalar() ? ParamType::Scalar : ParamType::Vector;
    };

    // Add slicing states placeholders

    std::set<std::string> read_only_slicing_state_opts = { "zhop" };

    wxDataViewItem slicing_state = m_params_list->AppendGroup(_L("[Global] Slicing State"), "re_slice");
    if (!universal_params.empty()) {
        wxDataViewItem read_only = m_params_list->AppendSubGroup(slicing_state, _L("Read Only"),  "lock_closed");
        for (const auto& opt_key : read_only_slicing_state_opts)
            m_params_list->AppendParam(read_only, get_type(opt_key, m_universal_config), opt_key);
    }

    if (!read_write_params.empty()) {
        wxDataViewItem read_write = m_params_list->AppendSubGroup(slicing_state, _L("Read Write"), "lock_open");
        for (const auto& opt_key : read_write_params)
            m_params_list->AppendParam(read_write, get_type(opt_key, m_read_write_config), opt_key);
    }

    //TODO: Orca: add other params which are related to slicing state that are specific to Orca

    // add other universal params, which are related to slicing state
    const std::set<std::string> other_slicing_state_opts    = { "initial_extruder"
//                                                              , "initial_filament_type"
                                                              , "initial_tool"
                                                              , "current_extruder"
                                                              , "is_extruder_used"
                                                              , "current_object_idx"
//                                                              , "has_single_extruder_multi_material_priming"
                                                              , "has_wipe_tower" };

    slicing_state = m_params_list->AppendGroup(_L("Slicing State"), "re_slice");
    for (const auto& opt_key : other_slicing_state_opts) {
        if (m_print_statistics_config.has(opt_key))
            m_params_list->AppendParam(slicing_state, get_type(opt_key, m_print_statistics_config), opt_key);
        else if(!universal_params.empty())
            m_params_list->AppendParam(slicing_state, get_type(opt_key, m_universal_config), opt_key);
    }

    const std::set<std::string> other_print_statistics_opts = { "extruded_volume_total"
                                                              , "extruded_weight"
                                                              , "extruded_weight_total"
                                                              , "total_layer_count"     };

    const std::set<std::string> other_presets_opts          = { "filament_preset"
//                                                              , "physical_printer_preset"
                                                              , "printer_preset"
                                                              , "print_preset"
                                                              /*, "num_extruders"*/     };

    const std::set<std::string> objects_info_opts           = { "num_instances"
                                                              , "num_objects"
                                                              , "scale"
                                                              , "input_filename"
                                                              , "input_filename_base"     };
                                                       
    const std::set<std::string> dimensions_opts             = { "first_layer_print_convex_hull"
                                                              , "first_layer_print_max"
                                                              , "first_layer_print_min"
                                                              , "first_layer_print_size"
                                                              , "print_bed_max"
                                                              , "print_bed_min"
                                                              , "print_bed_size"     };

    // Add universal placeholders

    if (!universal_params.empty()) {
//        wxDataViewItem group = m_params_list->AppendGroup(_L("Universal"), "equal");

        // Add print statistics subgroup

        if (!m_print_statistics_config.empty()) {
//            wxDataViewItem statistics = m_params_list->AppendSubGroup(group, _L("Print Statistics"), "info");
            wxDataViewItem statistics = m_params_list->AppendGroup(_L("Print Statistics"), "info");
            const std::vector<std::string> statistics_params = m_print_statistics_config.keys();
            for (const auto& opt_key : statistics_params)
                if (std::find(other_slicing_state_opts.begin(), other_slicing_state_opts.end(), opt_key) == other_slicing_state_opts.end())
                    m_params_list->AppendParam(statistics, get_type(opt_key, m_print_statistics_config), opt_key);
                // add other universal params, which are related to print statistics
            if (!universal_params.empty())
                for (const auto& opt_key : other_print_statistics_opts)
                    m_params_list->AppendParam(statistics, get_type(opt_key, m_universal_config), opt_key);
        }

        // Add objects info subgroup

        if (!universal_params.empty()) {
//            wxDataViewItem objects_info = m_params_list->AppendSubGroup(group, _L("Objects Info"), "advanced_plus");
            wxDataViewItem objects_info = m_params_list->AppendGroup(_L("Objects Info"), "advanced_plus");
            for (const auto& opt_key : objects_info_opts)
                m_params_list->AppendParam(objects_info, get_type(opt_key, m_universal_config), opt_key);
        }

        // Add objects info subgroup

        if (!universal_params.empty()) {
//            wxDataViewItem dimensions = m_params_list->AppendSubGroup(group, _L("Dimensions"), "measure");
            wxDataViewItem dimensions = m_params_list->AppendGroup(_L("Dimensions"), "measure");
            for (const auto& opt_key : dimensions_opts)
                m_params_list->AppendParam(dimensions, get_type(opt_key, m_universal_config), opt_key);
        }

        // Add timestamp subgroup

        PlaceholderParser parser;
        parser.update_timestamp();
        const DynamicConfig& ts_config = parser.config();
//        wxDataViewItem time_stamp = ts_config.empty() ? group : m_params_list->AppendSubGroup(group, _L("Timestamps"), "time");
        wxDataViewItem time_stamp = m_params_list->AppendGroup(_L("Timestamps"), "time");

        // Add un-grouped params

//        wxDataViewItem other = m_params_list->AppendSubGroup(group, _L("Other"), "add_gcode");
        wxDataViewItem other = m_params_list->AppendGroup(_L("Other"), "add_gcode");
        for (const auto& opt_key : universal_params)
            if (std::find(read_only_slicing_state_opts.begin(), read_only_slicing_state_opts.end(), opt_key)== read_only_slicing_state_opts.end() &&
                std::find(other_slicing_state_opts.begin(),     other_slicing_state_opts.end(), opt_key)    == other_slicing_state_opts.end() &&
                std::find(other_print_statistics_opts.begin(),  other_print_statistics_opts.end(), opt_key) == other_print_statistics_opts.end() &&
                std::find(other_presets_opts.begin(),           other_presets_opts.end(), opt_key)          == other_presets_opts.end() &&
                std::find(objects_info_opts.begin(),            objects_info_opts.end(), opt_key)           == objects_info_opts.end() &&
                std::find(dimensions_opts.begin(),              dimensions_opts.end(), opt_key)             == dimensions_opts.end() &&
                !m_print_statistics_config.has(opt_key)) {
                m_params_list->AppendParam(ts_config.has(opt_key) ? time_stamp : other, get_type(opt_key, m_universal_config), opt_key);
            }
        m_params_list->CheckAndDeleteIfEmpty(other);
    }

    // Add specific placeholders

    if (!specific_params.empty()) {
        wxDataViewItem group = m_params_list->AppendGroup(format_wxstr(_L("Specific for %1%"), custom_gcode_name), /*"not_equal"*/"add_gcode");
        for (const auto& opt_key : specific_params)
            m_params_list->AppendParam(group, get_type(opt_key, m_specific_config), opt_key);

        m_params_list->Expand(group);
    }

    // Add placeholders from presets

    wxDataViewItem presets = add_presets_placeholders();
    // add other params which are related to presets
    if (!universal_params.empty())
        for (const auto& opt_key : other_presets_opts)
            m_params_list->AppendParam(presets, get_type(opt_key, m_universal_config), opt_key);
}

wxDataViewItem EditGCodeDialog::add_presets_placeholders()
{
    auto get_set_from_vec = [](const std::vector<std::string>&vec) {
        return std::set<std::string>(vec.begin(), vec.end());
    };

    const bool                  is_fff           = wxGetApp().plater()->printer_technology() == ptFFF;
    const std::set<std::string> print_options    = get_set_from_vec(is_fff ? Preset::print_options()    : Preset::sla_print_options());
    const std::set<std::string> material_options = get_set_from_vec(is_fff ? Preset::filament_options() : Preset::sla_material_options());
    const std::set<std::string> printer_options  = get_set_from_vec(is_fff ? Preset::printer_options()  : Preset::sla_printer_options());

    const auto&full_config = wxGetApp().preset_bundle->full_config();

    wxDataViewItem group = m_params_list->AppendGroup(_L("Presets"), "cog");

    wxDataViewItem print = m_params_list->AppendSubGroup(group, _L("Print settings"), "cog");
    for (const auto&opt : print_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(print, optptr->is_scalar() ? ParamType::Scalar : ParamType::Vector, opt);

    wxDataViewItem material = m_params_list->AppendSubGroup(group, _(is_fff ? L("Filament settings") : L("SLA Materials settings")), is_fff ? "spool" : "resin");
    for (const auto&opt : material_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(material, optptr->is_scalar() ? ParamType::Scalar : ParamType::FilamentVector, opt);

    wxDataViewItem printer = m_params_list->AppendSubGroup(group, _L("Printer settings"), is_fff ? "printer" : "sla_printer");
    for (const auto&opt : printer_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(printer, optptr->is_scalar() ? ParamType::Scalar : ParamType::Vector, opt);

    return group;
}

void EditGCodeDialog::add_selected_value_to_gcode()
{
    const wxString val = m_params_list->GetSelectedValue();
    if (!val.IsEmpty())
        m_gcode_editor->WriteText(val + "\n");
}

void EditGCodeDialog::bind_list_and_button()
{
    m_params_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& evt) {
        wxString label;
        wxString description;

        const std::string opt_key = m_params_list->GetSelectedParamKey();
        if (!opt_key.empty()) {
            const ConfigOptionDef*    cod     { nullptr };
            const ConfigOption*       optptr  { nullptr };

            const auto& full_config = wxGetApp().preset_bundle->full_config();
            if (const ConfigDef* def = full_config.def(); def && def->has(opt_key)) {
                cod = def->get(opt_key);
                optptr = full_config.optptr(opt_key);
            }
            else {
                for (const DynamicConfig* config: { &m_read_write_config, &m_universal_config, &m_specific_config, &m_print_statistics_config }) {
                    optptr = config->optptr(opt_key);
                    if (optptr)
                        break;
                }
            }

            if (optptr) {
                const ConfigOptionType scalar_type = optptr->is_scalar() ? optptr->type() : static_cast<ConfigOptionType>(optptr->type() - coVectorType);
                wxString type_str = scalar_type == coNone           ? "none" :
                                                     scalar_type == coFloat          ? "float" :
                                                     scalar_type == coInt            ? "integer" :
                                                     scalar_type == coString         ? "string" :
                                                     scalar_type == coPercent        ? "percent" :
                                                     scalar_type == coFloatOrPercent ? "float or percent" :
                                                     scalar_type == coPoint          ? "point" :
                                                     scalar_type == coBool           ? "bool" :
                                                     scalar_type == coEnum           ? "enum" : "undef";
                if (!optptr->is_scalar())
                    type_str += "[]";

                label = (!cod || (cod->full_label.empty() && cod->label.empty()) ) ? format_wxstr("%1%\n(%2%)", opt_key, type_str) :
                        (!cod->full_label.empty() && !cod->label.empty() ) ?
                        format_wxstr("%1% > %2%\n(%3%)", _(cod->full_label), _(cod->label), type_str) :
                        format_wxstr("%1%\n(%2%)", cod->label.empty() ? _(cod->full_label) : _(cod->label), type_str);

                if (cod)
                    description = _(cod->tooltip);
            }
            else
                label = "Undef optptr";
        }

        m_param_label->SetLabel(label);
        m_param_description->SetLabel(description);

        Layout();
    });

    m_params_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& ) {
        add_selected_value_to_gcode();
    });

    m_add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        add_selected_value_to_gcode();
    });
}

void EditGCodeDialog::on_dpi_changed(const wxRect&suggested_rect)
{
    const int& em = em_unit();

    //Orca: use custom buttons
    for (auto button_item : m_button_list)
    {
        if (button_item.first == wxOK) {
            button_item.second->SetMinSize(BTN_SIZE);
            button_item.second->SetCornerRadius(FromDIP(12));
        }
        if (button_item.first == wxCANCEL) {
            button_item.second->SetMinSize(BTN_SIZE);
            button_item.second->SetCornerRadius(FromDIP(12));
        }
    }

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void EditGCodeDialog::on_sys_color_changed()
{
    m_add_btn->sys_color_changed();
}

//Orca
wxBoxSizer* EditGCodeDialog::create_btn_sizer(long flags)
{
    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    StateColor ok_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)
    );

    StateColor cancel_btn_bg(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    );

    StateColor cancel_btn_bd_(
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)
    );

    StateColor cancel_btn_text(
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)
    );


    StateColor calc_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)
    );

    if (flags & wxOK) {
        Button* ok_btn = new Button(this, _L("OK"));
        ok_btn->SetMinSize(BTN_SIZE);
        ok_btn->SetCornerRadius(FromDIP(12));
        ok_btn->SetBackgroundColor(ok_btn_bg);
        ok_btn->SetBorderColor(ok_btn_bd);
        ok_btn->SetTextColor(ok_btn_text);
        ok_btn->SetFocus();
        ok_btn->SetId(wxID_OK);
        btn_sizer->Add(ok_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
        m_button_list[wxOK] = ok_btn;
    }
    if (flags & wxCANCEL) {
        Button* cancel_btn = new Button(this, _L("Cancel"));
        cancel_btn->SetMinSize(BTN_SIZE);
        cancel_btn->SetCornerRadius(FromDIP(12));
        cancel_btn->SetBackgroundColor(cancel_btn_bg);
        cancel_btn->SetBorderColor(cancel_btn_bd_);
        cancel_btn->SetTextColor(cancel_btn_text);
        cancel_btn->SetId(wxID_CANCEL);
        btn_sizer->Add(cancel_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP / 2);
        m_button_list[wxCANCEL] = cancel_btn;
    }

    return btn_sizer;
}


const std::map<ParamType, std::string> ParamsInfo {
//    Type                      BitmapName
    { ParamType::Scalar,        "scalar_param"          },
    { ParamType::Vector,        "vector_param"          },
    { ParamType::FilamentVector,"vector_filament_param" },
};

static void make_bold(wxString& str)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = format_wxstr("<b>%1%</b>", str);
#endif
}

// ----------------------------------------------------------------------------
//                  ParamsModelNode: a node inside ParamsModel
// ----------------------------------------------------------------------------

ParamsNode::ParamsNode(const wxString& group_name, const std::string& icon_name)
: icon_name(icon_name)
, text(group_name)
{
    make_bold(text);
}

ParamsNode::ParamsNode( ParamsNode *        parent,
                        const wxString&     sub_group_name,
                        const std::string&  icon_name)
    : m_parent(parent)
    , icon_name(icon_name)
    , text(sub_group_name)
{
    make_bold(text);
}

ParamsNode::ParamsNode( ParamsNode*         parent,
                        ParamType           param_type,
                        const std::string&  param_key)
    : m_parent(parent)
    , m_param_type(param_type)
    , m_container(false)
    , param_key(param_key)
{
    text = from_u8(param_key);
    if (param_type == ParamType::Vector)
        text += "[]";
    else if (param_type == ParamType::FilamentVector)
        text += "[current_extruder]";

    icon_name = ParamsInfo.at(param_type);
}


// ----------------------------------------------------------------------------
//                  ParamsModel
// ----------------------------------------------------------------------------

ParamsModel::ParamsModel()
{
}

wxDataViewItem ParamsModel::AppendGroup(const wxString&    group_name,
                                        const std::string& icon_name)
{
    m_group_nodes.emplace_back(std::make_unique<ParamsNode>(group_name, icon_name));

    wxDataViewItem parent(nullptr);
    wxDataViewItem child((void*)m_group_nodes.back().get());

    ItemAdded(parent, child);
    m_ctrl->Expand(parent);
    return child;
}

wxDataViewItem ParamsModel::AppendSubGroup(wxDataViewItem       parent,
                                           const wxString&      sub_group_name,
                                           const std::string&   icon_name)
{
    ParamsNode* parent_node = static_cast<ParamsNode*>(parent.GetID());
    if (!parent_node)
        return wxDataViewItem(0);

    parent_node->Append(std::make_unique<ParamsNode>(parent_node, sub_group_name, icon_name));
    const wxDataViewItem  sub_group_item((void*)parent_node->GetChildren().back().get());

    ItemAdded(parent, sub_group_item);
    return sub_group_item;
}

wxDataViewItem ParamsModel::AppendParam(wxDataViewItem      parent,
                                        ParamType           param_type,
                                        const std::string&  param_key)
{
    ParamsNode* parent_node = static_cast<ParamsNode*>(parent.GetID());
    if (!parent_node)
        return wxDataViewItem(0);

    parent_node->Append(std::make_unique<ParamsNode>(parent_node, param_type, param_key));

    const wxDataViewItem  child_item((void*)parent_node->GetChildren().back().get());

    ItemAdded(parent, child_item);
    return child_item;
}

wxString ParamsModel::GetParamName(wxDataViewItem item)
{
    if (item.IsOk()) {
        ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
        if (node->IsParamNode())
            return node->text;
    }
    return wxEmptyString;
}

std::string ParamsModel::GetParamKey(wxDataViewItem item)
{
    if (item.IsOk()) {
        ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
        return node->param_key;
    }
    return std::string();
}

wxDataViewItem ParamsModel::Delete(const wxDataViewItem& item)
{
    auto ret_item = wxDataViewItem(nullptr);
    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (!node)      // happens if item.IsOk()==false
        return ret_item;

    // first remove the node from the parent's array of children;
    // NOTE: m_group_nodes is only a vector of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    ParamsNodePtrArray& children = node->GetChildren();
    // Delete all children
    while (!children.empty())
        Delete(wxDataViewItem(children.back().get()));

    auto node_parent = node->GetParent();

    ParamsNodePtrArray& parents_children = node_parent ? node_parent->GetChildren() : m_group_nodes;
    auto it = find_if(parents_children.begin(), parents_children.end(),
                                                   [node](std::unique_ptr<ParamsNode>& child) { return child.get() == node; });
    assert(it != parents_children.end());
    it = parents_children.erase(it);

    if (it != parents_children.end())
        ret_item = wxDataViewItem(it->get());

    wxDataViewItem parent(node_parent);
    // set m_container to FALSE if parent has no child
    if (node_parent) {
#ifndef __WXGTK__
        if (node_parent->GetChildren().empty())
            node_parent->SetContainer(false);
#endif //__WXGTK__
        ret_item = parent;
    }

    // notify control
    ItemDeleted(parent, item);
    return ret_item;
}

void ParamsModel::Clear()
{
    while (!m_group_nodes.empty())
        Delete(wxDataViewItem(m_group_nodes.back().get()));
}

void ParamsModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (col == (unsigned int)0)
#ifdef __linux__
        variant << wxDataViewIconText(node->text, get_bmp_bundle(node->icon_name)->GetIconFor(m_ctrl->GetParent()));
#else
        variant << DataViewBitmapText(node->text, get_bmp_bundle(node->icon_name)->GetBitmapFor(m_ctrl->GetParent()));
#endif //__linux__
    else
        wxLogError("DiffModel::GetValue: wrong column %d", col);
}

bool ParamsModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (col == (unsigned int)0) {
#ifdef __linux__
        wxDataViewIconText data;
        data << variant;
        node->icon = data.GetIcon();
#else
        DataViewBitmapText data;
        data << variant;
        node->icon = data.GetBitmap();
#endif
        node->text = data.GetText();
        return true;
    }

    wxLogError("DiffModel::SetValue: wrong column");
    return false;
}

wxDataViewItem ParamsModel::GetParent(const wxDataViewItem&item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());

    if (node->IsGroupNode())
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool ParamsModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    return node->IsContainer();
}
unsigned int ParamsModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ParamsNode* parent_node = (ParamsNode*)parent.GetID();

    if (parent_node == nullptr) {
        for (const auto& group : m_group_nodes)
            array.Add(wxDataViewItem((void*)group.get()));
    }
    else  {
        const ParamsNodePtrArray& children = parent_node->GetChildren();
        for (const std::unique_ptr<ParamsNode>& child : children)
            array.Add(wxDataViewItem((void*)child.get()));
    }

    return array.Count();
}


// ----------------------------------------------------------------------------
//                  ParamsViewCtrl
// ----------------------------------------------------------------------------

ParamsViewCtrl::ParamsViewCtrl(wxWindow *parent, wxSize size)
    : wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, size, wxDV_SINGLE | wxDV_NO_HEADER// | wxDV_ROW_LINES
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif
    ),
    m_em_unit(em_unit(parent))
{
    wxGetApp().UpdateDVCDarkUI(this);

    model = new ParamsModel();
    this->AssociateModel(model);
    model->SetAssociatedControl(this);

#ifdef __linux__
    wxDataViewIconTextRenderer* rd = new wxDataViewIconTextRenderer();
#ifdef SUPPORTS_MARKUP
    rd->EnableMarkup(true);
#endif
    wxDataViewColumn* column = new wxDataViewColumn("", rd, 0, 20 * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_CELL_INERT);
#else
    wxDataViewColumn* column = new wxDataViewColumn("", new BitmapTextRenderer(true, wxDATAVIEW_CELL_INERT), 0, 20 * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE);
#endif //__linux__
    this->AppendColumn(column);
    this->SetExpanderColumn(column);
}

wxDataViewItem ParamsViewCtrl::AppendGroup(const wxString& group_name, const std::string& icon_name)
{
    return model->AppendGroup(group_name, icon_name);
}

wxDataViewItem ParamsViewCtrl::AppendSubGroup(  wxDataViewItem      parent,
                                                const wxString&     sub_group_name,
                                                const std::string&  icon_name)
{
    return model->AppendSubGroup(parent, sub_group_name, icon_name);
}

wxDataViewItem ParamsViewCtrl::AppendParam( wxDataViewItem      parent,
                                            ParamType           param_type,
                                            const std::string&  param_key)
{
    return model->AppendParam(parent, param_type, param_key);
}

wxString ParamsViewCtrl::GetValue(wxDataViewItem item)
{
    return model->GetParamName(item);
}

wxString ParamsViewCtrl::GetSelectedValue()
{
    return model->GetParamName(this->GetSelection());
}

std::string ParamsViewCtrl::GetSelectedParamKey()
{
    return model->GetParamKey(this->GetSelection());
}

void ParamsViewCtrl::CheckAndDeleteIfEmpty(wxDataViewItem item)
{
    wxDataViewItemArray children;
    model->GetChildren(item, children);
    if (children.IsEmpty())
        model->Delete(item);
}

void ParamsViewCtrl::Clear()
{
    model->Clear();
}

void ParamsViewCtrl::Rescale(int em/* = 0*/)
{
//    model->Rescale();
    Refresh();
}
}}    // namespace Slic3r::GUI
