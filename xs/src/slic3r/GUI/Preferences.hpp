#ifndef slic3r_Preferences_hpp_
#define slic3r_Preferences_hpp_

#include "GUI.hpp"

#include <wx/dialog.h>
#include <map>

namespace Slic3r {
namespace GUI {

class ConfigOptionsGroup;

class PreferencesDialog : public wxDialog
{
	std::map<std::string, std::string>	m_values;
	std::shared_ptr<ConfigOptionsGroup>	m_optgroup;
	int		m_event_preferences;
public:
	PreferencesDialog(wxWindow* parent, int event_preferences);
	~PreferencesDialog(){ }

	void	build();
	void	accept();
};

} // GUI
} // Slic3r


#endif /* slic3r_Preferences_hpp_ */
