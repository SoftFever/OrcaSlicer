#ifndef FIELD_HPP
#define FIELD_HPP

#include "../wxinit.h"

#include <functional>
#include <boost/any.hpp>
#include <map>

// ConfigOptionDef definition
#include "Config.hpp"

namespace Slic3r { 
class ConfigOptionDef;
namespace GUI {



/// Interface class for fields
/// 
class Field {
protected:
    wxSizer*		_sizer;
    wxWindow*		_window;
    wxWindow*		_parent;

    /// Instantiate the underlying wxWidgets control/window.
    /// This function is expected to be called by the constructor.
    virtual void BUILD() = 0;

    /// Reference to underlying ConfigOptionDef this Field is
    /// implementing.
    /// TODO: This may not be necessary.
    const ConfigOptionDef& opt; 
	std::string		type;

public:
    std::function<void(wxCommandEvent&)> _on_change;
    std::function<void(wxCommandEvent&)> _on_kill_focus;
    // used if we need to know which ConfigOptionDef this corresponds.
    Field() : opt(ConfigOptionDef()), _on_change(nullptr), _on_kill_focus(nullptr){}
    Field(const ConfigOptionDef& opt) : opt(opt), type(opt.gui_type) { }
    Field(wxFrame* parent, const ConfigOptionDef& opt) : opt(opt),  _parent(parent) { }
    wxSizer* sizer() { return _sizer; }
    wxWindow* window() { return _window; }

    // 
    bool has_sizer() { return _sizer != nullptr; }
    bool has_window() { return _window != nullptr; }

    /// Return the wxWidgets ID for this object.
    ///
    wxWindowID get_id() { if (this->has_window()) return _window->GetId(); }

    /// Sets a value for this control.
    /// subclasses should overload with a specific version
    virtual void set_value(boost::any value) = 0;
    
    /// Gets a boost::any representing this control.
    /// subclasses should overload with a specific version
    virtual boost::any get_value() = 0;

    /// subclasses should overload with a specific version
    virtual void enable() = 0;
    virtual void disable() = 0;

};

class TextCtrl : public Field {
protected:
    void BUILD();
public:
    TextCtrl();
    TextCtrl(wxFrame* parent, const ConfigOptionDef& opt) : Field(parent, opt) { BUILD(); };

    void set_value(std::string value) { 
            dynamic_cast<wxTextCtrl*>(_window)->SetValue(wxString(value));
    }
    void set_value(boost::any value) { 
        try {
            dynamic_cast<wxTextCtrl*>(_window)->SetValue(boost::any_cast<wxString>(value));
        } catch (boost::bad_any_cast) {
            // TODO Log error and do nothing
        }
    }
    boost::any get_value() { return boost::any(dynamic_cast<wxTextCtrl*>(_window)->GetValue()); }
    
    void enable() { dynamic_cast<wxTextCtrl*>(_window)->Enable(); dynamic_cast<wxTextCtrl*>(_window)->SetEditable(1); }
    void disable() { dynamic_cast<wxTextCtrl*>(_window)->Disable(); dynamic_cast<wxTextCtrl*>(_window)->SetEditable(0); }
    void __on_change(wxCommandEvent&);
    
};

} }
#endif
