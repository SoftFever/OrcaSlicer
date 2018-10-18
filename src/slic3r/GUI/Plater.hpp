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

namespace Slic3r {

class Model;
class Print;

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectManipulation;
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

    ConfigOptionsGroup*     og_freq_chng_params();
    wxButton*               get_wiping_dialog_button();
    void                    update_objects_list_extruder_column(int extruders_count);
    void                    show_info_sizers(const bool show);
    void                    show_buttons(const bool show);
    void                    show_button(ButtonAction but_action, bool show);
    void                    enable_buttons(bool enable);
    bool                    is_multifilament();

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

    void add();

    void load_files(const std::vector<boost::filesystem::path> &input_files);

    void update(bool force_autocenter = false);
    void select_view(const std::string& direction);

    void remove(size_t obj_idx);
    void remove_selected();
    void increase(size_t num = 1);
    void decrease(size_t num = 1);
    void set_number_of_copies(size_t num);

    // Note: empty path means "use the default"
    boost::filesystem::path export_gcode(const boost::filesystem::path &output_path = boost::filesystem::path());
    void export_stl();
    void export_amf();
    void export_3mf();
    void reslice();
    void changed_object_settings(int obj_idx);
    void send_gcode();

    void on_extruders_change(int extruders_count);
    void on_config_change(const DynamicPrintConfig &config);

    wxGLCanvas* canvas3D();
private:
    struct priv;
    std::unique_ptr<priv> p;
};


}}

#endif
