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

#include "libslic3r/Preset.hpp"

#define BTN_GAP  FromDIP(20)
#define BTN_SIZE wxSize(FromDIP(58), FromDIP(24))

namespace Slic3r {
namespace GUI {

static wxArrayString get_patterns_list()
{
    wxArrayString patterns;
    for (const wxString& item : {
          "printer_model"
        , "nozzle_diameter"
        , "first_layer_temperature"
        , "first_layer_bed_temperature"
        , "first_layer_bed_temperature"
        , "first_layer_temperature"
        , "initial_tool"
    })
        patterns.Add(item);
    return patterns;
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

    m_params_list = new ParamsViewCtrl(this, wxSize(em * 20, em * 30));
    m_params_list->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_params_list);

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_copies");
    m_add_btn->SetToolTip(_L("Add selected placeholder to G-code"));

    m_gcode_editor = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxSize(em * 45, em * 30), wxTE_MULTILINE
#ifdef _WIN32
    | wxBORDER_SIMPLE
#endif
    );
    m_gcode_editor->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_gcode_editor);

    grid_sizer->Add(m_params_list,    1, wxEXPAND);
    grid_sizer->Add(m_add_btn,          0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(m_gcode_editor,     2, wxEXPAND);

    grid_sizer->AddGrowableRow(0, 1);
    grid_sizer->AddGrowableCol(0, 1);
    grid_sizer->AddGrowableCol(2, 1);

    m_param_label = new wxStaticText(this, wxID_ANY, _L("Select placeholder"));
    m_param_label->SetFont(wxGetApp().bold_font());

    //Orca: use custom buttons
    auto btn_sizer = create_btn_sizer(wxOK | wxCANCEL);
    for(auto btn : m_button_list)
        wxGetApp().UpdateDarkUI(btn.second);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top           , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(grid_sizer          , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_param_label       , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(btn_sizer                , 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->Fit();
    this->Layout();

    this->CenterOnScreen();

    init_params_list();
    bind_list_and_button();
}

std::string EditGCodeDialog::get_edited_gcode() const
{
    return into_u8(m_gcode_editor->GetValue());
}

void EditGCodeDialog::init_params_list()
{
    auto list = get_patterns_list();

    m_params_list->AppendGroup(GroupParamsType::SlicingState);
    for (const auto& sub_gr : { SubSlicingState::ReadOnly, SubSlicingState::ReadWrite }) {
        int i = 0;
        for (const wxString& name : list) {
            const auto param_type = static_cast<ParamType>(1 + std::modulus<int>()(i, 3));
            m_params_list->AppendParam(GroupParamsType::SlicingState, param_type, into_u8(name), sub_gr);
            ++i;
        }
    }

    auto get_set_from_vec = [](const std::vector<std::string>& vec) {
        return std::set<std::string>(vec.begin(), vec.end());
    };

    const bool is_fff = wxGetApp().plater()->printer_technology() == ptFFF;
    const std::set<std::string> print_options    = get_set_from_vec(is_fff ? Preset::print_options()    : Preset::sla_print_options());
    const std::set<std::string> material_options = get_set_from_vec(is_fff ? Preset::filament_options() : Preset::sla_material_options());
    const std::set<std::string> printer_options  = get_set_from_vec(is_fff ? Preset::printer_options()  : Preset::sla_printer_options());

    const auto& full_config = wxGetApp().preset_bundle->full_config();

    const auto& def = full_config.def()->get("")->label;

    m_params_list->AppendGroup(GroupParamsType::PrintSettings);
    for (const auto& opt : print_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(GroupParamsType::PrintSettings,    optptr->is_scalar() ? ParamType::Scalar : ParamType::Vector, opt);

    m_params_list->AppendGroup(GroupParamsType::MaterialSettings);
    for (const auto& opt : material_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(GroupParamsType::MaterialSettings, optptr->is_scalar() ? ParamType::Scalar : ParamType::FilamentVector, opt);

    m_params_list->AppendGroup(GroupParamsType::PrinterSettings);
    for (const auto& opt : printer_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(GroupParamsType::PrinterSettings,  optptr->is_scalar() ? ParamType::Scalar : ParamType::Vector, opt);
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

        const std::string opt_key = m_params_list->GetSelectedParamKey();
        if (!opt_key.empty()) {
            const auto& full_config = wxGetApp().preset_bundle->full_config();
            if (const ConfigDef* def = full_config.def();
                def && def->has(opt_key)) {
                const ConfigOptionDef* cod    = def->get(opt_key);
                const ConfigOption*    optptr = full_config.optptr(opt_key);
                const ConfigOptionType type   = optptr->type();

                wxString type_str = type == coNone                                          ? "none" :
                                    type == coFloat || type == coFloats                     ? "float" :
                                    type == coInt || type == coInts                         ? "integer" :
                                    type == coString || type == coStrings                   ? "string" :
                                    type == coPercent || type == coPercents                 ? "percent" :
                                    type == coFloatOrPercent || type == coFloatsOrPercents  ? "float ar percent" :
                                    type == coPoint || type == coPoints || type == coPoint3 ? "point" :
                                    type == coBool || type == coBools                       ? "bool" :
                                    type == coEnum                                          ? "enum" : "undef";

                label = ( cod->full_label.empty() &&  cod->label.empty() ) ? format_wxstr("Undef Label\n(%1%)", type_str) :
                        (!cod->full_label.empty() && !cod->label.empty() ) ?
                        format_wxstr("%1% > %2%\n(%3%)", _(cod->full_label), _(cod->label), type_str) :
                        format_wxstr("%1%\n(%2%)", cod->label.empty() ? _(cod->full_label) : _(cod->label), type_str);
            }
        }

        m_param_label->SetLabel(label);
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

const std::map<GroupParamsType, std::pair <wxString, std::string>> GroupParamsInfo {
//      Type                                Name                    BitmapName
    { GroupParamsType::SlicingState,    {L("Slicing State"),    "re_slice"  },  },
    { GroupParamsType::PrintSettings,   {L("Print settings"),   "cog"       },  },
    { GroupParamsType::MaterialSettings,{L("Material Settings"),"spool"     },  },
    { GroupParamsType::PrinterSettings, {L("Printer Settings"), "printer"   },  },
};

const std::map<SubSlicingState, std::pair <wxString, std::string>> SubSlicingStateInfo {
//      Type                             Name                 BitmapName
    { SubSlicingState::ReadOnly,    {L("Read Only"),    "lock_closed"   },  },
    { SubSlicingState::ReadWrite,   {L("Read Write"),   "lock_open"     },  },
};

const std::map<ParamType, std::string> ParamsInfo {
//      Type                      BitmapName
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

ParamsNode::ParamsNode(GroupParamsType type)
    : m_group_type (type)
{
    const auto& [name, icon_n] = GroupParamsInfo.at(type);
    text = _(name);
    make_bold(text);
    icon_name = icon_n;
}

ParamsNode::ParamsNode(ParamsNode *parent, SubSlicingState sub_type)
    : m_parent(parent)
    , m_group_type(parent->m_group_type)
{
    const auto& [name, icon_n] = SubSlicingStateInfo.at(sub_type);
    text = _(name);
    icon_name = icon_n;
}

ParamsNode::ParamsNode( ParamsNode*         parent,
                        ParamType           param_type,
                        const std::string&  param_key,
                        SubSlicingState     subgroup_type)
    : m_parent(parent)
    , m_group_type(parent->m_group_type)
    , m_sub_type(subgroup_type)
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


wxDataViewItem ParamsModel::AppendGroup(GroupParamsType type)
{
    m_group_nodes[type] = std::make_unique<ParamsNode>(type);

    wxDataViewItem parent(nullptr);
    wxDataViewItem child((void*)m_group_nodes[type].get());

    ItemAdded(parent, child);
    m_ctrl->Expand(parent);
    return child;
}

wxDataViewItem ParamsModel::AppendSubGroup(GroupParamsType type, SubSlicingState sub_type)
{
    m_sub_slicing_state_nodes[sub_type] = std::make_unique<ParamsNode>(m_group_nodes[type].get(), sub_type);

    const wxDataViewItem  group_item    ((void*)m_group_nodes[type].get());
    const wxDataViewItem  sub_group_item((void*)m_sub_slicing_state_nodes[sub_type].get());

    ItemAdded(group_item, sub_group_item);

    m_ctrl->Expand(group_item);
    return sub_group_item;
}

wxDataViewItem ParamsModel::AppendParam(GroupParamsType     type,
                                        ParamType           param_type,
                                        const std::string&  param_key,
                                        SubSlicingState     subgroup_type)
{
    ParamsNode* parent_node{ nullptr };
    if (subgroup_type == SubSlicingState::Undef)
        parent_node = m_group_nodes[type].get();
    else {
        if (m_sub_slicing_state_nodes.find(subgroup_type) == m_sub_slicing_state_nodes.end())
            AppendSubGroup(type, subgroup_type);
        parent_node = m_sub_slicing_state_nodes[subgroup_type].get();
    }

    parent_node->Append(std::make_unique<ParamsNode>(m_group_nodes[type].get(), param_type, param_key, subgroup_type));

    const wxDataViewItem  parent_item((void*)parent_node);
    const wxDataViewItem  child_item((void*)parent_node->GetChildren().back().get());

    ItemAdded(parent_item, child_item);
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

void ParamsModel::Rescale()
{

}

void ParamsModel::Clear()
{

}

void ParamsModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (col == 0)
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
    if (col == 0) {
#ifdef __linux__
        wxDataViewIconText data;
        data << variant;
        node->m_icon = data.GetIcon();
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
        for (const auto& [type, group] : m_group_nodes)
            array.Add(wxDataViewItem((void*)group.get()));
    }
    else if (parent_node->IsGroupNode() && parent_node->GetChildren().empty()) {
        for (const auto& [type, sub_group] : m_sub_slicing_state_nodes)
            array.Add(wxDataViewItem((void*)sub_group.get()));
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
    wxDataViewColumn* column = new wxDataViewColumn("", rd, 0, width * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_CELL_INERT);
#else
    wxDataViewColumn* column = new wxDataViewColumn("", new BitmapTextRenderer(true, wxDATAVIEW_CELL_INERT), 0, 20 * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE);
#endif //__linux__
    this->AppendColumn(column);
    this->SetExpanderColumn(column);
}

wxDataViewItem ParamsViewCtrl::AppendGroup(GroupParamsType type)
{
    return model->AppendGroup(type);
}

wxDataViewItem ParamsViewCtrl::AppendParam( GroupParamsType     group_type,
                                            ParamType           param_type,
                                            const std::string&  param_key,
                                            SubSlicingState     subgroup_type /*= SubSlicingState::Undef*/)
{
    return model->AppendParam(group_type, param_type, param_key, subgroup_type);
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

void ParamsViewCtrl::Clear()
{
    model->Clear();
}

void ParamsViewCtrl::Rescale(int em/* = 0*/)
{
    model->Rescale();
    Refresh();
}
}}    // namespace Slic3r::GUI
