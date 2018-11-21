#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/panel.h>
#include <wx/bmpcbox.h>

#include "Preset.hpp"

class wxButton;
class wxBoxSizer;
class wxGLCanvas;
class wxScrolledWindow;

namespace Slic3r {

class Model;
class Print;
class SLAPrint;

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectManipulation;
class ObjectSettings;
class ObjectList;

using t_optgroups = std::vector <std::shared_ptr<ConfigOptionsGroup>>;

class Plater;

class PresetComboBox : public wxBitmapComboBox
{
public:
    PresetComboBox(wxWindow *parent, Preset::Type preset_type);
    ~PresetComboBox();

    void set_label_marker(int item);
    void set_extruder_idx(const int extr_idx)   { extruder_idx = extr_idx; }
    int  get_extruder_idx() const               { return extruder_idx; }

private:
    typedef std::size_t Marker;
    enum { LABEL_ITEM_MARKER = 0x4d };

    Preset::Type preset_type;
    int last_selected;
    int extruder_idx = -1;
};

enum ButtonAction
{
    baUndef,
    baReslice,
    baExportGcode,
    baSendGcode
};

class Sidebar : public wxPanel
{
    /*ConfigMenuIDs*/int    m_mode;
public:
    Sidebar(Plater *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    void init_filament_combo(PresetComboBox **combo, const int extr_idx);
    void remove_unused_filament_combos(const int current_extruder_count);
    void update_presets(Slic3r::Preset::Type preset_type);

    ObjectManipulation*     obj_manipul();
    ObjectList*             obj_list();
    ObjectSettings*         obj_settings();
    wxScrolledWindow*       scrolled_panel();

    ConfigOptionsGroup*     og_freq_chng_params();
    wxButton*               get_wiping_dialog_button();
    void                    update_objects_list_extruder_column(int extruders_count);
    void                    show_info_sizer();
    void                    show_sliced_info_sizer(const bool show);
    void                    show_buttons(const bool show);
    void                    show_button(ButtonAction but_action, bool show);
    void                    enable_buttons(bool enable);
    bool                    is_multifilament();
    void                    set_mode_value(const /*ConfigMenuIDs*/int mode) { m_mode = mode; }

    std::vector<PresetComboBox*>& combos_filament();
private:
    struct priv;
    std::unique_ptr<priv> p;
};

class Plater: public wxPanel
{
public:
    Plater(wxWindow *parent, MainFrame *main_frame);
    Plater(Plater &&) = delete;
    Plater(const Plater &) = delete;
    Plater &operator=(Plater &&) = delete;
    Plater &operator=(const Plater &) = delete;
    ~Plater();

    Sidebar& sidebar();
    Model& model();
    Print& print();

#if ENABLE_NEW_MENU_LAYOUT
    void load_project();
    void add_model();
    void extract_config_from_project();
#else
    void add();
#endif // ENABLE_NEW_MENU_LAYOUT

#if ENABLE_NEW_MENU_LAYOUT
    void load_files(const std::vector<boost::filesystem::path>& input_files, bool load_model = true, bool load_config = true);
#else
    void load_files(const std::vector<boost::filesystem::path> &input_files);
#endif // ENABLE_NEW_MENU_LAYOUT

    void update();
    void select_view(const std::string& direction);

    void select_all();
    void remove(size_t obj_idx);
    void delete_object_from_model(size_t obj_idx);
    void remove_selected();
    void increase_instances(size_t num = 1);
    void decrease_instances(size_t num = 1);
    void set_number_of_copies(/*size_t num*/);
    bool is_selection_empty() const;

    void cut(size_t obj_idx, size_t instance_idx, coordf_t z);

    // Note: empty path means "use the default"
    void export_gcode(boost::filesystem::path output_path = boost::filesystem::path());
    void export_stl();
    void export_amf();
#if ENABLE_NEW_MENU_LAYOUT
    void export_3mf(const boost::filesystem::path& output_path = boost::filesystem::path());
#else
    void export_3mf();
#endif // ENABLE_NEW_MENU_LAYOUT
    void reslice();
    void changed_object(int obj_idx);
    void fix_through_netfabb(const int obj_idx);
    void send_gcode();

    void on_extruders_change(int extruders_count);
    void on_config_change(const DynamicPrintConfig &config);

#if ENABLE_NEW_MENU_LAYOUT
    const wxString& get_project_filename() const;
    bool is_export_gcode_scheduled() const;
#endif // ENABLE_NEW_MENU_LAYOUT

    int get_selected_object_idx();
    bool is_single_full_object_selection() const;
    wxGLCanvas* canvas3D();

    PrinterTechnology printer_technology() const;
private:
    struct priv;
    std::unique_ptr<priv> p;
};


}}

#endif
