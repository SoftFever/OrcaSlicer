#ifndef slic3r_GUI_ObjectList_hpp_
#define slic3r_GUI_ObjectList_hpp_

#include <wx/bitmap.h>
#include <wx/dataview.h>
#include <map>

class wxBoxSizer;
class PrusaObjectDataViewModel;

namespace Slic3r {
class ConfigOptionsGroup;
class DynamicPrintConfig;
class ModelObject;
class ModelVolume;

namespace GUI {

class ObjectList : public wxDataViewCtrl
{
    wxBoxSizer          *m_sizer {nullptr};

    DynamicPrintConfig  *m_default_config {nullptr};

    wxBitmap	m_icon_modifiermesh;
    wxBitmap	m_icon_solidmesh;
    wxBitmap	m_icon_manifold_warning;
    wxBitmap	m_bmp_cog;
    wxBitmap	m_bmp_split;

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

    void                get_settings_choice(wxMenu *menu, int id, bool is_part);
    void                menu_item_add_generic(wxMenuItem* &menu, int id);
    wxMenuItem*         menu_item_split(wxMenu* menu, int id);
    wxMenuItem*         menu_item_settings(wxMenu* menu, int id, const bool is_part);
    wxMenu*             create_add_part_popupmenu();
    wxMenu*             create_part_settings_popupmenu();
    wxMenu*             create_add_settings_popupmenu(bool is_part);

    void                load_subobject(bool is_modifier = false, bool is_lambda = false);
    void                load_part(ModelObject* model_object, wxArrayString& part_names, const bool is_modifier);
    void                load_lambda(ModelObject* model_object, wxArrayString& part_names, const bool is_modifier);
    void                load_lambda(const std::string& type_name);
    void                del_subobject();
    void                del_settings_from_config();
    bool                del_subobject_from_object(const int volume_id);
    void                split(const bool split_part);
    bool                get_volume_by_item(const bool split_part, const wxDataViewItem& item, ModelVolume*& volume);
    bool                is_splittable_object(const bool split_part);

    wxPoint             get_mouse_position_in_control();
    wxBoxSizer*         get_sizer(){return  m_sizer;}
    int                 get_sel_obj_id() const          { return m_selected_object_id; }
    bool                is_parts_changed() const        { return m_parts_changed; }
    bool                is_part_settings_changed() const{ return m_part_settings_changed; }

    void                 parts_changed(int obj_idx);
    void                 part_selection_changed();

    void                 update_manipulation_sizer(const bool is_simple_mode);

    // Add object to the list
    void add_object_to_list(size_t obj_idx);
    // Delete object from the list
    void delete_object_from_list();
    // Delete all objects from the list
    void delete_all_objects_from_list();
    // Set count of object on c++ side
    void set_object_count(int idx, int count);

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
    // correct current selections to avoid of the possible conflicts
    void fix_multiselection_conflicts();
};


}}

#endif //slic3r_GUI_ObjectList_hpp_
