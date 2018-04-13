#ifndef slic3r_PresetUpdate_hpp_
#define slic3r_PresetUpdate_hpp_

#include <memory>

namespace Slic3r {


class AppConfig;
class PresetBundle;

class PresetUpdater
{
public:
	PresetUpdater(int version_online_event, AppConfig *app_config);
	PresetUpdater(PresetUpdater &&) = delete;
	PresetUpdater(const PresetUpdater &) = delete;
	PresetUpdater &operator=(PresetUpdater &&) = delete;
	PresetUpdater &operator=(const PresetUpdater &) = delete;
	~PresetUpdater();

	void sync(AppConfig *app_config, PresetBundle *preset_bundle);

	void config_update(AppConfig *app_config);
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}
#endif
