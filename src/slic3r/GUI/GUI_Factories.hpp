#ifndef slic3r_GUI_Factories_hpp_
#define slic3r_GUI_Factories_hpp_

#include <map>
#include <vector>
#include <array>

#include <wx/bitmap.h>

#include "libslic3r/PrintConfig.hpp"
#include "wxExtensions.hpp"

class wxMenu;
class wxMenuItem;

namespace Slic3r {

enum class ModelVolumeType : int;

namespace GUI {

struct SettingsFactory
{
//				     category ->       vector ( option )
    typedef std::map<std::string, std::vector<std::string>> Bundle;
    static std::map<std::string, std::string>               CATEGORY_ICON;

    static wxBitmap                             get_category_bitmap(const std::string& category_name);
    static Bundle                               get_bundle(const DynamicPrintConfig* config, bool is_object_settings);
    static std::vector<std::string>             get_options(bool is_part);
};

class MenuFactory
{
public:
    static const std::vector<std::pair<std::string, std::string>> ADD_VOLUME_MENU_ITEMS;
    static std::vector<wxBitmap>    get_volume_bitmaps();

    MenuFactory();
    ~MenuFactory() = default;

    void    init(wxWindow* parent);
    void    update_object_menu();
    void    msw_rescale();

    wxMenu* default_menu();
    wxMenu* object_menu();
    wxMenu* sla_object_menu();
    wxMenu* part_menu();
    wxMenu* instance_menu();
    wxMenu* layer_menu();
    wxMenu* multi_selection_menu();

private:
    enum MenuType {
        mtObjectFFF = 0,
        mtObjectSLA,
        mtCount
    };

    wxWindow* m_parent {nullptr};

    MenuWithSeparators m_object_menu;
    MenuWithSeparators m_part_menu;
    MenuWithSeparators m_sla_object_menu;
    MenuWithSeparators m_default_menu;
    MenuWithSeparators m_instance_menu;

    // Removed/Prepended Items according to the view mode
    std::array<wxMenuItem*, mtCount> items_increase;
    std::array<wxMenuItem*, mtCount> items_decrease;
    std::array<wxMenuItem*, mtCount> items_set_number_of_copies;

    void        create_default_menu();
    void        create_common_object_menu(wxMenu *menu);
    void        create_object_menu();
    void        create_sla_object_menu();
    void        create_part_menu();
    void        create_instance_menu();

    wxMenu*     append_submenu_add_generic(wxMenu* menu, ModelVolumeType type);
    void        append_menu_items_add_volume(wxMenu* menu);
    wxMenuItem* append_menu_item_layers_editing(wxMenu* menu);
    wxMenuItem* append_menu_item_settings(wxMenu* menu);
    wxMenuItem* append_menu_item_change_type(wxMenu* menu);
    wxMenuItem* append_menu_item_instance_to_object(wxMenu* menu);
    wxMenuItem* append_menu_item_printable(wxMenu* menu);
    void        append_menu_items_osx(wxMenu* menu);
    wxMenuItem* append_menu_item_fix_through_netfabb(wxMenu* menu);
    void        append_menu_item_export_stl(wxMenu* menu);
    void        append_menu_item_reload_from_disk(wxMenu* menu);
    void        append_menu_item_change_extruder(wxMenu* menu);
    void        append_menu_item_delete(wxMenu* menu);
    void        append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu);
    void        append_menu_items_convert_unit(wxMenu* menu, int insert_pos = 1); // Add "Conver/Revert..." menu items (from/to inches/meters) after "Reload From Disk"
    void        append_menu_item_merge_to_multipart_object(wxMenu *menu);
//    void        append_menu_item_merge_to_single_object(wxMenu *menu);
    void        append_menu_items_mirror(wxMenu *menu);
    void        append_menu_items_instance_manipulation(wxMenu *menu);
    void        update_menu_items_instance_manipulation(MenuType type);
};

}}

#endif //slic3r_GUI_Factories_hpp_
