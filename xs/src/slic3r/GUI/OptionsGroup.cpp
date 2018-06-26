#include "OptionsGroup.hpp"
#include "ConfigExceptions.hpp"

#include <utility>
#include <wx/numformatter.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include "Utils.hpp"

namespace Slic3r { namespace GUI {

const t_field& OptionsGroup::build_field(const Option& opt, wxStaticText* label/* = nullptr*/) {
    return build_field(opt.opt_id, opt.opt, label);
}
const t_field& OptionsGroup::build_field(const t_config_option_key& id, wxStaticText* label/* = nullptr*/) {
	const ConfigOptionDef& opt = m_options.at(id).opt;
    return build_field(id, opt, label);
}

const t_field& OptionsGroup::build_field(const t_config_option_key& id, const ConfigOptionDef& opt, wxStaticText* label/* = nullptr*/) {
    // Check the gui_type field first, fall through
    // is the normal type.
    if (opt.gui_type.compare("select") == 0) {
    } else if (opt.gui_type.compare("select_open") == 0) {
		m_fields.emplace(id, STDMOVE(Choice::Create<Choice>(parent(), opt, id)));
    } else if (opt.gui_type.compare("color") == 0) {
		m_fields.emplace(id, STDMOVE(ColourPicker::Create<ColourPicker>(parent(), opt, id)));
    } else if (opt.gui_type.compare("f_enum_open") == 0 || 
                opt.gui_type.compare("i_enum_open") == 0 ||
                opt.gui_type.compare("i_enum_closed") == 0) {
		m_fields.emplace(id, STDMOVE(Choice::Create<Choice>(parent(), opt, id)));
    } else if (opt.gui_type.compare("slider") == 0) {
    } else if (opt.gui_type.compare("i_spin") == 0) { // Spinctrl
    } else if (opt.gui_type.compare("legend") == 0) { // StaticText
		m_fields.emplace(id, STDMOVE(StaticText::Create<StaticText>(parent(), opt, id)));
    } else { 
        switch (opt.type) {
            case coFloatOrPercent:
            case coFloat:
            case coFloats:
			case coPercent:
			case coPercents:
			case coString:
			case coStrings:
				m_fields.emplace(id, STDMOVE(TextCtrl::Create<TextCtrl>(parent(), opt, id)));
                break;
			case coBool:
			case coBools:
				m_fields.emplace(id, STDMOVE(CheckBox::Create<CheckBox>(parent(), opt, id)));
				break;
			case coInt:
			case coInts:
				m_fields.emplace(id, STDMOVE(SpinCtrl::Create<SpinCtrl>(parent(), opt, id)));
				break;
            case coEnum:
				m_fields.emplace(id, STDMOVE(Choice::Create<Choice>(parent(), opt, id)));
				break;
            case coPoints:
				m_fields.emplace(id, STDMOVE(PointCtrl::Create<PointCtrl>(parent(), opt, id)));
				break;
            case coNone:   break;
            default:
				throw /*//!ConfigGUITypeError("")*/std::logic_error("This control doesn't exist till now"); break;
        }
    }
    // Grab a reference to fields for convenience
    const t_field& field = m_fields[id];
	field->m_on_change = [this](std::string opt_id, boost::any value){
			//! This function will be called from Field.					
			//! Call OptionGroup._on_change(...)
			if (!this->m_disabled) 
				this->on_change_OG(opt_id, value);
	};
	field->m_on_kill_focus = [this](){
			//! This function will be called from Field.					
			if (!this->m_disabled) 
				this->on_kill_focus();
	};
    field->m_parent = parent();
	
	//! Label to change background color, when option is modified
	field->m_Label = label;
	field->m_back_to_initial_value = [this](std::string opt_id){
		if (!this->m_disabled)
			this->back_to_initial_value(opt_id);
	};
	field->m_back_to_sys_value = [this](std::string opt_id){
		if (!this->m_disabled)
			this->back_to_sys_value(opt_id);
	};
	if (!m_show_modified_btns) {
		field->m_Undo_btn->Hide();
		field->m_Undo_to_sys_btn->Hide();
	}
//	if (nonsys_btn_icon != nullptr)
//		field->set_nonsys_btn_icon(*nonsys_btn_icon);
    
	// assign function objects for callbacks, etc.
    return field;
}

void OptionsGroup::append_line(const Line& line, wxStaticText**	colored_Label/* = nullptr*/) {
//!    if (line.sizer != nullptr || (line.widget != nullptr && line.full_width > 0)){
	if ( (line.sizer != nullptr || line.widget != nullptr) && line.full_width){
		if (line.sizer != nullptr) {
            sizer->Add(line.sizer, 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
        if (line.widget != nullptr) {
            sizer->Add(line.widget(m_parent), 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
    }

	auto option_set = line.get_options();
	for (auto opt : option_set) 
		m_options.emplace(opt.opt_id, opt);

	// if we have a single option with no label, no sidetext just add it directly to sizer
	if (option_set.size() == 1 && label_width == 0 && option_set.front().opt.full_width &&
		option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr && 
		line.get_extra_widgets().size() == 0) {
		wxSizer* tmp_sizer;
#ifdef __WXGTK__
		tmp_sizer = new wxBoxSizer(wxVERTICAL);
        m_panel->SetSizer(tmp_sizer);
        m_panel->Layout();
#else
        tmp_sizer = sizer;
#endif /* __WXGTK__ */

		const auto& option = option_set.front();
		const auto& field = build_field(option);

		auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
		btn_sizer->Add(field->m_Undo_to_sys_btn);
		btn_sizer->Add(field->m_Undo_btn);
		tmp_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 0);
		if (is_window_field(field))
			tmp_sizer->Add(field->getWindow(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		if (is_sizer_field(field))
			tmp_sizer->Add(field->getSizer(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		return;
	}

    auto grid_sizer = m_grid_sizer;
#ifdef __WXGTK__
        m_panel->SetSizer(m_grid_sizer);
        m_panel->Layout();
#endif /* __WXGTK__ */

    // Build a label if we have it
	wxStaticText* label=nullptr;
    if (label_width != 0) {
		long label_style = staticbox ? 0 : wxALIGN_RIGHT;
#ifdef __WXGTK__
		// workaround for correct text align of the StaticBox on Linux
		// flags wxALIGN_RIGHT and wxALIGN_CENTRE don't work when Ellipsize flags are _not_ given.
		// Text is properly aligned only when Ellipsize is checked.
		label_style |= staticbox ? 0 : wxST_ELLIPSIZE_END;
#endif /* __WXGTK__ */
		label = new wxStaticText(parent(), wxID_ANY, line.label + (line.label.IsEmpty() ? "" : ":"), 
							wxDefaultPosition, wxSize(label_width, -1), label_style);
        label->SetFont(label_font);
        label->Wrap(label_width); // avoid a Linux/GTK bug
		grid_sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, 5);
		if (line.label_tooltip.compare("") != 0)
			label->SetToolTip(line.label_tooltip);
    }

    // If there's a widget, build it and add the result to the sizer.
	if (line.widget != nullptr) {
		auto wgt = line.widget(parent());
		// If widget doesn't have label, don't use border
		grid_sizer->Add(wgt, 0, wxEXPAND | wxBOTTOM | wxTOP, (wxOSX || line.label.IsEmpty()) ? 0 : 5);
		if (colored_Label != nullptr) *colored_Label = label;
		return;
	}
	
	// if we have a single option with no sidetext just add it directly to the grid sizer
	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	grid_sizer->Add(sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM|wxTOP|wxLEFT), staticbox ? 0 : 1);
	if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
		option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0) {
		const auto& option = option_set.front();
		const auto& field = build_field(option, label);

		sizer->Add(field->m_Undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL); 
		sizer->Add(field->m_Undo_btn, 0, wxALIGN_CENTER_VERTICAL);
		if (is_window_field(field)) 
			sizer->Add(field->getWindow(), option.opt.full_width ? 1 : 0, (option.opt.full_width ? wxEXPAND : 0) |
							wxBOTTOM | wxTOP | wxALIGN_CENTER_VERTICAL, (wxOSX||!staticbox) ? 0 : 2);
		if (is_sizer_field(field)) 
			sizer->Add(field->getSizer(), 0, (option.opt.full_width ? wxEXPAND : 0) | wxALIGN_CENTER_VERTICAL, 0);
		return;
	}

    // if we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things
	for (auto opt : option_set) {
		ConfigOptionDef option = opt.opt;
		// add label if any
		if (option.label != "") {
			wxString str_label = _(option.label);
//!			To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
// 			wxString str_label = (option.label == "Top" || option.label == "Bottom") ?
// 								wxGETTEXT_IN_CONTEXT("Layers", wxString(option.label.c_str()):
// 								L_str(option.label);
			label = new wxStaticText(parent(), wxID_ANY, str_label + ":", wxDefaultPosition, wxDefaultSize);
			label->SetFont(label_font);
			sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
		}

		// add field
		const Option& opt_ref = opt;
		auto& field = build_field(opt_ref, label);
		sizer->Add(field->m_Undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
		sizer->Add(field->m_Undo_btn, 0, wxALIGN_CENTER_VERTICAL, 0);
		is_sizer_field(field) ? 
			sizer->Add(field->getSizer(), 0, wxALIGN_CENTER_VERTICAL, 0) :
			sizer->Add(field->getWindow(), 0, wxALIGN_CENTER_VERTICAL, 0);
		
		// add sidetext if any
		if (option.sidetext != "") {
			auto sidetext = new wxStaticText(parent(), wxID_ANY, _(option.sidetext), wxDefaultPosition, wxDefaultSize);
			sidetext->SetFont(sidetext_font);
			sizer->Add(sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
		}

		// add side widget if any
		if (opt.side_widget != nullptr) {
			sizer->Add(opt.side_widget(parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);	//! requires verification
		}

		if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
		{
			sizer->AddSpacer(6);
	    }
	}
	// add extra sizers if any
	for (auto extra_widget : line.get_extra_widgets()) {
		sizer->Add(extra_widget(parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);		//! requires verification
	}
}

Line OptionsGroup::create_single_option_line(const Option& option) const {
	Line retval{ _(option.opt.label), _(option.opt.tooltip) };
    Option tmp(option);
    tmp.opt.label = std::string("");
    retval.append_option(tmp);
    return retval;
}

void OptionsGroup::on_change_OG(const t_config_option_key& opt_id, const boost::any& value) {
	if (m_on_change != nullptr)
		m_on_change(opt_id, value);
}

Option ConfigOptionsGroup::get_option(const std::string& opt_key, int opt_index /*= -1*/)
{
	if (!m_config->has(opt_key)) {
		std::cerr << "No " << opt_key << " in ConfigOptionsGroup config.";
	}

	std::string opt_id = opt_index == -1 ? opt_key : opt_key + "#" + std::to_string(opt_index);
	std::pair<std::string, int> pair(opt_key, opt_index);
	m_opt_map.emplace(opt_id, pair);

	return Option(*m_config->def()->get(opt_key), opt_id);
}

void ConfigOptionsGroup::on_change_OG(const t_config_option_key& opt_id, const boost::any& value)
{
	if (!m_opt_map.empty())
	{
		auto it = m_opt_map.find(opt_id);
		if (it == m_opt_map.end())
		{
			OptionsGroup::on_change_OG(opt_id, value);
			return;
		}		

		auto itOption = it->second;
		std::string opt_key = itOption.first;
		int opt_index = itOption.second;

		auto option = m_options.at(opt_id).opt;

		// get value
//!		auto field_value = get_value(opt_id);
		if (option.gui_flags.compare("serialized")==0) {
			if (opt_index != -1){
				// 		die "Can't set serialized option indexed value" ;
			}
			change_opt_value(*m_config, opt_key, value);
		}
		else {
			if (opt_index == -1) {
				// change_opt_value(*m_config, opt_key, field_value);
				//!? why field_value?? in this case changed value will be lose! No?
				change_opt_value(*m_config, opt_key, value);
			}
			else {
				change_opt_value(*m_config, opt_key, value, opt_index);
// 				auto value = m_config->get($opt_key);
// 				$value->[$opt_index] = $field_value;
// 				$self->config->set($opt_key, $value);
			}
		}
	}

	OptionsGroup::on_change_OG(opt_id, value); //!? Why doing this
}

void ConfigOptionsGroup::back_to_initial_value(const std::string& opt_key)
{
	if (m_get_initial_config == nullptr)
		return;
	back_to_config_value(m_get_initial_config(), opt_key);
}

void ConfigOptionsGroup::back_to_sys_value(const std::string& opt_key)
{
	if (m_get_sys_config == nullptr)
		return;
	if (!have_sys_config())
		return;
	back_to_config_value(m_get_sys_config(), opt_key);
}

void ConfigOptionsGroup::back_to_config_value(const DynamicPrintConfig& config, const std::string& opt_key)
{
	boost::any value;
	if (opt_key == "extruders_count"){
		auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
		value = int(nozzle_diameter->values.size());
	}
	else if (m_opt_map.find(opt_key) != m_opt_map.end())
	{
		auto opt_id = m_opt_map.find(opt_key)->first;
		std::string opt_short_key = m_opt_map.at(opt_id).first;
		int opt_index = m_opt_map.at(opt_id).second;
		value = get_config_value(config, opt_short_key, opt_index);
	}
	else{
		value = get_config_value(config, opt_key);
		change_opt_value(*m_config, opt_key, value);
		return;
	}

	set_value(opt_key, value);
	on_change_OG(opt_key, get_value(opt_key));
}

void ConfigOptionsGroup::reload_config(){
	for (t_opt_map::iterator it = m_opt_map.begin(); it != m_opt_map.end(); ++it) {
		auto opt_id = it->first;
		std::string opt_key = m_opt_map.at(opt_id).first;
		int opt_index = m_opt_map.at(opt_id).second;
		auto option = m_options.at(opt_id).opt;
		set_value(opt_id, config_value(opt_key, opt_index, option.gui_flags.compare("serialized") == 0 ));
	}

}

boost::any ConfigOptionsGroup::config_value(const std::string& opt_key, int opt_index, bool deserialize){

	if (deserialize) {
		// Want to edit a vector value(currently only multi - strings) in a single edit box.
		// Aggregate the strings the old way.
		// Currently used for the post_process config value only.
		if (opt_index != -1)
			throw std::out_of_range("Can't deserialize option indexed value");
// 		return join(';', m_config->get(opt_key)});
		return get_config_value(*m_config, opt_key);
	}
	else {
//		return opt_index == -1 ? m_config->get(opt_key) : m_config->get_at(opt_key, opt_index);
		return get_config_value(*m_config, opt_key, opt_index);
	}
}

boost::any ConfigOptionsGroup::get_config_value(const DynamicPrintConfig& config, const std::string& opt_key, int opt_index /*= -1*/)
{
	size_t idx = opt_index == -1 ? 0 : opt_index;
	
	boost::any ret;
	wxString text_value = wxString("");
	const ConfigOptionDef* opt = config.def()->get(opt_key);
	switch (opt->type){
	case coFloatOrPercent:{
		const auto &value = *config.option<ConfigOptionFloatOrPercent>(opt_key);
		if (value.percent)
		{
			text_value = wxString::Format(_T("%i"), int(value.value));
			text_value += "%";
		}
		else
			text_value = double_to_string(value.value);
		ret = text_value;
		break;
	}
	case coPercent:{
		double val = config.option<ConfigOptionPercent>(opt_key)->value;
		text_value = wxString::Format(_T("%i"), int(val));
		ret = text_value;// += "%";
	}
		break;
	case coPercents:
	case coFloats:
	case coFloat:{
		double val = opt->type == coFloats ?
					config.opt_float(opt_key, idx) :
						opt->type == coFloat ? config.opt_float(opt_key) :
						config.option<ConfigOptionPercents>(opt_key)->get_at(idx);
		ret = double_to_string(val);
		}
		break;
	case coString:
		ret = static_cast<wxString>(config.opt_string(opt_key));
		break;
	case coStrings:
		if (opt_key.compare("compatible_printers") == 0){
			ret = config.option<ConfigOptionStrings>(opt_key)->values;
			break;
		}
		if (config.option<ConfigOptionStrings>(opt_key)->values.empty())
			ret = text_value;
		else if (opt->gui_flags.compare("serialized") == 0){
			std::vector<std::string> values = config.option<ConfigOptionStrings>(opt_key)->values;
			if (!values.empty() && values[0].compare("") != 0)
				for (auto el : values)
					text_value += el + ";";
			ret = text_value;
		}
		else
			ret = static_cast<wxString>(config.opt_string(opt_key, static_cast<unsigned int>(idx)));
		break;
	case coBool:
		ret = config.opt_bool(opt_key);
		break;
	case coBools:
		ret = config.opt_bool(opt_key, idx);
		break;
	case coInt:
		ret = config.opt_int(opt_key);
		break;
	case coInts:
		ret = config.opt_int(opt_key, idx);
		break;
	case coEnum:{
		if (opt_key.compare("external_fill_pattern") == 0 ||
			opt_key.compare("fill_pattern") == 0 ){
			ret = static_cast<int>(config.option<ConfigOptionEnum<InfillPattern>>(opt_key)->value);
		}
		else if (opt_key.compare("gcode_flavor") == 0 ){
			ret = static_cast<int>(config.option<ConfigOptionEnum<GCodeFlavor>>(opt_key)->value);
		}
		else if (opt_key.compare("support_material_pattern") == 0){
			ret = static_cast<int>(config.option<ConfigOptionEnum<SupportMaterialPattern>>(opt_key)->value);
		}
		else if (opt_key.compare("seam_position") == 0)
			ret = static_cast<int>(config.option<ConfigOptionEnum<SeamPosition>>(opt_key)->value);
	}
		break;
	case coPoints:
		if (opt_key.compare("bed_shape") == 0)
			ret = config.option<ConfigOptionPoints>(opt_key)->values;
		else
			ret = config.option<ConfigOptionPoints>(opt_key)->get_at(idx);
		break;
	case coNone:
	default:
		break;
	}
	return ret;
}

Field* ConfigOptionsGroup::get_fieldc(const t_config_option_key& opt_key, int opt_index){
	Field* field = get_field(opt_key);
	if (field != nullptr)
		return field;
	std::string opt_id = "";
	for (t_opt_map::iterator it = m_opt_map.begin(); it != m_opt_map.end(); ++it) {
		if (opt_key == m_opt_map.at(it->first).first && opt_index == m_opt_map.at(it->first).second){
			opt_id = it->first;
			break;
		}
	}
	return opt_id.empty() ? nullptr : get_field(opt_id);
}

void ogStaticText::SetText(const wxString& value, bool wrap/* = true*/)
{
	SetLabel(value);
	if (wrap) Wrap(400);
	GetParent()->Layout();
}

} // GUI
} // Slic3r
