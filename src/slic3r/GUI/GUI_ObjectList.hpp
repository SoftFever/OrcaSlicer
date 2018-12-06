#ifndef slic3r_GUI_ObjectList_hpp_
#define slic3r_GUI_ObjectList_hpp_

#include <map>
#include <vector>

#include <wx/bitmap.h>
#include <wx/dataview.h>
#include <wx/menu.h>

#include "Event.hpp"
#include "wxExtensions.hpp"

class wxBoxSizer;
class wxMenuItem;
class PrusaObjectDataViewModel;

namespace Slic3r {
class ConfigOptionsGroup;
class DynamicPrintConfig;
class ModelObject;
class ModelVolume;

// FIXME: broken build on mac os because of this is missing:
typedef std::vector<std::string>    t_config_option_keys;

namespace GUI {

wxDECLARE_EVENT(EVT_OBJ_LIST_OBJECT_SELECT, SimpleEvent);

struct ItemForDelete
{
    ItemType    type;
    int         obj_idx; 
    int         sub_obj_idx;

    ItemForDelete(ItemType type, int obj_idx, int sub_obj_idx)
        : type(type), obj_idx(obj_idx), sub_obj_idx(sub_obj_idx)
    {}

    bool operator==(const ItemForDelete& r) const 
    {
        return (type == r.type && obj_idx == r.obj_idx && sub_obj_idx == r.sub_obj_idx);
    }

    bool operator<(const ItemForDelete& r) const
    {
        if (obj_idx != r.obj_idx)
            return (obj_idx < r.obj_idx);
        return (sub_obj_idx < r.sub_obj_idx);
    }
};

class ObjectList : public wxDataViewCtrl
{
    wxBoxSizer          *m_sizer {nullptr};

    DynamicPrintConfig  *m_default_config {nullptr};

    wxWindow            *m_parent {nullptr};

    wxBitmap	m_bmp_modifiermesh;
    wxBitmap	m_bmp_solidmesh;
    wxBitmap	m_bmp_support_enforcer;
    wxBitmap	m_bmp_support_blocker;
    wxBitmap	m_bmp_manifold_warning;
    wxBitmap	m_bmp_cog;
    wxBitmap	m_bmp_split;

    wxMenu      m_menu_object;
    wxMenu      m_menu_part;
    wxMenu      m_menu_sla_object;
    wxMenuItem* m_menu_item_split { nullptr };
    wxMenuItem* m_menu_item_split_part { nullptr };
    wxMenuItem* m_menu_item_settings { nullptr };

    std::vector<wxBitmap*> m_bmp_vector;

    int			m_selected_object_id = -1;
    bool		m_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
                                                    // happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
                                                    // calls this method again and again and again
#ifdef __WXOSX__
    wxString    m_selected_extruder = "";
#endif //__WXOSX__
    bool        m_parts_changed = false;
    bool        m_part_settings_changed = false;

public:
    ObjectList(wxWindow* parent);
    ~ObjectList();


    std::map<std::string, wxBitmap> CATEGORY_ICON;

    PrusaObjectDataViewModel	*m_objects_model{ nullptr };
    DynamicPrintConfig          *m_config {nullptr};

    std::vector<ModelObject*>   *m_objects{ nullptr };


    void                create_objects_ctrl();
    wxDataViewColumn*   create_objects_list_extruder_column(int extruders_count);
    void                update_objects_list_extruder_column(int extruders_count);
    // show/hide "Extruder" column for Objects List
    void                set_extruder_column_hidden(bool hide);
    // update extruder in current config
    void                update_extruder_in_config(const wxString& selection);

    void                init_icons();

    void                set_tooltip_for_item(const wxPoint& pt);

    void                selection_changed();
    void                context_menu();
    void                show_context_menu();
    void                key_event(wxKeyEvent& event);
    void                item_value_change(wxDataViewEvent& event);

    void                on_begin_drag(wxDataViewEvent &event);
    void                on_drop_possible(wxDataViewEvent &event);
    void                on_drop(wxDataViewEvent &event);

    void                get_settings_choice(const wxString& cat_name, const bool is_part);
    void                menu_item_add_generic(wxMenuItem* &menu, const int type);
    wxMenuItem*         menu_item_split(wxMenu* menu);
    wxMenuItem*         menu_item_settings(wxMenu* menu, const bool is_part, const bool is_sla_menu);
    void                create_object_popupmenu(wxMenu *menu);
    void                create_sla_object_popupmenu(wxMenu *menu);
    void                create_part_popupmenu(wxMenu *menu);
    wxMenu*             create_settings_popupmenu(wxMenu *parent_menu, bool is_part, const bool is_sla_menu);

    void                update_opt_keys(t_config_option_keys& t_optopt_keys);

    void                load_subobject(int type);
    void                load_part(ModelObject* model_object, wxArrayString& part_names, int type);
    void                load_generic_subobject(const std::string& type_name, const int type);
    void                del_object(const int obj_idx);
    void                del_subobject_item(wxDataViewItem& item);
    void                del_settings_from_config();
    void                del_instances_from_object(const int obj_idx);
    bool                del_subobject_from_object(const int obj_idx, const int idx, const int type);
    void                split();
    bool                get_volume_by_item(const wxDataViewItem& item, ModelVolume*& volume);
    bool                is_splittable();

    wxPoint             get_mouse_position_in_control();
    wxBoxSizer*         get_sizer() {return  m_sizer;}
    int                 get_selected_obj_idx() const;
    bool                is_parts_changed() const { return m_parts_changed; }
    bool                is_part_settings_changed() const { return m_part_settings_changed; }
    void                part_settings_changed();

    void                 parts_changed(int obj_idx);
    void                 part_selection_changed();

    // Add object to the list
    void add_object_to_list(size_t obj_idx);
    // Delete object from the list
    void delete_object_from_list();
    void delete_object_from_list(const size_t obj_idx);
    void delete_volume_from_list(const size_t obj_idx, const size_t vol_idx);
    void delete_instance_from_list(const size_t obj_idx, const size_t inst_idx);
    void delete_from_model_and_list(const ItemType type, const int obj_idx, const int sub_obj_idx);
    void delete_from_model_and_list(const std::vector<ItemForDelete>& items_for_delete);
    // Delete all objects from the list
    void delete_all_objects_from_list();
    // Increase instances count
    void increase_object_instances(const size_t obj_idx, const size_t num);
    // Decrease instances count
    void decrease_object_instances(const size_t obj_idx, const size_t num);

    // #ys_FIXME_to_delete
    // Unselect all objects in the list on c++ side
    void unselect_objects();
    // Select current object in the list on c++ side
    void select_current_object(int idx);
    // Select current volume in the list on c++ side
    void select_current_volume(int idx, int vol_idx);

    // Remove objects/sub-object from the list
    void remove();

    void init_objects();
    bool multiple_selection() const ;
    void update_selections();
    void update_selections_on_canvas();
    void select_item(const wxDataViewItem& item);
    void select_items(const wxDataViewItemArray& sels);
    void select_all();
    void select_item_all_children();
    // correct current selections to avoid of the possible conflicts
    void fix_multiselection_conflicts();

    ModelVolume* get_selected_model_volume();
    void change_part_type();

    void last_volume_is_deleted(const int obj_idx);
    bool has_multi_part_objects();
    void update_settings_items();

private:
    void OnStartEditing(wxDataViewEvent &event);

};


}}

#endif //slic3r_GUI_ObjectList_hpp_
