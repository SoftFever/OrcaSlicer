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
class ObjectDataViewModel;
class MenuWithSeparators;

namespace Slic3r {
class ConfigOptionsGroup;
class DynamicPrintConfig;
class ModelObject;
class ModelVolume;
enum class ModelVolumeType : int;

// FIXME: broken build on mac os because of this is missing:
typedef std::vector<std::string>    t_config_option_keys;

typedef std::map<std::string, std::vector<std::string>> SettingsBundle;

//				  category ->		vector 			 ( option	;  label )
typedef std::map< std::string, std::vector< std::pair<std::string, std::string> > > settings_menu_hierarchy;

typedef std::vector<ModelVolume*> ModelVolumePtrs;

typedef double                                              coordf_t;
typedef std::pair<coordf_t, coordf_t>                       t_layer_height_range;
typedef std::map<t_layer_height_range, DynamicPrintConfig>  t_layer_config_ranges;

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
public:
    enum SELECTION_MODE
    {
        smUndef     = 0,
        smVolume    = 1,
        smInstance  = 2,
        smLayer     = 4,
        smSettings  = 8,  // used for undo/redo
        smLayerRoot = 16, // used for undo/redo
    };

private:
    SELECTION_MODE  m_selection_mode {smUndef};
    int             m_selected_layers_range_idx;

    struct dragged_item_data
    {
        void init(const int obj_idx, const int subobj_idx, const ItemType type) {
            m_obj_idx = obj_idx;
            m_type = type;
            if (m_type&itVolume)
                m_vol_idx = subobj_idx;
            else
                m_inst_idxs.insert(subobj_idx);
        }

        void init(const int obj_idx, const ItemType type) {
            m_obj_idx = obj_idx;
            m_type = type;
        }

        void clear() {
            m_obj_idx = -1;
            m_vol_idx = -1;
            m_inst_idxs.clear();
            m_type = itUndef;
        }

        int obj_idx() const  { return m_obj_idx; }
        int sub_obj_idx() const  { return m_vol_idx; }
        ItemType type() const { return m_type; }
        std::set<int>& inst_idxs() { return m_inst_idxs; }

    private:
        int m_obj_idx = -1;
        int m_vol_idx = -1;
        std::set<int> m_inst_idxs{};
        ItemType m_type = itUndef;

    } m_dragged_data;

    wxBoxSizer          *m_sizer {nullptr};
    wxWindow            *m_parent {nullptr};

    ScalableBitmap	    m_bmp_modifiermesh;
    ScalableBitmap	    m_bmp_solidmesh;
    ScalableBitmap	    m_bmp_support_enforcer;
    ScalableBitmap	    m_bmp_support_blocker;
    ScalableBitmap	    m_bmp_manifold_warning;
    ScalableBitmap	    m_bmp_cog;

    MenuWithSeparators  m_menu_object;
    MenuWithSeparators  m_menu_part;
    MenuWithSeparators  m_menu_sla_object;
    MenuWithSeparators  m_menu_instance;
    MenuWithSeparators  m_menu_layer;
    MenuWithSeparators  m_menu_default;
    wxMenuItem* m_menu_item_settings { nullptr };
    wxMenuItem* m_menu_item_split_instances { nullptr };

    ObjectDataViewModel         *m_objects_model{ nullptr };
    DynamicPrintConfig          *m_config {nullptr};
    std::vector<ModelObject*>   *m_objects{ nullptr };

    std::vector<wxBitmap*>      m_bmp_vector;

    t_layer_config_ranges       m_layer_config_ranges_cache;

    int			m_selected_object_id = -1;
    bool		m_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
                                                    // happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
                                                    // calls this method again and again and again

    bool        m_prevent_update_extruder_in_config = false; // We use this flag to avoid updating of the extruder value in config 
                                                             // during updating of the extruder count.

    bool        m_prevent_canvas_selection_update = false; // This flag prevents changing selection on the canvas. See function
                                                           // update_settings_items - updating canvas selection is undesirable,
                                                           // because it would turn off the gizmos (mainly a problem for the SLA gizmo)

    int         m_selected_row = 0;
    wxDataViewItem m_last_selected_item {nullptr};
#ifdef __WXMSW__
    // Workaround for entering the column editing mode on Windows. Simulate keyboard enter when another column of the active line is selected.
    int 	    m_last_selected_column = -1;
#endif /* __MSW__ */

#if 0
    SettingsBundle m_freq_settings_fff;
    SettingsBundle m_freq_settings_sla;
#endif

public:
    ObjectList(wxWindow* parent);
    ~ObjectList();


    std::map<std::string, wxBitmap> CATEGORY_ICON;

    ObjectDataViewModel*        GetModel() const    { return m_objects_model; }
    DynamicPrintConfig*         config() const      { return m_config; }
    std::vector<ModelObject*>*  objects() const     { return m_objects; }

    ModelObject*                object(const int obj_idx) const ;

    void                create_objects_ctrl();
    void                create_popup_menus();
    wxDataViewColumn*   create_objects_list_extruder_column(size_t extruders_count);
    void                update_objects_list_extruder_column(size_t extruders_count);
    // show/hide "Extruder" column for Objects List
    void                set_extruder_column_hidden(const bool hide) const;
    // update extruder in current config
    void                update_extruder_in_config(const wxDataViewItem& item);
    // update changed name in the object model
    void                update_name_in_model(const wxDataViewItem& item) const;
    void                update_extruder_values_for_items(const size_t max_extruder);

    void                init_icons();
    void                msw_rescale_icons();

    // Get obj_idx and vol_idx values for the selected (by default) or an adjusted item
    void                get_selected_item_indexes(int& obj_idx, int& vol_idx, const wxDataViewItem& item = wxDataViewItem(0));
    // Get count of errors in the mesh
    int                 get_mesh_errors_count(const int obj_idx, const int vol_idx = -1) const;
    /* Get list of errors in the mesh. Return value is a string, used for the tooltip
     * Function without parameters is for a call from Manipulation panel, 
     * when we don't know parameters of selected item 
     */
    wxString            get_mesh_errors_list(const int obj_idx, const int vol_idx = -1) const;
    wxString            get_mesh_errors_list();
    void                set_tooltip_for_item(const wxPoint& pt);

    void                selection_changed();
    void                show_context_menu(const bool evt_context_menu);
#ifndef __WXOSX__
    void                key_event(wxKeyEvent& event);
#endif /* __WXOSX__ */

    void                copy();
    void                paste();
    void                undo();
    void                redo();

    void                get_settings_choice(const wxString& category_name);
    void                get_freq_settings_choice(const wxString& bundle_name);
    void                show_settings(const wxDataViewItem settings_item);

    wxMenu*             append_submenu_add_generic(wxMenu* menu, const ModelVolumeType type);
    void                append_menu_items_add_volume(wxMenu* menu);
    wxMenuItem*         append_menu_item_split(wxMenu* menu);
    wxMenuItem*         append_menu_item_layers_editing(wxMenu* menu);
    wxMenuItem*         append_menu_item_settings(wxMenu* menu);
    wxMenuItem*         append_menu_item_change_type(wxMenu* menu);
    wxMenuItem*         append_menu_item_instance_to_object(wxMenu* menu, wxWindow* parent);
    wxMenuItem*         append_menu_item_printable(wxMenu* menu, wxWindow* parent);
    void                append_menu_items_osx(wxMenu* menu);
    wxMenuItem*         append_menu_item_fix_through_netfabb(wxMenu* menu);
    void                append_menu_item_export_stl(wxMenu* menu) const;
    void                append_menu_item_reload_from_disk(wxMenu* menu) const;
    void                append_menu_item_change_extruder(wxMenu* menu) const;
    void                append_menu_item_delete(wxMenu* menu);
    void                append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu);
    void                create_object_popupmenu(wxMenu *menu);
    void                create_sla_object_popupmenu(wxMenu*menu);
    void                create_part_popupmenu(wxMenu*menu);
    void                create_instance_popupmenu(wxMenu*menu);
    void                create_default_popupmenu(wxMenu *menu);
    wxMenu*             create_settings_popupmenu(wxMenu *parent_menu);
    void                create_freq_settings_popupmenu(wxMenu *parent_menu, const bool is_object_settings = true);

    void                update_opt_keys(t_config_option_keys& t_optopt_keys, const bool is_object);

    void                load_subobject(ModelVolumeType type);
    void                load_part(ModelObject* model_object, std::vector<std::pair<wxString, bool>> &volumes_info, ModelVolumeType type);
	void                load_generic_subobject(const std::string& type_name, const ModelVolumeType type);
    void                load_shape_object(const std::string &type_name);
    void                del_object(const int obj_idx);
    void                del_subobject_item(wxDataViewItem& item);
    void                del_settings_from_config(const wxDataViewItem& parent_item);
    void                del_instances_from_object(const int obj_idx);
    void                del_layer_from_object(const int obj_idx, const t_layer_height_range& layer_range);
    void                del_layers_from_object(const int obj_idx);
    bool                del_subobject_from_object(const int obj_idx, const int idx, const int type);
    void                split();
    void                layers_editing();

    wxDataViewItem      add_layer_root_item(const wxDataViewItem obj_item);
    wxDataViewItem      add_settings_item(wxDataViewItem parent_item, const DynamicPrintConfig* config);

    DynamicPrintConfig  get_default_layer_config(const int obj_idx);
    bool                get_volume_by_item(const wxDataViewItem& item, ModelVolume*& volume);
    bool                is_splittable();
    bool                selected_instances_of_same_object();
    bool                can_split_instances();

    wxPoint             get_mouse_position_in_control();
    wxBoxSizer*         get_sizer() {return  m_sizer;}
    int                 get_selected_obj_idx() const;
    DynamicPrintConfig& get_item_config(const wxDataViewItem& item) const;
    SettingsBundle      get_item_settings_bundle(const DynamicPrintConfig* config, const bool is_object_settings);

    void                changed_object(const int obj_idx = -1) const;
    void                part_selection_changed();

    // Add object to the list
    void add_object_to_list(size_t obj_idx, bool call_selection_changed = true);
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
    void del_layer_range(const t_layer_height_range& range);
    void add_layer_range_after_current(const t_layer_height_range& current_range);
    void add_layer_item (const t_layer_height_range& range, 
                         const wxDataViewItem layers_item, 
                         const int layer_idx = -1);
    bool edit_layer_range(const t_layer_height_range& range, coordf_t layer_height);
    bool edit_layer_range(const t_layer_height_range& range, 
                          const t_layer_height_range& new_range);

    void init_objects();
    bool multiple_selection() const ;
    bool is_selected(const ItemType type) const;
    int  get_selected_layers_range_idx() const;
    void set_selected_layers_range_idx(const int range_idx) { m_selected_layers_range_idx = range_idx; }
    void set_selection_mode(SELECTION_MODE mode) { m_selection_mode = mode; }
    void update_selections();
    void update_selections_on_canvas();
    void select_item(const wxDataViewItem& item);
    void select_items(const wxDataViewItemArray& sels);
    void select_all();
    void select_item_all_children();
    void update_selection_mode();
    bool check_last_selection(wxString& msg_str);
    // correct current selections to avoid of the possible conflicts
    void fix_multiselection_conflicts();

    ModelVolume* get_selected_model_volume();
    void change_part_type();

    void last_volume_is_deleted(const int obj_idx);
    void update_settings_items();
    void update_and_show_object_settings_item();
    void update_settings_item_and_selection(wxDataViewItem item, wxDataViewItemArray& selections);
    void update_object_list_by_printer_technology();
    void update_object_menu();

    void instances_to_separated_object(const int obj_idx, const std::set<int>& inst_idx);
    void instances_to_separated_objects(const int obj_idx);
    void split_instances();
    void rename_item();
    void fix_through_netfabb();
    void update_item_error_icon(const int obj_idx, int vol_idx) const ;

    void fill_layer_config_ranges_cache();
    void paste_layers_into_list();
    void paste_volumes_into_list(int obj_idx, const ModelVolumePtrs& volumes);
    void paste_objects_into_list(const std::vector<size_t>& object_idxs);

    void msw_rescale();

    void update_after_undo_redo();
    //update printable state for item from objects model
    void update_printable_state(int obj_idx, int instance_idx);
    void toggle_printable_state(wxDataViewItem item);

private:
#ifdef __WXOSX__
//    void OnChar(wxKeyEvent& event);
#endif /* __WXOSX__ */
    void OnContextMenu(wxDataViewEvent &event);
    void list_manipulation(bool evt_context_menu = false);

    void OnBeginDrag(wxDataViewEvent &event);
    void OnDropPossible(wxDataViewEvent &event);
    void OnDrop(wxDataViewEvent &event);
    bool can_drop(const wxDataViewItem& item) const ;

    void ItemValueChanged(wxDataViewEvent &event);
#ifdef __WXMSW__
    // Workaround for entering the column editing mode on Windows. Simulate keyboard enter when another column of the active line is selected.
	void OnEditingStarted(wxDataViewEvent &event);
#endif /* __WXMSW__ */
    void OnEditingDone(wxDataViewEvent &event);

    void show_multi_selection_menu();
    void extruder_selection();
    void set_extruder_for_selected_items(const int extruder) const ;

    std::vector<std::string>        get_options(const bool is_part);
    const std::vector<std::string>& get_options_for_bundle(const wxString& bundle_name);
    void                            get_options_menu(settings_menu_hierarchy& settings_menu, const bool is_part);
};


}}

#endif //slic3r_GUI_ObjectList_hpp_
