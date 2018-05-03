#ifndef slic3r_PresetUpdate_hpp_
#define slic3r_PresetUpdate_hpp_

#include <memory>
#include <vector>

namespace Slic3r {


class AppConfig;
class PresetBundle;

class PresetUpdater
{
public:
	PresetUpdater(int version_online_event);
	PresetUpdater(PresetUpdater &&) = delete;
	PresetUpdater(const PresetUpdater &) = delete;
	PresetUpdater &operator=(PresetUpdater &&) = delete;
	PresetUpdater &operator=(const PresetUpdater &) = delete;
	~PresetUpdater();

	// If either version check or config updating is enabled, get the appropriate data in the background and cache it.
	void sync(PresetBundle *preset_bundle);

	// If version check is enabled, check if chaced online slic3r version is newer, notify if so.
	void slic3r_update_notify();

	// If updating is enabled, check if updates are available in cache, if so, ask about installation.
	// A false return value implies Slic3r should exit due to incompatibility of configuration.
	bool config_update() const;

	// "Update" a list of bundles from resources (behaves like an online update).
	void install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot = true) const;
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}
#endif
