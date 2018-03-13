#include "PresetUpdate.hpp"

#include <iostream>  // XXX
#include <thread>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/Utils/Http.hpp"

namespace fs = boost::filesystem;


namespace Slic3r {


struct PresetUpdater::priv
{
	PresetBundle *bundle;
	fs::path cache_path;
	std::thread thread;

	priv(PresetBundle *bundle);

	void download();
};


PresetUpdater::priv::priv(PresetBundle *bundle) :
	bundle(bundle),
	cache_path(fs::path(Slic3r::data_dir()) / "cache")
{}

void PresetUpdater::priv::download()
{
	std::cerr << "PresetUpdater::priv::download()" << std::endl;

	std::cerr << "Bundle vendors: " << bundle->vendors.size() << std::endl;
	for (const auto &vendor : bundle->vendors) {
		std::cerr << "vendor: " << vendor.name << std::endl;
		std::cerr << "  URL: " << vendor.config_update_url << std::endl;

		// TODO: Proper caching

		auto target_path = cache_path / vendor.id;
		target_path += ".ini";
		std::cerr << "target_path: " << target_path << std::endl;

		Http::get(vendor.config_update_url)
			.on_complete([&](std::string body, unsigned http_status) {
				std::cerr << "Got ini: " << http_status << ", body: " << body.size() << std::endl;
				fs::fstream file(target_path, std::ios::out | std::ios::binary | std::ios::trunc);
				file.write(body.c_str(), body.size());
			})
			.on_error([](std::string body, std::string error, unsigned http_status) {
				// TODO: what about errors?
				std::cerr << "Error: " << http_status << ", " << error << std::endl;
			})
			.perform_sync();
	}
}

PresetUpdater::PresetUpdater(PresetBundle *preset_bundle) : p(new priv(preset_bundle)) {}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		p->thread.detach();
	}
}

void PresetUpdater::download(AppConfig *app_config, PresetBundle *preset_bundle)
{
	std::cerr << "PresetUpdater::download()" << std::endl;

	auto self = std::make_shared<PresetUpdater>(preset_bundle);
	auto thread = std::thread([self](){
		self->p->download();
	});
	self->p->thread = std::move(thread);
}


// TODO: remove
namespace Utils {

void preset_update_check()
{
	std::cerr << "preset_update_check()" << std::endl;

	// TODO:
	// 1. Get a version tag or the whole bundle from the web
	// 2. Store into temporary location (?)
	// 3. ???
	// 4. Profit!
}

}

}
