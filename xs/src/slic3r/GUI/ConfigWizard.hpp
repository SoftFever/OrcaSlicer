#ifndef slic3r_ConfigWizard_hpp_
#define slic3r_ConfigWizard_hpp_

#include <memory>

#include <wx/dialog.h>

namespace Slic3r {

class PresetBundle;

namespace GUI {


class ConfigWizard: public wxDialog
{
public:
	ConfigWizard(wxWindow *parent);
	ConfigWizard(ConfigWizard &&) = delete;
	ConfigWizard(const ConfigWizard &) = delete;
	ConfigWizard &operator=(ConfigWizard &&) = delete;
	ConfigWizard &operator=(const ConfigWizard &) = delete;
	~ConfigWizard();

	static bool run(wxWindow *parent, PresetBundle *preset_bundle);
private:
	struct priv;
	std::unique_ptr<priv> p;

	friend class ConfigWizardPage;
};



}
}

#endif
