#ifndef slic3r_PresetUpdate_hpp_
#define slic3r_PresetUpdate_hpp_

#include <memory>

namespace Slic3r {


class AppConfig;
class PresetBundle;

class PresetUpdater
{
public:
	PresetUpdater(PresetBundle *preset_bundle);
	PresetUpdater(PresetUpdater &&) = delete;
	PresetUpdater(const PresetUpdater &) = delete;
	PresetUpdater &operator=(PresetUpdater &&) = delete;
	PresetUpdater &operator=(const PresetUpdater &) = delete;
	~PresetUpdater();

	static void download(AppConfig *app_config, PresetBundle *preset_bundle);
private:
	struct priv;
	std::unique_ptr<priv> p;
};


// TODO: Remove
namespace Utils {

void preset_update_check();

}

}

#endif
