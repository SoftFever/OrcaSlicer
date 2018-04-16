#include "PresetUpdater.hpp"

#include <iostream>    // XXX
#include <algorithm>
#include <thread>
#include <stack>
#include <stdexcept>
#include <boost/format.hpp>
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


namespace Slic3r {


enum {
	SLIC3R_VERSION_BODY_MAX = 256,
};

static const char *INDEX_FILENAME = "index.idx";
static const char *TMP_EXTENSION = ".download";


struct Update
{
	fs::path source;
	fs::path target;
	Version version;

	Update(fs::path &&source, const fs::path &target, const Version &version) :
		source(source),
		target(std::move(target)),
		version(version)
	{}

	Update(fs::path &&source, fs::path &&target) :
		source(source),
		target(std::move(target))
	{}

	std::string name() { return source.stem().string(); }
};

typedef std::vector<Update> Updates;


struct PresetUpdater::priv
{
	int version_online_event;
	std::vector<Index> index_db;

	bool enabled_version_check;
	bool enabled_config_update;
	std::string version_check_url;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	priv(int event, AppConfig *app_config);

	void set_download_prefs(AppConfig *app_config);
	bool get_file(const std::string &url, const fs::path &target_path) const;
	void prune_tmps() const;
	void sync_version() const;
	void sync_config(const std::set<VendorProfile> vendors) const;

	void check_install_indices() const;
	Updates config_update() const;
	void perform_updates(AppConfig *app_config, Updates &&updates) const;
};

PresetUpdater::priv::priv(int event, AppConfig *app_config) :
	version_online_event(event),
	cache_path(fs::path(Slic3r::data_dir()) / "cache"),
	rsrc_path(fs::path(resources_dir()) / "profiles"),
	vendor_path(fs::path(Slic3r::data_dir()) / "vendor"),
	cancel(false)
{
	set_download_prefs(app_config);
	check_install_indices();
	index_db = std::move(Index::load_db());
}

void PresetUpdater::priv::set_download_prefs(AppConfig *app_config)
{
	enabled_version_check = app_config->get("version_check") == "1";
	version_check_url = app_config->get("version_check_url");
	enabled_config_update = app_config->get("preset_update") == "1";
}

bool PresetUpdater::priv::get_file(const std::string &url, const fs::path &target_path) const
{
	std::cerr << "get_file(): " << url << " -> " << target_path << std::endl;

	// TODO: Proper caching

	bool res = false;
	fs::path tmp_path = target_path;
	tmp_path += TMP_EXTENSION;

	Http::get(url)
		.on_progress([this](Http::Progress, bool &cancel) {
			if (cancel) { cancel = true; }
		})
		.on_complete([&](std::string body, unsigned http_status) {
			fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
			file.write(body.c_str(), body.size());
			file.close();
			fs::rename(tmp_path, target_path);
			res = true;
		})
		.perform_sync();

	return res;
}

void PresetUpdater::priv::prune_tmps() const
{
	for (fs::directory_iterator it(cache_path); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == TMP_EXTENSION) {
			fs::remove(it->path());
		}
	}
}

void PresetUpdater::priv::sync_version() const
{
	if (! enabled_version_check) { return; }

	Http::get(version_check_url)
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
}

void PresetUpdater::priv::sync_config(const std::set<VendorProfile> vendors) const
{
	std::cerr << "sync_config()" << std::endl;

	if (!enabled_config_update) { return; }

	// Donwload vendor preset bundles
	for (const auto &index : index_db) {
		if (cancel) { return; }

		std::cerr << "Index: " << index.vendor() << std::endl;

		const auto vendor_it = vendors.find(VendorProfile(index.vendor()));
		if (vendor_it == vendors.end()) { continue; }

		const VendorProfile &vendor = *vendor_it;
		if (vendor.config_update_url.empty()) { continue; }

		// Download a fresh index
		const auto idx_url = vendor.config_update_url + "/" + INDEX_FILENAME;
		const auto idx_path = cache_path / (vendor.id + ".idx");
		if (! get_file(idx_url, idx_path)) { continue; }
		if (cancel) { return; }

		std::cerr << "Got a new index: " << idx_path << std::endl;

		// Load the fresh index up
		Index new_index;
		new_index.load(idx_path);

		// See if a there's a new version to download
		const auto recommended_it = new_index.recommended();
		if (recommended_it == new_index.end()) { continue; }
		const auto recommended = recommended_it->config_version;

		std::cerr << "Current vendor version: " << vendor.config_version.to_string() << std::endl;
		std::cerr << "Recommended version:\t" << recommended.to_string() << std::endl;

		if (vendor.config_version >= recommended) { continue; }

		// Download a fresh bundle
		const auto bundle_url = (boost::format("%1%/%2%.ini") % vendor.config_update_url % recommended.to_string()).str();
		const auto bundle_path = cache_path / (vendor.id + ".ini");
		if (! get_file(bundle_url, bundle_path)) { continue; }
		if (cancel) { return; }

		std::cerr << "Got a new bundle: " << bundle_path << std::endl;
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
	Updates updates;

	for (const auto idx : index_db) {
		const auto bundle_path = vendor_path / (idx.vendor() + ".ini");

		if (! fs::exists(bundle_path)) {
			continue;
		}

		// Perform a basic load and check the version
		const auto vp = VendorProfile::from_ini(bundle_path, false);

		const auto ver_current = idx.find(vp.config_version);
		if (ver_current == idx.end()) {
			// TODO: throw / ignore ?
		}

		const auto recommended = idx.recommended();
		if (recommended == idx.end()) {
			throw std::runtime_error((boost::format("Invalid index: `%1%`") % idx.vendor()).str());
		}

		if (! ver_current->is_current_slic3r_supported()) {

			// TODO: Downgrade situation

		} else if (recommended->config_version > ver_current->config_version) {
			// Config bundle update situation

			auto path_in_cache = cache_path / (idx.vendor() + ".ini");
			if (! fs::exists(path_in_cache)) {
				continue;
			}

			const auto cached_vp = VendorProfile::from_ini(path_in_cache, false);
			if (cached_vp.config_version == recommended->config_version) {
				updates.emplace_back(std::move(path_in_cache), bundle_path, *recommended);
			} else {
				// XXX: ?
			}
		}
	}

	return updates;
}

void PresetUpdater::priv::perform_updates(AppConfig *app_config, Updates &&updates) const
{
	SnapshotDB::singleton().take_snapshot(*app_config, Snapshot::SNAPSHOT_UPGRADE);

	for (const auto &update : updates) {
		fs::copy_file(update.source, update.target, fs::copy_option::overwrite_if_exists);

		PresetBundle bundle;
		bundle.load_configbundle(update.target.string(), PresetBundle::LOAD_CFGBNDLE_SYSTEM);

		auto preset_remover = [](const Preset &preset) {
			fs::remove(preset.file);
		};

		for (const auto &preset : bundle.prints)    { preset_remover(preset); }
		for (const auto &preset : bundle.filaments) { preset_remover(preset); }
		for (const auto &preset : bundle.printers)  { preset_remover(preset); }
	}
}


PresetUpdater::PresetUpdater(int version_online_event, AppConfig *app_config) :
	p(new priv(version_online_event, app_config))
{}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		p->cancel = true;
		p->thread.join();
	}
}

void PresetUpdater::sync(AppConfig *app_config, PresetBundle *preset_bundle)
{
	p->set_download_prefs(app_config);
	if (!p->enabled_version_check && !p->enabled_config_update) { return; }

	// Copy the whole vendors data for use in the background thread
	// Unfortunatelly as of C++11, it needs to be copied again
	// into the closure (but perhaps the compiler can elide this).
	std::set<VendorProfile> vendors = preset_bundle->vendors;

	p->thread = std::move(std::thread([this, vendors]() {
		this->p->prune_tmps();
		this->p->sync_version();
		this->p->sync_config(std::move(vendors));
	}));
}

void PresetUpdater::config_update(AppConfig *app_config)
{
	if (! p->enabled_config_update) { return; }

	auto updates = p->config_update();
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
			p->perform_updates(app_config, std::move(updates));
		}
	}
}

void PresetUpdater::install_bundles_rsrc(AppConfig *app_config, std::vector<std::string> &&bundles)
{
	Updates updates;

	for (const auto &bundle : bundles) {
		auto path_in_rsrc = p->rsrc_path / bundle;
		auto path_in_vendors = p->vendor_path / bundle;
		updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors));
	}

	p->perform_updates(app_config, std::move(updates));
}


}
