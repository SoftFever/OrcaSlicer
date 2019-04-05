#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectList.hpp"

#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "PresetBundle.hpp"
#include "libslic3r/Model.hpp"

#include <boost/algorithm/string.hpp>

#include "I18N.hpp"

#include <wx/wupdlock.h>

namespace Slic3r
{
namespace GUI
{

OG_Settings::OG_Settings(wxWindow* parent, const bool staticbox) :
    m_parent(parent)
{
    wxString title = staticbox ? " " : ""; // temporary workaround - #ys_FIXME
    m_og = std::make_shared<ConfigOptionsGroup>(parent, title);
}

bool OG_Settings::IsShown()
{
    return m_og->sizer->IsEmpty() ? false : m_og->sizer->IsShown(size_t(0));
}

void OG_Settings::Show(const bool show)
{
    m_og->Show(show);
}

void OG_Settings::Hide()
{
    Show(false);
}

void OG_Settings::UpdateAndShow(const bool show)
{
    Show(show);
//    m_parent->Layout();
}

wxSizer* OG_Settings::get_sizer()
{
    return m_og->sizer;
}



ObjectSettings::ObjectSettings(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_og->set_name(_(L("Additional Settings")));    

    m_settings_list_sizer = new wxBoxSizer(wxVERTICAL);
    m_og->sizer->Add(m_settings_list_sizer, 1, wxEXPAND | wxLEFT, 5);
}

void ObjectSettings::update_settings_list()
{
    m_settings_list_sizer->Clear(true);

    auto objects_ctrl   = wxGetApp().obj_list();
    auto objects_model  = wxGetApp().obj_list()->m_objects_model;
    auto config         = wxGetApp().obj_list()->m_config;

    const auto item = objects_ctrl->GetSelection();
    if (item && !objects_ctrl->multiple_selection() && 
        config && objects_model->IsSettingsItem(item))
	{
        auto extra_column = [config, this](wxWindow* parent, const Line& line)
		{
			auto opt_key = (line.get_options())[0].opt_id;  //we assume that we have one option per line

			auto btn = new wxBitmapButton(parent, wxID_ANY, create_scaled_bitmap(m_parent, "colorchange_delete_on.png"),
				wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
#ifdef __WXMSW__
            btn->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__
			btn->Bind(wxEVT_BUTTON, [opt_key, config, this](wxEvent &event) {
				config->erase(opt_key);
                wxGetApp().obj_list()->part_settings_changed();
                wxTheApp->CallAfter([this]() {
                    wxWindowUpdateLocker noUpdates(m_parent);
                    update_settings_list(); 
                    m_parent->Layout(); 
                });
			});
			return btn;
		};

		std::map<std::string, std::vector<std::string>> cat_options;
		auto opt_keys = config->keys();
        objects_ctrl->update_opt_keys(opt_keys); // update options list according to print technology

        m_og_settings.resize(0);
        std::vector<std::string> categories;
        if (!(opt_keys.size() == 1 && opt_keys[0] == "extruder"))// return;
        {
            const int extruders_cnt = wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA ? 1 :
                wxGetApp().preset_bundle->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

            for (auto& opt_key : opt_keys) {
                auto category = config->def()->get(opt_key)->category;
                if (category.empty() ||
                    (category == "Extruders" && extruders_cnt == 1)) continue;

                std::vector< std::string > new_category;

                auto& cat_opt = cat_options.find(category) == cat_options.end() ? new_category : cat_options.at(category);
                cat_opt.push_back(opt_key);
                if (cat_opt.size() == 1)
                    cat_options[category] = cat_opt;
            }

            for (auto& cat : cat_options) {
                if (cat.second.size() == 1 && cat.second[0] == "extruder")
                    continue;

                auto optgroup = std::make_shared<ConfigOptionsGroup>(m_og->ctrl_parent(), cat.first, config, false, extra_column);
                optgroup->label_width = 15 * wxGetApp().em_unit();
                optgroup->sidetext_width = 5.5 * wxGetApp().em_unit();

                optgroup->m_on_change = [](const t_config_option_key& opt_id, const boost::any& value) {
                                        wxGetApp().obj_list()->part_settings_changed(); };

                for (auto& opt : cat.second)
                {
                    if (opt == "extruder")
                        continue;
                    Option option = optgroup->get_option(opt);
                    option.opt.width = 12 * wxGetApp().em_unit();
                    optgroup->append_single_option_line(option);
                }
                optgroup->reload_config();
                m_settings_list_sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 0);
                m_og_settings.push_back(optgroup);

                categories.push_back(cat.first);
            }
        }

        if (m_og_settings.empty()) {
            objects_ctrl->select_item(objects_model->Delete(item));
        }
        else {
            if (!categories.empty())
                objects_model->UpdateSettingsDigest(item, categories);
        }
	}
}

void ObjectSettings::UpdateAndShow(const bool show)
{
    if (show)
        update_settings_list();

    OG_Settings::UpdateAndShow(show);
}

} //namespace GUI
} //namespace Slic3r 