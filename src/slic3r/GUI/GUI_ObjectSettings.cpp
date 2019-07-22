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

    m_bmp_delete = ScalableBitmap(parent, "cross");
}

bool ObjectSettings::update_settings_list()
{
    m_settings_list_sizer->Clear(true);
    m_og_settings.resize(0);

    auto objects_ctrl   = wxGetApp().obj_list();
    auto objects_model  = wxGetApp().obj_list()->GetModel();
    auto config         = wxGetApp().obj_list()->config();

    const auto item = objects_ctrl->GetSelection();
    
    if (!item || !objects_model->IsSettingsItem(item) || !config || objects_ctrl->multiple_selection())
        return false;

    const bool is_layers_range_settings = objects_model->GetItemType(objects_model->GetParent(item)) == itLayer;
	SettingsBundle cat_options = objects_ctrl->get_item_settings_bundle(config, is_layers_range_settings);

    if (!cat_options.empty())
    {
	    std::vector<std::string> categories;
        categories.reserve(cat_options.size());

        auto extra_column = [config, this](wxWindow* parent, const Line& line)
		{
			auto opt_key = (line.get_options())[0].opt_id;  //we assume that we have one option per line

			auto btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
            btn->SetToolTip(_(L("Remove parameter")));

			btn->Bind(wxEVT_BUTTON, [opt_key, config, this](wxEvent &event) {
                wxGetApp().plater()->take_snapshot(wxString::Format(_(L("Delete Option %s")), opt_key));
				config->erase(opt_key);
                wxGetApp().obj_list()->changed_object();
                wxTheApp->CallAfter([this]() {
                    wxWindowUpdateLocker noUpdates(m_parent);
                    update_settings_list(); 
                    m_parent->Layout(); 
                });
			});
			return btn;
		};

        for (auto& cat : cat_options)
        {
            if (cat.second.size() == 1 &&
                (cat.second[0] == "extruder" || is_layers_range_settings && cat.second[0] == "layer_height"))
                continue;

            categories.push_back(cat.first);

            auto optgroup = std::make_shared<ConfigOptionsGroup>(m_og->ctrl_parent(), _(cat.first), config, false, extra_column);
            optgroup->label_width = 15;
            optgroup->sidetext_width = 5.5;

            optgroup->m_on_change = [](const t_config_option_key& opt_id, const boost::any& value) {
                                    wxGetApp().obj_list()->changed_object(); };

            // call back for rescaling of the extracolumn control
            optgroup->rescale_extra_column_item = [this](wxWindow* win) {
                auto *ctrl = dynamic_cast<ScalableButton*>(win);
                if (ctrl == nullptr)
                    return;
                ctrl->SetBitmap_(m_bmp_delete);
            };

            const bool is_extruders_cat = cat.first == "Extruders";
            for (auto& opt : cat.second)
            {
                if (opt == "extruder" || is_layers_range_settings && opt == "layer_height")
                    continue;
                Option option = optgroup->get_option(opt);
                option.opt.width = 12;
                if (is_extruders_cat)
                    option.opt.max = wxGetApp().extruders_edited_cnt();
                optgroup->append_single_option_line(option);

                optgroup->get_field(opt)->m_on_change = [optgroup](const std::string& opt_id, const boost::any& value) {
                    // first of all take a snapshot and then change value in configuration
                    wxGetApp().plater()->take_snapshot(wxString::Format(_(L("Change Option %s")), opt_id));
                    optgroup->on_change_OG(opt_id, value);
                };

            }
            optgroup->reload_config();

            m_settings_list_sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 0);
            m_og_settings.push_back(optgroup);
        }

        if (!categories.empty())
            objects_model->UpdateSettingsDigest(item, categories);
    }
    else
    {
        objects_ctrl->select_item(objects_model->Delete(item));
        return false;
    } 
            
    return true;
}

void ObjectSettings::UpdateAndShow(const bool show)
{
    OG_Settings::UpdateAndShow(show ? update_settings_list() : false);
}

void ObjectSettings::msw_rescale()
{
    m_bmp_delete.msw_rescale();

    for (auto group : m_og_settings)
        group->msw_rescale();
}

} //namespace GUI
} //namespace Slic3r 