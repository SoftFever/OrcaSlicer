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
	ConfigWizard(wxWindow *parent, const PresetBundle &bundle);
	ConfigWizard(ConfigWizard &&) = delete;
	ConfigWizard(const ConfigWizard &) = delete;
	ConfigWizard &operator=(ConfigWizard &&) = delete;
	ConfigWizard &operator=(const ConfigWizard &) = delete;
	~ConfigWizard();

	static void run(wxWindow *parent);
private:
	struct priv;
	std::unique_ptr<priv> p;

	friend class ConfigWizardPage;
};



}
}

#endif
