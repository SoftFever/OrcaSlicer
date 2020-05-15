#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/dialog.h>
#include <map>

class wxRadioBox;

namespace Slic3r {
namespace GUI {

class ConfigOptionsGroup;

class PreferencesDialog : public DPIDialog
{
	std::map<std::string, std::string>	m_values;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_general;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_camera;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_gui;
	wxSizer*                            m_icon_size_sizer;
	wxRadioBox*							m_layout_mode_box;
    bool                                isOSX {false};
	bool								m_settings_layout_changed {false};
public:
	PreferencesDialog(wxWindow* parent);
	~PreferencesDialog() {}

	bool settings_layout_changed() { return m_settings_layout_changed; }

	void	build();
	void	accept();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void layout();
    void create_icon_size_slider();
    void create_settings_mode_widget();
};

} // GUI
} // Slic3r


#endif /* slic3r_Preferences_hpp_ */
