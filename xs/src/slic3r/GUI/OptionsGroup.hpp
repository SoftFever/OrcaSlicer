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

/// Wraps a ConfigOptionDef and adds function object for creating a side_widget.
struct Option {
	ConfigOptionDef			opt { ConfigOptionDef() };
	t_config_option_key		opt_id;//! {""};
    widget_t				side_widget {nullptr};
    bool					readonly {false};

	Option(const ConfigOptionDef& _opt, t_config_option_key id) :
		opt(_opt), opt_id(id) { translate(); }
	void		translate();
};
using t_option = std::unique_ptr<Option>;	//!

/// Represents option lines
class Line {
public:
    wxString	label {wxString("")};
    wxString	label_tooltip {wxString("")};
    size_t		full_width {0}; 
    wxSizer*	sizer {nullptr};
    widget_t	widget {nullptr};

    void append_option(const Option& option) {
        m_options.push_back(option);
    }
	void append_widget(const widget_t widget) {
		m_extra_widgets.push_back(widget);
    }
	Line(wxString label, wxString tooltip) :
		label(label), label_tooltip(tooltip) {}

    const std::vector<widget_t>&	get_extra_widgets() const {return m_extra_widgets;}
    const std::vector<Option>&		get_options() const { return m_options; }

private:
	std::vector<Option>		m_options;//! {std::vector<Option>()};
    std::vector<widget_t>	m_extra_widgets;//! {std::vector<widget_t>()};
};

using t_optionfield_map = std::map<t_config_option_key, t_field>;

class OptionsGroup {
public:
    const bool		staticbox {true};
    const wxString	title {wxString("")};
    size_t			label_width {200};
    wxSizer*		sizer {nullptr};
    column_t		extra_column {nullptr};
    t_change		m_on_change {nullptr};

    wxFont			sidetext_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };
    wxFont			label_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };

    /// Returns a copy of the pointer of the parent wxWindow.
    /// Accessor function is because users are not allowed to change the parent
    /// but defining it as const means a lot of const_casts to deal with wx functions.
    inline wxWindow* parent() const { return m_parent; }

    void		append_line(const Line& line);
    Line		create_single_option_line(const Option& option) const;
    void		append_single_option_line(const Option& option) { append_line(create_single_option_line(option)); }

    // return a non-owning pointer reference 
    inline /*const*/ Field*	get_field(t_config_option_key id) const { try { return m_fields.at(id).get(); } catch (std::out_of_range e) { return nullptr; } }
	bool			set_value(t_config_option_key id, boost::any value) { try { m_fields.at(id)->set_value(value); return true; } catch (std::out_of_range e) { return false; } }
	boost::any		get_value(t_config_option_key id) { boost::any out; try { out = m_fields.at(id)->get_value(); } catch (std::out_of_range e) { ; } return out; }

	inline void		enable() { for (auto& field : m_fields) field.second->enable(); }
    inline void		disable() { for (auto& field : m_fields) field.second->disable(); }

    OptionsGroup(wxWindow* _parent, wxString title) : 
		m_parent(_parent), title(title) {
        sizer = (staticbox ? new wxStaticBoxSizer(new wxStaticBox(_parent, wxID_ANY, title), wxVERTICAL) : new wxBoxSizer(wxVERTICAL));
        auto num_columns = 1U;
        if (label_width != 0) num_columns++;
        if (extra_column != nullptr) num_columns++;
        m_grid_sizer = new wxFlexGridSizer(0, num_columns, 0,0);
        static_cast<wxFlexGridSizer*>(m_grid_sizer)->SetFlexibleDirection(wxHORIZONTAL);
        static_cast<wxFlexGridSizer*>(m_grid_sizer)->AddGrowableCol(label_width != 0);

        sizer->Add(m_grid_sizer, 0, wxEXPAND | wxALL, wxOSX ? 0: 5);
    }

protected:
	std::map<t_config_option_key, Option>	m_options;
    wxWindow*				m_parent {nullptr};

    /// Field list, contains unique_ptrs of the derived type.
    /// using types that need to know what it is beyond the public interface 
    /// need to cast based on the related ConfigOptionDef.
    t_optionfield_map		m_fields;
    bool					m_disabled {false};
    wxGridSizer*			m_grid_sizer {nullptr};

    /// Generate a wxSizer or wxWindow from a configuration option
    /// Precondition: opt resolves to a known ConfigOption
    /// Postcondition: fields contains a wx gui object.
    const t_field&		build_field(const t_config_option_key& id, const ConfigOptionDef& opt);
    const t_field&		build_field(const t_config_option_key& id);
    const t_field&		build_field(const Option& opt);

    virtual void		on_kill_focus (){};
	virtual void		on_change_OG(t_config_option_key opt_id, boost::any value);
};

class ConfigOptionsGroup: public OptionsGroup {
public:
	ConfigOptionsGroup(wxWindow* parent, wxString title, DynamicPrintConfig* _config = nullptr) : 
		OptionsGroup(parent, title), m_config(_config) {}

    /// reference to libslic3r config, non-owning pointer (?).
    DynamicPrintConfig*		m_config {nullptr};
    bool					m_full_labels {0};
	std::map< std::string, std::pair<std::string, int> > m_opt_map;

	Option		get_option(const std::string opt_key, int opt_index = -1);
	Line		create_single_option_line(const std::string title, int idx = -1) /*const*/{
		Option option = get_option(title, idx);
		return OptionsGroup::create_single_option_line(option);
	}
	void		append_single_option_line(const Option& option)	{
		OptionsGroup::append_single_option_line(option);
	}
	void		append_single_option_line(const std::string title, int idx = -1)
	{
		Option option = get_option(title, idx);
		append_single_option_line(option);		
	}

	void		on_change_OG(t_config_option_key opt_id, boost::any value) override;
	void		on_kill_focus() override
	{
		reload_config();
	}
	void		reload_config();
	boost::any	config_value(std::string opt_key, int opt_index, bool deserialize);
	// return option value from config 
	boost::any	get_config_value(DynamicPrintConfig& config, std::string opt_key, int opt_index = -1);
	Field*		get_fieldc(t_config_option_key opt_key, int opt_index);
};

//  Static text shown among the options.
class ogStaticText :public wxStaticText{
public:
	ogStaticText() {}
	ogStaticText(wxWindow* parent, const char *text) : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize){}
	~ogStaticText(){}

	void SetText(wxString value);
};

}}
