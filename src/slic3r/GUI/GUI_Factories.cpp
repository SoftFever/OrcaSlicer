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
//BBS: add partplate related logic
#include "PartPlate.hpp"

#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/FixModelByWin10.hpp"

namespace Slic3r
{
namespace GUI
{

static PrinterTechnology printer_technology()
{
    return wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology();
}

static int filaments_count()
{
    return wxGetApp().filaments_cnt();
}

static bool is_improper_category(const std::string& category, const int filaments_cnt, const bool is_object_settings = true)
{
    return  category.empty() ||
        (filaments_cnt == 1 && (category == "Extruders" || category == "Wipe options")) ||
        (!is_object_settings && category == "Support material");
}


//-------------------------------------
//            SettingsFactory
//-------------------------------------

// pt_FFF
static SettingsFactory::Bundle FREQ_SETTINGS_BUNDLE_FFF =
{
    //BBS
    { L("Quality"), { "layer_height" } },
    { L("Shell"), { "wall_loops", "top_shell_layers", "bottom_shell_layers"} },
    { L("Infill")               , { "sparse_infill_density", "sparse_infill_pattern" } },
    // BBS
    { L("Support")     , { "enable_support", "support_type", "support_threshold_angle",
                                    "support_base_pattern", "support_on_build_plate_only","support_critical_regions_only",
                                    "support_base_pattern_spacing", "support_expansion"}},
    //BBS
    { L("Flush options")         , { "flush_into_infill", "flush_into_objects", "flush_into_support"} }
};

// pt_SLA
static SettingsFactory::Bundle FREQ_SETTINGS_BUNDLE_SLA =
{
    // BBS: remove SLA freq settings
};

//BBS: add setting data for table
std::map<std::string, std::vector<SimpleSettingData>>  SettingsFactory::OBJECT_CATEGORY_SETTINGS=
{
    { L("Quality"), {{"layer_height", "",1},
                    //{"initial_layer_print_height", "",2},
                    {"seam_position", "",2},
                    {"slice_closing_radius", "",3}, {"resolution", "",4},
                    {"xy_hole_compensation", "",5}, {"xy_contour_compensation", "",6}, {"elefant_foot_compensation", "",7}
                    }},
    { L("Support"), {{"brim_type", "",1},{"brim_width", "",2},{"brim_object_gap", "",3},
                    {"enable_support", "",4},{"support_type", "",5},{"support_threshold_angle", "",6},{"support_on_build_plate_only", "",7},
                    {"support_filament", "",8},{"support_interface_filament", "",9},{"support_expansion", "",24},{"support_style", "",25},
                     {"tree_support_branch_angle", "",10}, {"tree_support_wall_count", "",11},//tree support
                            {"support_top_z_distance", "",13},{"support_bottom_z_distance", "",12},{"support_base_pattern", "",14},{"support_base_pattern_spacing", "",15},
                            {"support_interface_top_layers", "",16},{"support_interface_bottom_layers", "",17},{"support_interface_spacing", "",18},{"support_bottom_interface_spacing", "",19},
                            {"support_object_xy_distance", "",20}, {"bridge_no_support", "",21},{"max_bridge_length", "",22},{"support_critical_regions_only", "",23}
                            }},
    { L("Speed"), {{"support_speed", "",12}, {"support_interface_speed", "",13}
                    }}
};

std::map<std::string, std::vector<SimpleSettingData>>  SettingsFactory::PART_CATEGORY_SETTINGS=
{
    { L("Quality"), {{"ironing_type", "",8},{"ironing_flow", "",9},{"ironing_spacing", "",10},{"bridge_flow", "",11},{"bridge_density", "", 1}
                    }},
    { L("Strength"), {{"wall_loops", "",1},{"top_shell_layers", L("Top Solid Layers"),1},{"top_shell_thickness", L("Top Minimum Shell Thickness"),1},
                    {"bottom_shell_layers", L("Bottom Solid Layers"),1}, {"bottom_shell_thickness", L("Bottom Minimum Shell Thickness"),1},
                    {"sparse_infill_density", "",1},{"sparse_infill_pattern", "",1},{"top_surface_pattern", "",1},{"bottom_surface_pattern", "",1},
                    {"infill_combination", "",1}, {"infill_wall_overlap", "",1}, {"infill_direction", "",1}, {"bridge_angle", "",1}, {"minimum_sparse_infill_area", "",1}
                    }},
    { L("Speed"), {{"outer_wall_speed", "",1},{"inner_wall_speed", "",2},{"sparse_infill_speed", "",3},{"top_surface_speed", "",4}, {"internal_solid_infill_speed", "",5},
                    {"enable_overhang_speed", "",6}, {"overhang_speed_classic", "",6}, {"overhang_1_4_speed", "",7}, {"overhang_2_4_speed", "",8}, {"overhang_3_4_speed", "",9}, {"overhang_4_4_speed", "",10},
                    {"bridge_speed", "",11}, {"gap_infill_speed", "",12}
                    }}
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

std::vector<SimpleSettingData> SettingsFactory::get_visible_options(const std::string& category, const bool is_part)
{
    /*t_config_option_keys options = {
        //Quality
        "wall_infill_order", "ironing_type", "inner_wall_line_width", "outer_wall_line_width", "top_surface_line_width",
        //Shell
        "wall_loops", "top_shell_layers", "bottom_shell_layers", "top_shell_thickness", "bottom_shell_thickness",
        //Infill
        "sparse_infill_density", "sparse_infill_pattern", "top_surface_pattern", "bottom_surface_pattern", "infill_combination", "infill_direction", "infill_wall_overlap",
        //speed
        "inner_wall_speed", "outer_wall_speed", "sparse_infill_speed", "internal_solid_infill_speed", "top_surface_speed", "gap_infill_speed"
        };

    t_config_option_keys object_options = {
        //Quality
        "layer_height", "initial_layer_print_height", "adaptive_layer_height", "seam_position", "xy_hole_compensation", "xy_contour_compensation", "elefant_foot_compensation", "support_line_width",
        //Support
        "enable_support", "support_type", "support_threshold_angle", "support_on_build_plate_only", "support_critical_regions_only", "enforce_support_layers",
        //tree support
        "tree_support_wall_count",
        //support
        "support_top_z_distance", "support_base_pattern", "support_base_pattern_spacing", "support_interface_top_layers", "support_interface_bottom_layers", "support_interface_spacing", "support_bottom_interface_spacing", "support_object_xy_distance",
        //adhesion
        "brim_type", "brim_width", "brim_object_gap", "raft_layers"
        };*/
    std::vector<SimpleSettingData> options;
    std::map<std::string, std::vector<SimpleSettingData>>::iterator it;

    it = PART_CATEGORY_SETTINGS.find(category);
    if (it != PART_CATEGORY_SETTINGS.end())
    {
        options = PART_CATEGORY_SETTINGS[category];
    }

    if (!is_part) {
        it = OBJECT_CATEGORY_SETTINGS.find(category);
        if (it != OBJECT_CATEGORY_SETTINGS.end())
            options.insert(options.end(), OBJECT_CATEGORY_SETTINGS[category].begin(), OBJECT_CATEGORY_SETTINGS[category].end());
    }

    auto sort_func = [](SimpleSettingData& setting1, SimpleSettingData& setting2) {
        return (setting1.priority < setting2.priority);
    };
    std::sort(options.begin(), options.end(), sort_func);
    return options;
}

std::map<std::string, std::vector<SimpleSettingData>> SettingsFactory::get_all_visible_options(const bool is_part)
{
    std::map<std::string, std::vector<SimpleSettingData>> option_maps;
    std::map<std::string, std::vector<SimpleSettingData>>::iterator it1, it2;

    option_maps = PART_CATEGORY_SETTINGS;
    if (!is_part) {
        for (it1 = OBJECT_CATEGORY_SETTINGS.begin(); it1 != OBJECT_CATEGORY_SETTINGS.end(); it1++)
        {
            std::string category = it1->first;
            it2 = PART_CATEGORY_SETTINGS.find(category);
            if (it2 != PART_CATEGORY_SETTINGS.end())
            {
                std::vector<SimpleSettingData>& options = option_maps[category];
                options.insert(options.end(), it1->second.begin(), it1->second.end());

                auto sort_func = [](SimpleSettingData& setting1, SimpleSettingData& setting2) {
                    return (setting1.priority < setting2.priority);
                };
                std::sort(options.begin(), options.end(), sort_func);
            }
            else {
                option_maps.insert(*it1);
            }
        }
    }

    return option_maps;
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

    const int filaments_cnt = wxGetApp().filaments_cnt();

    Bundle bundle;
    for (auto& opt_key : opt_keys)
    {
        auto category = config->def()->get(opt_key)->category;
        if (is_improper_category(category, filaments_cnt, is_object_settings))
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
    { L("Quality")              , "blank2"      },
    { L("Shell")                , "blank_14"    },
    { L("Infill")               , "blank_14"    },
    { L("Ironing")              , "blank_14"    },
    { L("Fuzzy Skin")           , "menu_fuzzy_skin"  },
    { L("Support")              , "support"     },
    { L("Speed")                , "blank_14"    },
    { L("Extruders")            , "blank_14"    },
    { L("Extrusion Width")      , "blank_14"    },
    { L("Wipe options")         , "blank_14"    },
    { L("Bed adhension")        , "blank_14"    },
//  { L("Speed > Acceleration") , "time"        },
    { L("Advanced")             , "blank_14"    },
    // BBS: remove SLA categories
};

wxBitmap SettingsFactory::get_category_bitmap(const std::string& category_name, bool menu_bmp)
{
    if (CATEGORY_ICON.find(category_name) == CATEGORY_ICON.end())
        return wxNullBitmap;
    return create_scaled_bitmap(CATEGORY_ICON.at(category_name));
}


//-------------------------------------
//            MenuFactory
//-------------------------------------

// Note: id accords to type of the sub-object (adding volume), so sequence of the menu items is important
#ifdef __WINDOWS__
const std::vector<std::pair<std::string, std::string>> MenuFactory::ADD_VOLUME_MENU_ITEMS = {
        {L("Add part"),              "menu_add_part" },           // ~ModelVolumeType::MODEL_PART
        {L("Add negative part"),     "menu_add_negative" },       // ~ModelVolumeType::NEGATIVE_VOLUME
        {L("Add modifier"),          "menu_add_modifier"},         // ~ModelVolumeType::PARAMETER_MODIFIER
        {L("Add support blocker"),   "menu_support_blocker"},     // ~ModelVolumeType::SUPPORT_BLOCKER
        {L("Add support enforcer"),  "menu_support_enforcer"}     // ~ModelVolumeType::SUPPORT_ENFORCER
};
#else
const std::vector<std::pair<std::string, std::string>> MenuFactory::ADD_VOLUME_MENU_ITEMS = {
        {L("Add part"),              "menu_add_part" },                 // ~ModelVolumeType::MODEL_PART
        {L("Add negative part"),     "menu_add_negative" },         // ~ModelVolumeType::NEGATIVE_VOLUME
        {L("Add modifier"),          "menu_add_modifier"},           // ~ModelVolumeType::PARAMETER_MODIFIER
        {L("Add support blocker"),   "menu_support_blocker"},    // ~ModelVolumeType::SUPPORT_BLOCKER
        {L("Add support enforcer"),  "menu_support_enforcer"}   // ~ModelVolumeType::SUPPORT_ENFORCER
};

#endif

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
    return plater()->get_current_canvas3D(true)->get_selection();
}

//				  category ->		vector 			 ( option	;  label )
typedef std::map< std::string, std::vector< std::pair<std::string, std::string> > > FullSettingsHierarchy;
static void get_full_settings_hierarchy(FullSettingsHierarchy& settings_menu, const bool is_part)
{
    auto options = SettingsFactory::get_options(is_part);

    const int filaments_cnt = filaments_count();

    DynamicPrintConfig config;
    for (auto& option : options)
    {
        auto const opt = config.def()->get(option);
        auto category = opt->category;
        if (is_improper_category(category, filaments_cnt, !is_part))
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
            wxGetSelectedChoices(selections, _L("Select settings"), category_name, names) != -1) {
            for (auto sel : selections)
                category_options[sel].second = true;
        }
        return category_options;
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

    const int filaments_cnt = filaments_count();

    for (auto& category : bundle) {
        if (is_improper_category(category.first, filaments_cnt, is_object_settings))
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
                    if (!is_object_settings && category_name == _("Quality")) {
                        const auto layer_height_it = std::find(options.begin(), options.end(), "layer_height");
                        if (layer_height_it != options.end())
                            options.erase(layer_height_it);
                    }

                    obj_list()->add_category_to_settings_from_frequent(options, item);
                },
            SettingsFactory::get_category_bitmap(category.first), menu,
            []() { return true; }, plater());
    }
}

std::vector<wxBitmap> MenuFactory::get_volume_bitmaps()
{
    std::vector<wxBitmap> volume_bmps;
    volume_bmps.reserve(ADD_VOLUME_MENU_ITEMS.size());
    for (auto item : ADD_VOLUME_MENU_ITEMS){
        if(!item.second.empty()){
            volume_bmps.push_back(create_scaled_bitmap(item.second));
        }
    }
    return volume_bmps;
}

void MenuFactory::append_menu_item_set_visible(wxMenu* menu)
{
    bool has_one_shown = false;
    const Selection& selection = plater()->canvas3D()->get_selection();
    for (unsigned int i : selection.get_volume_idxs()) {
        has_one_shown |= selection.get_volume(i)->visible;
    }

    append_menu_item(menu, wxID_ANY, has_one_shown ?_L("Hide") : _L("Show"), "",
        [has_one_shown](wxCommandEvent&) { plater()->set_selected_visible(!has_one_shown); }, "", nullptr,
        []() { return true; }, m_parent);
}

void MenuFactory::append_menu_item_delete(wxMenu* menu)
{
#ifdef __WINDOWS__
    append_menu_item(menu, wxID_ANY, _L("Delete") + "\tDel", _L("Delete the selected object"),
        [](wxCommandEvent&) { plater()->remove_selected(); }, "menu_delete", nullptr,
        []() { return plater()->can_delete(); }, m_parent);
#else
    append_menu_item(menu, wxID_ANY, _L("Delete") + "\tBackSpace", _L("Delete the selected object"),
        [](wxCommandEvent&) { plater()->remove_selected(); }, "", nullptr,
        []() { return plater()->can_delete(); }, m_parent);
#endif
}

void MenuFactory::append_menu_item_edit_text(wxMenu *menu)
{
#ifdef __WINDOWS__
    append_menu_item(
        menu, wxID_ANY, _L("Edit Text"), "", [](wxCommandEvent &) { plater()->edit_text(); }, "", nullptr,
        []() { return plater()->can_edit_text(); }, m_parent);
#else
    append_menu_item(
        menu, wxID_ANY, _L("Edit Text"), "", [](wxCommandEvent &) { plater()->edit_text(); }, "", nullptr,
        []() { return plater()->can_edit_text(); }, m_parent);
#endif
}

wxMenu* MenuFactory::append_submenu_add_generic(wxMenu* menu, ModelVolumeType type) {
    auto sub_menu = new wxMenu;

    if (type != ModelVolumeType::INVALID) {
        append_menu_item(sub_menu, wxID_ANY, _L("Load..."), "",
            [type](wxCommandEvent&) { obj_list()->load_subobject(type); }, "", menu);
        sub_menu->AppendSeparator();
    }

    for (auto &item : {L("Cube"), L("Cylinder"), L("Sphere"), L("Cone")})
    {
        append_menu_item(sub_menu, wxID_ANY, _(item), "",
            [type, item](wxCommandEvent&) { obj_list()->load_generic_subobject(item, type); }, "", menu);
    }

    for (auto &item : {L("3DBenchy"), L("Autodesk FDM Test"), L("Voron Cube")}) {
        append_menu_item(
            sub_menu, wxID_ANY, _(item), "",
            [type, item](wxCommandEvent &) {
              std::vector<boost::filesystem::path> input_files;
              std::string file_name = item;
              if (file_name == "3DBenchy")
                file_name = "3DBenchy.stl";
              else if (file_name == "Autodesk FDM Test")
                file_name = "ksr_fdmtest_v4.stl";
              else if (file_name == "Voron Cube")
                file_name = "Voron_Design_Cube_v7.stl";
              else
                return;
              input_files.push_back(
                  (boost::filesystem::path(Slic3r::resources_dir()) / "handy_models" / file_name));
              plater()->load_files(input_files, LoadStrategy::LoadModel);
            },
            "", menu);
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

    for (size_t type = 0; type < ADD_VOLUME_MENU_ITEMS.size(); type++)
    {
        auto& item = ADD_VOLUME_MENU_ITEMS[type];

        wxMenu* sub_menu = append_submenu_add_generic(menu, ModelVolumeType(type));
        append_submenu(menu, sub_menu, wxID_ANY, _(item.first), "", item.second,
            []() { return obj_list()->is_instance_or_object_selected(); }, m_parent);
    }
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

    menu->DestroySeparators(); // delete old separators

    // If there are selected more then one instance but not all of them
    // don't add settings menu items
    const Selection& selection = get_selection();
    if ((selection.is_multiple_full_instance() && !selection.is_single_full_object()) ||
        selection.is_multiple_volume() || selection.is_mixed()) // more than one volume(part) is selected on the scene
        return nullptr;

    const auto sel_vol = obj_list()->get_selected_model_volume();
    if (sel_vol && sel_vol->type() >= ModelVolumeType::SUPPORT_ENFORCER)
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
    // BBS remvoe freq setting popupmenu
    // create_freq_settings_popupmenu(menu, is_object_settings, item);

    menu->SetSecondSeparator();

    // Add full settings list
    auto  menu_item = new wxMenuItem(menu, wxID_ANY, menu_name);
    menu_item->SetBitmap(create_scaled_bitmap("cog"));
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
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _L("Set as an individual object"), "",
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
                _L("Set as individual objects") : _L("Set as an individual object"));

            evt.Enable(plater()->can_set_instance_to_object());
        }, menu_item->GetId());

    return menu_item;
}

wxMenuItem* MenuFactory::append_menu_item_printable(wxMenu* menu)
{
    // BBS: to be checked
    wxMenuItem* menu_item_printable = append_menu_check_item(menu, wxID_ANY, _L("Printable"), "",
        [](wxCommandEvent&) { obj_list()->toggle_printable_state(); }, menu);

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

void MenuFactory::append_menu_item_rename(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Rename"), "",
        [](wxCommandEvent&) { obj_list()->rename_item(); }, "", menu);

    menu->AppendSeparator();
}

wxMenuItem* MenuFactory::append_menu_item_fix_through_netfabb(wxMenu* menu)
{
    if (!is_windows10())
        return nullptr;

    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _L("Fix model"), "",
        [](wxCommandEvent&) { obj_list()->fix_through_netfabb(); }, "", menu,
        []() {return plater()->can_fix_through_netfabb(); }, plater());

    return menu_item;
}

void MenuFactory::append_menu_item_export_stl(wxMenu* menu, bool is_mulity_menu)
{
    append_menu_item(menu, wxID_ANY, _L("Export as STL") + dots, "",
        [](wxCommandEvent&) { plater()->export_stl(false, true); }, "", nullptr,
        [is_mulity_menu]() {
            const Selection& selection = plater()->canvas3D()->get_selection();
            if (is_mulity_menu)
                return selection.is_multiple_full_instance() || selection.is_multiple_full_object();
            else
                return selection.is_single_full_instance() || selection.is_single_full_object();
        }, m_parent);
}

void MenuFactory::append_menu_item_reload_from_disk(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Reload from disk"), _L("Reload the selected parts from disk"),
        [](wxCommandEvent&) { plater()->reload_from_disk(); }, "", menu,
        []() { return plater()->can_reload_from_disk(); }, m_parent);
}

void MenuFactory::append_menu_item_replace_with_stl(wxMenu *menu)
{
    append_menu_item(menu, wxID_ANY, _L("Replace with STL"), _L("Replace the selected part with new STL"),
        [](wxCommandEvent &) { plater()->replace_with_stl(); }, "", menu,
        []() { return plater()->can_replace_with_stl(); }, m_parent);
}

void MenuFactory::append_menu_item_change_extruder(wxMenu* menu)
{
    // BBS
    const std::vector<wxString> names = { _L("Change filament"), _L("Set filament for selected items") };
    // Delete old menu item
    for (const wxString& name : names) {
        const int item_id = menu->FindItem(name);
        if (item_id != wxNOT_FOUND)
            menu->Destroy(item_id);
    }

    const int filaments_cnt = filaments_count();
    if (filaments_cnt <= 1)
        return;

    wxDataViewItemArray sels;
    obj_list()->GetSelections(sels);
    if (sels.IsEmpty())
        return;

    std::vector<wxBitmap*> icons = get_extruder_color_icons(true);
    wxMenu* extruder_selection_menu = new wxMenu();
    const wxString& name = sels.Count() == 1 ? names[0] : names[1];

    int initial_extruder = -1; // negative value for multiple object/part selection
    if (sels.Count() == 1) {
        const ModelConfig& config = obj_list()->get_item_config(sels[0]);
        // BBS: set default extruder to 1
        initial_extruder = config.has("extruder") ? config.extruder() : 1;
    }

    for (int i = 0; i <= filaments_cnt; i++)
    {
        bool is_active_extruder = i == initial_extruder;
        int icon_idx = i == 0 ? 0 : i - 1;

        const wxString& item_name = (i == 0 ? _L("Default") : wxString::Format(_L("Filament %d"), i)) +
            (is_active_extruder ? " (" + _L("active") + ")" : "");

        if (icon_idx >= 0 && icon_idx < icons.size()) {
            append_menu_item(
                extruder_selection_menu, wxID_ANY, item_name, "", [i](wxCommandEvent &) { obj_list()->set_extruder_for_selected_items(i); }, *icons[icon_idx], menu,
                [is_active_extruder]() { return !is_active_extruder; }, m_parent);
        } else {
            append_menu_item(
                extruder_selection_menu, wxID_ANY, item_name, "", [i](wxCommandEvent &) { obj_list()->set_extruder_for_selected_items(i); }, "", menu,
                [is_active_extruder]() { return !is_active_extruder; }, m_parent);
        }
    }

    menu->AppendSubMenu(extruder_selection_menu, name);
}

void MenuFactory::append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Scale to build volume"), _L("Scale an object to fit the build volume"),
        [](wxCommandEvent&) { plater()->scale_selection_to_fit_print_volume(); }, "", menu);
}

void MenuFactory::append_menu_items_flush_options(wxMenu* menu)
{
    const wxString name = _L("Flush Options");
    // Delete old menu item
    const int item_id = menu->FindItem(name);
    if (item_id != wxNOT_FOUND)
        menu->Destroy(item_id);

    bool show_flush_option_menu = false;
    ObjectList* object_list = obj_list();
    const Selection& selection = get_selection();
    if (selection.get_object_idx() < 0)
        return;
    if (wxGetApp().plater()->get_partplate_list().get_curr_plate()->contains(selection.get_bounding_box())) {
        auto plate_extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_extruders();
        for (auto extruder : plate_extruders) {
            if (extruder != plate_extruders[0])
                show_flush_option_menu = true;
        }
    }
    if (!show_flush_option_menu)
        return;

    DynamicPrintConfig& global_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    ModelConfig& select_object_config = object_list->object(selection.get_object_idx())->config;

    auto keys = select_object_config.keys();
    for (auto key : FREQ_SETTINGS_BUNDLE_FFF["Flush options"]) {
        if (find(keys.begin(), keys.end(), key) == keys.end()) {
            const ConfigOption* option = global_config.option(key);
            select_object_config.set_key_value(key, option->clone());
        }
    }


    wxMenu* flush_options_menu = new wxMenu();
    append_menu_check_item(flush_options_menu, wxID_ANY, _L("Flush into objects' infill"), "",
        [&select_object_config](wxCommandEvent&) {
            const ConfigOption* option = select_object_config.option(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][0]);
            select_object_config.set_key_value(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][0], new ConfigOptionBool(!option->getBool()));
            wxGetApp().obj_settings()->UpdateAndShow(true);
        }, menu, []() {return true; }, [&select_object_config]() {const ConfigOption* option = select_object_config.option(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][0]); return option->getBool(); }, m_parent);

    append_menu_check_item(flush_options_menu, wxID_ANY, _L("Flush into this object"), "",
        [&select_object_config](wxCommandEvent&) {
            const ConfigOption* option = select_object_config.option(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][1]);
            select_object_config.set_key_value(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][1], new ConfigOptionBool(!option->getBool()));
            wxGetApp().obj_settings()->UpdateAndShow(true);
        }, menu, []() {return true; }, [&select_object_config]() {const ConfigOption* option = select_object_config.option(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][1]); return option->getBool(); }, m_parent);

    append_menu_check_item(flush_options_menu, wxID_ANY, _L("Flush into objects' support"), "",
        [&select_object_config](wxCommandEvent&) {
            const ConfigOption* option = select_object_config.option(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][2]);
            select_object_config.set_key_value(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][2], new ConfigOptionBool(!option->getBool()));
            wxGetApp().obj_settings()->UpdateAndShow(true);
        }, menu, []() {return true; }, [&select_object_config]() {const ConfigOption* option = select_object_config.option(FREQ_SETTINGS_BUNDLE_FFF["Flush options"][2]); return option->getBool(); }, m_parent);

    size_t i = 0;
    for (auto node = menu->GetMenuItems().GetFirst(); node; node = node->GetNext())
    {
        i++;
        wxMenuItem* item = node->GetData();
        if (item->GetItemLabelText() == _L("Edit in Parameter Table"))
            break;
    }
    menu->Insert(i, wxID_ANY, _L("Flush Options"), flush_options_menu);
}

void MenuFactory::append_menu_items_convert_unit(wxMenu* menu)
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
        {ConversionType::CONV_FROM_INCH , _L("Convert from inch") },
        {ConversionType::CONV_TO_INCH   , _L("Restore to inch") },
        {ConversionType::CONV_FROM_METER, _L("Convert from meter") },
        {ConversionType::CONV_TO_METER  , _L("Restore to meter") } };

    for (auto item : items) {
        int menu_id = menu->FindItem(item.second);
        if (can_append(item.first)) {
            // Add menu item if it doesn't exist
            if (menu_id == wxNOT_FOUND)
                append_menu_item(menu, wxID_ANY, item.second, item.second,
                    [item](wxCommandEvent&) { plater()->convert_unit(item.first); }, "", menu,
                    []() { return true; }, m_parent);
        }
        else if (menu_id != wxNOT_FOUND) {
            // Delete menu item
            menu->Destroy(menu_id);
        }
    }
}

void MenuFactory::append_menu_item_merge_to_multipart_object(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Assemble"), _L("Assemble the selected objects to an object with multiple parts"),
        [](wxCommandEvent&) { obj_list()->merge(true); }, "", menu,
        []() { return obj_list()->can_merge_to_multipart_object(); }, m_parent);
}

void MenuFactory::append_menu_item_merge_to_single_object(wxMenu* menu)
{
    menu->AppendSeparator();
    append_menu_item(menu, wxID_ANY, _L("Assemble"), _L("Assemble the selected objects to an object with single part"),
        [](wxCommandEvent&) { obj_list()->merge(false); }, "", menu,
        []() { return obj_list()->can_merge_to_single_object(); }, m_parent);
}

void MenuFactory::append_menu_item_merge_parts_to_single_part(wxMenu* menu)
{
    menu->AppendSeparator();
    append_menu_item(menu, wxID_ANY, _L("Assemble"), _L("Assemble the selected parts to a single part"),
        [](wxCommandEvent&) { obj_list()->merge_volumes(); }, "", menu,
        []() { return true; }, m_parent);
}

void MenuFactory::append_menu_items_mirror(wxMenu* menu)
{
    wxMenu* mirror_menu = new wxMenu();
    if (!mirror_menu)
        return;

    append_menu_item(mirror_menu, wxID_ANY, _L("Along X axis"), _L("Mirror along the X axis"),
        [](wxCommandEvent&) { plater()->mirror(X); }, "", menu);
    append_menu_item(mirror_menu, wxID_ANY, _L("Along Y axis"), _L("Mirror along the Y axis"),
        [](wxCommandEvent&) { plater()->mirror(Y); }, "", menu);
    append_menu_item(mirror_menu, wxID_ANY, _L("Along Z axis"), _L("Mirror along the Z axis"),
        [](wxCommandEvent&) { plater()->mirror(Z); }, "", menu);

    append_submenu(menu, mirror_menu, wxID_ANY, _L("Mirror"), _L("Mirror object"), "",
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
#ifdef __WINDOWS__
    append_submenu(&m_default_menu, sub_menu, wxID_ANY, _L("Add Primitive"), "", "menu_add_part",
        []() {return true; }, m_parent);
#else
    append_submenu(&m_default_menu, sub_menu, wxID_ANY, _L("Add Primitive"), "", "",
        []() {return true; }, m_parent);
#endif

    m_default_menu.AppendSeparator();

    append_menu_check_item(&m_default_menu, wxID_ANY, _L("Show Labels"), "",
        [](wxCommandEvent&) { plater()->show_view3D_labels(!plater()->are_view3D_labels_shown()); plater()->get_current_canvas3D()->post_event(SimpleEvent(wxEVT_PAINT)); }, &m_default_menu,
        []() { return plater()->is_view3D_shown(); }, [this]() { return plater()->are_view3D_labels_shown(); }, m_parent);
}

void MenuFactory::create_common_object_menu(wxMenu* menu)
{
    append_menu_item_rename(menu);
    // BBS
    //append_menu_items_instance_manipulation(menu);
    // Delete menu was moved to be after +/- instace to make it more difficult to be selected by mistake.
    append_menu_item_delete(menu);
    //append_menu_item_instance_to_object(menu);
    menu->AppendSeparator();

    // BBS
    append_menu_item_reload_from_disk(menu);
    append_menu_item_export_stl(menu);
    // "Scale to print volume" makes a sense just for whole object
    append_menu_item_scale_selection_to_fit_print_volume(menu);

    append_menu_item_fix_through_netfabb(menu);
    append_menu_items_mirror(menu);
}

void MenuFactory::create_object_menu()
{
    create_common_object_menu(&m_object_menu);
    wxMenu* split_menu = new wxMenu();
    if (!split_menu)
        return;

    append_menu_item(split_menu, wxID_ANY, _L("To objects"), _L("Split the selected object into multiple objects"),
        [](wxCommandEvent&) { plater()->split_object(); }, "split_objects", &m_object_menu,
        []() { return plater()->can_split(true); }, m_parent);
    append_menu_item(split_menu, wxID_ANY, _L("To parts"), _L("Split the selected object into multiple parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts", &m_object_menu,
        []() { return plater()->can_split(false); }, m_parent);

    append_submenu(&m_object_menu, split_menu, wxID_ANY, _L("Split"), _L("Split the selected object"), "",
        []() { return plater()->can_split(true) || plater()->can_split(false); }, m_parent);
    m_object_menu.AppendSeparator();

    // BBS: remove Layers Editing
    m_object_menu.AppendSeparator();

    // "Add (volumes)" popupmenu will be added later in append_menu_items_add_volume()
}

void MenuFactory::create_bbl_object_menu()
{
    // Object Clone
    append_menu_item_clone(&m_object_menu);
    // Object Repair
    append_menu_item_fix_through_netfabb(&m_object_menu);
    // Object Simplify
    append_menu_item_simplify(&m_object_menu);
    // Object Center
    append_menu_item_center(&m_object_menu);
    // Object Split
    wxMenu* split_menu = new wxMenu();
    if (!split_menu)
        return;
    append_menu_item(split_menu, wxID_ANY, _L("To objects"), _L("Split the selected object into multiple objects"),
        [](wxCommandEvent&) { plater()->split_object(); }, "split_objects", &m_object_menu,
        []() { return plater()->can_split(true); }, m_parent);
    append_menu_item(split_menu, wxID_ANY, _L("To parts"), _L("Split the selected object into multiple parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts", &m_object_menu,
        []() { return plater()->can_split(false); }, m_parent);

    append_submenu(&m_object_menu, split_menu, wxID_ANY, _L("Split"), _L("Split the selected object"), "",
        []() { return plater()->can_split(true); }, m_parent);

    // Mirror
    append_menu_items_mirror(&m_object_menu);
    // Delete
    append_menu_item_delete(&m_object_menu);
    m_object_menu.AppendSeparator();
    // Modifier Part
    // BBS
    append_menu_items_add_volume(&m_object_menu);
    m_object_menu.AppendSeparator();
    // Set filament insert menu item here
    // Set Printable
    wxMenuItem* menu_item_printable = append_menu_item_printable(&m_object_menu);
    append_menu_item_per_object_process(&m_object_menu);
    // Enter per object parameters
    append_menu_item_per_object_settings(&m_object_menu);
    m_object_menu.AppendSeparator();
    append_menu_item_reload_from_disk(&m_object_menu);
    append_menu_item_replace_with_stl(&m_object_menu);
    append_menu_item_export_stl(&m_object_menu);
}

void MenuFactory::create_bbl_assemble_object_menu()
{
    // Delete
    append_menu_item_delete(&m_assemble_object_menu);
    // Object Repair
    append_menu_item_fix_through_netfabb(&m_assemble_object_menu);
    // Object Simplify
    append_menu_item_simplify(&m_assemble_object_menu);
    m_assemble_object_menu.AppendSeparator();
}

void MenuFactory::create_sla_object_menu()
{
    create_common_object_menu(&m_sla_object_menu);
    append_menu_item(&m_sla_object_menu, wxID_ANY, _L("Split"), _L("Split the selected object into multiple objects"),
        [](wxCommandEvent&) { plater()->split_object(); }, "split_objects", nullptr,
        []() { return plater()->can_split(true); }, m_parent);

    m_sla_object_menu.AppendSeparator();

    // Add the automatic rotation sub-menu
    append_menu_item(&m_sla_object_menu, wxID_ANY, _L("Auto orientation"), _L("Auto orient the object to improve print quality."),
        [](wxCommandEvent&) { plater()->optimize_rotation(); });
}

void MenuFactory::create_part_menu()
{
    wxMenu* menu = &m_part_menu;
    append_menu_item_rename(menu);
    append_menu_item_delete(menu);
    append_menu_item_reload_from_disk(menu);
    append_menu_item_export_stl(menu);
    append_menu_item_fix_through_netfabb(menu);
    append_menu_items_mirror(menu);
    append_menu_item_merge_parts_to_single_part(menu);

    append_menu_item(menu, wxID_ANY, _L("Split"), _L("Split the selected object into multiple parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts", nullptr,
        []() { return plater()->can_split(false); }, m_parent);

    menu->AppendSeparator();
    append_menu_item_change_type(menu);
    append_menu_items_mirror(&m_part_menu);
    append_menu_item(&m_part_menu, wxID_ANY, _L("Split"), _L("Split the selected object into multiple parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts", nullptr,
        []() { return plater()->can_split(false); }, m_parent);
    m_part_menu.AppendSeparator();
    append_menu_item_per_object_settings(&m_part_menu);
}

void MenuFactory::create_bbl_part_menu()
{
    wxMenu* menu = &m_part_menu;

    append_menu_item_delete(menu);
    append_menu_item_edit_text(menu);
    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_simplify(menu);
    append_menu_item_center(menu);
    append_menu_items_mirror(menu);
    wxMenu* split_menu = new wxMenu();
    if (!split_menu)
        return;

    append_menu_item(split_menu, wxID_ANY, _L("To objects"), _L("Split the selected object into mutiple objects"),
        [](wxCommandEvent&) { plater()->split_object(); }, "split_objects", menu,
        []() { return plater()->can_split(true); }, m_parent);
    append_menu_item(split_menu, wxID_ANY, _L("To parts"), _L("Split the selected object into mutiple parts"),
        [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts", menu,
        []() { return plater()->can_split(false); }, m_parent);

    append_submenu(menu, split_menu, wxID_ANY, _L("Split"), _L("Split the selected object"), "",
        []() { return plater()->can_split(true); }, m_parent);
    menu->AppendSeparator();
    append_menu_item_per_object_settings(menu);
    append_menu_item_change_type(menu);
    append_menu_item_reload_from_disk(menu);
    append_menu_item_replace_with_stl(menu);
}

void MenuFactory::create_bbl_assemble_part_menu()
{
    wxMenu* menu = &m_assemble_part_menu;

    append_menu_item_delete(menu);
    append_menu_item_simplify(menu);
    menu->AppendSeparator();
}

//BBS: add part plate related logic
void MenuFactory::create_plate_menu()
{
    wxMenu* menu = &m_plate_menu;
    // select objects on current plate
    append_menu_item(menu, wxID_ANY, _L("Select All"), _L("select all objects on current plate"),
        [](wxCommandEvent&) {
            plater()->select_curr_plate_all();
        }, "", nullptr, []() {
            PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
            assert(plate);
            return !plate->get_objects().empty();
        }, m_parent);

    // delete objects on current plate
    append_menu_item(menu, wxID_ANY, _L("Delete All"), _L("delete all objects on current plate"),
        [](wxCommandEvent&) {
            plater()->remove_curr_plate_all();
        }, "", nullptr, []() {
            PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
            assert(plate);
            return !plate->get_objects().empty();
        }, m_parent);

    // arrange objects on current plate
    append_menu_item(menu, wxID_ANY, _L("Arrange"), _L("arrange current plate"),
        [](wxCommandEvent&) {
            PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
            assert(plate);
            plater()->set_prepare_state(Job::PREPARE_STATE_MENU);
            plater()->arrange();
        }, "", nullptr,
        []() {
            return !plater()->get_partplate_list().get_selected_plate()->get_objects().empty();
        },
        m_parent);

    // orient objects on current plate
    append_menu_item(menu, wxID_ANY, _L("Auto Rotate"), _L("auto rotate current plate"),
        [](wxCommandEvent&) {
            PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
            assert(plate);
            //BBS TODO call auto rotate for current plate
            plater()->set_prepare_state(Job::PREPARE_STATE_MENU);
            plater()->orient();
        }, "", nullptr,
        []() {
            return !plater()->get_partplate_list().get_selected_plate()->get_objects().empty();
        }, m_parent);

    // delete current plate
#ifdef __WINDOWS__
    append_menu_item(menu, wxID_ANY, _L("Delete") + "\tDel", _L("Remove the selected plate"),
        [](wxCommandEvent&) { plater()->delete_plate(); }, "menu_delete", nullptr,
        []() { return plater()->can_delete_plate(); }, m_parent);
#else
    append_menu_item(menu, wxID_ANY, _L("Delete") + "\tBackSpace", _L("Remove the selected plate"),
        [](wxCommandEvent&) { plater()->delete_plate(); }, "", nullptr,
        []() { return plater()->can_delete_plate(); }, m_parent);
#endif


    // add shapes
    menu->AppendSeparator();
    wxMenu* sub_menu = append_submenu_add_generic(menu, ModelVolumeType::INVALID);

#ifdef __WINDOWS__
    append_submenu(menu, sub_menu, wxID_ANY, _L("Add Primitive"), "", "menu_add_part",
        []() {return true; }, m_parent);
#else
    append_submenu(menu, sub_menu, wxID_ANY, _L("Add Primitive"), "", "",
        []() {return true; }, m_parent);
#endif


    return;
}

void MenuFactory::init(wxWindow* parent)
{
    m_parent = parent;

    create_default_menu();
    //BBS
    //create_object_menu();
    create_sla_object_menu();
    //create_part_menu();

    create_bbl_object_menu();
    create_bbl_part_menu();
    create_bbl_assemble_object_menu();
    create_bbl_assemble_part_menu();

    //BBS: add part plate related logic
    create_plate_menu();

    // create "Instance to Object" menu item
    append_menu_item_instance_to_object(&m_instance_menu);
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
    append_menu_item_change_filament(&m_object_menu);
    append_menu_items_convert_unit(&m_object_menu);
    append_menu_items_flush_options(&m_object_menu);
    return &m_object_menu;
}

wxMenu* MenuFactory::sla_object_menu()
{
    append_menu_items_convert_unit(&m_sla_object_menu);
    append_menu_item_settings(&m_sla_object_menu);
    //update_menu_items_instance_manipulation(mtObjectSLA);

    return &m_sla_object_menu;
}

wxMenu* MenuFactory::part_menu()
{
    append_menu_items_convert_unit(&m_part_menu);
    append_menu_item_change_filament(&m_part_menu);
    append_menu_item_per_object_settings(&m_part_menu);
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
    //BBS
    wxDataViewItemArray sels;
    obj_list()->GetSelections(sels);
    bool multi_volume = true;

    for (const wxDataViewItem& item : sels) {
        multi_volume = list_model()->GetItemType(item) & itVolume;
        if (!(list_model()->GetItemType(item) & (itVolume | itObject | itInstance)))
            // show this menu only for Objects(Instances mixed with Objects)/Volumes selection
            return nullptr;
    }

    wxMenu* menu = new MenuWithSeparators();
    if (!multi_volume) {
        int index = 0;
        if (obj_list()->can_merge_to_multipart_object()) {
            append_menu_item_merge_to_multipart_object(menu);
            index++;
        }
        append_menu_item_center(menu);
        append_menu_item_fix_through_netfabb(menu);
        //append_menu_item_simplify(menu);
        append_menu_item_delete(menu);
        menu->AppendSeparator();
        //BBS
        append_menu_item_change_filament(menu);

        append_menu_item_set_printable(menu);
        append_menu_item_per_object_process(menu);
        menu->AppendSeparator();
        append_menu_items_convert_unit(menu);
        menu->AppendSeparator();
        append_menu_item_export_stl(menu, true);
    }
    else {
        append_menu_item_center(menu);
        append_menu_item_fix_through_netfabb(menu);
        //append_menu_item_simplify(menu);
        append_menu_item_delete(menu);
        append_menu_items_convert_unit(menu);
        wxMenu* split_menu = new wxMenu();
        if (split_menu) {
            append_menu_item(split_menu, wxID_ANY, _L("To objects"), _L("Split the selected object into multiple objects"),
                [](wxCommandEvent&) { plater()->split_object(); }, "split_objects", menu,
                []() { return plater()->can_split(true); }, m_parent);
            append_menu_item(split_menu, wxID_ANY, _L("To parts"), _L("Split the selected object into multiple parts"),
                [](wxCommandEvent&) { plater()->split_volume(); }, "split_parts", menu,
                []() { return plater()->can_split(false); }, m_parent);

            append_submenu(menu, split_menu, wxID_ANY, _L("Split"), _L("Split the selected object"), "",
                []() { return plater()->can_split(true); }, m_parent);
        }
        menu->AppendSeparator();
        append_menu_item_change_filament(menu);
    }
    return menu;
}

wxMenu* MenuFactory::assemble_multi_selection_menu()
{
    wxDataViewItemArray sels;
    obj_list()->GetSelections(sels);

    for (const wxDataViewItem& item : sels)
        if (!(list_model()->GetItemType(item) & (itVolume | itObject | itInstance)))
            // show this menu only for Objects(Instances mixed with Objects)/Volumes selection
            return nullptr;

    wxMenu* menu = new MenuWithSeparators();
    append_menu_item_set_visible(menu);
    //append_menu_item_fix_through_netfabb(menu);
    //append_menu_item_simplify(menu);
    append_menu_item_delete(menu);
    menu->AppendSeparator();
    append_menu_item_change_extruder(menu);
    return menu;
}


//BBS: add partplate related logic
wxMenu* MenuFactory::plate_menu()
{
    append_menu_item_locked(&m_plate_menu);
    return &m_plate_menu;
}

wxMenu* MenuFactory::assemble_object_menu()
{
    wxMenu* menu = new MenuWithSeparators();
    // Set Visible
    append_menu_item_set_visible(menu);
    // Delete
    append_menu_item_delete(menu);
    //// Object Repair
    //append_menu_item_fix_through_netfabb(menu);
    //// Object Simplify
    //append_menu_item_simplify(menu);
    menu->AppendSeparator();

    // Set filament
    append_menu_item_change_extruder(menu);
    //// Enter per object parameters
    //append_menu_item_per_object_settings(menu);
    return menu;
}

wxMenu* MenuFactory::assemble_part_menu()
{
    wxMenu* menu = new MenuWithSeparators();

    append_menu_item_set_visible(menu);
    append_menu_item_delete(menu);
    //append_menu_item_simplify(menu);
    menu->AppendSeparator();

    append_menu_item_change_extruder(menu);
    //append_menu_item_per_object_settings(menu);
    return menu;
}

void MenuFactory::append_menu_item_clone(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _L("Clone") , "",
        [this](wxCommandEvent&) {
            plater()->clone_selection();
        }, "", nullptr,
        []() {
            return true;
        }, m_parent);
}

void MenuFactory::append_menu_item_simplify(wxMenu* menu)
{
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _L("Simplify Model"), "",
        [](wxCommandEvent&) { obj_list()->simplify(); }, "", menu,
        []() {return plater()->can_simplify(); }, m_parent);
}

void MenuFactory::append_menu_item_center(wxMenu* menu)
{
     append_menu_item(menu, wxID_ANY, _L("Center") , "",
        [this](wxCommandEvent&) {
            plater()->center_selection();
        }, "", nullptr,
        []() {
            if (plater()->canvas3D()->get_canvas_type() != GLCanvas3D::ECanvasType::CanvasView3D)
                return false;
            else {
                Selection& selection = plater()->get_view3D_canvas3D()->get_selection();
                PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
                Vec3d model_pos = selection.get_bounding_box().center();
                Vec3d center_pos = plate->get_center_origin();
                return !( (model_pos.x() == center_pos.x()) && (model_pos.y() == center_pos.y()) );
            } //disable if model is at center / not in View3D
        }, m_parent);
}

void MenuFactory::append_menu_item_per_object_process(wxMenu* menu)
{
    const std::vector<wxString> names = { _L("Edit Process Settings"), _L("Edit Process Settings") };
    append_menu_item(menu, wxID_ANY, names[0], names[1],
        [](wxCommandEvent&) {
            wxGetApp().obj_list()->switch_to_object_process();
        }, "", nullptr,
        []() {
            Selection& selection = plater()->canvas3D()->get_selection();
            return selection.is_single_full_object() ||
                selection.is_multiple_full_object() ||
                selection.is_single_full_instance() ||
                selection.is_multiple_full_instance() ||
                selection.is_single_volume() ||
                selection.is_multiple_volume();
        }, m_parent);
}

void MenuFactory::append_menu_item_per_object_settings(wxMenu* menu)
{
    const std::vector<wxString> names = { _L("Edit in Parameter Table"), _L("Edit print parameters for a single object") };
    // Delete old menu item
    for (const wxString& name : names) {
        const int item_id = menu->FindItem(name);
        if (item_id != wxNOT_FOUND)
            menu->Destroy(item_id);
    }

    append_menu_item(menu, wxID_ANY, names[0], names[1],
        [](wxCommandEvent&) {
            plater()->PopupObjectTableBySelection();
        }, "", nullptr,
        []() {
            Selection& selection = plater()->canvas3D()->get_selection();
            return selection.is_single_full_object() || selection.is_single_full_instance() || selection.is_single_volume();
        }, m_parent);
}

void MenuFactory::append_menu_item_change_filament(wxMenu* menu)
{
    const std::vector<wxString> names = { _L("Change Filament"), _L("Set Filament for selected items") };
    // Delete old menu item
    for (const wxString& name : names) {
        const int item_id = menu->FindItem(name);
        if (item_id != wxNOT_FOUND)
            menu->Destroy(item_id);
    }

    int filaments_cnt = filaments_count();
    if (filaments_cnt <= 1)
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
    if (icons.size() < filaments_cnt) {
        BOOST_LOG_TRIVIAL(warning) << boost::format("Warning: icons size %1%, filaments_cnt=%2%")%icons.size()%filaments_cnt;
        if (icons.size() <= 1)
            return;
        else
            filaments_cnt = icons.size();
    }
    wxMenu* extruder_selection_menu = new wxMenu();
    const wxString& name = sels.Count() == 1 ? names[0] : names[1];

    int initial_extruder = -1; // negative value for multiple object/part selection
    if (sels.Count() == 1) {
        const ModelConfig& config = obj_list()->get_item_config(sels[0]);
        // BBS
        const auto sel_vol = obj_list()->get_selected_model_volume();
        if (sel_vol && sel_vol->type() == ModelVolumeType::PARAMETER_MODIFIER)
            initial_extruder = config.has("extruder") ? config.extruder() : 0;
        else
            initial_extruder = config.has("extruder") ? config.extruder() : 1;
    }

    // BBS
    bool has_modifier = false;
    for (auto sel : sels) {
        if (obj_list()->GetModel()->GetVolumeType(sel) == ModelVolumeType::PARAMETER_MODIFIER) {
            has_modifier = true;
            break;
        }
    }

    for (int i = has_modifier ? 0 : 1; i <= filaments_cnt; i++)
    {
        // BBS
        //bool is_active_extruder = i == initial_extruder;
        bool is_active_extruder = false;

        const wxString& item_name = (i == 0 ? _L("Default") : wxString::Format(_L("Filament %d"), i)) +
            (is_active_extruder ? " (" + _L("current") + ")" : "");

        append_menu_item(extruder_selection_menu, wxID_ANY, item_name, "",
            [i](wxCommandEvent&) { obj_list()->set_extruder_for_selected_items(i); }, i == 0 ? wxNullBitmap : *icons[i - 1], menu,
            [is_active_extruder]() { return !is_active_extruder; }, m_parent);
    }
    menu->Append(wxID_ANY, name, extruder_selection_menu, _L("Change Filament"));
}

void MenuFactory::append_menu_item_set_printable(wxMenu* menu)
{
    const Selection& selection = plater()->canvas3D()->get_selection();
    bool all_printable = true;
    ObjectList* list = obj_list();
    wxDataViewItemArray sels;
    list->GetSelections(sels);

    for (wxDataViewItem item : sels) {
        ItemType type = list->GetModel()->GetItemType(item);
        bool check;
        if (type != itInstance && type != itObject)
            continue;
        else {
            int obj_idx = list->GetModel()->GetObjectIdByItem(item);
            int inst_idx = type == itObject ? 0 : list->GetModel()->GetInstanceIdByItem(item);
            all_printable &= list->object(obj_idx)->instances[inst_idx]->printable;
        }
    }

    wxString menu_text = all_printable ? _L("Set Unprintable") : _L("Set Printable");
    append_menu_item(menu, wxID_ANY, menu_text, "", [this, all_printable](wxCommandEvent&) {
        Selection& selection = plater()->canvas3D()->get_selection();
        selection.set_printable(!all_printable);
        }, "", nullptr, []() { return true; }, m_parent);
}

void MenuFactory::append_menu_item_locked(wxMenu* menu)
{
    const std::vector<wxString> names = { _L("Unlock"), _L("Lock") };
    // Delete old menu item
    for (const wxString& name : names) {
        const int item_id = menu->FindItem(name);
        if (item_id != wxNOT_FOUND)
            menu->Destroy(item_id);
    }

    PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
    assert(plate);
    wxString lock_text = plate->is_locked() ? names[0] : names[1];

    auto item = append_menu_item(menu, wxID_ANY, lock_text, "",
        [plate](wxCommandEvent&) {
            bool lock = plate->is_locked();
            plate->lock(!lock);
        }, "", nullptr, []() { return true; }, m_parent);

    m_parent->Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& evt) {
        PartPlate* plate = plater()->get_partplate_list().get_selected_plate();
        assert(plate);
        //bool check = plate->is_locked();
        //evt.Check(check);
        plater()->set_current_canvas_as_dirty();
    }, item->GetId());
}


void MenuFactory::update_object_menu()
{
    append_menu_items_add_volume(&m_object_menu);
}

void MenuFactory::update_default_menu()
{
    for (auto& name : { _L("Add Primitive") , _L("Show Labels") }) {
        const auto menu_item_id = m_default_menu.FindItem(name);
        if (menu_item_id != wxNOT_FOUND)
            m_default_menu.Destroy(menu_item_id);
    }
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
    // BBS: fix
#if 0
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
#endif
}


} //namespace GUI
} //namespace Slic3r
