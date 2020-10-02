#include "UnsavedChangesDialog.hpp"

#include <cstddef>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "ExtraRenderers.hpp"
#include "wxExtensions.hpp"

//#define FTS_FUZZY_MATCH_IMPLEMENTATION
//#include "fts_fuzzy_match.h"

#include "BitmapCache.hpp"

using boost::optional;

namespace Slic3r {

namespace GUI {

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside UnsavedChangesModel
// ----------------------------------------------------------------------------

static const std::map<Preset::Type, std::string> type_icon_names = {
    {Preset::TYPE_PRINT,        "cog"           },
    {Preset::TYPE_SLA_PRINT,    "cog"           },
    {Preset::TYPE_FILAMENT,     "spool"         },
    {Preset::TYPE_SLA_MATERIAL, "resin"         },
    {Preset::TYPE_PRINTER,      "printer"       },
};

static std::string black    = "#000000";
static std::string grey     = "#808080";
static std::string orange   = "#ed6b21";

static void color_string(wxString& str, const std::string& color)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = from_u8((boost::format("<span color=\"%1%\">%2%</span>") % color % into_u8(str)).str());
#endif
}

static void make_string_bold(wxString& str)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = from_u8((boost::format("<b>%1%</b>") % into_u8(str)).str());
#endif
}

// preset(root) node
ModelNode::ModelNode(Preset::Type preset_type, const wxString& text, wxWindow* parent_win) :
    m_parent_win(parent_win),
    m_parent(nullptr),
    m_preset_type(preset_type),
    m_icon_name(type_icon_names.at(preset_type)),
    m_text(text)
{
    UpdateIcons();
}

// group node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_icon_name(icon_name),
    m_text(text)
{
    UpdateIcons();
}

// category node
ModelNode::ModelNode(ModelNode* parent, const wxString& text) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_text(text)
{
}

#ifdef __linux__
wxIcon ModelNode::get_bitmap(const wxString& color)
#else
wxBitmap ModelNode::get_bitmap(const wxString& color)
#endif // __linux__
{
    /* It's supposed that standard size of an icon is 48px*16px for 100% scaled display.
     * So set sizes for solid_colored icons used for filament preset
     * and scale them in respect to em_unit value
     */
    const double em = em_unit(m_parent_win);
    const int icon_width    = lround(6.4 * em);
    const int icon_height   = lround(1.6 * em);

    BitmapCache bmp_cache;
    unsigned char rgb[3];
    BitmapCache::parse_color(into_u8(color), rgb);
    // there is no need to scale created solid bitmap
#ifndef __linux__
    return bmp_cache.mksolid(icon_width, icon_height, rgb, true);
#else
    wxIcon icon;
    icon.CopyFromBitmap(bmp_cache.mksolid(icon_width, icon_height, rgb, true));
    return icon;
#endif // __linux__
}

// option node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, const wxString& old_value, const wxString& new_value) :
    m_parent(parent),
    m_old_color(old_value.StartsWith("#") ? old_value : ""),
    m_new_color(new_value.StartsWith("#") ? new_value : ""),
    m_container(false),
    m_text(text),
    m_old_value(old_value),
    m_new_value(new_value)
{
    // check if old/new_value is color
    if (m_old_color.IsEmpty()) {
        if (!m_new_color.IsEmpty())
            m_old_value = _L("Undef");
    }
    else {
        m_old_color_bmp = get_bitmap(m_old_color);
        m_old_value.Clear();
    }

    if (m_new_color.IsEmpty()) {
        if (!m_old_color.IsEmpty())
            m_new_value = _L("Undef");
    }
    else {
        m_new_color_bmp = get_bitmap(m_new_color);
        m_new_value.Clear();
    }

    // "color" strings
    color_string(m_old_value, black);
    color_string(m_new_value, orange);
}

void ModelNode::UpdateEnabling()
{
    auto change_text_color = [](wxString& str, const std::string& clr_from, const std::string& clr_to)
    {
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
        std::string old_val = into_u8(str);
        boost::replace_all(old_val, clr_from, clr_to);
        str = from_u8(old_val);
#endif
    };

    if (!m_toggle) {
        change_text_color(m_text,      black, grey);
        change_text_color(m_old_value, black, grey);
        change_text_color(m_new_value, orange,grey);
    }
    else {
        change_text_color(m_text,      grey, black);
        change_text_color(m_old_value, grey, black);
        change_text_color(m_new_value, grey, orange);
    }
    // update icons for the colors
    UpdateIcons();
}

void ModelNode::UpdateIcons()
{
    // update icons for the colors, if any exists
    if (!m_old_color.IsEmpty())
        m_old_color_bmp = get_bitmap(m_toggle ? m_old_color : grey);
    if (!m_new_color.IsEmpty())
        m_new_color_bmp = get_bitmap(m_toggle ? m_new_color : grey);

    // update main icon, if any exists
    if (m_icon_name.empty())
        return;

#ifdef __linux__
    m_icon.CopyFromBitmap(create_scaled_bitmap(m_icon_name, m_parent_win, 16, !m_toggle));
#else
    m_icon = create_scaled_bitmap(m_icon_name, m_parent_win, 16, !m_toggle);
#endif //__linux__
}


// ----------------------------------------------------------------------------
//                          UnsavedChangesModel
// ----------------------------------------------------------------------------

UnsavedChangesModel::UnsavedChangesModel(wxWindow* parent) :
    m_parent_win(parent)
{
}

UnsavedChangesModel::~UnsavedChangesModel()
{
    for (ModelNode* preset_node : m_preset_nodes)
        delete preset_node;
}

wxDataViewItem UnsavedChangesModel::AddPreset(Preset::Type type, wxString preset_name)
{
    // "color" strings
    color_string(preset_name, black);
    make_string_bold(preset_name);

    auto preset = new ModelNode(type, preset_name, m_parent_win);
    m_preset_nodes.emplace_back(preset);

    wxDataViewItem child((void*)preset);
    wxDataViewItem parent(nullptr);

    ItemAdded(parent, child);
    return child;
}

ModelNode* UnsavedChangesModel::AddOption(ModelNode* group_node, wxString option_name, wxString old_value, wxString new_value)
{
    ModelNode* option = new ModelNode(group_node, option_name, old_value, new_value);
    group_node->Append(option);
    wxDataViewItem group_item = wxDataViewItem((void*)group_node);
    ItemAdded(group_item, wxDataViewItem((void*)option));

    m_ctrl->Expand(group_item);
    return option;
}

ModelNode* UnsavedChangesModel::AddOptionWithGroup(ModelNode* category_node, wxString group_name, wxString option_name, wxString old_value, wxString new_value)
{
    ModelNode* group_node = new ModelNode(category_node, group_name);
    category_node->Append(group_node);
    ItemAdded(wxDataViewItem((void*)category_node), wxDataViewItem((void*)group_node));

    return AddOption(group_node, option_name, old_value, new_value);
}

ModelNode* UnsavedChangesModel::AddOptionWithGroupAndCategory(ModelNode* preset_node, wxString category_name, wxString group_name, wxString option_name, wxString old_value, wxString new_value)
{
    ModelNode* category_node = new ModelNode(preset_node, category_name, "cog");
    preset_node->Append(category_node);
    ItemAdded(wxDataViewItem((void*)preset_node), wxDataViewItem((void*)category_node));

    return AddOptionWithGroup(category_node, group_name, option_name, old_value, new_value);
}

wxDataViewItem UnsavedChangesModel::AddOption(Preset::Type type, wxString category_name, wxString group_name, wxString option_name,
                                              wxString old_value, wxString new_value)
{
    // "color" strings
    color_string(category_name, black);
    color_string(group_name,    black);
    color_string(option_name,   black);

    // "make" strings bold
    make_string_bold(category_name);
    make_string_bold(group_name);

    // add items
    for (ModelNode* preset : m_preset_nodes)
        if (preset->type() == type)
        {
            for (ModelNode* category : preset->GetChildren())
                if (category->text() == category_name)
                {
                    for (ModelNode* group : category->GetChildren())
                        if (group->text() == group_name)
                            return wxDataViewItem((void*)AddOption(group, option_name, old_value, new_value));
                    
                    return wxDataViewItem((void*)AddOptionWithGroup(category, group_name, option_name, old_value, new_value));
                }

            return wxDataViewItem((void*)AddOptionWithGroupAndCategory(preset, category_name, group_name, option_name, old_value, new_value));
        }

    return wxDataViewItem(nullptr);    
}

static void update_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        bool toggle = parent->IsToggled();
        for (ModelNode* child : parent->GetChildren()) {
            child->Toggle(toggle);
            child->UpdateEnabling();
            update_children(child);
        }
    }
}

static void update_parents(ModelNode* node)
{
    ModelNode* parent = node->GetParent();
    if (parent) {
        bool toggle = false;
        for (ModelNode* child : parent->GetChildren()) {
            if (child->IsToggled()) {
                toggle = true;
                break;
            }
        }
        parent->Toggle(toggle);
        parent->UpdateEnabling();
        update_parents(parent);
    }
}

void UnsavedChangesModel::UpdateItemEnabling(wxDataViewItem item)
{
    assert(item.IsOk());
    ModelNode* node = (ModelNode*)item.GetID();
    node->UpdateEnabling();

    update_children(node);
    update_parents(node);    
}

bool UnsavedChangesModel::IsEnabledItem(const wxDataViewItem& item)
{
    assert(item.IsOk());
    ModelNode* node = (ModelNode*)item.GetID();
    return node->IsToggled();
}

void UnsavedChangesModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ModelNode* node = (ModelNode*)item.GetID();
    switch (col)
    {
    case colToggle:
        variant = node->m_toggle;
        break;
#ifdef __linux__
    case colIconText:
        variant << wxDataViewIconText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << wxDataViewIconText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colNewValue:
        variant << wxDataViewIconText(node->m_new_value, node->m_new_color_bmp);
        break;
#else
    case colIconText:
        variant << DataViewBitmapText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << DataViewBitmapText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colNewValue:
        variant << DataViewBitmapText(node->m_new_value, node->m_new_color_bmp);
        break;
#endif //__linux__

    default:
        wxLogError("UnsavedChangesModel::GetValue: wrong column %d", col);
    }
}

bool UnsavedChangesModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ModelNode* node = (ModelNode*)item.GetID();
    switch (col)
    {
    case colToggle:
        node->m_toggle = variant.GetBool();
        return true;
#ifdef __linux__
    case colIconText: {
        wxDataViewIconText data;
        data << variant;
        node->m_icon = data.GetIcon();
        node->m_text = data.GetText();
        return true; }
    case colOldValue: {
        wxDataViewIconText data;
        data << variant;
        node->m_old_color_bmp   = data.GetIcon();
        node->m_old_value       = data.GetText();
        return true; }
    case colNewValue: {
        wxDataViewIconText data;
        data << variant;
        node->m_new_color_bmp   = data.GetIcon();
        node->m_new_value       = data.GetText();
        return true; }
#else
    case colIconText: {
        DataViewBitmapText data;
        data << variant;
        node->m_icon = data.GetBitmap();
        node->m_text = data.GetText();
        return true; }
    case colOldValue: {
        DataViewBitmapText data;
        data << variant;
        node->m_old_color_bmp   = data.GetBitmap();
        node->m_old_value       = data.GetText();
        return true; }
    case colNewValue: {
        DataViewBitmapText data;
        data << variant;
        node->m_new_color_bmp   = data.GetBitmap();
        node->m_new_value       = data.GetText();
        return true; }
#endif //__linux__
    default:
        wxLogError("UnsavedChangesModel::SetValue: wrong column");
    }
    return false;
}

bool UnsavedChangesModel::IsEnabled(const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());
    if (col == colToggle)
        return true;

    // disable unchecked nodes
    return ((ModelNode*)item.GetID())->IsToggled();
}

wxDataViewItem UnsavedChangesModel::GetParent(const wxDataViewItem& item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ModelNode* node = (ModelNode*)item.GetID();

    // "MyMusic" also has no parent
    if (node->IsRoot())
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool UnsavedChangesModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ModelNode* node = (ModelNode*)item.GetID();
    return node->IsContainer();
}

unsigned int UnsavedChangesModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ModelNode* node = (ModelNode*)parent.GetID();
    if (!node) {
        for (auto preset_node : m_preset_nodes)
            array.Add(wxDataViewItem((void*)preset_node));
        return m_preset_nodes.size();
    }

    if (node->GetChildCount() == 0)
        return 0;

    unsigned int count = node->GetChildren().GetCount();
    for (unsigned int pos = 0; pos < count; pos++) {
        ModelNode* child = node->GetChildren().Item(pos);
        array.Add(wxDataViewItem((void*)child));
    }

    return count;
}


wxString UnsavedChangesModel::GetColumnType(unsigned int col) const
{
    switch (col)
    {
    case colToggle:
        return "bool";
    case colIconText:
    case colOldValue:
    case colNewValue:
    default:
        return "DataViewBitmapText";//"string";
    }
}

static void rescale_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        for (ModelNode* child : parent->GetChildren()) {
            child->UpdateIcons();
            rescale_children(child);
        }
    }
}

void UnsavedChangesModel::Rescale()
{
    for (ModelNode* node : m_preset_nodes) {
        node->UpdateIcons();
        rescale_children(node);
    }
}


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------

UnsavedChangesDialog::UnsavedChangesDialog(const wxString& header)
    : DPIDialog(nullptr, wxID_ANY, _L("Unsaved Changes"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    build(Preset::TYPE_INVALID, nullptr, "", header);
}

UnsavedChangesDialog::UnsavedChangesDialog(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset)
    : DPIDialog(nullptr, wxID_ANY, _L("Unsaved Changes"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    build(type, dependent_presets, new_selected_preset);
}

void UnsavedChangesDialog::build(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header)
{
    wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    SetBackgroundColour(bgr_clr);

#if ENABLE_WX_3_1_3_DPI_CHANGED_EVENT && defined(__WXMSW__)
    // ys_FIXME! temporary workaround for correct font scaling
    // Because of from wxWidgets 3.1.3 auto rescaling is implemented for the Fonts,
    // From the very beginning set dialog font to the wxSYS_DEFAULT_GUI_FONT
    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#endif // ENABLE_WX_3_1_3_DPI_CHANGED_EVENT

    int border = 10;
    int em = em_unit();

    m_action_line = new wxStaticText(this, wxID_ANY, "");
    m_action_line->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());

    m_tree       = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(em * 80, em * 30), wxBORDER_SIMPLE | wxDV_VARIABLE_LINE_HEIGHT | wxDV_ROW_LINES);
    m_tree_model = new UnsavedChangesModel(this);
    m_tree->AssociateModel(m_tree_model);
    m_tree_model->SetAssociatedControl(m_tree);

    m_tree->AppendToggleColumn(L"\u2714", UnsavedChangesModel::colToggle, wxDATAVIEW_CELL_ACTIVATABLE, 6 * em);//2610,11,12 //2714

    auto append_bmp_text_column = [this](const wxString& label, unsigned model_column, int width, bool set_expander = false) 
    {
#ifdef __linux__
        wxDataViewIconTextRenderer* rd = new wxDataViewIconTextRenderer();
#ifdef SUPPORTS_MARKUP
        rd->EnableMarkup(true);
#endif
        wxDataViewColumn* column = new wxDataViewColumn(label, rd, model_column, width, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_CELL_INERT);
#else
        wxDataViewColumn* column = new wxDataViewColumn(label, new BitmapTextRenderer(true), model_column, width, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE);
#endif //__linux__
        m_tree->AppendColumn(column);
        if (set_expander)
            m_tree->SetExpanderColumn(column);
    };

    append_bmp_text_column("",              UnsavedChangesModel::colIconText, 30 * em);
    append_bmp_text_column(_L("Old Value"), UnsavedChangesModel::colOldValue, 20 * em);
    append_bmp_text_column(_L("New Value"), UnsavedChangesModel::colNewValue, 20 * em);

    m_tree->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &UnsavedChangesDialog::item_value_changed, this);
    m_tree->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,  &UnsavedChangesDialog::context_menu, this);

    // Add Buttons 
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCANCEL);

    auto add_btn = [this, buttons](ScalableButton** btn, int& btn_id, const std::string& icon_name, Action close_act, int idx, bool process_enable = true)
    {
        *btn = new ScalableButton(this, btn_id = NewControlId(), icon_name, "", wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, true);
        buttons->Insert(idx, *btn, 0, wxLEFT, 5);

        (*btn)->Bind(wxEVT_BUTTON, [this, close_act](wxEvent&) { close(close_act); });
        if (process_enable)
            (*btn)->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(!m_empty_selection); });
        (*btn)->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { show_info_line(Action::Undef); e.Skip(); });
    };

    int btn_idx = 0;
    add_btn(&m_save_btn, m_save_btn_id, "save", Action::Save, btn_idx++);

    const PresetCollection& printers = wxGetApp().preset_bundle->printers;
    if (dependent_presets && (type == dependent_presets->type() ? 
        dependent_presets->get_edited_preset().printer_technology() == dependent_presets->find_preset(new_selected_preset)->printer_technology() : 
        printers.get_edited_preset().printer_technology() == printers.find_preset(new_selected_preset)->printer_technology() ) )
        add_btn(&m_move_btn, m_move_btn_id, "paste_menu", Action::Move, btn_idx++);
    add_btn(&m_continue_btn, m_continue_btn_id, "cross", Action::Continue, btn_idx, false);

    m_info_line = new wxStaticText(this, wxID_ANY, "");
    m_info_line->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
    m_info_line->Hide();

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(m_action_line,0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_tree,       1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_info_line,  0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 2*border);
    topSizer->Add(buttons,      0, wxEXPAND | wxALL, border);

    update(type, dependent_presets, new_selected_preset, header);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    show_info_line(Action::Undef);
}

void UnsavedChangesDialog::item_value_changed(wxDataViewEvent& event)
{
    if (event.GetColumn() != UnsavedChangesModel::colToggle)
        return;

    wxDataViewItem item = event.GetItem();

    m_tree_model->UpdateItemEnabling(item);
    m_tree->Refresh();

    // update an enabling of the "save/move" buttons
    m_empty_selection = get_selected_options().empty();
}

void UnsavedChangesDialog::context_menu(wxDataViewEvent& event)
{
    if (!m_has_long_strings)
        return;

    wxDataViewItem item = event.GetItem();
    if (!item)
    {
        wxPoint mouse_pos = wxGetMousePosition() - m_tree->GetScreenPosition();
        wxDataViewColumn* col = nullptr;
        m_tree->HitTest(mouse_pos, item, col);

        if (!item)
            item = m_tree->GetSelection();

        if (!item)
            return;
    }

    auto it = m_items_map.find(item);
    if (it == m_items_map.end() || !it->second.is_long)
        return;
    FullCompareDialog(it->second.opt_name, it->second.old_val, it->second.new_val).ShowModal();
}

void UnsavedChangesDialog::show_info_line(Action action, std::string preset_name)
{
    if (m_motion_action == action)
        return;
    if (action == Action::Undef && !m_has_long_strings)
        m_info_line->Hide();
    else {
        wxString text;
        if (action == Action::Undef)
            text = _L("Some fields are too long to fit. Right click on it to show full text.");
        else if (action == Action::Continue)
            text = _L("All changed options will be reverted.");
        else {
            std::string act_string = action == Action::Save ? _u8L("save") : _u8L("move");
            if (preset_name.empty())
                text = from_u8((boost::format("Press to %1% selected options.") % act_string).str());
            else
                text = from_u8((boost::format("Press to %1% selected options to the preset \"%2%\".") % act_string % preset_name).str());
            text += "\n" + _L("Unselected options will be reverted.");
        }
        m_info_line->SetLabel(text);
        m_info_line->Show();
    }

    m_motion_action = action;

    Layout();
    Refresh();
}

void UnsavedChangesDialog::close(Action action)
{
    m_exit_action = action;
    this->EndModal(wxID_CLOSE);
}

template<class T>
wxString get_string_from_enum(const std::string& opt_key, const DynamicPrintConfig& config, bool is_infill = false)
{
    const ConfigOptionDef& def = config.def()->options.at(opt_key);
    const std::vector<std::string>& names = def.enum_labels;//ConfigOptionEnum<T>::get_enum_names();
    T val = config.option<ConfigOptionEnum<T>>(opt_key)->value;

    // Each infill doesn't use all list of infill declared in PrintConfig.hpp.
    // So we should "convert" val to the correct one
    if (is_infill) {
        for (auto key_val : *def.enum_keys_map)
            if ((int)key_val.second == (int)val) {
                auto it = std::find(def.enum_values.begin(), def.enum_values.end(), key_val.first);
                if (it == def.enum_values.end())
                    return "";
                return from_u8(_utf8(names[it - def.enum_values.begin()]));
            }
        return _L("Undef");
    }
    return from_u8(_utf8(names[static_cast<int>(val)]));
}

static int get_id_from_opt_key(std::string opt_key)
{
    int pos = opt_key.find("#");
    if (pos > 0) {
        boost::erase_head(opt_key, pos + 1);
        return atoi(opt_key.c_str());
    }
    return 0;
}

static std::string get_pure_opt_key(std::string opt_key)
{
    int pos = opt_key.find("#");
    if (pos > 0)
        boost::erase_tail(opt_key, opt_key.size() - pos);
    return opt_key;
}

static wxString get_string_value(std::string opt_key, const DynamicPrintConfig& config)
{
    int opt_idx = get_id_from_opt_key(opt_key);
    opt_key = get_pure_opt_key(opt_key);

    if (config.option(opt_key)->is_nil())
        return _L("N/A");

    wxString out;

    const ConfigOptionDef* opt = config.def()->get(opt_key);
    bool is_nullable = opt->nullable;

    switch (opt->type) {
    case coInt:
        return from_u8((boost::format("%1%") % config.opt_int(opt_key)).str());
    case coInts: {
        int val = is_nullable ? 
            config.opt<ConfigOptionIntsNullable>(opt_key)->get_at(opt_idx) :
            config.opt<ConfigOptionInts>(opt_key)->get_at(opt_idx);
        return from_u8((boost::format("%1%") % val).str());
    }
    case coBool:
        return config.opt_bool(opt_key) ? "true" : "false";
    case coBools: {
        bool val = is_nullable ?
            config.opt<ConfigOptionBoolsNullable>(opt_key)->get_at(opt_idx) :
            config.opt<ConfigOptionBools>(opt_key)->get_at(opt_idx);
        return val ? "true" : "false";
    }
    case coPercent:
        return from_u8((boost::format("%1%%%") % int(config.optptr(opt_key)->getFloat())).str());
    case coPercents: {
        double val = is_nullable ?
            config.opt<ConfigOptionPercentsNullable>(opt_key)->get_at(opt_idx) :
            config.opt<ConfigOptionPercents>(opt_key)->get_at(opt_idx);
        return from_u8((boost::format("%1%%%") % int(val)).str());
    }
    case coFloat:
        return double_to_string(config.opt_float(opt_key));
    case coFloats: {
        double val = is_nullable ?
            config.opt<ConfigOptionFloatsNullable>(opt_key)->get_at(opt_idx) :
            config.opt<ConfigOptionFloats>(opt_key)->get_at(opt_idx);
        return double_to_string(val);
    }
    case coString:
        return from_u8(config.opt_string(opt_key));
    case coStrings: {
        const ConfigOptionStrings* strings = config.opt<ConfigOptionStrings>(opt_key);
        if (strings) {
            if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
                if (strings->empty())
                    return _L("All"); 
                for (size_t id = 0; id < strings->size(); id++)
                    out += from_u8(strings->get_at(id)) + "\n";
                out.RemoveLast(1);
                return out;
            }
            if (!strings->empty())
                return from_u8(strings->get_at(opt_idx));
        }
        break;
        }
    case coFloatOrPercent: {
        const ConfigOptionFloatOrPercent* opt = config.opt<ConfigOptionFloatOrPercent>(opt_key);
        if (opt)
            out = double_to_string(opt->value) + (opt->percent ? "%" : "");
        return out;
    }
    case coEnum: {
        if (opt_key == "top_fill_pattern" ||
            opt_key == "bottom_fill_pattern" ||
            opt_key == "fill_pattern")
            return get_string_from_enum<InfillPattern>(opt_key, config, true);
        if (opt_key == "gcode_flavor")
            return get_string_from_enum<GCodeFlavor>(opt_key, config);
        if (opt_key == "machine_limits_usage")
            return get_string_from_enum<MachineLimitsUsage>(opt_key, config);
        if (opt_key == "ironing_type")
            return get_string_from_enum<IroningType>(opt_key, config);
        if (opt_key == "support_material_pattern")
            return get_string_from_enum<SupportMaterialPattern>(opt_key, config);
        if (opt_key == "seam_position")
            return get_string_from_enum<SeamPosition>(opt_key, config);
        if (opt_key == "display_orientation")
            return get_string_from_enum<SLADisplayOrientation>(opt_key, config);
        if (opt_key == "support_pillar_connection_mode")
            return get_string_from_enum<SLAPillarConnectionMode>(opt_key, config);
        break;
    }
    case coPoints: {
        if (opt_key == "bed_shape") {
            BedShape shape(*config.option<ConfigOptionPoints>(opt_key));
            return shape.get_full_name_with_params();
        }
        Vec2d val = config.opt<ConfigOptionPoints>(opt_key)->get_at(opt_idx);
        return from_u8((boost::format("[%1%]") % ConfigOptionPoint(val).serialize()).str());
    }
    default:
        break;
    }
    return out;
}

wxString UnsavedChangesDialog::get_short_string(wxString full_string)
{
    int max_len = 30;
    if (full_string.IsEmpty() || full_string.StartsWith("#") || 
        (full_string.Find("\n") == wxNOT_FOUND && full_string.Length() < max_len))
        return full_string;

    m_has_long_strings = true;

    int n_pos = full_string.Find("\n");
    if (n_pos != wxNOT_FOUND && n_pos < max_len)
        max_len = n_pos;

    full_string.Truncate(max_len);
    return full_string + dots;
}

void UnsavedChangesDialog::update(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header)
{
    PresetCollection* presets = dependent_presets;

    // activate buttons and labels
    m_save_btn      ->Bind(wxEVT_ENTER_WINDOW, [this, presets]              (wxMouseEvent& e) { show_info_line(Action::Save, presets ? presets->get_selected_preset().name : ""); e.Skip(); });
    if (m_move_btn) {
        bool is_empty_name = type != dependent_presets->type();
        m_move_btn  ->Bind(wxEVT_ENTER_WINDOW, [this, new_selected_preset, is_empty_name]  (wxMouseEvent& e) { show_info_line(Action::Move, is_empty_name ? "" : new_selected_preset); e.Skip(); });
    }
    m_continue_btn  ->Bind(wxEVT_ENTER_WINDOW, [this]                       (wxMouseEvent& e) { show_info_line(Action::Continue); e.Skip(); });

    m_continue_btn->SetLabel(_L("Continue without changes"));

    if (type == Preset::TYPE_INVALID) {
        m_action_line   ->SetLabel(header + "\n" + _L("Next presets have the following unsaved changes:"));
        m_save_btn      ->SetLabel(_L("Save selected"));
    }
    else {
        wxString action_msg;
        if (type == dependent_presets->type()) {
            action_msg = _L("has the following unsaved changes:");
            if (m_move_btn)
                m_move_btn->SetLabel(from_u8((boost::format(_u8L("Move selected to preset: %1%")) % ("\"" + new_selected_preset + "\"")).str()));
        }
        else {
            action_msg = type == Preset::TYPE_PRINTER ?
                        _L("is not compatible with printer") :
                        _L("is not compatible with print profile");
            action_msg += " \"" + from_u8(new_selected_preset) + "\"\n";
            action_msg += _L("and it has the following unsaved changes:");

            if (m_move_btn)
                m_move_btn->SetLabel(_L("Move selected to the first compatible preset"));
        }
        m_action_line->SetLabel(from_u8((boost::format(_utf8(L("Preset \"%1%\" %2%"))) % _utf8(presets->get_edited_preset().name) % action_msg).str()));
        m_save_btn->SetLabel(from_u8((boost::format(_u8L("Save selected to preset: %1%")) % ("\"" + presets->get_selected_preset().name + "\"")).str()));
    }

    update_tree(type, presets);
}

void UnsavedChangesDialog::update_tree(Preset::Type type, PresetCollection* presets_)
{
    Search::OptionsSearcher& searcher = wxGetApp().sidebar().get_searcher();
    searcher.sort_options_by_opt_key();

    // list of the presets with unsaved changes
    std::vector<PresetCollection*> presets_list;
    if (type == Preset::TYPE_INVALID)
    {
        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                presets_list.emplace_back(tab->get_presets());
    }
    else
        presets_list.emplace_back(presets_);

    // Display a dialog showing the dirty options in a human readable form.
    for (PresetCollection* presets : presets_list)
    {
        const DynamicPrintConfig& old_config = presets->get_selected_preset().config;
        const DynamicPrintConfig& new_config = presets->get_edited_preset().config;
        type = presets->type();

        m_tree_model->AddPreset(type, from_u8(presets->get_edited_preset().name));

        // Collect dirty options.
        const bool deep_compare = (type == Preset::TYPE_PRINTER || type == Preset::TYPE_SLA_MATERIAL);
        auto dirty_options = presets->current_dirty_options(deep_compare);
        auto dirty_options_ = presets->current_dirty_options();

        // process changes of extruders count
        if (type == Preset::TYPE_PRINTER &&
            old_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() != new_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()) {
            wxString local_label = _L("Extruders count");
            wxString old_val = from_u8((boost::format("%1%") % old_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());
            wxString new_val = from_u8((boost::format("%1%") % new_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());

            ItemData item_data = { "extruders_count", local_label, old_val, new_val, type };
            m_items_map.emplace(m_tree_model->AddOption(type, _L("General"), _L("Capabilities"), local_label, old_val, new_val), item_data);

        }

        for (const std::string& opt_key : dirty_options) {
            const Search::Option& option = searcher.get_option(opt_key);

            ItemData item_data = { opt_key, option.label_local, get_string_value(opt_key, old_config), get_string_value(opt_key, new_config), type };

            wxString old_val = get_short_string(item_data.old_val);
            wxString new_val = get_short_string(item_data.new_val);
            if (old_val != item_data.old_val || new_val != item_data.new_val)
                item_data.is_long = true;

            m_items_map.emplace(m_tree_model->AddOption(type, option.category_local, option.group_local, option.label_local, old_val, new_val), item_data);
        }
    }
}

std::vector<std::string> UnsavedChangesDialog::get_unselected_options(Preset::Type type)
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) {
        if (item.second.opt_key == "extruders_count")
            continue;
        if (item.second.type == type && !m_tree_model->IsEnabledItem(item.first))
            ret.emplace_back(get_pure_opt_key(item.second.opt_key));
    }

    return ret;
}

std::vector<std::string> UnsavedChangesDialog::get_selected_options()
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) 
        if (m_tree_model->IsEnabledItem(item.first))
            ret.emplace_back(get_pure_opt_key(item.second.opt_key));

    return ret;
}

void UnsavedChangesDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL, m_save_btn_id, m_move_btn_id, m_continue_btn_id });
    for (auto btn : { m_save_btn, m_move_btn, m_continue_btn } )
        btn->msw_rescale();

    const wxSize& size = wxSize(80 * em, 30 * em);
    SetMinSize(size);

    m_tree->GetColumn(UnsavedChangesModel::colToggle  )->SetWidth(6 * em);
    m_tree->GetColumn(UnsavedChangesModel::colIconText)->SetWidth(30 * em);
    m_tree->GetColumn(UnsavedChangesModel::colOldValue)->SetWidth(20 * em);
    m_tree->GetColumn(UnsavedChangesModel::colNewValue)->SetWidth(20 * em);

    m_tree_model->Rescale();
    m_tree->Refresh();

    Fit();
    Refresh();
}

void UnsavedChangesDialog::on_sys_color_changed()
{
    for (auto btn : { m_save_btn, m_move_btn, m_continue_btn } )
        btn->msw_rescale();
    // msw_rescale updates just icons, so use it
    m_tree_model->Rescale();
    m_tree->Refresh();

    Refresh();
}


//------------------------------------------
//          FullCompareDialog
//------------------------------------------

FullCompareDialog::FullCompareDialog(const wxString& option_name, const wxString& old_value, const wxString& new_value)
    : wxDialog(nullptr, wxID_ANY, option_name, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    SetBackgroundColour(bgr_clr);

    int border = 10;

    wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, this);

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2, 2, 1, 0);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    grid_sizer->AddGrowableCol(0,1);
    grid_sizer->AddGrowableCol(1,1);
    grid_sizer->AddGrowableRow(1,1);

    auto add_header = [grid_sizer, border, this](wxString label) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, label);
        text->SetFont(this->GetFont().Bold());
        grid_sizer->Add(text, 0, wxALL, border);
    };

    auto add_value = [grid_sizer, border, this](wxString label, bool is_colored = false) {
        wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, label, wxDefaultPosition, wxSize(300, -1), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE | wxTE_RICH);
        text->SetStyle(0, label.Len(), wxTextAttr(is_colored ? wxColour(orange) : wxNullColour, wxNullColour, this->GetFont()));
        grid_sizer->Add(text, 1, wxALL | wxEXPAND, border);
    };

    add_header(_L("Old value"));
    add_header(_L("New value"));
    add_value(old_value);
    add_value(new_value, true);

    sizer->Add(grid_sizer, 1, wxEXPAND);

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(sizer,   1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(buttons, 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}


}

}    // namespace Slic3r::GUI
