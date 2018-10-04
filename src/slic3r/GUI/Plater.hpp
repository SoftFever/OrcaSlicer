#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>

#include <wx/panel.h>

#include "Preset.hpp"

namespace Slic3r {

class Model;

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectManipulation;

using t_optgroups = std::vector <std::shared_ptr<ConfigOptionsGroup>>;

class Sidebar : public wxPanel
{
public:
    Sidebar(wxWindow *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    void update_presets(Slic3r::Preset::Type preset_type);

    ObjectManipulation*     obj_manipul();

    ConfigOptionsGroup*     get_optgroup(size_t i); // #ys_FIXME_for_delete
    t_optgroups&            get_optgroups();// #ys_FIXME_for_delete
    wxButton*               get_wiping_dialog_button();
    void                    update_objects_list_extruder_column(int extruders_count);
    int                     get_ol_selection();

private:
    struct priv;
    std::unique_ptr<priv> p;

    friend class Plater;    // XXX: better encapsulation?
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

    // TODO: use fs::path
    // Note: empty string means request default path
    std::string export_gcode(const std::string &output_path);
    void reslice();
private:
    struct priv;
    std::unique_ptr<priv> p;
};


}}

#endif
