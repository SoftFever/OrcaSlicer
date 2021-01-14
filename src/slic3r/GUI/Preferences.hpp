#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/dialog.h>
#include <map>

class wxRadioBox;
class wxColourPickerCtrl;

namespace Slic3r {
namespace GUI {

class ConfigOptionsGroup;

class PreferencesDialog : public DPIDialog
{
	std::map<std::string, std::string>	m_values;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_general;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_camera;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_gui;
#if ENABLE_ENVIRONMENT_MAP
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_render;
#endif // ENABLE_ENVIRONMENT_MAP
	wxSizer*                            m_icon_size_sizer;
	wxRadioBox*							m_layout_mode_box;
	wxColourPickerCtrl*					m_sys_colour {nullptr};
	wxColourPickerCtrl*					m_mod_colour {nullptr};
    bool                                isOSX {false};
	bool								m_settings_layout_changed {false};
	bool								m_seq_top_layer_only_changed{ false };
public:
	PreferencesDialog(wxWindow* parent);
	~PreferencesDialog() {}

	bool settings_layout_changed() const { return m_settings_layout_changed; }
	bool seq_top_layer_only_changed() const { return m_seq_top_layer_only_changed; }

	void	build();
	void	accept();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void layout();
    void create_icon_size_slider();
    void create_settings_mode_widget();
    void create_settings_text_color_widget();
};

} // GUI
} // Slic3r


#endif /* slic3r_Preferences_hpp_ */
