#include "UnsavedChangesDialog.hpp"

#include <cstddef>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/nowide/convert.hpp>

#include "wx/dataview.h"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "ObjectDataViewModel.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

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
#if defined(SUPPORTS_MARKUP) && defined(wxHAS_GENERIC_DATAVIEWCTRL)
    str = from_u8((boost::format("<span color=\"%1%\">%2%</span>") % color % into_u8(str)).str());
#endif
}

static void make_string_bold(wxString& str)
{
#if defined(SUPPORTS_MARKUP) && defined(wxHAS_GENERIC_DATAVIEWCTRL)
    str = from_u8((boost::format("<b>%1%</b>") % into_u8(str)).str());
#endif
}

// preset(root) node
ModelNode::ModelNode(Preset::Type preset_type, const wxString& text) :
    m_parent(nullptr),
    m_preset_type(preset_type),
    m_text(text)
{
    m_icon = create_scaled_bitmap(type_icon_names.at(preset_type));
}

// group node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name) :
    m_parent(parent),
    m_text(text)
{
    m_icon = create_scaled_bitmap(icon_name);
}

// category node
ModelNode::ModelNode(ModelNode* parent, const wxString& text) :
    m_parent(parent),
    m_text(text)
{
}

wxBitmap ModelNode::get_bitmap(const wxString& color)
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
     return bmp_cache.mksolid(icon_width, icon_height, rgb, true);
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
#if defined(SUPPORTS_MARKUP) && defined(wxHAS_GENERIC_DATAVIEWCTRL)
    auto change_text_color = [](wxString& str, const std::string& clr_from, const std::string& clr_to)
    {
        std::string old_val = into_u8(str);
        boost::replace_all(old_val, clr_from, clr_to);
        str = from_u8(old_val);
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
#endif
    // update icons for the colors
    if (!m_old_color.IsEmpty())
         m_old_color_bmp = get_bitmap(m_toggle? m_old_color : grey);
    if (!m_new_color.IsEmpty())
         m_new_color_bmp = get_bitmap(m_toggle? m_new_color : grey);
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

    auto preset = new ModelNode(type, preset_name);
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
    ItemAdded(wxDataViewItem((void*)group_node), wxDataViewItem((void*)option));

    return option;
}

ModelNode* UnsavedChangesModel::AddOptionWithGroup(ModelNode* category_node, wxString group_name, wxString option_name, wxString old_value, wxString new_value)
{
    ModelNode* group_node = new ModelNode(category_node, group_name);
    category_node->Append(group_node);
    wxDataViewItem group_item = wxDataViewItem((void*)group_node);
    ItemAdded(wxDataViewItem((void*)category_node), group_item);
    m_ctrl->Expand(group_item);

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

void UnsavedChangesModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ModelNode* node = (ModelNode*)item.GetID();
    switch (col)
    {
    case colToggle:
        variant = node->m_toggle;
        break;
    case colIconText:
        variant << DataViewBitmapText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << DataViewBitmapText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colNewValue:
        variant << DataViewBitmapText(node->m_new_value, node->m_new_color_bmp);
        break;

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
    case colIconText: {
        DataViewBitmapText data;
        data << variant;
        node->m_icon = data.GetBitmap();
        node->m_text = data.GetText();
        return true; }
    case colToggle:
        node->m_toggle = variant.GetBool();
        return true;
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


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------

UnsavedChangesDialog::UnsavedChangesDialog(Preset::Type type)
    : DPIDialog(nullptr, wxID_ANY, _L("Unsaved Changes"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    SetBackgroundColour(bgr_clr);

    int border = 10;
    int em = em_unit();

    m_tree       = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(em * 80, em * 30), wxBORDER_SIMPLE | wxDV_VARIABLE_LINE_HEIGHT);
    m_tree_model = new UnsavedChangesModel(this);
    m_tree->AssociateModel(m_tree_model);
    m_tree_model->SetAssociatedControl(m_tree);

    m_tree->AppendColumn(new wxDataViewColumn("",           new BitmapTextRenderer(true), UnsavedChangesModel::colIconText, 30 * em, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE));

    m_tree->AppendToggleColumn(L"\u2714", UnsavedChangesModel::colToggle, wxDATAVIEW_CELL_ACTIVATABLE, 6 * em);//2610,11,12 //2714

    m_tree->AppendColumn(new wxDataViewColumn("Old value",  new BitmapTextRenderer(true), UnsavedChangesModel::colOldValue, 20 * em, wxALIGN_TOP));
    m_tree->AppendColumn(new wxDataViewColumn("New value",  new BitmapTextRenderer(true), UnsavedChangesModel::colNewValue, 20 * em, wxALIGN_TOP));

    m_tree->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &UnsavedChangesDialog::item_value_changed, this);

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCANCEL);

    Tab* tab = wxGetApp().get_tab(type);
    assert(tab);

    PresetCollection* presets = tab->get_presets();

    wxString label= from_u8((boost::format(_u8L("Save selected to preset:%1%"))% ("\"" + presets->get_selected_preset().name + "\"")).str());
    auto save_btn = new wxButton(this, m_save_btn_id = NewControlId(), label);
    save_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { close(Action::Save); });
    buttons->Insert(0, save_btn, 0, wxLEFT, 5);

    label = from_u8((boost::format(_u8L("Move selected to preset:%1%"))% ("\"NewSelectedPreset\"")).str());
    auto move_btn = new wxButton(this, m_move_btn_id = NewControlId(), label);
    move_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { close(Action::Move); });
    buttons->Insert(1, move_btn, 0, wxLEFT, 5);

    auto continue_btn = new wxButton(this, m_continue_btn_id = NewControlId(), _L("Continue without changes"));
    continue_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { close(Action::Continue); });
    buttons->Insert(2, continue_btn, 0, wxLEFT, 5);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(new wxStaticText(this, wxID_ANY, _L("There is unsaved changes for") + (": \"" + tab->title() + "\"")), 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_tree, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(buttons, 0, wxEXPAND | wxALL, border);

    update(type);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void UnsavedChangesDialog::item_value_changed(wxDataViewEvent& event)
{
    if (event.GetColumn() != UnsavedChangesModel::colToggle)
        return;

    wxDataViewItem item = event.GetItem();

    m_tree_model->UpdateItemEnabling(item);
    m_tree->Refresh();
}

void UnsavedChangesDialog::close(Action action)
{
    m_action = action;
    this->EndModal(wxID_CLOSE);
}

template<class T>
wxString get_string_from_enum(const std::string& opt_key, const DynamicPrintConfig& config)
{
    const std::vector<std::string>& names = config.def()->options.at(opt_key).enum_labels;//ConfigOptionEnum<T>::get_enum_names();
    T val = config.option<ConfigOptionEnum<T>>(opt_key)->value;
    return from_u8(_u8L(names[static_cast<int>(val)]));
}

static wxString get_string_value(const std::string& opt_key, const DynamicPrintConfig& config)
{
    wxString out;

    // FIXME controll, if opt_key has index
    int opt_idx = 0;

    ConfigOptionType type = config.def()->options.at(opt_key).type;

    switch (type) {
    case coInt:
        return from_u8((boost::format("%1%") % config.opt_int(opt_key)).str());
    case coInts: {
        const ConfigOptionInts* opt = config.opt<ConfigOptionInts>(opt_key);
        if (opt)
            return from_u8((boost::format("%1%") % opt->get_at(opt_idx)).str());
        break;
    }
    case coBool:
        return config.opt_bool(opt_key) ? "true" : "false";
    case coBools: {
        const ConfigOptionBools* opt = config.opt<ConfigOptionBools>(opt_key);
        if (opt)
            return opt->get_at(opt_idx) ? "true" : "false";
        break;
    }
    case coPercent:
        return from_u8((boost::format("%1%%%") % int(config.optptr(opt_key)->getFloat())).str());
    case coPercents: {
        const ConfigOptionPercents* opt = config.opt<ConfigOptionPercents>(opt_key);
        if (opt)
            return from_u8((boost::format("%1%%%") % int(opt->get_at(opt_idx))).str());
        break;
    }
    case coFloat:
        return double_to_string(config.opt_float(opt_key));
    case coFloats: {
        const ConfigOptionFloats* opt = config.opt<ConfigOptionFloats>(opt_key);
        if (opt)
            return double_to_string(opt->get_at(opt_idx));
        break;
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
            return get_string_from_enum<InfillPattern>(opt_key, config);
        if (opt_key == "gcode_flavor")
            return get_string_from_enum<GCodeFlavor>(opt_key, config);
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
            /*
        if (opt_key == "bed_shape") {
            config.option<ConfigOptionPoints>(opt_key)->values = boost::any_cast<std::vector<Vec2d>>(value);
            break;
        }
        ConfigOptionPoints* vec_new = new ConfigOptionPoints{ boost::any_cast<Vec2d>(value) };
        config.option<ConfigOptionPoints>(opt_key)->set_at(vec_new, opt_index, 0);
    */
        return "Points";
    }
    default:
        break;
    }
    return out;
}

void UnsavedChangesDialog::update(Preset::Type type)
{
    Tab* tab = wxGetApp().get_tab(type);
    assert(tab);

    PresetCollection* presets = tab->get_presets();
    // Display a dialog showing the dirty options in a human readable form.
    const DynamicPrintConfig& old_config = presets->get_selected_preset().config;
    const DynamicPrintConfig& new_config = presets->get_edited_preset().config;

    m_tree_model->AddPreset(type, from_u8(presets->get_edited_preset().name));

    Search::OptionsSearcher& searcher = wxGetApp().sidebar().get_searcher();
    searcher.sort_options_by_opt_key();

    // Collect dirty options.
    for (const std::string& opt_key : presets->current_dirty_options()) {
        const Search::Option& option = searcher.get_option(opt_key);

        m_tree_model->AddOption(type, option.category_local, option.group_local, option.label_local, 
                                get_string_value(opt_key, old_config), get_string_value(opt_key, new_config));
    }
}


void UnsavedChangesDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL, m_save_btn_id, m_move_btn_id, m_continue_btn_id });

    const wxSize& size = wxSize(80 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void UnsavedChangesDialog::on_sys_color_changed()
{
    // msw_rescale updates just icons, so use it
//    m_tree_model->msw_rescale();

    Refresh();
}


}

}    // namespace Slic3r::GUI
