#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/panel.h>

#include "Preset.hpp"

class wxButton;


namespace Slic3r {

class Model;

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectManipulation;
class ObjectList;

using t_optgroups = std::vector <std::shared_ptr<ConfigOptionsGroup>>;

class Plater;

class Sidebar : public wxPanel
{
public:
    Sidebar(Plater *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    void update_presets(Slic3r::Preset::Type preset_type);

    ObjectManipulation*     obj_manipul(); 
    ObjectList*             obj_list();

    ConfigOptionsGroup*     og_freq_chng_params();
    wxButton*               get_wiping_dialog_button();
    void                    update_objects_list_extruder_column(int extruders_count);
    int                     get_ol_selection();
    void                    show_info_sizers(const bool show);
    void                    show_buttons(const bool show);
    void                    enable_buttons(bool enable);

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
    Model&  model();

    void update(bool force_autocenter = false);
    void remove(size_t obj_idx);
    void remove_selected();

    void load_files(const std::vector<boost::filesystem::path> &input_files);

    // Note: empty path means "use the default"
    boost::filesystem::path export_gcode(const boost::filesystem::path &output_path = boost::filesystem::path());
    void export_stl();
    void export_amf();
    void export_3mf();
    void reslice();
    void send_gcode();
private:
    struct priv;
    std::unique_ptr<priv> p;
};


}}

#endif
