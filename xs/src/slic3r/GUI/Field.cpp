#include "GUI.hpp"//"slic3r_gui.hpp"
#include "Field.hpp"

//#include <wx/event.h>
#include <regex>
#include <wx/numformatter.h>
#include "PrintConfig.hpp"

namespace Slic3r { namespace GUI {

    void Field::_on_kill_focus(wxFocusEvent& event) {
        // Without this, there will be nasty focus bugs on Windows.
        // Also, docs for wxEvent::Skip() say "In general, it is recommended to skip all 
        // non-command events to allow the default handling to take place."
        event.Skip(1);

        // call the registered function if it is available
//!        if (on_kill_focus) 
//!            on_kill_focus(opt_id);
    }
    void Field::_on_change(wxCommandEvent& event) {
        std::cerr << "calling Field::_on_change \n";
//!        if (on_change != nullptr  && !disable_change_event)
//!            on_change(opt_id, "A");
    }


	bool Field::is_matched(std::string string, std::string pattern)
	{
		std::regex regex_pattern(pattern, std::regex_constants::icase); // use ::icase to make the matching case insensitive like /i in perl
		return std::regex_match(string, regex_pattern);
	}

	void TextCtrl::BUILD() {
        auto size = wxSize(wxDefaultSize);
        if (opt.height >= 0) size.SetHeight(opt.height);
        if (opt.width >= 0) size.SetWidth(opt.width);

		wxString text_value = wxString(""); 

		switch (opt.type) {
		case coFloatOrPercent:
		{
			if (static_cast<const ConfigOptionFloatOrPercent*>(opt.default_value)->percent)
			{
				text_value = wxString::Format(_T("%i"), int(opt.default_value->getFloat()));
				text_value += "%";
			}
			else
				wxNumberFormatter::ToString(opt.default_value->getFloat(), 2);
			break;
		}
		case coPercent:
		{
			text_value = wxString::Format(_T("%i"), int(opt.default_value->getFloat()));
			text_value += "%";
			break;
		}	
		case coPercents:
		{
			const ConfigOptionPercents *vec = static_cast<const ConfigOptionPercents*>(opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			double val = vec->get_at(0);
			text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
			break;
		}			
		case coFloat:
		{
			double val = opt.default_value->getFloat();
			text_value = (val - int(val)) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
			break;
		}			
		case coFloats:
		{
			const ConfigOptionFloats *vec = static_cast<const ConfigOptionFloats*>(opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			double val = vec->get_at(0);
			text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
			break;
		}
		case coString:			
			text_value = static_cast<const ConfigOptionString*>(opt.default_value)->value;
			break;
		case coStrings:
		{
			const ConfigOptionStrings *vec = static_cast<const ConfigOptionStrings*>(opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			text_value = vec->values.at(0);
			break;
		}
		default:
			break; 
		}

		auto temp = new wxTextCtrl(parent, wxID_ANY, text_value, wxDefaultPosition, size, (opt.multiline ? wxTE_MULTILINE : 0));

        if (opt.tooltip.length() > 0) { temp->SetToolTip(opt.tooltip); }
        
        temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { _on_change(e); }), temp->GetId());
        temp->Bind(wxEVT_KILL_FOCUS, ([this](wxFocusEvent e) { _on_kill_focus(e); }), temp->GetId());

        // recast as a wxWindow to fit the calling convention
        window = dynamic_cast<wxWindow*>(temp);

    }

    void TextCtrl::enable() { dynamic_cast<wxTextCtrl*>(window)->Enable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(true); }
    void TextCtrl::disable() { dynamic_cast<wxTextCtrl*>(window)->Disable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(false); }
    void TextCtrl::set_tooltip(const wxString& tip) { }

void CheckBox::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (opt.height >= 0) size.SetHeight(opt.height);
	if (opt.width >= 0) size.SetWidth(opt.width);

	bool check_value =	opt.type == coBool ? 
						opt.default_value->getBool() : opt.type == coBools ? 
						static_cast<ConfigOptionBools*>(opt.default_value)->values.at(0) : 
    					false;

	auto temp = new wxCheckBox(parent, wxID_ANY, wxString(""), wxDefaultPosition, size); 
	temp->SetValue(check_value);
	if (opt.readonly) temp->Disable();

	temp->Bind(wxEVT_CHECKBOX, ([this](wxCommandEvent e) { _on_change(e); }), temp->GetId());

	if (opt.tooltip.length() > 0) { temp->SetToolTip(opt.tooltip); }

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

int undef_spin_val = -9999;		//! Probably, It's not necessary

void SpinCtrl::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (opt.height >= 0) size.SetHeight(opt.height);
	if (opt.width >= 0) size.SetWidth(opt.width);

	wxString	text_value = wxString("");
	int			default_value = 0;

	switch (opt.type) {
	case coInt:
		default_value = opt.default_value->getInt();
		text_value = wxString::Format(_T("%i"), default_value);
		break;
	case coInts:
	{
		const ConfigOptionInts *vec = static_cast<const ConfigOptionInts*>(opt.default_value);
		if (vec == nullptr || vec->empty()) break;
		for (size_t id = 0; id < vec->size(); ++id)
		{
			default_value = vec->get_at(id);
			text_value += wxString::Format(_T("%i"), default_value);
		}
		break;
	}
	default:
		break;
	}

	auto temp = new wxSpinCtrl(parent, wxID_ANY, text_value, wxDefaultPosition, size,
		0, opt.min >0 ? opt.min : 0, opt.max < 2147483647 ? opt.max : 2147483647, default_value);

	temp->Bind(wxEVT_SPINCTRL, ([=](wxCommandEvent e) { tmp_value = undef_spin_val; _on_change(e); }), temp->GetId());
	temp->Bind(wxEVT_KILL_FOCUS, ([this](wxFocusEvent e) { tmp_value = undef_spin_val; _on_kill_focus(e); }), temp->GetId());
	temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e)
	{
// 		# On OSX / Cocoa, wxSpinCtrl::GetValue() doesn't return the new value
// 		# when it was changed from the text control, so the on_change callback
// 		# gets the old one, and on_kill_focus resets the control to the old value.
// 		# As a workaround, we get the new value from $event->GetString and store
// 		# here temporarily so that we can return it from $self->get_value
		std::string value = e.GetString();
		if (is_matched(value, "^\d+$"))
			tmp_value = std::stoi(value);
		_on_change(e);
// 		# We don't reset tmp_value here because _on_change might put callbacks
// 		# in the CallAfter queue, and we want the tmp value to be available from
// 		# them as well.
	}), temp->GetId());


	if (opt.tooltip.length() > 0) { temp->SetToolTip(opt.tooltip); }

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

void Choice::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (opt.height >= 0) size.SetHeight(opt.height);
	if (opt.width >= 0) size.SetWidth(opt.width);

	auto temp = new wxComboBox(parent, wxID_ANY, wxString(""), wxDefaultPosition, size);
	if (opt.gui_type.compare("select_open") != 0)
		temp->SetExtraStyle(wxCB_READONLY);

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	if (opt.enum_labels.empty() && opt.enum_values.empty()){
	}
	else{
		for (auto el : opt.enum_labels.empty() ? opt.enum_values : opt.enum_labels)
			temp->Append(wxString(el));
		set_selection();
	}
 	temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { _on_change(e); }), temp->GetId());
 	temp->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent e) { _on_change(e); }), temp->GetId());

	if (opt.tooltip.length() > 0) temp->SetToolTip(opt.tooltip);
}

void Choice::set_selection()
{
	wxString text_value = wxString("");
	switch (opt.type){
	case coFloat:
	case coPercent:	{
		double val = opt.default_value->getFloat();
		text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 1);
		auto idx = 0;
		for (auto el : opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		if (opt.type == coPercent) text_value += "%";
		idx == opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coEnum:{
		int id_value = static_cast<const ConfigOptionEnum<SeamPosition>*>(opt.default_value)->value; //!!
		dynamic_cast<wxComboBox*>(window)->SetSelection(id_value);
		break;
	}
	case coInt:{
		int val = opt.default_value->getInt(); //!!
		text_value = wxString::Format(_T("%i"), int(val));
		auto idx = 0;
		for (auto el : opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
// 	case coString:{
// 		text_value = static_cast<const ConfigOptionString*>(opt.default_value)->value;
// 
// 		auto idx = 0;
// 		for (auto el : opt.enum_values)
// 		{
// 			if (el.compare(text_value) == 0)
// 				break;
// 			++idx;
// 		}
// 		idx == opt.enum_values.size() ?
// 			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
// 			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
// 		break;
// 	}
	case coStrings:{
		text_value = static_cast<const ConfigOptionStrings*>(opt.default_value)->values.at(0);

		auto idx = 0;
		for (auto el : opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	}
}

void Choice::set_value(const std::string value)  //! Redundant?
{
	disable_change_event = true;

	auto idx=0;
	for (auto el : opt.enum_values)
	{
		if (el.compare(value) == 0)
			break;
		++idx;
	}

	idx == opt.enum_values.size() ? 
		dynamic_cast<wxComboBox*>(window)->SetValue(value) :
		dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
	
	disable_change_event = false;
}

//! it's needed for _update_serial_ports()
void Choice::set_values(const std::vector<std::string> values)
{
	disable_change_event = true;

// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
// 	# but we want to preserve it
	auto ww = dynamic_cast<wxComboBox*>(window);
	auto value = ww->GetValue();
	ww->Clear();
	for (auto el : values)
		ww->Append(wxString(el));
	ww->SetValue(value);

	disable_change_event = false;
}

void ColourPicker::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (opt.height >= 0) size.SetHeight(opt.height);
	if (opt.width >= 0) size.SetWidth(opt.width);

	wxString clr(static_cast<ConfigOptionStrings*>(opt.default_value)->values.at(0));
	auto temp = new wxColourPickerCtrl(parent, wxID_ANY, clr, wxDefaultPosition, size);
		
	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->Bind(wxEVT_COLOURPICKER_CHANGED, ([=](wxCommandEvent e) { _on_change(e); }), temp->GetId());

	if (opt.tooltip.length() > 0) temp->SetToolTip(opt.tooltip);

}

void Point::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (opt.height >= 0) size.SetHeight(opt.height);
	if (opt.width >= 0) size.SetWidth(opt.width);

	auto temp = new wxBoxSizer(wxHORIZONTAL);
	// 	$self->wxSizer($sizer);
	// 
	wxSize field_size(40, -1);

	auto default_pt = static_cast<ConfigOptionPoints*>(opt.default_value)->values.at(0);
	double val = default_pt.x;
	wxString X = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);
	val = default_pt.y;
	wxString Y = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2);

	x_textctrl = new wxTextCtrl(parent, wxID_ANY, X, wxDefaultPosition, field_size);
	y_textctrl = new wxTextCtrl(parent, wxID_ANY, Y, wxDefaultPosition, field_size);

	temp->Add(new wxStaticText(parent, wxID_ANY, "x:")/*, 0, wxALIGN_CENTER_VERTICAL, 0*/);
	temp->Add(x_textctrl);
	temp->Add(new wxStaticText(parent, wxID_ANY, "y:")/*, 0, wxALIGN_CENTER_VERTICAL, 0*/);
	temp->Add(y_textctrl);

	x_textctrl->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { _on_change(e/*$self->option->opt_id*/); }), x_textctrl->GetId());
	y_textctrl->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { _on_change(e/*$self->option->opt_id*/); }), x_textctrl->GetId());

	// 	// recast as a wxWindow to fit the calling convention
	sizer = dynamic_cast<wxSizer*>(temp);

	if (opt.tooltip.length() > 0)
	{
		x_textctrl->SetToolTip(opt.tooltip);
		y_textctrl->SetToolTip(opt.tooltip);
	}
}

void Point::set_value(const Pointf value)
{
	disable_change_event = true;

	double val = value.x;
	x_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2));
	val = value.y;
	y_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2));

	disable_change_event = false;
}

boost::any Point::get_value()
{
	Pointf ret_point;
	double val;
	x_textctrl->GetValue().ToDouble(&val);
	ret_point.x = val;
	y_textctrl->GetValue().ToDouble(&val);
	ret_point.y = val;
	return ret_point;
}

} // GUI
} // Slic3r


