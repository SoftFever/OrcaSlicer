#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>
#include <map>

class wxColourPickerCtrl;

namespace Slic3r {

	enum  NotifyReleaseMode {
		NotifyReleaseAll,
		NotifyReleaseOnly,
		NotifyReleaseNone
	};

namespace GUI {

class ConfigOptionsGroup;
class OG_CustomCtrl;

class PreferencesDialog : public DPIDialog
{
	std::map<std::string, std::string>	m_values;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_general;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_camera;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_gui;
#ifdef _WIN32
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_dark_mode;
#endif //_WIN32
#if ENABLE_ENVIRONMENT_MAP
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup_render;
#endif // ENABLE_ENVIRONMENT_MAP
	wxSizer*                            m_icon_size_sizer;
	wxColourPickerCtrl*					m_sys_colour {nullptr};
	wxColourPickerCtrl*					m_mod_colour {nullptr};
    bool                                isOSX {false};
	bool								m_settings_layout_changed {false};
	bool								m_seq_top_layer_only_changed{ false };
	bool								m_recreate_GUI{false};

public:
	explicit PreferencesDialog(wxWindow* parent, int selected_tab = 0, const std::string& highlight_opt_key = std::string());
	~PreferencesDialog() = default;

	bool settings_layout_changed() const { return m_settings_layout_changed; }
	bool seq_top_layer_only_changed() const { return m_seq_top_layer_only_changed; }
	bool recreate_GUI() const { return m_recreate_GUI; }
	void	build(size_t selected_tab = 0);
	void	update_ctrls_alignment();
	void	accept(wxEvent&);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void layout();
    void create_icon_size_slider();
    void create_settings_mode_widget();
    void create_settings_text_color_widget();
	void init_highlighter(const t_config_option_key& opt_key);
	std::vector<ConfigOptionsGroup*> optgroups();

	struct PreferencesHighlighter
	{
		void set_timer_owner(wxEvtHandler* owner, int timerid = wxID_ANY);
		void init(std::pair<OG_CustomCtrl*, bool*>);
		void blink();
		void invalidate();

	private:
		OG_CustomCtrl* m_custom_ctrl{ nullptr };
		bool* m_show_blink_ptr{ nullptr };
		int				m_blink_counter{ 0 };
		wxTimer         m_timer;
	}
	m_highlighter;
};

} // GUI
} // Slic3r


#endif /* slic3r_Preferences_hpp_ */
