#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/panel.h>
#include <wx/bmpcbox.h>

#include "Preset.hpp"

#include "3DScene.hpp"
#include "GLTexture.hpp"

class wxButton;
class ScalableButton;
class wxBoxSizer;
class wxGLCanvas;
class wxScrolledWindow;
class wxString;

namespace Slic3r {

class Model;
class ModelObject;
class Print;
class SLAPrint;

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectManipulation;
class ObjectSettings;
class ObjectLayers;
class ObjectList;
class GLCanvas3D;

using t_optgroups = std::vector <std::shared_ptr<ConfigOptionsGroup>>;

class Plater;
enum class ActionButtonType : int;

class PresetComboBox : public wxBitmapComboBox
{
public:
    PresetComboBox(wxWindow *parent, Preset::Type preset_type);
    ~PresetComboBox();

    ScalableButton* edit_btn { nullptr };

	enum LabelItemType {
		LABEL_ITEM_MARKER = 0x4d,
		LABEL_ITEM_CONFIG_WIZARD = 0x4e
	};

    void set_label_marker(int item, LabelItemType label_item_type = LABEL_ITEM_MARKER);
    void set_extruder_idx(const int extr_idx)   { extruder_idx = extr_idx; }
    int  get_extruder_idx() const               { return extruder_idx; }
    int  em_unit() const                        { return m_em_unit; }
    void check_selection();

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
    void remove_unused_filament_combos(const int current_extruder_count);
    void update_all_preset_comboboxes();
    void update_presets(Slic3r::Preset::Type preset_type);
    void update_mode_sizer() const;
    void update_reslice_btn_tooltip() const;
    void msw_rescale();

    ObjectManipulation*     obj_manipul();
    ObjectList*             obj_list();
    ObjectSettings*         obj_settings();
    ObjectLayers*           obj_layers();
    wxScrolledWindow*       scrolled_panel();
    wxPanel*                presets_panel();

    ConfigOptionsGroup*     og_freq_chng_params(const bool is_fff);
    wxButton*               get_wiping_dialog_button();
    void                    update_objects_list_extruder_column(int extruders_count);
    void                    show_info_sizer();
    void                    show_sliced_info_sizer(const bool show);
    void                    enable_buttons(bool enable);
    void                    set_btn_label(const ActionButtonType btn_type, const wxString& label) const;
    bool                    show_reslice(bool show) const;
	bool                    show_export(bool show) const;
	bool                    show_send(bool show) const;
    bool                    is_multifilament();
    void                    update_mode();

    std::vector<PresetComboBox*>& combos_filament();
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
    void add_model();
    void extract_config_from_project();

    void load_files(const std::vector<boost::filesystem::path>& input_files, bool load_model = true, bool load_config = true);
    // To be called when providing a list of files to the GUI slic3r on command line.
    void load_files(const std::vector<std::string>& input_files, bool load_model = true, bool load_config = true);

    void update();
    void stop_jobs();
    void select_view(const std::string& direction);
    void select_view_3D(const std::string& name);

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

    void cut(size_t obj_idx, size_t instance_idx, coordf_t z, bool keep_upper = true, bool keep_lower = true, bool rotate_lower = false);

    void export_gcode();
    void export_stl(bool extended = false, bool selection_only = false);
    void export_amf();
    void export_3mf(const boost::filesystem::path& output_path = boost::filesystem::path());
    void reslice();
    void reslice_SLA_supports(const ModelObject &object);
    void changed_object(int obj_idx);
    void changed_objects(const std::vector<size_t>& object_idxs);
    void schedule_background_process(bool schedule = true);
    bool is_background_process_running() const;
    void suppress_background_process(const bool stop_background_process) ;
    void fix_through_netfabb(const int obj_idx, const int vol_idx = -1);
    void send_gcode();

    void take_snapshot(const std::string &snapshot_name);
    void take_snapshot(const wxString &snapshot_name);
    void suppress_snapshots();
    void allow_snapshots();
    void undo();
    void redo();
    void undo_to(int selection);
    void redo_to(int selection);
    bool undo_redo_string_getter(const bool is_undo, int idx, const char** out_text);

    void on_extruders_change(int extruders_count);
    void on_config_change(const DynamicPrintConfig &config);
    // On activating the parent window.
    void on_activate();

    void update_object_menu();

    wxString get_project_filename(const wxString& extension = wxEmptyString) const;
    void set_project_filename(const wxString& filename);

    bool is_export_gcode_scheduled() const;

    int get_selected_object_idx();
    bool is_single_full_object_selection() const;
    GLCanvas3D* canvas3D();

    PrinterTechnology   printer_technology() const;
    void                set_printer_technology(PrinterTechnology printer_technology);

    void copy_selection_to_clipboard();
    void paste_from_clipboard();

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

    void msw_rescale();

    const Camera& get_camera() const;

private:
    struct priv;
    std::unique_ptr<priv> p;

    friend class SuppressBackgroundProcessingUpdate;
};

class SuppressBackgroundProcessingUpdate
{
public:
    SuppressBackgroundProcessingUpdate();
    ~SuppressBackgroundProcessingUpdate();
private:
    bool m_was_running;
};

}}

#endif
