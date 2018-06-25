#include "GUI.hpp"//"slic3r_gui.hpp"
#include "Field.hpp"

//#include <wx/event.h>
#include <regex>
#include <wx/numformatter.h>
#include <wx/tooltip.h>
#include "PrintConfig.hpp"
#include <boost/algorithm/string/predicate.hpp>

namespace Slic3r { namespace GUI {

	wxString double_to_string(double const value)
	{
		if (value - int(value) == 0)
			return wxString::Format(_T("%i"), int(value));
		else {
			int precision = 4;
			for (size_t p = 1; p < 4; p++)
			{
				double cur_val = pow(10, p)*value;
				if (cur_val - int(cur_val) == 0) {
					precision = p;
					break;
				}
			}
			return wxNumberFormatter::ToString(value, precision, wxNumberFormatter::Style_None);
		}
	}

	void Field::PostInitialize(){
		auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
		m_Undo_btn			= new MyButton(m_parent, wxID_ANY, "", wxDefaultPosition,wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
		m_Undo_to_sys_btn	= new MyButton(m_parent, wxID_ANY, "", wxDefaultPosition,wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
		if (wxMSW) {
			m_Undo_btn->SetBackgroundColour(color);
			m_Undo_to_sys_btn->SetBackgroundColour(color);
		}
		m_Undo_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent){ on_back_to_initial_value(); }));
		m_Undo_to_sys_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent){ on_back_to_sys_value(); }));

		//set default bitmap
		wxBitmap bmp;
		bmp.LoadFile(from_u8(var("bullet_white.png")), wxBITMAP_TYPE_PNG);
		set_undo_bitmap(&bmp);
		set_undo_to_sys_bitmap(&bmp);

		BUILD();
	}

	void Field::on_kill_focus(wxEvent& event) {
        // Without this, there will be nasty focus bugs on Windows.
        // Also, docs for wxEvent::Skip() say "In general, it is recommended to skip all 
        // non-command events to allow the default handling to take place."
		event.Skip();
		// call the registered function if it is available
        if (m_on_kill_focus!=nullptr) 
            m_on_kill_focus();
    }
    void Field::on_change_field()
	{
//        std::cerr << "calling Field::_on_change \n";
        if (m_on_change != nullptr  && !m_disable_change_event)
            m_on_change(m_opt_id, get_value());
    }

	void Field::on_back_to_initial_value(){
		if (m_back_to_initial_value != nullptr && m_is_modified_value)
			m_back_to_initial_value(m_opt_id);
	}

	void Field::on_back_to_sys_value(){
		if (m_back_to_sys_value != nullptr && m_is_nonsys_value)
			m_back_to_sys_value(m_opt_id);
	}

	wxString Field::get_tooltip_text(const wxString& default_string)
	{
		wxString tooltip_text("");
		wxString tooltip = L_str(m_opt.tooltip);
		if (tooltip.length() > 0)
			tooltip_text = tooltip + "(" + _(L("default")) + ": " +
							(boost::iends_with(m_opt_id, "_gcode") ? "\n" : "") + 
							default_string + ")";

		return tooltip_text;
	}

	bool Field::is_matched(const std::string& string, const std::string& pattern)
	{
		std::regex regex_pattern(pattern, std::regex_constants::icase); // use ::icase to make the matching case insensitive like /i in perl
		return std::regex_match(string, regex_pattern);
	}

	void Field::get_value_by_opt_type(wxString& str)
	{
		switch (m_opt.type){
		case coInt:
			m_value = wxAtoi(str);
			break;
		case coPercent:
		case coPercents:
		case coFloats:
		case coFloat:{
			if (m_opt.type == coPercent && str.Last() == '%') 
				str.RemoveLast();
			else if (str.Last() == '%')	{
				wxString label = m_Label->GetLabel();
				if		(label.Last() == '\n')	label.RemoveLast();
				while	(label.Last() == ' ')	label.RemoveLast();
				if		(label.Last() == ':')	label.RemoveLast();
				show_error(m_parent, wxString::Format(_(L("%s doesn't support percentage")), label));
				set_value(double_to_string(m_opt.min), true);
				m_value = double(m_opt.min);
				break;
			}
			double val;
			str.ToCDouble(&val);
			if (m_opt.min > val || val > m_opt.max)
			{
				show_error(m_parent, _(L("Input value is out of range")));
				if (m_opt.min > val) val = m_opt.min;
				if (val > m_opt.max) val = m_opt.max;
				set_value(double_to_string(val), true);
			}
			m_value = val;
			break; }
		case coString:
		case coStrings:
		case coFloatOrPercent:
			m_value = str.ToStdString();
			break;
		default:
			break;
		}
	}

	void TextCtrl::BUILD() {
        auto size = wxSize(wxDefaultSize);
        if (m_opt.height >= 0) size.SetHeight(m_opt.height);
        if (m_opt.width >= 0) size.SetWidth(m_opt.width);

		wxString text_value = wxString(""); 

		switch (m_opt.type) {
		case coFloatOrPercent:
		{
			text_value = double_to_string(m_opt.default_value->getFloat());
 			if (static_cast<const ConfigOptionFloatOrPercent*>(m_opt.default_value)->percent)
 				text_value += "%";
			break;
		}
		case coPercent:
		{
			text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getFloat()));
			text_value += "%";
			break;
		}	
		case coPercents:
		case coFloats:
		case coFloat:
		{
			double val = m_opt.type == coFloats ?
				static_cast<const ConfigOptionFloats*>(m_opt.default_value)->get_at(0) :
				m_opt.type == coFloat ? 
					m_opt.default_value->getFloat() :
					static_cast<const ConfigOptionPercents*>(m_opt.default_value)->get_at(0);
			text_value = double_to_string(val);
			break;
		}
		case coString:			
			text_value = static_cast<const ConfigOptionString*>(m_opt.default_value)->value;
			break;
		case coStrings:
		{
			const ConfigOptionStrings *vec = static_cast<const ConfigOptionStrings*>(m_opt.default_value);
			if (vec == nullptr || vec->empty()) break;
			if (vec->size() > 1)
				break;
			text_value = vec->values.at(0);
			break;
		}
		default:
			break; 
		}

		auto temp = new wxTextCtrl(m_parent, wxID_ANY, text_value, wxDefaultPosition, size, (m_opt.multiline ? wxTE_MULTILINE : 0));

		temp->SetToolTip(get_tooltip_text(text_value));
        
		temp->Bind(wxEVT_LEFT_DOWN, ([temp](wxEvent& event)
		{
			//! to allow the default handling
			event.Skip();
			//! eliminating the g-code pop up text description
			bool flag = false;
#ifdef __WXGTK__
			// I have no idea why, but on GTK flag works in other way
			flag = true;
#endif // __WXGTK__
			temp->GetToolTip()->Enable(flag);
		}), temp->GetId());

#if !defined(__WXGTK__)
		temp->Bind(wxEVT_KILL_FOCUS, ([this, temp](wxEvent& e)
		{
			e.Skip();//	on_kill_focus(e);
			temp->GetToolTip()->Enable(true);
		}), temp->GetId());
#endif // __WXGTK__

		temp->Bind(wxEVT_TEXT, ([this](wxCommandEvent)
		{
#ifdef __WXGTK__
			bChangedValueEvent = true;
#else
			on_change_field();
#endif //__WXGTK__
		}), temp->GetId());

#ifdef __WXGTK__
		temp->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event)
		{
			if (bChangedValueEvent)	{
				on_change_field();
				bChangedValueEvent = false;
			}
			event.Skip();
		});
#endif //__WXGTK__

		// select all text using Ctrl+A
		temp->Bind(wxEVT_CHAR, ([temp](wxKeyEvent& event)
		{
			if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL))
				temp->SetSelection(-1, -1); //select all
			event.Skip();
		}));

        // recast as a wxWindow to fit the calling convention
        window = dynamic_cast<wxWindow*>(temp);
    }	

	boost::any& TextCtrl::get_value()
	{
		wxString ret_str = static_cast<wxTextCtrl*>(window)->GetValue();
		get_value_by_opt_type(ret_str);

		return m_value;
	}

	void TextCtrl::enable() { dynamic_cast<wxTextCtrl*>(window)->Enable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(true); }
    void TextCtrl::disable() { dynamic_cast<wxTextCtrl*>(window)->Disable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(false); }

void CheckBox::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	bool check_value =	m_opt.type == coBool ? 
						m_opt.default_value->getBool() : m_opt.type == coBools ? 
						static_cast<ConfigOptionBools*>(m_opt.default_value)->values.at(0) : 
    					false;

	auto temp = new wxCheckBox(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size); 
	temp->SetValue(check_value);
	if (m_opt.readonly) temp->Disable();

	temp->Bind(wxEVT_CHECKBOX, ([this](wxCommandEvent e) { on_change_field(); }), temp->GetId());

	temp->SetToolTip(get_tooltip_text(check_value ? "true" : "false")); 

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

boost::any& CheckBox::get_value()
{
// 	boost::any m_value;
	bool value = dynamic_cast<wxCheckBox*>(window)->GetValue();
	if (m_opt.type == coBool)
		m_value = static_cast<bool>(value);
	else
		m_value = static_cast<unsigned char>(value);
 	return m_value;
}

int undef_spin_val = -9999;		//! Probably, It's not necessary

void SpinCtrl::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	wxString	text_value = wxString("");
	int			default_value = 0;

	switch (m_opt.type) {
	case coInt:
		default_value = m_opt.default_value->getInt();
		text_value = wxString::Format(_T("%i"), default_value);
		break;
	case coInts:
	{
		const ConfigOptionInts *vec = static_cast<const ConfigOptionInts*>(m_opt.default_value);
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

	const int min_val = m_opt_id == "standby_temperature_delta" ? 
						-500 : m_opt.min > 0 ? 
						m_opt.min : 0;
	const int max_val = m_opt.max < 2147483647 ? m_opt.max : 2147483647;

	auto temp = new wxSpinCtrl(m_parent, wxID_ANY, text_value, wxDefaultPosition, size,
		0, min_val, max_val, default_value);

// 	temp->Bind(wxEVT_SPINCTRL, ([this](wxCommandEvent e) { tmp_value = undef_spin_val; on_change_field(); }), temp->GetId());
// 	temp->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent& e) { tmp_value = undef_spin_val; on_kill_focus(e); }), temp->GetId());
	temp->Bind(wxEVT_TEXT, ([this](wxCommandEvent e)
	{
// 		# On OSX / Cocoa, wxSpinCtrl::GetValue() doesn't return the new value
// 		# when it was changed from the text control, so the on_change callback
// 		# gets the old one, and on_kill_focus resets the control to the old value.
// 		# As a workaround, we get the new value from $event->GetString and store
// 		# here temporarily so that we can return it from $self->get_value
		std::string value = e.GetString().utf8_str().data();
		if (is_matched(value, "^\\d+$"))
			tmp_value = std::stoi(value);
		on_change_field();
// 		# We don't reset tmp_value here because _on_change might put callbacks
// 		# in the CallAfter queue, and we want the tmp value to be available from
// 		# them as well.
	}), temp->GetId());
	
	temp->SetToolTip(get_tooltip_text(text_value));

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

void Choice::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	wxComboBox* temp;	
	if (!m_opt.gui_type.empty() && m_opt.gui_type.compare("select_open") != 0)
		temp = new wxComboBox(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size);
	else
		temp = new wxComboBox(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, NULL, wxCB_READONLY);

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	if (m_opt.enum_labels.empty() && m_opt.enum_values.empty()){
	}
	else{
		for (auto el : m_opt.enum_labels.empty() ? m_opt.enum_values : m_opt.enum_labels){
			const wxString& str = m_opt_id == "support" ? L_str(el) : el;
			temp->Append(str);
		}
		set_selection();
	}
 	temp->Bind(wxEVT_TEXT, ([this](wxCommandEvent e) { on_change_field(); }), temp->GetId());
 	temp->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent e) { on_change_field(); }), temp->GetId());

	temp->SetToolTip(get_tooltip_text(temp->GetValue()));
}

void Choice::set_selection()
{
	wxString text_value = wxString("");
	switch (m_opt.type){
	case coFloat:
	case coPercent:	{
		double val = m_opt.default_value->getFloat();
		text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 1);
		size_t idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
//		if (m_opt.type == coPercent) text_value += "%";
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coEnum:{
		int id_value = static_cast<const ConfigOptionEnum<SeamPosition>*>(m_opt.default_value)->value; //!!
		dynamic_cast<wxComboBox*>(window)->SetSelection(id_value);
		break;
	}
	case coInt:{
		int val = m_opt.default_value->getInt(); //!!
		text_value = wxString::Format(_T("%i"), int(val));
		size_t idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coStrings:{
		text_value = static_cast<const ConfigOptionStrings*>(m_opt.default_value)->values.at(0);

		size_t idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	}
}

void Choice::set_value(const std::string& value, bool change_event)  //! Redundant?
{
	m_disable_change_event = !change_event;

	size_t idx=0;
	for (auto el : m_opt.enum_values)
	{
		if (el.compare(value) == 0)
			break;
		++idx;
	}

	idx == m_opt.enum_values.size() ? 
		dynamic_cast<wxComboBox*>(window)->SetValue(value) :
		dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
	
	m_disable_change_event = false;
}

void Choice::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;

	switch (m_opt.type){
	case coInt:
	case coFloat:
	case coPercent:
	case coString:
	case coStrings:{
		wxString text_value;
		if (m_opt.type == coInt) 
			text_value = wxString::Format(_T("%i"), int(boost::any_cast<int>(value)));
		else
			text_value = boost::any_cast<wxString>(value);
		auto idx = 0;
		for (auto el : m_opt.enum_values)
		{
			if (el.compare(text_value) == 0)
				break;
			++idx;
		}
		idx == m_opt.enum_values.size() ?
			dynamic_cast<wxComboBox*>(window)->SetValue(text_value) :
			dynamic_cast<wxComboBox*>(window)->SetSelection(idx);
		break;
	}
	case coEnum:{
		int val = boost::any_cast<int>(value);
		if (m_opt_id.compare("external_fill_pattern") == 0)
		{
			if (!m_opt.enum_values.empty()){
				std::string key;
				t_config_enum_values map_names = ConfigOptionEnum<InfillPattern>::get_enum_values();				
				for (auto it : map_names) {
					if (val == it.second) {
						key = it.first;
						break;
					}
				}

				size_t idx = 0;
				for (auto el : m_opt.enum_values)
				{
					if (el.compare(key) == 0)
						break;
					++idx;
				}

				val = idx == m_opt.enum_values.size() ? 0 : idx;
			}
			else
				val = 0;
		}
		dynamic_cast<wxComboBox*>(window)->SetSelection(val);
		break;
	}
	default:
		break;
	}

	m_disable_change_event = false;
}

//! it's needed for _update_serial_ports()
void Choice::set_values(const std::vector<std::string>& values)
{
	if (values.empty())
		return;
	m_disable_change_event = true;

// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
// 	# but we want to preserve it
	auto ww = dynamic_cast<wxComboBox*>(window);
	auto value = ww->GetValue();
	ww->Clear();
	ww->Append("");
	for (auto el : values)
		ww->Append(wxString(el));
	ww->SetValue(value);

	m_disable_change_event = false;
}

boost::any& Choice::get_value()
{
// 	boost::any m_value;
	wxString ret_str = static_cast<wxComboBox*>(window)->GetValue();	

	if (m_opt_id == "support")
		return m_value = boost::any(ret_str);//ret_str;

	if (m_opt.type != coEnum)
		/*m_value = */get_value_by_opt_type(ret_str);
	else
	{
		int ret_enum = static_cast<wxComboBox*>(window)->GetSelection(); 
		if (m_opt_id.compare("external_fill_pattern") == 0)
		{
			if (!m_opt.enum_values.empty()){
				std::string key = m_opt.enum_values[ret_enum];
				t_config_enum_values map_names = ConfigOptionEnum<InfillPattern>::get_enum_values();
				int value = map_names.at(key);

				m_value = static_cast<InfillPattern>(value);
			}
			else
				m_value = static_cast<InfillPattern>(0);
		}
		if (m_opt_id.compare("fill_pattern") == 0)
			m_value = static_cast<InfillPattern>(ret_enum);
		else if (m_opt_id.compare("gcode_flavor") == 0)
			m_value = static_cast<GCodeFlavor>(ret_enum);
		else if (m_opt_id.compare("support_material_pattern") == 0)
			m_value = static_cast<SupportMaterialPattern>(ret_enum);
		else if (m_opt_id.compare("seam_position") == 0)
			m_value = static_cast<SeamPosition>(ret_enum);
	}	

	return m_value;
}

void ColourPicker::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	wxString clr(static_cast<ConfigOptionStrings*>(m_opt.default_value)->values.at(0));
	auto temp = new wxColourPickerCtrl(m_parent, wxID_ANY, clr, wxDefaultPosition, size);
		
	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->Bind(wxEVT_COLOURPICKER_CHANGED, ([this](wxCommandEvent e) { on_change_field(); }), temp->GetId());

	temp->SetToolTip(get_tooltip_text(clr));
}

boost::any& ColourPicker::get_value(){
// 	boost::any m_value;

	auto colour = static_cast<wxColourPickerCtrl*>(window)->GetColour();
	auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), colour.Red(), colour.Green(), colour.Blue());
	m_value = clr_str.ToStdString();

	return m_value;
}

void PointCtrl::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	auto temp = new wxBoxSizer(wxHORIZONTAL);
	// 	$self->wxSizer($sizer);
	// 
	wxSize field_size(40, -1);

	auto default_pt = static_cast<ConfigOptionPoints*>(m_opt.default_value)->values.at(0);
	double val = default_pt.x;
	wxString X = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None);
	val = default_pt.y;
	wxString Y = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None);

	x_textctrl = new wxTextCtrl(m_parent, wxID_ANY, X, wxDefaultPosition, field_size);
	y_textctrl = new wxTextCtrl(m_parent, wxID_ANY, Y, wxDefaultPosition, field_size);

	temp->Add(new wxStaticText(m_parent, wxID_ANY, "x : "), 0, wxALIGN_CENTER_VERTICAL, 0);
	temp->Add(x_textctrl);
	temp->Add(new wxStaticText(m_parent, wxID_ANY, "   y : "), 0, wxALIGN_CENTER_VERTICAL, 0);
	temp->Add(y_textctrl);

	x_textctrl->Bind(wxEVT_TEXT, ([this](wxCommandEvent e) { on_change_field(); }), x_textctrl->GetId());
	y_textctrl->Bind(wxEVT_TEXT, ([this](wxCommandEvent e) { on_change_field(); }), y_textctrl->GetId());

	// 	// recast as a wxWindow to fit the calling convention
	sizer = dynamic_cast<wxSizer*>(temp);

	x_textctrl->SetToolTip(get_tooltip_text(X+", "+Y));
	y_textctrl->SetToolTip(get_tooltip_text(X+", "+Y));
}

void PointCtrl::set_value(const Pointf& value, bool change_event)
{
	m_disable_change_event = !change_event;

	double val = value.x;
	x_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None));
	val = value.y;
	y_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None));

	m_disable_change_event = false;
}

void PointCtrl::set_value(const boost::any& value, bool change_event)
{
	Pointf pt;
	const Pointf *ptf = boost::any_cast<Pointf>(&value);
	if (!ptf)
	{
		ConfigOptionPoints* pts = boost::any_cast<ConfigOptionPoints*>(value);
		pt = pts->values.at(0);
	}
	else
		pt = *ptf;
	set_value(pt, change_event);
}

boost::any& PointCtrl::get_value()
{
	Pointf ret_point;
	double val;
	x_textctrl->GetValue().ToDouble(&val);
	ret_point.x = val;
	y_textctrl->GetValue().ToDouble(&val);
	ret_point.y = val;
	return m_value = ret_point;
}

} // GUI
} // Slic3r


