#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/panel.h>
#include <wx/bmpcbox.h>

#include "Preset.hpp"
#include "Selection.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "Jobs/Job.hpp"
#include "wxExtensions.hpp"
#include "Search.hpp"

class wxButton;
class ScalableButton;
class wxScrolledWindow;
class wxString;

namespace Slic3r {

class Model;
class ModelObject;
class ModelInstance;
class Print;
class SLAPrint;
enum SLAPrintObjectStep : unsigned int;

using ModelInstancePtrs = std::vector<ModelInstance*>;

namespace UndoRedo {
    class Stack;
    struct Snapshot;
}

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectManipulation;
class ObjectSettings;
class ObjectLayers;
class ObjectList;
class GLCanvas3D;
class Mouse3DController;
struct Camera;
class Bed3D;
class GLToolbar;

using t_optgroups = std::vector <std::shared_ptr<ConfigOptionsGroup>>;

class Plater;
enum class ActionButtonType : int;

class PresetComboBox : public PresetBitmapComboBox
{
public:
    PresetComboBox(wxWindow *parent, Preset::Type preset_type);
    ~PresetComboBox();

    ScalableButton* edit_btn { nullptr };

	enum LabelItemType {
		LABEL_ITEM_MARKER = 0xffffff01,
		LABEL_ITEM_WIZARD_PRINTERS,
        LABEL_ITEM_WIZARD_FILAMENTS,
        LABEL_ITEM_WIZARD_MATERIALS,

        LABEL_ITEM_MAX,
	};

    void set_label_marker(int item, LabelItemType label_item_type = LABEL_ITEM_MARKER);
    void set_extruder_idx(const int extr_idx)   { extruder_idx = extr_idx; }
    int  get_extruder_idx() const               { return extruder_idx; }
    int  em_unit() const                        { return m_em_unit; }
    void check_selection(int selection);

    void msw_rescale();

private:
    typedef std::size_t Marker;

    Preset::Type preset_type;
    int last_selected;
    int extruder_idx = -1;
    int m_em_unit;
};

class Sidebar : public wxPanel
{
    ConfigOptionMode    m_mode;
public:
    Sidebar(Plater *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    void init_filament_combo(PresetComboBox **combo, const int extr_idx);
    void remove_unused_filament_combos(const size_t current_extruder_count);
    void update_all_preset_comboboxes();
    void update_presets(Slic3r::Preset::Type preset_type);
    void update_mode_sizer() const;
    void update_reslice_btn_tooltip() const;
    void msw_rescale();
    void sys_color_changed();
    void search();
    void jump_to_option(size_t selected);

    ObjectManipulation*     obj_manipul();
    ObjectList*             obj_list();
    ObjectSettings*         obj_settings();
    ObjectLayers*           obj_layers();
    wxScrolledWindow*       scrolled_panel();
    wxPanel*                presets_panel();

    ConfigOptionsGroup*     og_freq_chng_params(const bool is_fff);
    wxButton*               get_wiping_dialog_button();
    void                    update_objects_list_extruder_column(size_t extruders_count);
    void                    show_info_sizer();
    void                    show_sliced_info_sizer(const bool show);
    void                    update_sliced_info_sizer();
    void                    enable_buttons(bool enable);
    void                    set_btn_label(const ActionButtonType btn_type, const wxString& label) const;
    bool                    show_reslice(bool show) const;
	bool                    show_export(bool show) const;
	bool                    show_send(bool show) const;
    bool                    show_disconnect(bool show)const;
	bool                    show_export_removable(bool show) const;
    bool                    is_multifilament();
    void                    update_mode();
    bool                    is_collapsed();
    void                    collapse(bool collapse);
    void                    update_searcher();
    void                    update_ui_from_settings();

    std::vector<PresetComboBox*>&   combos_filament();
    Search::OptionsSearcher&        get_searcher();
    std::string&                    get_search_line();

private:
    struct priv;
    std::unique_ptr<priv> p;
};

class Plater: public wxPanel
{
public:
    using fs_path = boost::filesystem::path;

    Plater(wxWindow *parent, MainFrame *main_frame);
    Plater(Plater &&) = delete;
    Plater(const Plater &) = delete;
    Plater &operator=(Plater &&) = delete;
    Plater &operator=(const Plater &) = delete;
    ~Plater();

    Sidebar& sidebar();
    Model& model();
    const Print& fff_print() const;
    Print& fff_print();
    const SLAPrint& sla_print() const;
    SLAPrint& sla_print();

    void new_project();
    void load_project();
    void load_project(const wxString& filename);
    void add_model(bool imperial_units = false);
    void import_sl1_archive();
    void extract_config_from_project();
#if ENABLE_GCODE_VIEWER_AS_STATE
    void load_gcode();
    void load_gcode(const wxString& filename);
#endif // ENABLE_GCODE_VIEWER_AS_STATE

    std::vector<size_t> load_files(const std::vector<boost::filesystem::path>& input_files, bool load_model = true, bool load_config = true, bool imperial_units = false);
    // To be called when providing a list of files to the GUI slic3r on command line.
    std::vector<size_t> load_files(const std::vector<std::string>& input_files, bool load_model = true, bool load_config = true, bool imperial_units = false);

    void update();
    void stop_jobs();
    void select_view(const std::string& direction);
    void select_view_3D(const std::string& name);

    bool is_preview_shown() const;
    bool is_preview_loaded() const;
    bool is_view3D_shown() const;

    bool are_view3D_labels_shown() const;
    void show_view3D_labels(bool show);

    bool is_sidebar_collapsed() const;
    void collapse_sidebar(bool show);

#if ENABLE_SLOPE_RENDERING
    bool is_view3D_slope_shown() const;
    void show_view3D_slope(bool show);

    bool is_view3D_layers_editing_enabled() const;
#endif // ENABLE_SLOPE_RENDERING

    // Called after the Preferences dialog is closed and the program settings are saved.
    // Update the UI based on the current preferences.
    void update_ui_from_settings();

    void select_all();
    void deselect_all();
    void remove(size_t obj_idx);
    void reset();
    void reset_with_confirm();
    void delete_object_from_model(size_t obj_idx);
    void remove_selected();
    void increase_instances(size_t num = 1);
    void decrease_instances(size_t num = 1);
    void set_number_of_copies(/*size_t num*/);
    bool is_selection_empty() const;
    void scale_selection_to_fit_print_volume();
    void convert_unit(bool from_imperial_unit);

    void cut(size_t obj_idx, size_t instance_idx, coordf_t z, bool keep_upper = true, bool keep_lower = true, bool rotate_lower = false);

    void export_gcode(bool prefer_removable = true);
    void export_stl(bool extended = false, bool selection_only = false);
    void export_amf();
    void export_3mf(const boost::filesystem::path& output_path = boost::filesystem::path());
    void reload_from_disk();
    void reload_all_from_disk();
    bool has_toolpaths_to_export() const;
    void export_toolpaths_to_obj() const;
    void reslice();
    void reslice_SLA_supports(const ModelObject &object, bool postpone_error_messages = false);
    void reslice_SLA_hollowing(const ModelObject &object, bool postpone_error_messages = false);
    void reslice_SLA_until_step(SLAPrintObjectStep step, const ModelObject &object, bool postpone_error_messages = false);
    void changed_object(int obj_idx);
    void changed_objects(const std::vector<size_t>& object_idxs);
    void schedule_background_process(bool schedule = true);
    bool is_background_process_update_scheduled() const;
    void suppress_background_process(const bool stop_background_process) ;
    void fix_through_netfabb(const int obj_idx, const int vol_idx = -1);
    void send_gcode();
	void eject_drive();

    void take_snapshot(const std::string &snapshot_name);
    void take_snapshot(const wxString &snapshot_name);
    void undo();
    void redo();
    void undo_to(int selection);
    void redo_to(int selection);
    bool undo_redo_string_getter(const bool is_undo, int idx, const char** out_text);
    void undo_redo_topmost_string_getter(const bool is_undo, std::string& out_text);
    bool search_string_getter(int idx, const char** label, const char** tooltip);
    // For the memory statistics. 
    const Slic3r::UndoRedo::Stack& undo_redo_stack_main() const;
#if ENABLE_GCODE_VIEWER_AS_STATE
    void clear_undo_redo_stack_main();
#endif // ENABLE_GCODE_VIEWER_AS_STATE
    // Enter / leave the Gizmos specific Undo / Redo stack. To be used by the SLA support point editing gizmo.
    void enter_gizmos_stack();
    void leave_gizmos_stack();

    void on_extruders_change(size_t extruders_count);
    void on_config_change(const DynamicPrintConfig &config);
    void force_filament_colors_update();
    void force_print_bed_update();
    // On activating the parent window.
    void on_activate();
    std::vector<std::string> get_extruder_colors_from_plater_config() const;
    std::vector<std::string> get_colors_for_color_print() const;

    void update_object_menu();
    void show_action_buttons(const bool is_ready_to_slice) const;

    wxString get_project_filename(const wxString& extension = wxEmptyString) const;
    void set_project_filename(const wxString& filename);

    bool is_export_gcode_scheduled() const;
    
    const Selection& get_selection() const;
    int get_selected_object_idx();
    bool is_single_full_object_selection() const;
    GLCanvas3D* canvas3D();
    GLCanvas3D* get_current_canvas3D();
    BoundingBoxf bed_shape_bb() const;
    
    void arrange();
    void find_new_position(const ModelInstancePtrs  &instances, coord_t min_d);

    void set_current_canvas_as_dirty();
    void unbind_canvas_event_handlers();
    void reset_canvas_volumes();

    PrinterTechnology   printer_technology() const;
    const DynamicPrintConfig * config() const;
    void                set_printer_technology(PrinterTechnology printer_technology);

    void copy_selection_to_clipboard();
    void paste_from_clipboard();
    void search(bool plater_is_active);

    bool can_delete() const;
    bool can_delete_all() const;
    bool can_increase_instances() const;
    bool can_decrease_instances() const;
    bool can_set_instance_to_object() const;
    bool can_fix_through_netfabb() const;
    bool can_split_to_objects() const;
    bool can_split_to_volumes() const;
    bool can_arrange() const;
    bool can_layers_editing() const;
    bool can_paste_from_clipboard() const;
    bool can_copy_to_clipboard() const;
    bool can_undo() const;
    bool can_redo() const;
    bool can_reload_from_disk() const;

    void msw_rescale();
    void sys_color_changed();

    bool init_view_toolbar();
#if ENABLE_GCODE_VIEWER_AS_STATE
    void enable_view_toolbar(bool enable);
#endif // ENABLE_GCODE_VIEWER_AS_STATE
    bool init_collapse_toolbar();
#if ENABLE_GCODE_VIEWER_AS_STATE
    void enable_collapse_toolbar(bool enable);
#endif // ENABLE_GCODE_VIEWER_AS_STATE

    const Camera& get_camera() const;
    Camera& get_camera();

#if ENABLE_ENVIRONMENT_MAP
    void init_environment_texture();
    unsigned int get_environment_texture_id() const;
#endif // ENABLE_ENVIRONMENT_MAP

    const Bed3D& get_bed() const;
    Bed3D& get_bed();

    const GLToolbar& get_view_toolbar() const;
    GLToolbar& get_view_toolbar();

    const GLToolbar& get_collapse_toolbar() const;
    GLToolbar& get_collapse_toolbar();

#if ENABLE_GCODE_VIEWER
    void update_preview_bottom_toolbar();
    void update_preview_moves_slider();
#endif // ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER_AS_STATE
    void reset_last_loaded_gcode() { m_last_loaded_gcode = ""; }
#endif // ENABLE_GCODE_VIEWER_AS_STATE

    const Mouse3DController& get_mouse3d_controller() const;
    Mouse3DController& get_mouse3d_controller();

	void set_bed_shape() const;
#if ENABLE_GCODE_VIEWER_AS_STATE
    void set_bed_shape(const Pointfs& shape, const std::string& custom_texture, const std::string& custom_model) const;
#endif // ENABLE_GCODE_VIEWER_AS_STATE

    // ROII wrapper for suppressing the Undo / Redo snapshot to be taken.
	class SuppressSnapshots
	{
	public:
		SuppressSnapshots(Plater *plater) : m_plater(plater)
		{
			m_plater->suppress_snapshots();
		}
		~SuppressSnapshots()
		{
			m_plater->allow_snapshots();
		}
	private:
		Plater *m_plater;
	};

	// ROII wrapper for taking an Undo / Redo snapshot while disabling the snapshot taking by the methods called from inside this snapshot.
	class TakeSnapshot
	{
	public:
		TakeSnapshot(Plater *plater, const wxString &snapshot_name) : m_plater(plater)
		{
			m_plater->take_snapshot(snapshot_name);
			m_plater->suppress_snapshots();
		}
		~TakeSnapshot()
		{
			m_plater->allow_snapshots();
		}
	private:
		Plater *m_plater;
	};

    bool inside_snapshot_capture();

	// Wrapper around wxWindow::PopupMenu to suppress error messages popping out while tracking the popup menu.
	bool PopupMenu(wxMenu *menu, const wxPoint& pos = wxDefaultPosition);
    bool PopupMenu(wxMenu *menu, int x, int y) { return this->PopupMenu(menu, wxPoint(x, y)); }

private:
    struct priv;
    std::unique_ptr<priv> p;

    // Set true during PopupMenu() tracking to suppress immediate error message boxes.
    // The error messages are collected to m_tracking_popup_menu_error_message instead and these error messages
    // are shown after the pop-up dialog closes.
    bool 	 m_tracking_popup_menu = false;
    wxString m_tracking_popup_menu_error_message;

#if ENABLE_GCODE_VIEWER_AS_STATE
    wxString m_last_loaded_gcode;
#endif // ENABLE_GCODE_VIEWER_AS_STATE

    void suppress_snapshots();
    void allow_snapshots();

    friend class SuppressBackgroundProcessingUpdate;
};

class SuppressBackgroundProcessingUpdate
{
public:
    SuppressBackgroundProcessingUpdate();
    ~SuppressBackgroundProcessingUpdate();
private:
    bool m_was_scheduled;
};

} // namespace GUI
} // namespace Slic3r

#endif
