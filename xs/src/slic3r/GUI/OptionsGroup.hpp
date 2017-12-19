#include <wx/wx.h>
#include <wx/stattext.h>
#include <wx/settings.h>
//#include <wx/window.h>

#include <map>
#include <functional>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include "Field.hpp"
//#include "slic3r_gui.hpp"
#include "GUI.hpp"

// Translate the ifdef 
#ifdef __WXOSX__
    #define wxOSX true
#else
    #define wxOSX false
#endif

#define BORDER(a, b) ((wxOSX ? a : b))

namespace Slic3r { namespace GUI {

/// Widget type describes a function object that returns a wxWindow (our widget) and accepts a wxWidget (parent window).
using widget_t = std::function<wxSizer*(wxWindow*)>;//!std::function<wxWindow*(wxWindow*)>;
using column_t = std::function<wxSizer*(const Line&)>;

class StaticText;

/// Wraps a ConfigOptionDef and adds function object for creating a side_widget.
struct Option {
	ConfigOptionDef opt { ConfigOptionDef() };
	t_config_option_key opt_id;//! {""};
    widget_t side_widget {nullptr};
    bool readonly {false};

    Option(const ConfigOptionDef& _opt, t_config_option_key id) : opt(_opt), opt_id(id) {};
};
using t_option = std::unique_ptr<Option>;	//!

/// Represents option lines
class Line {
public:
    wxString label {wxString("")};
    wxString label_tooltip {wxString("")};
    size_t full_width {0}; 
    wxSizer* sizer {nullptr};
    widget_t widget {nullptr};

    inline void append_option(const Option& option) {
        _options.push_back(option);
    }
    Line(std::string label, std::string tooltip) : label(wxString(label)), label_tooltip(wxString(tooltip)) {} ;

    const std::vector<widget_t>& get_extra_widgets() const {return _extra_widgets;}
    const std::vector<Option>& get_options() const { return _options; }

private:
	std::vector<Option> _options;//! {std::vector<Option>()};
    std::vector<widget_t> _extra_widgets;//! {std::vector<widget_t>()};
};

using t_optionfield_map = std::map<t_config_option_key, t_field>;

class OptionsGroup {
public:

    const bool staticbox {true};
    const wxString title {wxString("")};
    size_t label_width {180};
    wxSizer* sizer {nullptr};
    column_t extra_column {nullptr};
//    t_change on_change {nullptr};

    /// Returns a copy of the pointer of the parent wxWindow.
    /// Accessor function is because users are not allowed to change the parent
    /// but defining it as const means a lot of const_casts to deal with wx functions.
    inline wxWindow* parent() const { return _parent; }

    wxFont sidetext_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };
    wxFont label_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };


    void append_line(const Line& line);
    virtual Line create_single_option_line(const Option& option) const;
    inline void append_single_option_line(const Option& option) { append_line(create_single_option_line(option)); }

    // return a non-owning pointer reference 
    inline const Field* get_field(t_config_option_key id) const { try { return fields.at(id).get(); } catch (std::out_of_range e) { return nullptr; } }
//!    inline const Option& get_option(t_config_option_key id) const { try { return options.at(id).get(); } catch (std::out_of_range e) { return nullptr; } }
 //   }
//!    inline void set_value(t_config_option_key id, boost::any value) { try { fields.at(id).set_value(value); } catch (std::out_of_range e) {;}  }

    inline void enable() { for (auto& field : fields) field.second->enable(); }
    inline void disable() { for (auto& field : fields) field.second->disable(); }



    OptionsGroup(wxWindow* _parent, std::string title, const ConfigDef& configs) : options(configs.options), _parent(_parent), title(wxString(title)) {
        sizer = (staticbox ? new wxStaticBoxSizer(new wxStaticBox(_parent, wxID_ANY, title), wxVERTICAL) : new wxBoxSizer(wxVERTICAL));
        auto num_columns = 1U;
        if (label_width != 0) num_columns++;
        if (extra_column != nullptr) num_columns++;
        _grid_sizer = new wxFlexGridSizer(0, num_columns, 0,0);
        static_cast<wxFlexGridSizer*>(_grid_sizer)->SetFlexibleDirection(wxHORIZONTAL);
        static_cast<wxFlexGridSizer*>(_grid_sizer)->AddGrowableCol(label_width != 0);

        sizer->Add(_grid_sizer, 0, wxEXPAND | wxALL, wxOSX ? 0: 5);
    };

protected:
    const t_optiondef_map& options; 
    wxWindow* _parent {nullptr};

    /// Field list, contains unique_ptrs of the derived type.
    /// using types that need to know what it is beyond the public interface 
    /// need to cast based on the related ConfigOptionDef.
    t_optionfield_map fields;
    bool _disabled {false};
    wxGridSizer* _grid_sizer {nullptr};

    /// Generate a wxSizer or wxWindow from a configuration option
    /// Precondition: opt resolves to a known ConfigOption
    /// Postcondition: fields contains a wx gui object.
    const t_field& build_field(const t_config_option_key& id, const ConfigOptionDef& opt);
    const t_field& build_field(const t_config_option_key& id);
    const t_field& build_field(const Option& opt);

    virtual void _on_kill_focus (t_config_option_key id);
//!    virtual void _on_change(t_config_option_key id, config_value value);


};

class ConfigOptionsGroup: public OptionsGroup {
public:
    /// reference to libslic3r config, non-owning pointer (?).
    const DynamicPrintConfig* config {nullptr};
    bool full_labels {0};
	ConfigOptionsGroup(wxWindow* parent, std::string title, DynamicPrintConfig* _config) : OptionsGroup(parent, title, *(_config->def())), config(_config) {}
};

}}
