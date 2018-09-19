#ifndef slic3r_ConfigWizard_hpp_
#define slic3r_ConfigWizard_hpp_

#include <memory>

#include <wx/dialog.h>

namespace Slic3r {

class PresetBundle;
class PresetUpdater;

namespace GUI {


class ConfigWizard: public wxDialog
{
public:
	// Why is the Wizard run
	enum RunReason {
		RR_DATA_EMPTY,                  // No or empty datadir
		RR_DATA_LEGACY,                 // Pre-updating datadir
		RR_DATA_INCOMPAT,               // Incompatible datadir - Slic3r downgrade situation
		RR_USER,                        // User requested the Wizard from the menus
	};

	ConfigWizard(wxWindow *parent, RunReason run_reason);
	ConfigWizard(ConfigWizard &&) = delete;
	ConfigWizard(const ConfigWizard &) = delete;
	ConfigWizard &operator=(ConfigWizard &&) = delete;
	ConfigWizard &operator=(const ConfigWizard &) = delete;
	~ConfigWizard();

	// Run the Wizard. Return whether it was completed.
	bool run(PresetBundle *preset_bundle, const PresetUpdater *updater);

	static const wxString& name();
private:
	struct priv;
	std::unique_ptr<priv> p;

	friend class ConfigWizardPage;
};



}
}

#endif
