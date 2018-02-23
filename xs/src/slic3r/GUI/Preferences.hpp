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
	PreferencesDialog(wxWindow* parent, int event_preferences) : wxDialog(parent, wxID_ANY, _(L("Preferences")),
		wxDefaultPosition, wxDefaultSize), m_event_preferences(event_preferences) {	build(); }
	~PreferencesDialog(){ }

	void	build();
	void	accept();
};

} // GUI
} // Slic3r

