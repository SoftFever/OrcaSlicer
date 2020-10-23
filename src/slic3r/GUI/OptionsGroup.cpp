#include "OptionsGroup.hpp"
#include "ConfigExceptions.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "OG_CustomCtrl.hpp"

#include <utility>
#include <wx/numformatter.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "I18N.hpp"

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
		m_fields.emplace(id, std::move(Choice::Create<Choice>(this->ctrl_parent(), opt, id)));
    } else if (opt.gui_type.compare("color") == 0) {
		m_fields.emplace(id, std::move(ColourPicker::Create<ColourPicker>(this->ctrl_parent(), opt, id)));
    } else if (opt.gui_type.compare("f_enum_open") == 0 || 
                opt.gui_type.compare("i_enum_open") == 0 ||
                opt.gui_type.compare("i_enum_closed") == 0) {
		m_fields.emplace(id, std::move(Choice::Create<Choice>(this->ctrl_parent(), opt, id)));
    } else if (opt.gui_type.compare("slider") == 0) {
		m_fields.emplace(id, std::move(SliderCtrl::Create<SliderCtrl>(this->ctrl_parent(), opt, id)));
    } else if (opt.gui_type.compare("i_spin") == 0) { // Spinctrl
    } else if (opt.gui_type.compare("legend") == 0) { // StaticText
		m_fields.emplace(id, std::move(StaticText::Create<StaticText>(this->ctrl_parent(), opt, id)));
    } else { 
        switch (opt.type) {
            case coFloatOrPercent:
            case coFloat:
            case coFloats:
			case coPercent:
			case coPercents:
			case coString:
			case coStrings:
				m_fields.emplace(id, std::move(TextCtrl::Create<TextCtrl>(this->ctrl_parent(), opt, id)));
                break;
			case coBool:
			case coBools:
				m_fields.emplace(id, std::move(CheckBox::Create<CheckBox>(this->ctrl_parent(), opt, id)));
				break;
			case coInt:
			case coInts:
				m_fields.emplace(id, std::move(SpinCtrl::Create<SpinCtrl>(this->ctrl_parent(), opt, id)));
				break;
            case coEnum:
				m_fields.emplace(id, std::move(Choice::Create<Choice>(this->ctrl_parent(), opt, id)));
				break;
            case coPoints:
				m_fields.emplace(id, std::move(PointCtrl::Create<PointCtrl>(this->ctrl_parent(), opt, id)));
				break;
            case coNone:   break;
            default:
				throw Slic3r::LogicError("This control doesn't exist till now"); break;
        }
    }
    // Grab a reference to fields for convenience
    const t_field& field = m_fields[id];
	field->m_on_change = [this](const std::string& opt_id, const boost::any& value) {
			//! This function will be called from Field.					
			//! Call OptionGroup._on_change(...)
			if (!m_disabled) 
				this->on_change_OG(opt_id, value);
	};
    field->m_on_kill_focus = [this](const std::string& opt_id) {
			//! This function will be called from Field.					
			if (!m_disabled) 
				this->on_kill_focus(opt_id);
	};
    field->m_on_set_focus = [this](const std::string& opt_id) {
			//! This function will be called from Field.					
			if (!m_disabled) 
				this->on_set_focus(opt_id);
	};
    field->m_parent = parent();
	
	//! Label to change background color, when option is modified
	field->m_Label = label;
	field->m_back_to_initial_value = [this](std::string opt_id) {
		if (!m_disabled)
			this->back_to_initial_value(opt_id);
	};
	field->m_back_to_sys_value = [this](std::string opt_id) {
		if (!this->m_disabled)
			this->back_to_sys_value(opt_id);
	};
    
	// assign function objects for callbacks, etc.
    return field;
}

OptionsGroup::OptionsGroup(	wxWindow* _parent, const wxString& title,
                            bool is_tab_opt /* = false */,
                            column_t extra_clmn /* = nullptr */) :
                m_parent(_parent), title(title),
                m_show_modified_btns(is_tab_opt),
                staticbox(title!=""), extra_column(extra_clmn)
{
}

void OptionsGroup::add_undo_buttons_to_sizer(wxSizer* sizer, const t_field& field)
{
	if (!m_show_modified_btns && field->m_Undo_btn) {
        field->m_Undo_btn->set_as_hidden();
		field->m_Undo_to_sys_btn->set_as_hidden();
		field->m_blinking_bmp->Hide();
		return;
	}

    sizer->Add(field->m_blinking_bmp, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 2);
	sizer->Add(field->m_Undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
	sizer->Add(field->m_Undo_btn, 0, wxALIGN_CENTER_VERTICAL);
}

void OptionsGroup::append_line(const Line& line)
{
	m_lines.emplace_back(line);

	if (line.full_width && (
		line.widget != nullptr ||
		!line.get_extra_widgets().empty())
		)
		return;

	auto option_set = line.get_options();
	for (auto opt : option_set) 
		m_options.emplace(opt.opt_id, opt);

	// add mode value for current line to m_options_mode
    if (!option_set.empty())
        m_options_mode.push_back(option_set[0].opt.mode);
}

void OptionsGroup::activate_line(Line& line)
{
	m_use_custom_ctrl_as_parent = false;

	if (line.full_width && (
		line.widget != nullptr ||
		!line.get_extra_widgets().empty())
		) {
        if (line.widget != nullptr) {
			// description lines
            sizer->Add(line.widget(this->ctrl_parent()), 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
		if (!line.get_extra_widgets().empty()) {
			const auto h_sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(h_sizer, 1, wxEXPAND | wxALL, wxOSX ? 0 : 15);

            bool is_first_item = true;
			for (auto extra_widget : line.get_extra_widgets()) {
				h_sizer->Add(extra_widget(this->ctrl_parent()), is_first_item ? 1 : 0, wxLEFT, 15);
				is_first_item = false;
			}
			return;
		}
    }

    if (!custom_ctrl && m_show_modified_btns) {
        custom_ctrl = new OG_CustomCtrl((wxWindow*)this->stb, this);
        sizer->Add(custom_ctrl, 0, wxEXPAND | wxALL, wxOSX || !staticbox ? 0 : 5);
    }

	auto option_set = line.get_options();

	// Set sidetext width for a better alignment of options in line
	// "m_show_modified_btns==true" means that options groups are in tabs
	if (option_set.size() > 1 && m_show_modified_btns) {
		sidetext_width = Field::def_width_thinner();
	}

	// if we have a single option with no label, no sidetext just add it directly to sizer
    if (option_set.size() == 1 && label_width == 0 && option_set.front().opt.full_width &&
        option_set.front().opt.label.empty() &&
		option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr && 
		line.get_extra_widgets().size() == 0) {

		const auto& option = option_set.front();
		const auto& field = build_field(option);

		if (!custom_ctrl) {
			auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
			add_undo_buttons_to_sizer(btn_sizer, field);
			sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, 0);
		}
		if (is_window_field(field))
			sizer->Add(field->getWindow(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		if (is_sizer_field(field))
			sizer->Add(field->getSizer(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		return;
	}

    auto grid_sizer = m_grid_sizer;

    if (custom_ctrl)
        m_use_custom_ctrl_as_parent = true;

	// if we have an extra column, build it
	if (extra_column && !m_show_modified_btns)
	{
		m_extra_column_item_ptrs.push_back(extra_column(this->ctrl_parent(), line));
		grid_sizer->Add(m_extra_column_item_ptrs.back(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	}

	// Build a label if we have it
	wxStaticText* label=nullptr;
    if (label_width != 0) {
        if (custom_ctrl) {
            if (line.near_label_widget)
                line.near_label_widget_win = line.near_label_widget(this->ctrl_parent());
        }
        else {
            if (!line.near_label_widget || !line.label.IsEmpty()) {
                // Only create the label if it is going to be displayed.
                long label_style = staticbox ? 0 : wxALIGN_RIGHT;
#ifdef __WXGTK__
                // workaround for correct text align of the StaticBox on Linux
                // flags wxALIGN_RIGHT and wxALIGN_CENTRE don't work when Ellipsize flags are _not_ given.
                // Text is properly aligned only when Ellipsize is checked.
                label_style |= staticbox ? 0 : wxST_ELLIPSIZE_END;
#endif /* __WXGTK__ */
                label = new wxStaticText(this->ctrl_parent(), wxID_ANY, line.label + (line.label.IsEmpty() ? "" : ": "),
                    wxDefaultPosition, wxSize(label_width * wxGetApp().em_unit(), -1), label_style);
                label->SetBackgroundStyle(wxBG_STYLE_PAINT);
                label->SetFont(wxGetApp().normal_font());
                label->Wrap(label_width * wxGetApp().em_unit()); // avoid a Linux/GTK bug
            }
            if (!line.near_label_widget)
                grid_sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, line.label.IsEmpty() ? 0 : 5);
            else {
                m_near_label_widget_ptrs.push_back(line.near_label_widget(this->ctrl_parent()));

                if (line.label.IsEmpty())
                    grid_sizer->Add(m_near_label_widget_ptrs.back(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 7);
                else {
                    // If we're here, we have some widget near the label
                    // so we need a horizontal sizer to arrange these things
                    auto sizer = new wxBoxSizer(wxHORIZONTAL);
                    grid_sizer->Add(sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM | wxTOP | wxLEFT), staticbox ? 0 : 1);
                    sizer->Add(m_near_label_widget_ptrs.back(), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 7);
                    sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, 5);
                }
            }
            if (label != nullptr && line.label_tooltip != "")
                label->SetToolTip(line.label_tooltip);
        }
    }

	if (!custom_ctrl && line.full_Label != nullptr)
		*line.full_Label = label; // Initiate the pointer to the control of the full label, if we need this one.

	// If there's a widget, build it and add the result to the sizer.
	if (line.widget != nullptr) {
		auto wgt = line.widget(this->ctrl_parent());
        if (custom_ctrl)
            line.widget_sizer = wgt;
        else
            grid_sizer->Add(wgt, 0, wxEXPAND | wxBOTTOM | wxTOP, (wxOSX || line.label.IsEmpty()) ? 0 : 5);
		return;
	}

	// If we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things
	auto sizer = new wxBoxSizer(wxHORIZONTAL);
    if (!custom_ctrl)
        grid_sizer->Add(sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM | wxTOP | wxLEFT), staticbox ? 0 : 1);
	// If we have a single option with no sidetext just add it directly to the grid sizer
	if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
        option_set.front().opt.label.empty() &&
		option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0) {
		const auto& option = option_set.front();
		const auto& field = build_field(option, label);

        if (custom_ctrl) {
            if (is_window_field(field) && option.opt.full_width)
                field->getWindow()->SetSize(wxSize(3 * Field::def_width_wider() * wxGetApp().em_unit(), -1));
        }
        else {
            add_undo_buttons_to_sizer(sizer, field);
            if (is_window_field(field))
                sizer->Add(field->getWindow(), option.opt.full_width ? 1 : 0,
                    wxBOTTOM | wxTOP | (option.opt.full_width ? wxEXPAND : wxALIGN_CENTER_VERTICAL), (wxOSX || !staticbox) ? 0 : 2);
            if (is_sizer_field(field))
                sizer->Add(field->getSizer(), 1, option.opt.full_width ? wxEXPAND : wxALIGN_CENTER_VERTICAL, 0);
        }
        return;
	}

    for (auto opt : option_set) {
		ConfigOptionDef option = opt.opt;
		wxSizer* sizer_tmp = sizer;
		// add label if any
		if (!option.label.empty() && !custom_ctrl) {
//!			To correct translation by context have to use wxGETTEXT_IN_CONTEXT macro from wxWidget 3.1.1
			wxString str_label = (option.label == L_CONTEXT("Top", "Layers") || option.label == L_CONTEXT("Bottom", "Layers")) ?
				_CTX(option.label, "Layers") :
				_(option.label);
			label = new wxStaticText(this->ctrl_parent(), wxID_ANY, str_label + ": ", wxDefaultPosition, //wxDefaultSize); 
				wxSize(sublabel_width != -1 ? sublabel_width * wxGetApp().em_unit() : -1, -1), wxALIGN_RIGHT);
			label->SetBackgroundStyle(wxBG_STYLE_PAINT);
            label->SetFont(wxGetApp().normal_font());
			sizer_tmp->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
		}

		// add field
		const Option& opt_ref = opt;
		auto& field = build_field(opt_ref, label);
        if (!custom_ctrl) {
            add_undo_buttons_to_sizer(sizer_tmp, field);
            if (option_set.size() == 1 && option_set.front().opt.full_width)
            {
                const auto v_sizer = new wxBoxSizer(wxVERTICAL);
                sizer_tmp->Add(v_sizer, 1, wxEXPAND);
                is_sizer_field(field) ?
                    v_sizer->Add(field->getSizer(), 0, wxEXPAND) :
                    v_sizer->Add(field->getWindow(), 0, wxEXPAND);
                break;
            }

            is_sizer_field(field) ?
                sizer_tmp->Add(field->getSizer(), 0, wxALIGN_CENTER_VERTICAL, 0) :
                sizer_tmp->Add(field->getWindow(), 0, wxALIGN_CENTER_VERTICAL, 0);

            // add sidetext if any
            if (!option.sidetext.empty() || sidetext_width > 0) {
                auto sidetext = new wxStaticText(this->ctrl_parent(), wxID_ANY, _(option.sidetext), wxDefaultPosition,
                    wxSize(sidetext_width != -1 ? sidetext_width * wxGetApp().em_unit() : -1, -1), wxALIGN_LEFT);
                sidetext->SetBackgroundStyle(wxBG_STYLE_PAINT);
                sidetext->SetFont(wxGetApp().normal_font());
                sizer_tmp->Add(sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
                field->set_side_text_ptr(sidetext);
            }

            // add side widget if any
            if (opt.side_widget != nullptr) {
                sizer_tmp->Add(opt.side_widget(this->ctrl_parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);    //! requires verification
            }

            if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
                sizer_tmp->AddSpacer(6);
        }
	}

	// add extra sizers if any
	for (auto extra_widget : line.get_extra_widgets()) 
    {
        if (line.get_extra_widgets().size() == 1 && !staticbox)
        {
            // extra widget for non-staticbox option group (like for the frequently used parameters on the sidebar) should be wxALIGN_RIGHT
            const auto v_sizer = new wxBoxSizer(wxVERTICAL);
            sizer->Add(v_sizer, option_set.size() == 1 ? 0 : 1, wxEXPAND);
            v_sizer->Add(extra_widget(this->ctrl_parent()), 0, wxALIGN_RIGHT);
            return;
        }

        auto extra_wgt = extra_widget(this->ctrl_parent());
        if (custom_ctrl)
            line.extra_widget_sizer = extra_wgt;
        else
            sizer->Add(extra_wgt, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);        //! requires verification
	}
}

// create all controls for the option group from the m_lines
bool OptionsGroup::activate(std::function<void()> throw_if_canceled)
{
	if (sizer)//(!sizer->IsEmpty())
		return false;

	try {
		if (staticbox) {
			stb = new wxStaticBox(m_parent, wxID_ANY, _(title));
			if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
			stb->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
		}
		else
			stb = nullptr;
		sizer = (staticbox ? new wxStaticBoxSizer(stb, wxVERTICAL) : new wxBoxSizer(wxVERTICAL));

		auto num_columns = 1U;
		size_t grow_col = 1;

		if (label_width == 0)
			grow_col = 0;
		else
			num_columns++;

		if (extra_column) {
			num_columns++;
			grow_col++;
		}

		m_grid_sizer = new wxFlexGridSizer(0, num_columns, 1, 0);
		static_cast<wxFlexGridSizer*>(m_grid_sizer)->SetFlexibleDirection(wxBOTH);
		static_cast<wxFlexGridSizer*>(m_grid_sizer)->AddGrowableCol(grow_col);

		sizer->Add(m_grid_sizer, 0, wxEXPAND | wxALL, wxOSX || !staticbox ? 0 : 5);

		// activate lines
		for (Line& line: m_lines) {
			throw_if_canceled();
			activate_line(line);
		}
	} catch (UIBuildCanceled&) {
		auto p = sizer;
		this->clear();
		p->Clear(true);
		delete p;
		throw;
	}

	return true;
}
// delete all controls from the option group
void OptionsGroup::clear()
{
	if (!sizer)
		return;

	m_grid_sizer = nullptr;
	sizer = nullptr;

	for (Line& line : m_lines) {
		if(line.full_Label)
			*line.full_Label = nullptr;

        if (line.near_label_widget_win)
            line.near_label_widget_win = nullptr;

        if (line.widget_sizer) {
            line.widget_sizer->Clear(true);
            line.widget_sizer = nullptr;
        }

        if (line.extra_widget_sizer) {
            line.extra_widget_sizer->Clear(true);
            line.extra_widget_sizer = nullptr;
        }
	}

    if (custom_ctrl) {
        for (auto const &item : m_fields) {
            wxWindow* win = item.second.get()->getWindow();
            if (win)
                win = nullptr;
        }
        custom_ctrl = nullptr;
    }

	m_extra_column_item_ptrs.clear();
	m_near_label_widget_ptrs.clear();
	m_fields.clear();
}

Line OptionsGroup::create_single_option_line(const Option& option) const {
// 	Line retval{ _(option.opt.label), _(option.opt.tooltip) };
    wxString tooltip = _(option.opt.tooltip);
    edit_tooltip(tooltip);
	Line retval{ _(option.opt.label), tooltip };
    Option tmp(option);
    tmp.opt.label = std::string("");
    retval.append_option(tmp);
    return retval;
}

void OptionsGroup::clear_fields_except_of(const std::vector<std::string> left_fields)
{
    auto it = m_fields.begin();
    while (it != m_fields.end()) {
        if (std::find(left_fields.begin(), left_fields.end(), it->first) == left_fields.end())
            it = m_fields.erase(it);
        else 
            it++;
    }
}

void OptionsGroup::on_set_focus(const std::string& opt_key)
{
    if (m_set_focus != nullptr)
        m_set_focus(opt_key);
}

void OptionsGroup::on_change_OG(const t_config_option_key& opt_id, const boost::any& value) {
	if (m_on_change != nullptr)
		m_on_change(opt_id, value);
}

Option ConfigOptionsGroup::get_option(const std::string& opt_key, int opt_index /*= -1*/)
{
	if (!m_config->has(opt_key)) {
		std::cerr << "No " << opt_key << " in ConfigOptionsGroup config.\n";
	}

	std::string opt_id = opt_index == -1 ? opt_key : opt_key + "#" + std::to_string(opt_index);
	std::pair<std::string, int> pair(opt_key, opt_index);
	m_opt_map.emplace(opt_id, pair);

	if (m_show_modified_btns) // fill group and category values just fro options from Settings Tab 
	    wxGetApp().sidebar().get_searcher().add_key(opt_id, title, this->config_category());

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

		auto 				itOption  = it->second;
		const std::string  &opt_key   = itOption.first;
		int 			    opt_index = itOption.second;

		this->change_opt_value(opt_key, value, opt_index == -1 ? 0 : opt_index);
	}

	OptionsGroup::on_change_OG(opt_id, value); 
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
	if (opt_key == "extruders_count") {
		auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
		value = int(nozzle_diameter->values.size());
	}
    else if (m_opt_map.find(opt_key) == m_opt_map.end() ||
		    // This option don't have corresponded field
		     opt_key == "bed_shape"				|| opt_key == "filament_ramming_parameters" || 
		     opt_key == "compatible_printers"	|| opt_key == "compatible_prints" ) {
        value = get_config_value(config, opt_key);
        this->change_opt_value(opt_key, value);
        return;
    }
	else
	{
		auto opt_id = m_opt_map.find(opt_key)->first;
		std::string opt_short_key = m_opt_map.at(opt_id).first;
		int opt_index = m_opt_map.at(opt_id).second;
		value = get_config_value(config, opt_short_key, opt_index);
	}

	set_value(opt_key, value);
	on_change_OG(opt_key, get_value(opt_key));
}

void ConfigOptionsGroup::on_kill_focus(const std::string& opt_key)
{
    if (m_fill_empty_value)
        m_fill_empty_value(opt_key);
    else
	    reload_config();
}

void ConfigOptionsGroup::reload_config()
{
	for (auto &kvp : m_opt_map) {
		// Name of the option field (name of the configuration key, possibly suffixed with '#' and the index of a scalar inside a vector.
		const std::string &opt_id    = kvp.first;
		// option key (may be scalar or vector)
		const std::string &opt_key   = kvp.second.first;
		// index in the vector option, zero for scalars
		int 			   opt_index = kvp.second.second;
		const ConfigOptionDef &option = m_options.at(opt_id).opt;
		this->set_value(opt_id, config_value(opt_key, opt_index, option.gui_flags == "serialized"));
	}
}

void ConfigOptionsGroup::Hide()
{
    Show(false);
}

void ConfigOptionsGroup::Show(const bool show)
{
    sizer->ShowItems(show);
#if 0//#ifdef __WXGTK__
    m_panel->Show(show);
    m_grid_sizer->Show(show);
#endif /* __WXGTK__ */
}

bool ConfigOptionsGroup::is_visible(ConfigOptionMode mode)
{
    if (m_options_mode.empty())
        return true;
    if (m_options_mode.size() == 1)
        return m_options_mode[0] <= mode;

    size_t hidden_row_cnt = 0;
    for (auto opt_mode : m_options_mode)
        if (opt_mode > mode)
            hidden_row_cnt++;

    return hidden_row_cnt != m_options_mode.size();
}

bool ConfigOptionsGroup::update_visibility(ConfigOptionMode mode)
{
	if (m_options_mode.empty() || !m_grid_sizer)
		return true;

    if (custom_ctrl) {
        bool show = custom_ctrl->update_visibility(mode);
        this->Show(show);
        return show;
    }

	int opt_mode_size = m_options_mode.size();
	if (m_grid_sizer->GetEffectiveRowsCount() != opt_mode_size &&
		opt_mode_size == 1)
		return m_options_mode[0] <= mode;

	Show(true);

    int coef = 0;
    int hidden_row_cnt = 0;
    const int cols = m_grid_sizer->GetCols();
    for (auto opt_mode : m_options_mode) {
		const bool show = opt_mode <= mode;
        if (!show) {
            hidden_row_cnt++;
            for (int i = 0; i < cols; ++i)
                m_grid_sizer->Show(coef + i, show);
        }
        coef+= cols;
	}

    if (hidden_row_cnt == opt_mode_size) {
        sizer->ShowItems(false);
        return false;
    }
    return true;
}

void ConfigOptionsGroup::msw_rescale()
{
    // update bitmaps for extra column items (like "mode markers" or buttons on settings panel)
    if (rescale_extra_column_item)
        for (auto extra_col : m_extra_column_item_ptrs)
            rescale_extra_column_item(extra_col);

    // update bitmaps for near label widgets (like "Set uniform scale" button on settings panel)
    if (rescale_near_label_widget)
        for (auto near_label_widget : m_near_label_widget_ptrs)
            rescale_near_label_widget(near_label_widget);

    // update undo buttons : rescale bitmaps
    for (const auto& field : m_fields)
        field.second->msw_rescale(sidetext_width>0);

    if (custom_ctrl)
        custom_ctrl->msw_rescale();

    const int em = em_unit(parent());

    // rescale width of label column
    if (m_grid_sizer && !m_options_mode.empty() && label_width > 1)
    {
        const int cols = m_grid_sizer->GetCols();
        const int rows = m_grid_sizer->GetEffectiveRowsCount();
        const int label_col = extra_column == nullptr ? 0 : 1;

        for (int i = 0; i < rows; i++)
        {
            const wxSizerItem* label_item = m_grid_sizer->GetItem(i*cols+label_col);
            if (label_item->IsWindow())
            {
                auto label = dynamic_cast<wxStaticText*>(label_item->GetWindow());
                if (label != nullptr) {
                    label->SetMinSize(wxSize(label_width*em, -1));
                }
            }
            else if (label_item->IsSizer()) // case when we have near_label_widget
            {
                const wxSizerItem* l_item = label_item->GetSizer()->GetItem(1);
                if (l_item->IsWindow())
                {
                    auto label = dynamic_cast<wxStaticText*>(l_item->GetWindow());
                    if (label != nullptr) {
                        label->SetMinSize(wxSize(label_width*em, -1));
                    }
                }
            }
        }
        m_grid_sizer->Layout();
    }
}

void ConfigOptionsGroup::sys_color_changed()
{
	// update bitmaps for near label widgets (like "Set uniform scale" button on settings panel)
	if (rescale_near_label_widget)
		for (auto near_label_widget : m_near_label_widget_ptrs)
			rescale_near_label_widget(near_label_widget);

	// update undo buttons : rescale bitmaps
	for (const auto& field : m_fields)
		field.second->sys_color_changed();
}

boost::any ConfigOptionsGroup::config_value(const std::string& opt_key, int opt_index, bool deserialize) {

	if (deserialize) {
		// Want to edit a vector value(currently only multi - strings) in a single edit box.
		// Aggregate the strings the old way.
		// Currently used for the post_process config value only.
		if (opt_index != -1)
			throw Slic3r::OutOfRange("Can't deserialize option indexed value");
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

    if (opt->nullable)
    {
        switch (opt->type)
        {
        case coPercents:
        case coFloats: {
            if (config.option(opt_key)->is_nil())
                ret = _(L("N/A"));
            else {
                double val = opt->type == coFloats ?
                            config.option<ConfigOptionFloatsNullable>(opt_key)->get_at(idx) :
                            config.option<ConfigOptionPercentsNullable>(opt_key)->get_at(idx);
                ret = double_to_string(val); }
            }
            break;
        case coBools:
            ret = config.option<ConfigOptionBoolsNullable>(opt_key)->values[idx];
            break;
        case coInts:
            ret = config.option<ConfigOptionIntsNullable>(opt_key)->get_at(idx);
            break;
        default:
            break;
        }
        return ret;
    }

	switch (opt->type) {
	case coFloatOrPercent:{
		const auto &value = *config.option<ConfigOptionFloatOrPercent>(opt_key);

        text_value = double_to_string(value.value);
		if (value.percent)
			text_value += "%";

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
		if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
			ret = config.option<ConfigOptionStrings>(opt_key)->values;
			break;
		}
		if (opt_key == "filament_ramming_parameters") {
			ret = config.opt_string(opt_key, static_cast<unsigned int>(idx));
			break;
		}
		if (config.option<ConfigOptionStrings>(opt_key)->values.empty())
			ret = text_value;
		else if (opt->gui_flags.compare("serialized") == 0) {
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
		if (opt_key == "top_fill_pattern" ||
			opt_key == "bottom_fill_pattern" ||
			opt_key == "fill_pattern" ) {
			ret = static_cast<int>(config.option<ConfigOptionEnum<InfillPattern>>(opt_key)->value);
		}
		else if (opt_key == "ironing_type") {
			ret = static_cast<int>(config.option<ConfigOptionEnum<IroningType>>(opt_key)->value);
		}
		else if (opt_key == "gcode_flavor") {
			ret = static_cast<int>(config.option<ConfigOptionEnum<GCodeFlavor>>(opt_key)->value);
		}
		else if (opt_key == "machine_limits_usage") {
			ret = static_cast<int>(config.option<ConfigOptionEnum<MachineLimitsUsage>>(opt_key)->value);
		}
		else if (opt_key == "support_material_pattern") {
			ret = static_cast<int>(config.option<ConfigOptionEnum<SupportMaterialPattern>>(opt_key)->value);
		}
		else if (opt_key == "seam_position") {
			ret = static_cast<int>(config.option<ConfigOptionEnum<SeamPosition>>(opt_key)->value);
		}
		else if (opt_key == "host_type") {
			ret = static_cast<int>(config.option<ConfigOptionEnum<PrintHostType>>(opt_key)->value);
		}
        else if (opt_key == "display_orientation") {
            ret  = static_cast<int>(config.option<ConfigOptionEnum<SLADisplayOrientation>>(opt_key)->value);
        }
        else if (opt_key == "support_pillar_connection_mode") {
            ret  = static_cast<int>(config.option<ConfigOptionEnum<SLAPillarConnectionMode>>(opt_key)->value);
        }
        else if (opt_key == "printhost_authorization_type") {
            ret  = static_cast<int>(config.option<ConfigOptionEnum<AuthorizationType>>(opt_key)->value);
        }
	}
		break;
	case coPoints:
		if (opt_key == "bed_shape")
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

Field* ConfigOptionsGroup::get_fieldc(const t_config_option_key& opt_key, int opt_index)
{
	Field* field = get_field(opt_key);
	if (field != nullptr)
		return field;
	std::string opt_id = "";
	for (t_opt_map::iterator it = m_opt_map.begin(); it != m_opt_map.end(); ++it) {
		if (opt_key == m_opt_map.at(it->first).first && opt_index == m_opt_map.at(it->first).second) {
			opt_id = it->first;
			break;
		}
	}
	return opt_id.empty() ? nullptr : get_field(opt_id);
}

// Change an option on m_config, possibly call ModelConfig::touch().
void ConfigOptionsGroup::change_opt_value(const t_config_option_key& opt_key, const boost::any& value, int opt_index /*= 0*/)

{
	Slic3r::GUI::change_opt_value(const_cast<DynamicPrintConfig&>(*m_config), opt_key, value, opt_index);
	if (m_modelconfig)
		m_modelconfig->touch();
}

ogStaticText::ogStaticText(wxWindow* parent, const wxString& text) : 
    wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize)
{
    if (!text.IsEmpty()) {
		Wrap(60 * wxGetApp().em_unit());
		GetParent()->Layout();
    }
}


void ogStaticText::SetText(const wxString& value, bool wrap/* = true*/)
{
	SetLabel(value);
    if (wrap) Wrap(60 * wxGetApp().em_unit());
	GetParent()->Layout();
}

} // GUI
} // Slic3r
