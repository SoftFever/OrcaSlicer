#ifndef SLIC3R_GUI_FIELD_HPP
#define SLIC3R_GUI_FIELD_HPP

#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

#include <memory>
#include <functional>
#include <boost/any.hpp>

#include <wx/spinctrl.h>
#include <wx/clrpicker.h>

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Config.hpp"

//#include "slic3r_gui.hpp"
#include "GUI.hpp"

namespace Slic3r { namespace GUI {

class Field;
using t_field = std::unique_ptr<Field>;
using t_kill_focus = std::function<void()>;
using t_change = std::function<void(t_config_option_key, boost::any)>;

class Field {
protected:
    // factory function to defer and enforce creation of derived type. 
    virtual void	PostInitialize() { BUILD(); }
    
    /// Finish constructing the Field's wxWidget-related properties, including setting its own sizer, etc.
    virtual void	BUILD() = 0;

    /// Call the attached on_kill_focus method. 
	//! It's important to use wxEvent instead of wxFocusEvent,
	//! in another case we can't unfocused control at all
	void			on_kill_focus(wxEvent& event);
    /// Call the attached on_change method. 
    void			on_change_field();

public:
    /// parent wx item, opportunity to refactor (probably not necessary - data duplication)
    wxWindow*		m_parent {nullptr};

    /// Function object to store callback passed in from owning object.
	t_kill_focus	m_on_kill_focus {nullptr};

    /// Function object to store callback passed in from owning object.
	t_change		m_on_change {nullptr};

	// This is used to avoid recursive invocation of the field change/update by wxWidgets.
    bool			m_disable_change_event {false};

    /// Copy of ConfigOption for deduction purposes
    const ConfigOptionDef			m_opt {ConfigOptionDef()};
	const t_config_option_key		m_opt_id;//! {""};

    /// Sets a value for this control.
    /// subclasses should overload with a specific version
    /// Postcondition: Method does not fire the on_change event.
    virtual void		set_value(boost::any value) = 0;
    
    /// Gets a boost::any representing this control.
    /// subclasses should overload with a specific version
    virtual boost::any	get_value() = 0;

    virtual void		enable() = 0;
    virtual void		disable() = 0;

    /// Fires the enable or disable function, based on the input.
    inline void			toggle(bool en) { en ? enable() : disable(); }

	virtual wxString	get_tooltip_text(const wxString& default_string);

    Field(const ConfigOptionDef& opt, const t_config_option_key& id) : m_opt(opt), m_opt_id(id) {};
    Field(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : m_parent(parent), m_opt(opt), m_opt_id(id) {};

    /// If you don't know what you are getting back, check both methods for nullptr. 
    virtual wxSizer*	getSizer()  { return nullptr; }
    virtual wxWindow*	getWindow() { return nullptr; }

	bool		is_matched(std::string string, std::string pattern);
	boost::any	get_value_by_opt_type(wxString str, ConfigOptionType type);

    /// Factory method for generating new derived classes.
    template<class T>
    static t_field Create(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id)  // interface for creating shared objects
    {
        auto p = Slic3r::make_unique<T>(parent, opt, id);
        p->PostInitialize();
		return std::move(p); //!p;
    }
};

/// Convenience function, accepts a const reference to t_field and checks to see whether 
/// or not both wx pointers are null.
inline bool is_bad_field(const t_field& obj) { return obj->getSizer() == nullptr && obj->getWindow() == nullptr; }

/// Covenience function to determine whether this field is a valid window field.
inline bool is_window_field(const t_field& obj) { return !is_bad_field(obj) && obj->getWindow() != nullptr; }

/// Covenience function to determine whether this field is a valid sizer field.
inline bool is_sizer_field(const t_field& obj) { return !is_bad_field(obj) && obj->getSizer() != nullptr; }

class TextCtrl : public Field {
    using Field::Field;
public:
	TextCtrl(const ConfigOptionDef& opt, const t_config_option_key& id) : Field(opt,  id) {}
	TextCtrl(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : Field(parent, opt, id) {}

    void BUILD();
    wxWindow* window {nullptr};

    virtual void	set_value(std::string value) {
		m_disable_change_event = true;
        dynamic_cast<wxTextCtrl*>(window)->SetValue(wxString(value));
		m_disable_change_event = false;
    }
    virtual void	set_value(boost::any value) {
		m_disable_change_event = true;
		dynamic_cast<wxTextCtrl*>(window)->SetValue(boost::any_cast<wxString>(value));
		m_disable_change_event = false;
    }

	boost::any		get_value() override;

    virtual void	enable();
    virtual void	disable();
    virtual wxWindow* getWindow() { return window; }
};

class CheckBox : public Field {
	using Field::Field;
public:
	CheckBox(const ConfigOptionDef& opt, const t_config_option_key& id) : Field(opt, id) {}
	CheckBox(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : Field(parent, opt, id) {}

	wxWindow*		window{ nullptr };
	void			BUILD() override;

	void			set_value(const bool value) {
		m_disable_change_event = true;
		dynamic_cast<wxCheckBox*>(window)->SetValue(value);
		m_disable_change_event = false;
	}
	void			set_value(boost::any value) {
		m_disable_change_event = true;
		dynamic_cast<wxCheckBox*>(window)->SetValue(boost::any_cast<bool>(value));
		m_disable_change_event = false;
	}
	boost::any		get_value() override {
		return boost::any(dynamic_cast<wxCheckBox*>(window)->GetValue());
	}

	void			enable() override { dynamic_cast<wxCheckBox*>(window)->Enable(); }
	void			disable() override { dynamic_cast<wxCheckBox*>(window)->Disable(); }
	wxWindow*		getWindow() override { return window; }
};

class SpinCtrl : public Field {
	using Field::Field;
public:
	SpinCtrl(const ConfigOptionDef& opt, const t_config_option_key& id) : Field(opt, id), tmp_value(-9999) {}
	SpinCtrl(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : Field(parent, opt, id), tmp_value(-9999) {}

	int				tmp_value;

	wxWindow*		window{ nullptr };
	void			BUILD() override;

	void			set_value(const std::string value) {
		m_disable_change_event = true;
		dynamic_cast<wxSpinCtrl*>(window)->SetValue(value);
		m_disable_change_event = false;
	}
	void			set_value(boost::any value) {
		m_disable_change_event = true;
		dynamic_cast<wxSpinCtrl*>(window)->SetValue(boost::any_cast<int>(value));
		m_disable_change_event = false;
	}
	boost::any		get_value() override {
		return boost::any(dynamic_cast<wxSpinCtrl*>(window)->GetValue());
	}

	void			enable() override { dynamic_cast<wxSpinCtrl*>(window)->Enable(); }
	void			disable() override { dynamic_cast<wxSpinCtrl*>(window)->Disable(); }
	wxWindow*		getWindow() override { return window; }
};

class Choice : public Field {
	using Field::Field;
public:
	Choice(const ConfigOptionDef& opt, const t_config_option_key& id) : Field(opt, id) {}
	Choice(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : Field(parent, opt, id) {}

	wxWindow*		window{ nullptr };
	void			BUILD() override;

	void			set_selection();
	void			set_value(const std::string value);
	void			set_value(boost::any value);
	void			set_values(const std::vector<std::string> values);
	boost::any		get_value() override;

	void			enable() override { dynamic_cast<wxComboBox*>(window)->Enable(); };
	void			disable() override{ dynamic_cast<wxComboBox*>(window)->Disable(); };
	wxWindow*		getWindow() override { return window; }
};

class ColourPicker : public Field {
	using Field::Field;
public:
	ColourPicker(const ConfigOptionDef& opt, const t_config_option_key& id) : Field(opt, id) {}
	ColourPicker(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : Field(parent, opt, id) {}

	wxWindow*		window{ nullptr };
	void			BUILD()  override;

	void			set_value(const std::string value) {
		m_disable_change_event = true;
		dynamic_cast<wxColourPickerCtrl*>(window)->SetColour(value);
		m_disable_change_event = false;
	 	}
	void			set_value(boost::any value) {
		m_disable_change_event = true;
		dynamic_cast<wxColourPickerCtrl*>(window)->SetColour(boost::any_cast<wxString>(value));
		m_disable_change_event = false;
	}

	boost::any		get_value() override;

	void			enable() override { dynamic_cast<wxColourPickerCtrl*>(window)->Enable(); };
	void			disable() override{ dynamic_cast<wxColourPickerCtrl*>(window)->Disable(); };
	wxWindow*		getWindow() override { return window; }
};

class PointCtrl : public Field {
	using Field::Field;
public:
	PointCtrl(const ConfigOptionDef& opt, const t_config_option_key& id) : Field(opt, id) {}
	PointCtrl(wxWindow* parent, const ConfigOptionDef& opt, const t_config_option_key& id) : Field(parent, opt, id) {}

	wxSizer*		sizer{ nullptr };
	wxTextCtrl*		x_textctrl;
	wxTextCtrl*		y_textctrl;

	void			BUILD()  override;

	void			set_value(const Pointf value);
	void			set_value(boost::any value);
	boost::any		get_value() override;

	void			enable() override {
		x_textctrl->Enable();
		y_textctrl->Enable(); };
	void			disable() override{
		x_textctrl->Disable();
		y_textctrl->Disable(); };
	wxSizer*		getSizer() override { return sizer; }
};


#endif
} // GUI
} // Slic3r


