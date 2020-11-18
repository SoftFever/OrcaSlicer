#ifndef slic3r_PresetUpdate_hpp_
#define slic3r_PresetUpdate_hpp_

#include <memory>
#include <vector>

#include <wx/event.h>

namespace Slic3r {


class AppConfig;
class PresetBundle;
class Semver;

class PresetUpdater
{
public:
	PresetUpdater();
	PresetUpdater(PresetUpdater &&) = delete;
	PresetUpdater(const PresetUpdater &) = delete;
	PresetUpdater &operator=(PresetUpdater &&) = delete;
	PresetUpdater &operator=(const PresetUpdater &) = delete;
	~PresetUpdater();

	// If either version check or config updating is enabled, get the appropriate data in the background and cache it.
	void sync(PresetBundle *preset_bundle);

	// If version check is enabled, check if chaced online slic3r version is newer, notify if so.
	void slic3r_update_notify();

	enum UpdateResult {
		R_NOOP,
		R_INCOMPAT_EXIT,
		R_INCOMPAT_CONFIGURED,
		R_UPDATE_INSTALLED,
		R_UPDATE_REJECT,
		R_UPDATE_NOTIFICATION
	};

	// If updating is enabled, check if updates are available in cache, if so, ask about installation.
	// A false return value implies Slic3r should exit due to incompatibility of configuration.
	// Providing old slic3r version upgrade profiles on upgrade of an application even in case
	// that the config index installed from the Internet is equal to the index contained in the installation package.
	// no_notification = force modal textbox, otherwise some cases only shows notification
	UpdateResult config_update(const Semver &old_slic3r_version, bool no_notification) const;

	// "Update" a list of bundles from resources (behaves like an online update).
	void install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot = true) const;

	void on_update_notification_confirm();
private:
	struct priv;
	std::unique_ptr<priv> p;
};

wxDECLARE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);


}
#endif
