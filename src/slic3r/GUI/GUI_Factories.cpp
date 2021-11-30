#include "libslic3r/libslic3r.h"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"

#include "GUI_Factories.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "ObjectDataViewModel.hpp"

#include "OptionsGroup.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"
#include "format.hpp"

#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/FixModelByWin10.hpp"
#ifdef __APPLE__
#include "wx/dcclient.h"
#include "slic3r/Utils/MacDarkMode.hpp"
#endif

namespace Slic3r
{
namespace GUI
{

static PrinterTechnology printer_technology()
{
    return wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology();
}

static int extruders_count()
{
    return wxGetApp().extruders_edited_cnt();
}

static bool is_improper_category(const std::string& category, const int extruders_cnt, const bool is_object_settings = true)
{
    return  category.empty() ||
        (extruders_cnt == 1 && (category == "Extruders" || category == "Wipe options")) ||
        (!is_object_settings && category == "Support material");
}


//-------------------------------------
//            SettingsFactory
//-------------------------------------

// pt_FFF
static SettingsFactory::Bundle FREQ_SETTINGS_BUNDLE_FFF =
{
    { L("Layers and Perimeters"), { "layer_height" , "perimeters", "top_solid_layers", "bottom_solid_layers" } },
    { L("Infill")               , { "fill_density", "fill_pattern" } },
    { L("Support material")     , { "support_material", "support_material_auto", "support_material_threshold",
                                    "support_material_pattern", "support_material_interface_pattern", "support_material_buildplate_only",
                                    "support_material_spacing" } },
    { L("Wipe options")         , { "wipe_into_infill", "wipe_into_objects" } }
};

// pt_SLA
static SettingsFactory::Bundle FREQ_SETTINGS_BUNDLE_SLA =
{
    { L("Pad and Support")      , { "supports_enable", "pad_enable" } }
};

std::vector<std::string> SettingsFactory::get_options(const bool is_part)
{
    if (printer_technology() == ptSLA) {
        SLAPrintObjectConfig full_sla_config;
        auto options = full_sla_config.keys();
        options.erase(find(options.begin(), options.end(), "layer_height"));
        return options;
    }

    PrintRegionConfig reg_config;
    auto options = reg_config.keys();
    if (!is_part) {
        PrintObjectConfig obj_config;
        std::vector<std::string> obj_options = obj_config.keys();
        options.insert(options.end(), obj_options.begin(), obj_options.end());
    }
    return options;
}

SettingsFactory::Bundle SettingsFactory::get_bundle(const DynamicPrintConfig* config, bool is_object_settings)
{
    auto opt_keys = config->keys();
    if (opt_keys.empty())
        return Bundle();

    // update options list according to print technology
    auto full_current_opts = get_options(!is_object_settings);
    for (int i = opt_keys.size() - 1; i >= 0; --i)
        if (find(full_current_opts.begin(), full_current_opts.end(), opt_keys[i]) == full_current_opts.end())
            opt_keys.erase(opt_keys.begin() + i);

    if (opt_keys.empty())
        return Bundle();

    const int extruders_cnt = wxGetApp().extruders_edited_cnt();

    Bundle bundle;
    for (auto& opt_key : opt_keys)
    {
        auto category = config->def()->get(opt_key)->category;
        if (is_improper_category(category, extruders_cnt, is_object_settings))
            continue;

        std::vector< std::string > new_category;

        auto& cat_opt = bundle.find(category) == bundle.end() ? new_category : bundle.at(category);
        cat_opt.push_back(opt_key);
        if (cat_opt.size() == 1)
            bundle[category] = cat_opt;
    }

    return bundle;
}

// Fill CategoryItem
std::map<std::string, std::string> SettingsFactory::CATEGORY_ICON =
{
//    settings category name      related bitmap name
    // ptFFF
    { L("Layers and Perimeters"), "layers"      },
    { L("Infill")               , "infill"      },
    { L("Ironing")              , "ironing"     },
    { L("Fuzzy Skin")           , "fuzzy_skin"  },
    { L("Support material")     , "support"     },
    { L("Speed")                , "time"        },
    { L("Extruders")            , "funnel"      },
    { L("Extrusion Width")      , "funnel"      },
    { L("Wipe options")         , "funnel"      },
    { L("Skirt and brim")       , "skirt+brim"  },
//  { L("Speed > Acceleration") , "time"        },
    { L("Advanced")             , "wrench"      },
    // ptSLA                    ,               
    { L("Supports")             , "support"     },
    { L("Pad")                  , "pad"         },
    { L("Hollowing")            , "hollowing"   }
};

wxBitmap SettingsFactory::get_category_bitmap(const std::string& category_name, bool menu_bmp /*= true*/)
{
    if (CATEGORY_ICON.find(category_name) == CATEGORY_ICON.end())
        return wxNullBitmap;
    return menu_bmp ? create_menu_bitmap(CATEGORY_ICON.at(category_name)) : create_scaled_bitmap(CATEGORY_ICON.at(category_name));
}


//-------------------------------------
//            MenuFactory
//-------------------------------------

// Note: id accords to type of the sub-object (adding volume), so sequence of the menu items is important
const std::vector<std::pair<std::string, std::string>> MenuFactory::ADD_VOLUME_MENU_ITEMS {
//       menu_item Name              menu_item bitmap name
        {L("Add part"),              "add_part" },           // ~ModelVolumeType::MODEL_PART
        {L("Add negative volume"),   "add_negative" },       // ~ModelVolumeType::NEGATIVE_VOLUME
        {L("Add modifier"),          "add_modifier"},        // ~ModelVolumeType::PARAMETER_MODIFIER
        {L("Add support blocker"),   "support_blocker"},     // ~ModelVolumeType::SUPPORT_BLOCKER
        {L("Add support enforcer"),  "support_enforcer"},    // ~ModelVolumeType::SUPPORT_ENFORCER
};

static Plater* plater()
{
    return wxGetApp().plater();
}

static ObjectList* obj_list()
{
    return wxGetApp().obj_list();
}

static ObjectDataViewModel* list_model()
{
    return wxGetApp().obj_list()->GetModel();
}

static const Selection& get_selection()
{
    return plater()->canvas3D()->get_selection();
}

//				  category ->		vector 			 ( option	;  label )
typedef std::map< std::string, std::vector< std::pair<std::string, std::string> > > FullSettingsHierarchy;
static void get_full_settings_hierarchy(FullSettingsHierarchy& settings_menu, const bool is_part)
{
    auto options = SettingsFactory::get_options(is_part);

    const int extruders_cnt = extruders_count();

    DynamicPrintConfig config;
    for (auto& option : options)
    {
        auto const opt = config.def()->get(option);
        auto category = opt->category;
        if (is_improper_category(category, extruders_cnt, !is_part))
            continue;

        const std::string& label = !opt->full_label.empty() ? opt->full_label : opt->label;
        std::pair<std::string, std::string> option_label(option, label);
        std::vector< std::pair<std::string, std::string> > new_category;
        auto& cat_opt_label = settings_menu.find(category) == settings_menu.end() ? new_category : settings_menu.at(category);
        cat_opt_label.push_back(option_label);
        if (cat_opt_label.size() == 1)
            settings_menu[category] = cat_opt_label;
    }
}

static int GetSelectedChoices(  wxArrayInt& selections,
                                const wxString& message,
                                const wxString& caption,
                                const wxArrayString& choices)
{
    wxMultiChoiceDialog dialog(nullptr, message, caption, choices);
    wxGetApp().UpdateDlgDarkUI(&dialog);

    // call this even if selections array is empty and this then (correctly)
    // deselects the first item which is selected by default
    dialog.SetSelections(selections);

#ifdef __APPLE__
    // Improvements for ChoiceListBox: Height of control will restect to items count
    for (auto child : dialog.GetChildren())
        if (dynamic_cast<wxListBox*>(child) && !choices.IsEmpty()) {
            wxClientDC dc(child);

            int height = dc.GetTextExtent(choices[0]).y;
            int width = 0;
            for (const auto& string : choices)
                width = std::max(width, dc.GetTextExtent(string).x);

            // calculate best size of ListBox
            height += 3 * mac_max_scaling_factor(); // extend height by margins
            width += 3 * height;                   // extend width by checkbox width and margins

            // don't make the listbox too tall (limit height to around 10 items)
            // but don't make it too small neither
            int list_height = wxMax(height * wxMin(wxMax(choices.Count(), 3), 10), 70);
            wxSize sz_best = wxSize(width, list_height);

            wxSize sz = child->GetSize();
            child->SetMinSize(sz_best);

            // extend Dialog size, if calculated best size of ListBox is bigger then its size
            wxSize dlg_sz = dialog.GetSize();
            if (int delta_x = sz_best.x - sz.x; delta_x > 0) dlg_sz.x += delta_x;
            if (int delta_y = sz_best.y - sz.y; delta_y > 0) dlg_sz.y += delta_y;
            dialog.SetSize(dlg_sz);

            break;
        }
#endif

    if (dialog.ShowModal() != wxID_OK)
    {
        // NB: intentionally do not clear the selections array here, the caller
        //     might want to preserve its original contents if the dialog was
        //     cancelled
        return -1;
    }

    selections = dialog.GetSelections();
    return static_cast<int>(selections.GetCount());
}

static wxMenu* create_settings_popupmenu(wxMenu* parent_menu, const bool is_object_settings, wxDataViewItem item/*, ModelConfig& config*/)
{
    wxMenu* menu = new wxMenu;

    FullSettingsHierarchy categories;
    get_full_settings_hierarchy(categories, !is_object_settings);

    auto get_selected_options_for_category = [categories, item](const wxString& category_name) {
        wxArrayString names;
        wxArrayInt selections;

        std::vector< std::pair<std::string, bool> > category_options;
        for (auto& cat : categories) {
            if (_(cat.first) == category_name) {
                ModelConfig& config = obj_list()->get_item_config(item);
                auto opt_keys = config.keys();

                int sel = 0;
                for (const std::pair<std::string, std::string>& pair : cat.second) {
                    names.Add(_(pair.second));
                    if (find(opt_keys.begin(), opt_keys.end(), pair.first) != opt_keys.end())
                        selections.Add(sel);
                    sel++;
                    category_options.push_back(std::make_pair(pair.first, false));
                }
                break;
            }
        }

        if (!category_options.empty() &&
            GetSelectedChoices(selections, _L("Select showing settings"), category_name, names) != -1) {
            for (auto sel : selections)
                category_options[sel].second = true;
        }
        return category_options;

#if 0
        if (selections.size() > 0)
        {
            // Add selected items to the "Quick menu"
            SettingsFactory::Bundle& freq_settings = printer_technology() == ptSLA ?
                m_freq_settings_sla : m_freq_settings_fff;
            bool changed_existing = false;

            std::vector<std::string> tmp_freq_cat = {};

            for (auto& cat : freq_settings)
            {
                if (_(cat.first) == category_name)
                {
                    std::vector<std::string>& freq_settings_category = cat.second;
                    freq_settings_category.clear();
                    freq_settings_category.reserve(selection_cnt);
                    for (auto sel : selections)
                        freq_settings_category.push_back((*settings_list)[sel].first);

                    changed_existing = true;
                    break;
                }
            }

            if (!changed_existing)
            {
                // Create new "Quick menu" item
                for (auto& cat : settings_menu)
                {
                    if (_(cat.first) == category_name)
                    {
                        freq_settings[cat.first] = std::vector<std::string>{};

                        std::vector<std::string>& freq_settings_category = freq_settings.find(cat.first)->second;
                        freq_settings_category.reserve(selection_cnt);
                        for (auto sel : selections)
                            freq_settings_category.push_back((*settings_list)[sel].first);
                        break;
                    }
                }
            }
        }
#endif
    };

    for (auto cat : categories) {
        append_menu_item(menu, wxID_ANY, _(cat.first), "",
                         [menu, item, get_selected_options_for_category](wxCommandEvent& event) {
                            std::vector< std::pair<std::string, bool> > category_options = get_selected_options_for_category(menu->GetLabel(event.GetId()));
                            obj_list()->add_category_to_settings_from_selection(category_options, item);
                         }, SettingsFactory::get_category_bitmap(cat.first), parent_menu,
                         []() { return true; }, plater());
    }

    return menu;
}

static void create_freq_settings_popupmenu(wxMenu* menu, const bool is_object_settings, wxDataViewItem item)
{
    // Add default settings bundles
    const SettingsFactory::Bundle& bundle = printer_technology() == ptFFF ? FREQ_SETTINGS_BUNDLE_FFF : FREQ_SETTINGS_BUNDLE_SLA;

    const int extruders_cnt = extruders_count();

    for (auto& category : bundle) {
        if (is_improper_category(category.first, extruders_cnt, is_object_settings))
            continue;

        append_menu_item(menu, wxID_ANY, _(category.first), "",
            [menu, item, is_object_settings, bundle](wxCommandEvent& event) {
                    wxString category_name = menu->GetLabel(event.GetId());
                    std::vector<std::string> options;
                    for (auto& category : bundle) 
                        if (category_name == _(category.first)) {
                            options = category.second;
                            break;
                        }
                    if (options.empty())
                        return;
                    // Because of we couldn't edited layer_height for ItVolume from settings list,
                    // correct options according to the selected item type : remove "layer_height" option
                    if (!is_object_settings && category_name == _("Layers and Perimeters")) {
                        const auto layer_height_it = std::find(options.begin(), options.end(), "layer_height");
                        if (layer_height_it != options.end())
                            options.erase(layer_height_it);
                    }

                    obj_list()->add_category_to_settings_from_frequent(options, item);
                },
            SettingsFactory::get_category_bitmap(category.first), menu,
            []() { return true; }, plater());
    }
#if 0
    // Add "Quick" settings bundles
    const SettingsFactory::Bundle& bundle_quick = printer_technology() == ptFFF ? m_freq_settings_fff : m_freq_settings_sla;

    for (auto& category : bundle_quick) {
        if (is_improper_category(category.first, extruders_cnt))
            continue;

        append_menu_item(menu, wxID_ANY, from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()), "",
            [menu, item, is_object_settings, bundle](wxCommandEvent& event) {
                wxString category_name = menu->GetLabel(event.GetId());
                std::vector<std::string> options;
                for (auto& category : bundle)
                    if (category_name == from_u8((boost::format(_L("Quick Add Settings (%s)")) % _(category.first)).str())) {
                        options = category.second;
                        break;
                    }
                if (options.empty())
                    return;
                // Because of we couldn't edited layer_height for ItVolume from settings list,
                // correct options according to the selected item type : remove "layer_height" option
                if (!is_object_settings) {
                    const auto layer_height_it = std::find(options.begin(), options.end(), "layer_height");
                    if (layer_height_it != options.end())
                        options.erase(layer_height_it);
                }
                obj_list()->add_category_to_settings_from_frequent(options, item);
            },
            SettingsFactory::get_category_bitmap(category.first), menu,
            [this]() { return true; }, plater());
    }
#endif
}

std::vector<wxBitmap> MenuFactory::get_volume_bitmaps()
{
    std::vector<wxBitmap> volume_bmps;
    volume_bmps.reserve(ADD_VOLUME_MENU_ITEMS.size());
    for (auto item : ADD_VOLUME_MENU_ITEMS)
        volume_bmps.push_back(create_menu_bitmap(item.second));
    return volume_bmps;
}

void MenuFactory::append_menu_item_delete(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Delete") + "\tDel", _L("Remove the selected object"),
        [](wxCommandEvent&) { plater()->remove_selected(); }, "delete", nullptr, 
        []() { return plater()->can_delete(); }, m_parent);

    menu->AppendSeparator();

}

wxMenu* MenuFactory::append_submenu_add_generic(wxMenu* menu, ModelVolumeType type) {
    auto sub_menu = new wxMenu;

    if (wxGetApp().get_mode() == comExpert && type != ModelVolumeType::INVALID) {
        append_menu_item(sub_menu, wxID_ANY, _L("Load") + " " + dots, "",
            [type](wxCommandEvent&) { obj_list()->load_subobject(type); }, "", menu);
        sub_menu->AppendSeparator();
    }

    for (auto& item : { L("Box"), L("Cylinder"), L("Sphere"), L("Slab") })
    {
        if (type == ModelVolumeType::INVALID && strncmp(item, "Slab", 4) == 0)
            continue;
        append_menu_item(sub_menu, wxID_ANY, _(item), "",
            [type, item](wxCommandEvent&) { obj_list()->load_generic_subobject(item, type); }, "", menu);
    }

    if (wxGetApp().get_mode() >= comAdvanced) {
        sub_menu->AppendSeparator();
        append_menu_item(sub_menu, wxID_ANY, _L("Gallery"), "",
            [type](wxCommandEvent&) { obj_list()->load_subobject(type, true); }, "", menu);
    }

    return sub_menu;
}

void MenuFactory::append_menu_items_add_volume(wxMenu* menu)
{
    // Update "add" items(delete old & create new)  settings popupmenu
    for (auto& item : ADD_VOLUME_MENU_ITEMS) {
        const auto settings_id = menu->FindItem(_(item.first));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }

    // Update "Height range Modifier" item (delete old & create new)
    if (const auto range_id = menu->FindItem(_L("Height range Modifier")); range_id != wxNOT_FOUND)
        menu->Destroy(range_id);

    const ConfigOptionMode mode = wxGetApp().get_mode();

    if (mode == comAdvanced) {
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)].first), "",
            [](wxCommandEvent&) { obj_list()->load_subobject(ModelVolumeType::MODEL_PART); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)].second, nullptr,
            []() { return obj_list()->is_instance_or_object_selected(); }, m_parent);
    }
    if (mode == comSimple) {
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)].first), "",
            [](wxCommandEvent&) { obj_list()->load_generic_subobject(L("Box"), ModelVolumeType::SUPPORT_ENFORCER); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)].second, nullptr,
            []() { return obj_list()->is_instance_or_object_selected(); }, m_parent);
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)].first), "",
            [](wxCommandEvent&) { obj_list()->load_generic_subobject(L("Box"), ModelVolumeType::SUPPORT_BLOCKER); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)].second, nullptr,
            []() { return obj_list()->is_instance_or_object_selected(); }, m_parent);

        return;
    }

    for (size_t type = (mode == comExpert ? 0 : 1); type < ADD_VOLUME_MENU_ITEMS.size(); type++)
    {
        auto& item = ADD_VOLUME_MENU_ITEMS[type];

        wxMenu* sub_menu = append_submenu_add_generic(menu, ModelVolumeType(type));
        append_submenu(menu, sub_menu, wxID_ANY, _(item.first), "", item.second,
            []() { return obj_list()->is_instance_or_object_selected(); }, m_parent);
    }

    append_menu_item_layers_editing(menu);
}

wxMenuItem* MenuFactory::append_menu_item_layers_editing(wxMenu* menu)
{
    return append_menu_item(menu, wxID_ANY, _L("Height range Modifier"), "",
        [](wxCommandEvent&) { obj_list()->layers_editing(); }, "edit_layers_all", menu,
        []() { return obj_list()->is_instance_or_object_selected(); }, m_parent);
}

wxMenuItem* MenuFactory::append_menu_item_settings(wxMenu* menu_)
{
    MenuWithSeparators* menu = dynamic_cast<MenuWithSeparators*>(menu_);

    const wxString menu_name = _L("Add settings");
    // Delete old items from settings popupmenu
    auto settings_id = menu->FindItem(menu_name);
    if (settings_id != wxNOT_FOUND)
        menu->Destroy(settings_id);

    for (auto& it : FREQ_SETTINGS_BUNDLE_FFF)
    {
        settings_id = menu->FindItem(_(it.first));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
    for (auto& it : FREQ_SETTINGS_BUNDLE_SLA)
    {
        settings_id = menu->FindItem(_(it.first));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
#if 0
    for (auto& it : m_freq_settings_fff)
    {
        settings_id = menu->FindItem(from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
    for (auto& it : m_freq_settings_sla)
    {
        settings_id = menu->FindItem(from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
#endif
    menu->DestroySeparators(); // delete old separators

    // If there are selected more then one instance but not all of them
    // don't add settings menu items
    const Selection& selection = get_selection();
    if ((selection.is_multiple_full_instance() && !selection.is_single_full_object()) ||
        selection.is_multiple_volume() || selection.is_mixed()) // more than one volume(part) is selected on the scene
        return nullptr;

    const auto sel_vol = obj_list()->get_selected_model_volume();
    if (sel_vol && sel_vol->type() != ModelVolumeType::MODEL_PART && sel_vol->type() != ModelVolumeType::PARAMETER_MODIFIER )
        return nullptr;

    const ConfigOptionMode mode = wxGetApp().get_mode();
    if (mode == comSimple)
        return nullptr;

    // Create new items for settings popupmenu

    if (printer_technology() == ptFFF ||
        (menu->GetMenuItems().size() > 0 && !menu->GetMenuItems().back()->IsSeparator()))
        menu->SetFirstSeparator();

    // detect itemm for adding of the setting
    ObjectList* object_list = obj_list();
    ObjectDataViewModel* obj_model = list_model();

    const wxDataViewItem sel_item = // when all instances in object are selected
                                    object_list->GetSelectedItemsCount() > 1 && selection.is_single_full_object() ?
                                    obj_model->GetItemById(selection.get_object_idx()) :
                                    object_list->GetSelection();
    if (!sel_item)
        return nullptr;

    // If we try to add settings for object/part from 3Dscene,
    // for the second try there is selected ItemSettings in ObjectList.
    // So, check if selected item isn't SettingsItem. And get a SettingsItem's parent item, if yes
    wxDataViewItem item = obj_model->GetItemType(sel_item) & itSettings ? obj_model->GetParent(sel_item) : sel_item;
    const ItemType item_type = obj_model->GetItemType(item);
    const bool is_object_settings = !(item_type& itVolume || item_type & itLayer);

    // Add frequently settings
    create_freq_settings_popupmenu(menu, is_object_settings, item);

    if (mode == comAdvanced)
        return nullptr;

    menu->SetSecondSeparator();

    // Add full settings list
    auto  menu_item = new wxMenuItem(menu, wxID_ANY, menu_name);
    menu_item->SetBitmap(create_menu_bitmap("cog"));
    menu_item->SetSubMenu(create_settings_popupmenu(menu, is_object_settings, item));

    return menu->Append(menu_item);
}

wxMenuItem* MenuFactory::append_menu_item_change_type(wxMenu* menu)
{
    return append_menu_item(menu, wxID_ANY, _L("Change type"), "",
        [](wxCommandEvent&) { obj_list()->change_part_type(); }, "", menu,
        []() {
            wxDataViewItem item = obj_list()->GetSelection();
            return item.IsOk() || obj_list()->GetModel()->GetItemType(item) == itVolume;
        }, m_parent);
}

wxMenuItem* MenuFactory::append_menu_item_instance_to_object(wxMenu* menu)
{
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _L("Set as a Separated Object"), "",
        [](wxCommandEvent&) { obj_list()->split_instances(); }, "", menu);

    /* New behavior logic:
     * 1. Split Object to several separated object, if ALL instances are selected
     * 2. Separate selected instances from the initial object to the separated object,
     *    if some (not all) instances are selected
     */
    m_parent->Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& evt)
        {
            const Selection& selection = plater()->canvas3D()->get_selection();
            evt.SetText(selection.is_single_full_object() ?
                _L("Set as a Separated Objects") : _L("Set as a Separated Object"));

            evt.Enable(plater()->can_set_instance_to_object());
        }, menu_item->GetId());

    return menu_item;
}

wxMenuItem* MenuFactory::append_menu_item_printable(wxMenu* menu)
{
    wxMenuItem* menu_item_printable = append_menu_check_item(menu, wxID_ANY, _L("Printable"), "", 
        [](wxCommandEvent& ) { obj_list()->toggle_printable_state(); }, menu);

    m_parent->Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& evt) {
        ObjectList* list = obj_list();
        wxDataViewItemArray sels;
        list->GetSelections(sels);
        wxDataViewItem frst_item = sels[0];
        ItemType type = list->GetModel()->GetItemType(frst_item);
        bool check;
        if (type != itInstance && type != itObject)
            check = false;
        else {
            int obj_idx = list->GetModel()->GetObjectIdByItem(frst_item);
            int inst_idx = type == itObject ? 0 : list->GetModel()->GetInstanceIdByItem(frst_item);
            check = list->object(obj_idx)->instances[inst_idx]->printable;
        }
            
        evt.Check(check);
        plater()->set_current_canvas_as_dirty();

    }, menu_item_printable->GetId());

    return menu_item_printable;
}

void MenuFactory::append_menu_items_osx(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Rename"), "",
        [](wxCommandEvent&) { obj_list()->rename_item(); }, "", menu);

    menu->AppendSeparator();
}

wxMenuItem* MenuFactory::append_menu_item_fix_through_netfabb(wxMenu* menu)
{
    if (!is_windows10())
        return nullptr;
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _L("Fix through the Netfabb"), "",
        [](wxCommandEvent&) { obj_list()->fix_through_netfabb(); }, "", menu,
        []() {return plater()->can_fix_through_netfabb(); }, m_parent);

    return menu_item;
}

wxMenuItem* MenuFactory::append_menu_item_simplify(wxMenu* menu)
{
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _L("Simplify model"), "",
        [](wxCommandEvent&) { obj_list()->simplify(); }, "", menu,
        []() {return plater()->can_simplify(); }, m_parent);
    menu->AppendSeparator();

    return menu_item;
}

void MenuFactory::append_menu_item_export_stl(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Export as STL") + dots, "",
        [](wxCommandEvent&) { plater()->export_stl(false, true); }, "", nullptr,
        []() {
            const Selection& selection = plater()->canvas3D()->get_selection();
            return selection.is_single_full_instance() || selection.is_single_full_object();
        }, m_parent);
    menu->AppendSeparator();
}

void MenuFactory::append_menu_item_reload_from_disk(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Reload from disk"), _L("Reload the selected volumes from disk"),
        [](wxCommandEvent&) { plater()->reload_from_disk(); }, "", menu,
        []() { return plater()->can_reload_from_disk(); }, m_parent);
}

void MenuFactory::append_menu_item_replace_with_stl(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Replace with STL"), _L("Replace the selected volume with new STL"),
        [](wxCommandEvent&) { plater()->replace_with_stl(); }, "", menu,
        []() { return plater()->can_replace_with_stl(); }, m_parent);
}

void MenuFactory::append_menu_item_change_extruder(wxMenu* menu)
{
    const std::vector<wxString> names = { _L("Change extruder"), _L("Set extruder for selected items") };
    // Delete old menu item
    for (const wxString& name : names) {
        const int item_id = menu->FindItem(name);
        if (item_id != wxNOT_FOUND)
            menu->Destroy(item_id);
    }

    const int extruders_cnt = extruders_count();
    if (extruders_cnt <= 1)
        return;

    wxDataViewItemArray sels;
    obj_list()->GetSelections(sels);
    if (sels.IsEmpty())
        return;

    if (sels.Count() == 1) {
        const auto sel_vol = obj_list()->get_selected_model_volume();
        if (sel_vol && sel_vol->type() != ModelVolumeType::MODEL_PART && sel_vol->type() != ModelVolumeType::PARAMETER_MODIFIER)
            return;
    }

    std::vector<wxBitmap*> icons = get_extruder_color_icons(true);
    wxMenu* extruder_selection_menu = new wxMenu();
    const wxString& name = sels.Count() == 1 ? names[0] : names[1];

    int initial_extruder = -1; // negative value for multiple object/part selection
    if (sels.Count() == 1) {
        const ModelConfig& config = obj_list()->get_item_config(sels[0]);
        initial_extruder = config.has("extruder") ? config.extruder() : 0;
    }

    for (int i = 0; i <= extruders_cnt; i++)
    {
        bool is_active_extruder = i == initial_extruder;
        int icon_idx = i == 0 ? 0 : i - 1;

        const wxString& item_name = (i == 0 ? _L("Default") : wxString::Format(_L("Extruder %d"), i)) +
            (is_active_extruder ? " (" + _L("active") + ")" : "");

        append_menu_item(extruder_selection_menu, wxID_ANY, item_name, "",
            [i](wxCommandEvent&) { obj_list()->set_extruder_for_selected_items(i); }, *icons[icon_idx], menu,
            [is_active_extruder]() { return !is_active_extruder; }, m_parent);

    }

    append_submenu(menu, extruder_selection_menu, wxID_ANY, name, _L("Use another extruder"),
        "edit_uni"/* : "change_extruder"*/, []() {return true; }, m_parent);

//    menu->AppendSubMenu(extruder_selection_menu, name);
}

void MenuFactory::append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu)
{
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
    append_menu_item(menu, wxID_ANY, _L("Scale to print volume"), _L("Scale the selected object to fit the print volume"),
        [](wxCommandEvent&) { plater()->scale_selection_to_fit_print_volume(); }, "", menu,
        []() { return plater()->can_scale_to_print_volume(); }, m_parent);
#else
    append_menu_item(menu, wxID_ANY, _L("Scale to print volume"), _L("Scale the selected object to fit the print volume"),
        [](wxCommandEvent&) { plater()->scale_selection_to_fit_print_volume(); }, "", menu);
#endif // ENABLE_ENHANCED_PRINT_VOLUME_FIT
}

void MenuFactory::append_menu_items_convert_unit(wxMenu* menu, int insert_pos/* = 1*/)
{
    std::vector<int> obj_idxs, vol_idxs;
    obj_list()->get_selection_indexes(obj_idxs, vol_idxs);
    if (obj_idxs.empty() && vol_idxs.empty())
        return;

    auto volume_respects_conversion = [](ModelVolume* volume, ConversionType conver_type)
    {
        return  (conver_type == ConversionType::CONV_FROM_INCH && volume->source.is_converted_from_inches) ||
            (conver_type == ConversionType::CONV_TO_INCH && !volume->source.is_converted_from_inches) ||
            (conver_type == ConversionType::CONV_FROM_METER && volume->source.is_converted_from_meters) ||
            (conver_type == ConversionType::CONV_TO_METER && !volume->source.is_converted_from_meters);
    };

    auto can_append = [obj_idxs, vol_idxs, volume_respects_conversion](ConversionType conver_type)
    {
        ModelObjectPtrs objects;
        for (int obj_idx : obj_idxs) {
            ModelObject* object = obj_list()->object(obj_idx);
            if (vol_idxs.empty()) {
                for (ModelVolume* volume : object->volumes)
                    if (volume_respects_conversion(volume, conver_type))
                        return false;
            }
            else {
                for (int vol_idx : vol_idxs)
                    if (volume_respects_conversion(object->volumes[vol_idx], conver_type))
                        return false;
            }
        }
        return true;
    };

    std::vector<std::pair<ConversionType, wxString>> items = {
        {ConversionType::CONV_FROM_INCH , _L("Convert from imperial units") },
        {ConversionType::CONV_TO_INCH   , _L("Revert conversion from imperial units") },
        {ConversionType::CONV_FROM_METER, _L("Convert from meters") },
        {ConversionType::CONV_TO_METER  , _L("Revert conversion from meters") } };

    for (auto item : items) {
        int menu_id = menu->FindItem(item.second);
        if (can_append(item.first)) {
            // Add menu item if it doesn't exist
            if (menu_id == wxNOT_FOUND)
                append_menu_item(menu, wxID_ANY, item.second, item.second,
                    [item](wxCommandEvent&) { plater()->convert_unit(item.first); }, "", menu,
                    []() { return true; }, m_parent, insert_pos);
        }
        else if (menu_id != wxNOT_FOUND) {
            // Delete menu item
            menu->Destroy(menu_id);
        }
    }
}

void MenuFactory::append_menu_item_merge_to_multipart_object(wxMenu* menu)
{
    menu->AppendSeparator();
    append_menu_item(menu, wxID_ANY, _L("Merge"), _L("Merge objects to the one multipart object"),
        [](wxCommandEvent&) { obj_list()->merge(true); }, "", menu,
        []() { return obj_list()->can_merge_to_multipart_object(); }, m_parent);
}
/*
void MenuFactory::append_menu_item_merge_to_single_object(wxMenu* menu)
{
    menu->AppendSeparator();
    append_menu_item(menu, wxID_ANY, _L("Merge"), _L("Merge objects to the one single object"),
        [](wxCommandEvent&) { obj_list()->merge(false); }, "", menu,
        []() { return obj_list()->can_merge_to_single_object(); }, m_parent);
}
*/
void MenuFactory::append_menu_items_mirror(wxMenu* menu)
{
    wxMenu* mirror_menu = new wxMenu();
    if (!mirror_menu)
        return;

    append_menu_item(mirror_menu, wxID_ANY, _L("Along X axis"), _L("Mirror the selected object along the X axis"),
        [](wxCommandEvent&) { plater()->mirror(X); }, "mark_X", menu);
    append_menu_item(mirror_menu, wxID_ANY, _L("Along Y axis"), _L("Mirror the selected object along the Y axis"),
        [](wxCommandEvent&) { plater()->mirror(Y); }, "mark_Y", menu);
    append_menu_item(mirror_menu, wxID_ANY, _L("Along Z axis"), _L("Mirror the selected object along the Z axis"),
        [](wxCommandEvent&) { plater()->mirror(Z); }, "mark_Z", menu);

    append_submenu(menu, mirror_menu, wxID_ANY, _L("Mirror"), _L("Mirror the selected object"), "",
        []() { return plater()->can_mirror(); }, m_parent);
}

MenuFactory::MenuFactory()
{
    for (int i = 0; i < mtCount; i++) {
        items_increase[i] = nullptr;
        items_decrease[i] = nullptr;
        items_set_number_of_copies[i] = nullptr;
    }
}

void MenuFactory::create_default_menu()
{
    wxMenu* sub_menu = append_submenu_add_generic(&m_default_menu, ModelVolumeType::INVALID);
    append_submenu(&m_default_menu, sub_menu, wxID_ANY, _L("Add Shape"), "", "add_part",
        []() {return true; }, m_parent);
}

void MenuFactory::create_common_object_menu(wxMenu* menu)
{
#ifdef __WXOSX__  
    append_menu_items_osx(menu);
#endif // __WXOSX__
    append_menu_items_instance_manipulation(menu);
    // Delete menu was moved to be after +/- instace to make it more difficult to be selected by mistake.
    append_menu_item_delete(menu);
    append_menu_item_instance_to_object(menu);
    menu->AppendSeparator();

    append_menu_item_printable(menu);
    menu->AppendSeparator();

    append_menu_item_reload_from_disk(menu);
    append_menu_item_replace_with_stl(menu);
    append_menu_item_export_stl(menu);
    // "Scale to print volume" makes a sense just for whole object
    append_menu_item_scale_selection_to_fit_print_volume(menu);

    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_simplify(menu);
    append_menu_items_mirror(menu);
}

void MenuFactory::create_object_menu()
{
    create_common_object_menu(&m_object_menu);
    wxMenu* split_menu = new wxMenu();
    if (!split_menu)
        return;

    append_menu_item(split_menu, wxID_ANY, _L("To objects"), _L("Split the selected object into individual objects"),
        [](wxCommandEvent&) { plater()->split_object(); }, "split_object_SMALL", &m_object_menu, 
        []() { return plater()->can_split(true); }, m_parent);
    append_menu_item(split_menu, wxID_ANY, _L("To parts"), _L("Split the selected object into individual parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts_SMALL", &m_object_menu, 
        []() { return plater()->can_split(false); }, m_parent);

    append_submenu(&m_object_menu, split_menu, wxID_ANY, _L("Split"), _L("Split the selected object"), "",
        []() { return plater()->can_split(true) && wxGetApp().get_mode() > comSimple; }, m_parent);
    m_object_menu.AppendSeparator();

    // "Height range Modifier" and "Add (volumes)" menu items will be added later in append_menu_items_add_volume()
}

void MenuFactory::create_sla_object_menu()
{
    create_common_object_menu(&m_sla_object_menu);
    append_menu_item(&m_sla_object_menu, wxID_ANY, _L("Split"), _L("Split the selected object into individual objects"),
        [](wxCommandEvent&) { plater()->split_object(); }, "split_object_SMALL", nullptr,
        []() { return plater()->can_split(true); }, m_parent);

    m_sla_object_menu.AppendSeparator();
}

void MenuFactory::create_part_menu()
{
    wxMenu* menu = &m_part_menu;
#ifdef __WXOSX__  
    append_menu_items_osx(menu);
#endif // __WXOSX__
    append_menu_item_delete(menu);
    append_menu_item_reload_from_disk(menu);
    append_menu_item_replace_with_stl(menu);
    append_menu_item_export_stl(menu);
    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_simplify(menu);
    append_menu_items_mirror(menu);

    append_menu_item(menu, wxID_ANY, _L("Split"), _L("Split the selected object into individual parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts_SMALL", nullptr, 
        []() { return plater()->can_split(false); }, m_parent);

    menu->AppendSeparator();
    append_menu_item_change_type(menu);

}

void MenuFactory::create_instance_menu()
{
    wxMenu* menu = &m_instance_menu;
    // create "Instance to Object" menu item
    append_menu_item_instance_to_object(menu);
    append_menu_item_printable(menu);
}

void MenuFactory::init(wxWindow* parent)
{
    m_parent = parent;

    create_default_menu();
    create_object_menu();
    create_sla_object_menu();
    create_part_menu();
    create_instance_menu();
}

void MenuFactory::update()
{
    update_default_menu();
    update_object_menu();
}

wxMenu* MenuFactory::default_menu()
{
    return &m_default_menu;
}

wxMenu* MenuFactory::object_menu()
{
    append_menu_items_convert_unit(&m_object_menu, 11);
    append_menu_item_settings(&m_object_menu);
    append_menu_item_change_extruder(&m_object_menu);
    update_menu_items_instance_manipulation(mtObjectFFF);

    return &m_object_menu;
}

wxMenu* MenuFactory::sla_object_menu()
{
    append_menu_items_convert_unit(&m_sla_object_menu, 11);
    append_menu_item_settings(&m_sla_object_menu);
    update_menu_items_instance_manipulation(mtObjectSLA);

    return &m_sla_object_menu;
}

wxMenu* MenuFactory::part_menu()
{
    append_menu_items_convert_unit(&m_part_menu, 2);
    append_menu_item_settings(&m_part_menu);
    append_menu_item_change_extruder(&m_part_menu);

    return &m_part_menu;
}

wxMenu* MenuFactory::instance_menu()
{
    return &m_instance_menu;
}

wxMenu* MenuFactory::layer_menu()
{
    MenuWithSeparators* menu = new MenuWithSeparators();
    append_menu_item_settings(menu);

    return menu;
}

wxMenu* MenuFactory::multi_selection_menu()
{
    wxDataViewItemArray sels;
    obj_list()->GetSelections(sels);

    for (const wxDataViewItem& item : sels)
        if (!(list_model()->GetItemType(item) & (itVolume | itObject | itInstance)))
            // show this menu only for Objects(Instances mixed with Objects)/Volumes selection
            return nullptr;

    wxMenu* menu = new MenuWithSeparators();

    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_reload_from_disk(menu);
    append_menu_items_convert_unit(menu);
    if (obj_list()->can_merge_to_multipart_object())
        append_menu_item_merge_to_multipart_object(menu);
    if (extruders_count() > 1)
        append_menu_item_change_extruder(menu);
    if (list_model()->GetItemType(sels[0]) != itVolume)
        append_menu_item_printable(menu);

    return menu;
}

void MenuFactory::append_menu_items_instance_manipulation(wxMenu* menu)
{
    MenuType type = menu == &m_object_menu ? mtObjectFFF : mtObjectSLA;

    items_increase[type]                = append_menu_item(menu, wxID_ANY, _L("Add instance") + "\t+", _L("Add one more instance of the selected object"),
        [](wxCommandEvent&) { plater()->increase_instances();      }, "add_copies", nullptr, 
        []() { return plater()->can_increase_instances(); }, m_parent);
    items_decrease[type]                = append_menu_item(menu, wxID_ANY, _L("Remove instance") + "\t-", _L("Remove one instance of the selected object"),
        [](wxCommandEvent&) { plater()->decrease_instances();      }, "remove_copies", nullptr, 
        []() { return plater()->can_decrease_instances(); }, m_parent);
    items_set_number_of_copies[type]    = append_menu_item(menu, wxID_ANY, _L("Set number of instances") + dots, _L("Change the number of instances of the selected object"),
        [](wxCommandEvent&) { plater()->set_number_of_copies();    }, "number_of_copies", nullptr,
        []() { return plater()->can_increase_instances(); }, m_parent);

    append_menu_item(menu, wxID_ANY, _L("Fill bed with instances") + dots, _L("Fill the remaining area of bed with instances of the selected object"),
        [](wxCommandEvent&) { plater()->fill_bed_with_instances();    }, "", nullptr, 
        []() { return plater()->can_increase_instances(); }, m_parent);

}

void MenuFactory::update_menu_items_instance_manipulation(MenuType type)
{
    wxMenu* menu = type == mtObjectFFF ? &m_object_menu : type == mtObjectSLA ? &m_sla_object_menu : nullptr;
    if (menu)
        return;
    // Remove/Prepend "increase/decrease instances" menu items according to the view mode.
    // Suppress to show those items for a Simple mode
    if (wxGetApp().get_mode() == comSimple) {
        if (menu->FindItem(_L("Add instance")) != wxNOT_FOUND)
        {
            // Detach an items from the menu, but don't delete them
            // so that they can be added back later
            // (after switching to the Advanced/Expert mode)
            menu->Remove(items_increase[type]);
            menu->Remove(items_decrease[type]);
            menu->Remove(items_set_number_of_copies[type]);
        }
    }
    else {
        if (menu->FindItem(_L("Add instance")) == wxNOT_FOUND)
        {
            // Prepend items to the menu, if those aren't not there
            menu->Prepend(items_set_number_of_copies[type]);
            menu->Prepend(items_decrease[type]);
            menu->Prepend(items_increase[type]);
        }
    }
}

void MenuFactory::update_object_menu()
{
    append_menu_items_add_volume(&m_object_menu);
}

void MenuFactory::update_default_menu()
{
    const auto menu_item_id = m_default_menu.FindItem(_("Add Shape"));
    if (menu_item_id != wxNOT_FOUND)
        m_default_menu.Destroy(menu_item_id);
    create_default_menu();
}

void MenuFactory::msw_rescale()
{
    for (MenuWithSeparators* menu : { &m_object_menu, &m_sla_object_menu, &m_part_menu, &m_default_menu })
        msw_rescale_menu(dynamic_cast<wxMenu*>(menu));
}

#ifdef _WIN32
// For this class is used code from stackoverflow:
// https://stackoverflow.com/questions/257288/is-it-possible-to-write-a-template-to-check-for-a-functions-existence
// Using this code we can to inspect of an existence of IsWheelInverted() function in class T
template <typename T>
class menu_has_update_def_colors
{
    typedef char one;
    struct two { char x[2]; };

    template <typename C> static one test(decltype(&C::UpdateDefColors));
    template <typename C> static two test(...);

public:
    static constexpr bool value = sizeof(test<T>(0)) == sizeof(char);
};

template<typename T>
static void update_menu_item_def_colors(T* item)
{
    if constexpr (menu_has_update_def_colors<wxMenuItem>::value) {
        item->UpdateDefColors();
    }
}
#endif

void MenuFactory::sys_color_changed()
{
    for (MenuWithSeparators* menu : { &m_object_menu, &m_sla_object_menu, &m_part_menu, &m_default_menu }) {
        msw_rescale_menu(dynamic_cast<wxMenu*>(menu));// msw_rescale_menu updates just icons, so use it
#ifdef _WIN32 
        // but under MSW we have to update item's bachground color
        for (wxMenuItem* item : menu->GetMenuItems())
            update_menu_item_def_colors(item);
#endif
    }
}

void MenuFactory::sys_color_changed(wxMenuBar* menubar)
{
    for (size_t id = 0; id < menubar->GetMenuCount(); id++) {
        wxMenu* menu = menubar->GetMenu(id);
        msw_rescale_menu(menu);
#ifdef _WIN32 
        // but under MSW we have to update item's bachground color
        for (wxMenuItem* item : menu->GetMenuItems())
            update_menu_item_def_colors(item);
#endif
    }
    menubar->Refresh();
}


} //namespace GUI
} //namespace Slic3r 
