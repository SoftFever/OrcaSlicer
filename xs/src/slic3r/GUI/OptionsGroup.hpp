#ifndef OPTIONSGROUP_HPP
#define OPTIONSGROUP_HPP

#include <boost/any.hpp>
#include <map>
#include "wxinit.h"
#include "Widget.hpp"
#include "OptionsGroup/Field.hpp"
#include "Config.hpp"
namespace Slic3r { 
class ConfigOptionDef;
namespace GUI {


/// Enumeration class to provide flags for these GUI hints.
/// they resolve to hex numbers to permit boolean masking.
enum class GUI_Type {
    i_enum_open = 0x1,
    f_enum_open = 0x2,
    select_open = 0x4
};

// Map these flags 
/*constexpr */std::map<std::string, GUI_Type>gui_type_map = 
    { { "i_enum_open", GUI_Type::i_enum_open },
      { "f_enum_open", GUI_Type::f_enum_open },
      { "select_open", GUI_Type::select_open }
    };

// Abstraction cribbed from Slic3r::ConfigOptionDefGroup::Line
// Unsure if templated class or function overloading is the appropriate thing here.
class Line {
private:
    std::vector<ConfigOptionDef> _options;
    std::vector<Widget> _extra_widgets;
    Widget _widget;
    wxSizer* _sizer;
    wxString _tooltip;
public:
    wxString label;
    bool full_width;
    wxSizer* sizer() const { return _sizer; }
    Line() : label(wxT("")), _tooltip(wxT("")), _sizer(nullptr), full_width(false), _widget(Widget()) { }
    Line(const ConfigOptionDef& z) : label(z.label), _tooltip(z.tooltip), _sizer(nullptr), full_width(false), _widget(Widget()) { append_option(z); }
    Line(const wxString& label, const wxString& tooltip) : label(label), _tooltip(tooltip), _sizer(nullptr), full_width(false), _widget(Widget()) { }
    inline void append_option(const ConfigOptionDef& z) { _options.push_back(z); };
    void append_widget(const Widget& wid) { _extra_widgets.push_back(wid); }
    std::vector<ConfigOptionDef> options() const { return _options; }
    const std::vector<Widget> extra_widgets() const { return _extra_widgets; }
    bool has_sizer() const { return _sizer != nullptr; }
    bool has_widget() const { return _widget.valid(); }
    Widget widget() const { return _widget; }
    const wxString tooltip() const { return _tooltip; }
};


// OptionsGroup building class, cribbed from Slic3r::OptionGroup
// Usage: Instantitate, add individual items to it, and add its sizer to another wxWidgets sizer.
class OptionsGroup {
    private:
        bool _disabled;
        wxFlexGridSizer* _grid_sizer;
        wxSizer* _sizer;
        void BUILD(); 
        const bool staticbox;
        wxFrame* _parent;
        std::map<size_t, Field*> fields;
        Field* _build_field(const ConfigOptionDef& opt);
    public:
        const wxString title;

        size_t label_width;
        wxFont label_font;
        wxFont sidetext_font;
        bool extra_column;

        OptionsGroup() : _parent(nullptr), title(wxT("")), staticbox(1), fields(std::map<size_t, Field*>()){};
        OptionsGroup(wxFrame* parent, std::string title) : 
            _parent(parent), 
            title(title.c_str()), 
            staticbox(1),
            extra_column(false),
            label_width(0),
            fields(std::map<size_t, Field*>())
            { BUILD(); }

        OptionsGroup(wxFrame* parent, std::string, size_t label_width) :
            _parent(parent), 
            title(title.c_str()), 
            staticbox(1),
            extra_column(false),
            fields(std::map<size_t, Field*>()),
            label_width(label_width) { BUILD(); }

        void append_line(const Line& line);
        Line create_single_option_line(const ConfigOptionDef& opt) { Line a = Line(opt); append_line(a); return a; }
        void append_single_option_line(const Line& line);

        wxSizer* sizer() { return _sizer; }
        void disable() { for (auto& f: fields) f.second->disable(); }
        void enable() { for (auto& f: fields) f.second->enable(); }
};




} }

#endif
