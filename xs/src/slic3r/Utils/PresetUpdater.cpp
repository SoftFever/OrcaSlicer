#include "PresetUpdater.hpp"

#include <iostream>    // XXX
#include <algorithm>
#include <thread>
#include <stack>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <wx/app.h>
#include <wx/event.h>
#include <wx/msgdlg.h>

#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/Config/Snapshot.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;

// XXX: Prevent incomplete file downloads: download a tmp file, then move
//      Delete incomplete ones on startup.

namespace Slic3r {


// TODO: proper URL
// TODO: Actually, use index
static const std::string SLIC3R_VERSION_URL = "https://gist.githubusercontent.com/vojtechkral/4d8fd4a3b8699a01ec892c264178461c/raw/e9187c3e15ceaf1a90f29b7c43cf3ccc746140f0/slic3rPE.version";
enum {
	SLIC3R_VERSION_BODY_MAX = 256,
};


struct Update
{
	fs::path source;
	fs::path target;
	Version version;

	Update(const fs::path &source, fs::path &&target, const Version &version) :
		source(source),
		target(std::move(target)),
		version(version)
	{}

	std::string name() { return source.stem().string(); }
};

typedef std::vector<Update> Updates;


struct PresetUpdater::priv
{
	int version_online_event;
	AppConfig *app_config;
	bool version_check;
	bool preset_update;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	priv(int event, AppConfig *app_config);

	void download(const std::set<VendorProfile> vendors) const;

	void check_install_indices() const;
	Updates config_update() const;
};

PresetUpdater::priv::priv(int event, AppConfig *app_config) :
	version_online_event(event),
	app_config(app_config),
	version_check(false),
	preset_update(false),
	cache_path(fs::path(Slic3r::data_dir()) / "cache"),
	rsrc_path(fs::path(resources_dir()) / "profiles"),
	vendor_path(fs::path(Slic3r::data_dir()) / "vendor"),
	cancel(false)
{}

void PresetUpdater::priv::download(const std::set<VendorProfile> vendors) const
{
	if (!version_check) { return; }

	// Download current Slic3r version
	Http::get(SLIC3R_VERSION_URL)
		.size_limit(SLIC3R_VERSION_BODY_MAX)
		.on_progress([this](Http::Progress, bool &cancel) {
			cancel = this->cancel;
		})
		.on_complete([&](std::string body, unsigned http_status) {
			boost::trim(body);
			wxCommandEvent* evt = new wxCommandEvent(version_online_event);
			evt->SetString(body);
			GUI::get_app()->QueueEvent(evt);
		})
		.perform_sync();

	if (!preset_update) { return; }

	// Donwload vendor preset bundles
	for (const auto &vendor : vendors) {
		if (cancel) { return; }

		// TODO: Proper caching

		auto target_path = cache_path / vendor.id;
		target_path += ".ini";

		Http::get(vendor.config_update_url)
			.on_progress([this](Http::Progress, bool &cancel) {
				cancel = this->cancel;
			})
			.on_complete([&](std::string body, unsigned http_status) {
				fs::fstream file(target_path, std::ios::out | std::ios::binary | std::ios::trunc);
				file.write(body.c_str(), body.size());
			})
			.perform_sync();
	}
}

void PresetUpdater::priv::check_install_indices() const
{
	for (fs::directory_iterator it(rsrc_path); it != fs::directory_iterator(); ++it) {
		const auto &path = it->path();
		if (path.extension() == ".idx") {
			const auto path_in_cache = cache_path / path.filename();

			// TODO: compare versions
			if (! fs::exists(path_in_cache)) {
				fs::copy_file(path, path_in_cache, fs::copy_option::overwrite_if_exists);
			}
		}
	}
}

Updates PresetUpdater::priv::config_update() const
{
	priv::check_install_indices();
	const auto index_db = Index::load_db();     // TODO: Keep in Snapshots singleton?

	Updates updates;

	for (const auto idx : index_db) {
		const auto bundle_path = vendor_path / (idx.vendor() + ".ini");

		// If the bundle doesn't exist at all, update from resources
		// if (! fs::exists(bundle_path)) {
		// 	auto path_in_rsrc = rsrc_path / (idx.vendor() + ".ini");

		// 	// Otherwise load it and check for chached updates
		// 	const auto rsrc_vp = VendorProfile::from_ini(path_in_rsrc, false);

		// 	const auto rsrc_ver = idx.find(rsrc_vp.config_version);
		// 	if (rsrc_ver == idx.end()) {
		// 		// TODO: throw
		// 	}

		// 	if (fs::exists(path_in_rsrc)) {
		// 		updates.emplace_back(bundle_path, std::move(path_in_rsrc), *rsrc_ver);
		// 	} else {
		// 		// XXX: ???
		// 	}

		// 	continue;
		// }

		if (! fs::exists(bundle_path)) {
			continue;
		}

		// Perform a basic load and check the version
		const auto vp = VendorProfile::from_ini(bundle_path, false);

		const auto ver_current = idx.find(vp.config_version);
		if (ver_current == idx.end()) {
			// TODO: throw
		}

		const auto recommended = idx.recommended();
		if (recommended == idx.end()) {
			// TODO: throw
		}

		if (! ver_current->is_current_slic3r_supported()) {

			// TODO: Downgrade situation

		} else if (recommended->config_version > ver_current->config_version) {
			// Config bundle update situation

			auto path_in_cache = cache_path / (idx.vendor() + ".ini");
			const auto cached_vp = VendorProfile::from_ini(path_in_cache, false);
			if (cached_vp.config_version == recommended->config_version) {
				updates.emplace_back(bundle_path, std::move(path_in_cache), *ver_current);
			} else {
				// XXX: ???
			}
		}
	}

	// Check for bundles that don't have an index
	// for (fs::directory_iterator it(rsrc_path); it != fs::directory_iterator(); ++it) {
	// 	if (it->path().extension() == ".ini") {
	// 		const auto &path = it->path();
	// 		const auto vendor_id = path.stem().string();

	// 		const auto needle = std::find_if(index_db.begin(), index_db.end(), [&vendor_id](const Index &idx) {
	// 			return idx.vendor() == vendor_id;
	// 		});
	// 		if (needle != index_db.end()) {
	// 			continue;
	// 		}

	// 		auto vp = VendorProfile::from_ini(path, false);
	// 		auto path_in_data = vendor_path / path.filename();

	// 		if (! fs::exists(path_in_data)) {
	// 			Version version;
	// 			version.config_version = vp.config_version;
	// 			updates.emplace_back(path, std::move(path_in_data), version);
	// 		}
	// 	}
	// }

	return updates;
}


PresetUpdater::PresetUpdater(int version_online_event, AppConfig *app_config) :
	p(new priv(version_online_event, app_config))
{
	p->preset_update = app_config->get("preset_update") == "1";
	// preset_update implies version_check:   // XXX: not any more
	p->version_check = p->preset_update || app_config->get("version_check") == "1";
}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		p->cancel = true;
		p->thread.join();
	}
}

void PresetUpdater::download(PresetBundle *preset_bundle)
{

	// Copy the whole vendors data for use in the background thread
	// Unfortunatelly as of C++11, it needs to be copied again
	// into the closure (but perhaps the compiler can elide this).
	std::set<VendorProfile> vendors = preset_bundle->vendors;

	p->thread = std::move(std::thread([this, vendors]() {
		this->p->download(std::move(vendors));
	}));
}

void PresetUpdater::config_update()
{
	const auto updates = p->config_update();
	if (updates.size() > 0) {
		const auto msg = _(L("Configuration update is available. Would you like to install it?"));

		auto ext_msg = _(L(
			"Note that a full configuration snapshot will be created first. It can then be restored at any time "
			"should there be a problem with the new version.\n\n"
			"Updated configuration bundles:\n"
		));

		for (const auto &update : updates) {
			ext_msg += update.target.stem().string() + " " + update.version.config_version.to_string();
			if (! update.version.comment.empty()) {
				ext_msg += std::string(" (") + update.version.comment + ")";
			}
			ext_msg += "\n";
		}

		wxMessageDialog dlg(NULL, msg, _(L("Configuration update")), wxYES_NO|wxCENTRE);
		dlg.SetExtendedMessage(ext_msg);
		const auto res = dlg.ShowModal();
		std::cerr << "After modal" << std::endl;
		if (res == wxID_YES) {
			// User gave clearance, updates are go

			// TODO: Comment?
			SnapshotDB::singleton().take_snapshot(*p->app_config, Snapshot::SNAPSHOT_UPGRADE, "");

			for (const auto &update : updates) {
				fs::copy_file(update.source, update.target, fs::copy_option::overwrite_if_exists);
				
				PresetBundle bundle;
				bundle.load_configbundle(update.target.string(), PresetBundle::LOAD_CFGBNDLE_SYSTEM);

				auto preset_remover = [](const Preset &preset) {
					fs::remove(preset.file);
				};

				for (const auto &preset : bundle.prints) { preset_remover(preset); }
				for (const auto &preset : bundle.filaments) { preset_remover(preset); }
				for (const auto &preset : bundle.printers) { preset_remover(preset); }
			}
		}
	}
}


}
