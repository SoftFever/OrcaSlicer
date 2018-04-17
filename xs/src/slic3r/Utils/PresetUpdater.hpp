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

	void sync(PresetBundle *preset_bundle);
	void slic3r_update_notify();
	void config_update() const;
	void install_bundles_rsrc(std::vector<std::string> &&bundles);
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}
#endif
