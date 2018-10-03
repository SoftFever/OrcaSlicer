#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>

#include <wx/panel.h>
#include <wx/scrolwin.h>

#include "Preset.hpp"

class wxBoxSizer;

namespace Slic3r {
namespace GUI {

class MainFrame;
class ConfigOptionsGroup;

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

    void add_frequently_changed_parameters(wxWindow* parent, wxBoxSizer* sizer);
    void add_objects_list(wxWindow* parent, wxBoxSizer* sizer);
    void add_object_settings(wxWindow* parent, wxBoxSizer* sizer, t_optgroups& optgroups);


    ConfigOptionsGroup*     get_optgroup(size_t i);
    t_optgroups&            get_optgroups();
    wxButton*               get_wiping_dialog_button();
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
