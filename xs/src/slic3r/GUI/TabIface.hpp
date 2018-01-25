#include <vector>

namespace Slic3r {
	class DynamicPrintConfig;
	class PresetCollection;

namespace GUI {
	class Tab;
}

class TabIface {
public:
	TabIface() : m_tab(nullptr) {}
	TabIface(GUI::Tab *tab) : m_tab(tab) {}
//	TabIface(const TabIface &rhs) : m_tab(rhs.m_tab) {}

	void		load_current_preset();
	void		update_tab_ui();
	void		update_ui_from_settings();
	void		select_preset(char* name);
	char*		title();
	void		load_config(DynamicPrintConfig* config);
	bool		current_preset_is_dirty();
	DynamicPrintConfig*		get_config();
	PresetCollection*			TabIface::get_presets();
	std::vector<std::string>	TabIface::get_dependent_tabs();

protected:
	GUI::Tab   *m_tab;
};

}; // namespace Slic3r
