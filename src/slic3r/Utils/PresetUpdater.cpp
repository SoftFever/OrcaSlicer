#include "PresetUpdater.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>
#include <ostream>
#include <utility>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include <wx/app.h>
#include <wx/msgdlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/format.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/GUI/UpdateDialogs.hpp"
#include "slic3r/GUI/ConfigWizard.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Config/Version.hpp"
#include "slic3r/Config/Snapshot.hpp"

namespace fs = boost::filesystem;
using Slic3r::GUI::Config::Index;
using Slic3r::GUI::Config::Version;
using Slic3r::GUI::Config::Snapshot;
using Slic3r::GUI::Config::SnapshotDB;



// FIXME: Incompat bundle resolution doesn't deal with inherited user presets


namespace Slic3r {


enum {
	SLIC3R_VERSION_BODY_MAX = 256,
};

static const char *INDEX_FILENAME = "index.idx";
static const char *TMP_EXTENSION = ".download";


void copy_file_fix(const fs::path &source, const fs::path &target)
{
	static const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;   // aka 644

	BOOST_LOG_TRIVIAL(debug) << format("PresetUpdater: Copying %1% -> %2%", source, target);

	// Make sure the file has correct permission both before and after we copy over it
	if (fs::exists(target)) {
		fs::permissions(target, perms);
	}
	fs::copy_file(source, target, fs::copy_option::overwrite_if_exists);
	fs::permissions(target, perms);
}

struct Update
{
	fs::path source;
	fs::path target;

	Version version;
	std::string vendor;
	std::string changelog_url;

	bool forced_update;

	Update() {}
	Update(fs::path &&source, fs::path &&target, const Version &version, std::string vendor, std::string changelog_url, bool forced = false)
		: source(std::move(source))
		, target(std::move(target))
		, version(version)
		, vendor(std::move(vendor))
		, changelog_url(std::move(changelog_url))
		, forced_update(forced)
	{}

	void install() const
	{
		copy_file_fix(source, target);
	}

	friend std::ostream& operator<<(std::ostream& os, const Update &self)
	{
		os << "Update(" << self.source.string() << " -> " << self.target.string() << ')';
		return os;
	}
};

struct Incompat
{
	fs::path bundle;
	Version version;
	std::string vendor;

	Incompat(fs::path &&bundle, const Version &version, std::string vendor)
		: bundle(std::move(bundle))
		, version(version)
		, vendor(std::move(vendor))
	{}

	void remove() {
		// Remove the bundle file
		fs::remove(bundle);

		// Look for an installed index and remove it too if any
		const fs::path installed_idx = bundle.replace_extension("idx");
		if (fs::exists(installed_idx)) {
			fs::remove(installed_idx);
		}
	}

	friend std::ostream& operator<<(std::ostream& os , const Incompat &self) {
		os << "Incompat(" << self.bundle.string() << ')';
		return os;
	}
};

struct Updates
{
	std::vector<Incompat> incompats;
	std::vector<Update> updates;
};


wxDEFINE_EVENT(EVT_SLIC3R_VERSION_ONLINE, wxCommandEvent);


struct PresetUpdater::priv
{
	std::vector<Index> index_db;

	bool enabled_version_check;
	bool enabled_config_update;
	std::string version_check_url;

	fs::path cache_path;
	fs::path rsrc_path;
	fs::path vendor_path;

	bool cancel;
	std::thread thread;

	bool has_waiting_updates { false };
	Updates waiting_updates;

	priv();

	void set_download_prefs(AppConfig *app_config);
	bool get_file(const std::string &url, const fs::path &target_path) const;
	void prune_tmps() const;
	void sync_version() const;
	void sync_config(const VendorMap vendors);

	void check_install_indices() const;
	Updates get_config_updates(const Semver& old_slic3r_version) const;
	void perform_updates(Updates &&updates, bool snapshot = true) const;
	void set_waiting_updates(Updates u);
};

PresetUpdater::priv::priv()
	: cache_path(fs::path(Slic3r::data_dir()) / "cache")
	, rsrc_path(fs::path(resources_dir()) / "profiles")
	, vendor_path(fs::path(Slic3r::data_dir()) / "vendor")
	, cancel(false)
{
	set_download_prefs(GUI::wxGetApp().app_config);
	// Install indicies from resources. Only installs those that are either missing or older than in resources.
	check_install_indices();
	// Load indices from the cache directory.
	index_db = Index::load_db();
}

// Pull relevant preferences from AppConfig
void PresetUpdater::priv::set_download_prefs(AppConfig *app_config)
{
	enabled_version_check = app_config->get("version_check") == "1";
	version_check_url = app_config->version_check_url();
	enabled_config_update = app_config->get("preset_update") == "1" && !app_config->legacy_datadir();
}

// Downloads a file (http get operation). Cancels if the Updater is being destroyed.
bool PresetUpdater::priv::get_file(const std::string &url, const fs::path &target_path) const
{
	bool res = false;
	fs::path tmp_path = target_path;
	tmp_path += format(".%1%%2%", get_current_pid(), TMP_EXTENSION);

	BOOST_LOG_TRIVIAL(info) << format("Get: `%1%`\n\t-> `%2%`\n\tvia tmp path `%3%`",
		url,
		target_path.string(),
		tmp_path.string());

	Http::get(url)
		.on_progress([this](Http::Progress, bool &cancel) {
			if (cancel) { cancel = true; }
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%",
				url,
				http_status,
				error);
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
			file.write(body.c_str(), body.size());
			file.close();
			fs::rename(tmp_path, target_path);
			res = true;
		})
		.perform_sync();

	return res;
}

// Remove leftover paritally downloaded files, if any.
void PresetUpdater::priv::prune_tmps() const
{
    for (auto &dir_entry : boost::filesystem::directory_iterator(cache_path))
		if (is_plain_file(dir_entry) && dir_entry.path().extension() == TMP_EXTENSION) {
			BOOST_LOG_TRIVIAL(debug) << "Cache prune: " << dir_entry.path().string();
			fs::remove(dir_entry.path());
		}
}

// Get Slic3rPE version available online, save in AppConfig.
void PresetUpdater::priv::sync_version() const
{
	if (! enabled_version_check) { return; }

	BOOST_LOG_TRIVIAL(info) << format("Downloading %1% online version from: `%2%`", SLIC3R_APP_NAME, version_check_url);

	Http::get(version_check_url)
		.size_limit(SLIC3R_VERSION_BODY_MAX)
		.on_progress([this](Http::Progress, bool &cancel) {
			cancel = this->cancel;
		})
		.on_error([&](std::string body, std::string error, unsigned http_status) {
			(void)body;
			BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%",
				version_check_url,
				http_status,
				error);
		})
		.on_complete([&](std::string body, unsigned /* http_status */) {
			boost::trim(body);
			const auto nl_pos = body.find_first_of("\n\r");
			if (nl_pos != std::string::npos) {
				body.resize(nl_pos);
			}

			if (! Semver::parse(body)) {
				BOOST_LOG_TRIVIAL(warning) << format("Received invalid contents from `%1%`: Not a correct semver: `%2%`", SLIC3R_APP_NAME, body);
				return;
			}

			BOOST_LOG_TRIVIAL(info) << format("Got %1% online version: `%2%`. Sending to GUI thread...", SLIC3R_APP_NAME, body);

			wxCommandEvent* evt = new wxCommandEvent(EVT_SLIC3R_VERSION_ONLINE);
			evt->SetString(GUI::from_u8(body));
			GUI::wxGetApp().QueueEvent(evt);
		})
		.perform_sync();
}

// Download vendor indices. Also download new bundles if an index indicates there's a new one available.
// Both are saved in cache.
void PresetUpdater::priv::sync_config(const VendorMap vendors)
{
	BOOST_LOG_TRIVIAL(info) << "Syncing configuration cache";

	if (!enabled_config_update) { return; }

	// Donwload vendor preset bundles
	// Over all indices from the cache directory:
	for (auto &index : index_db) {
		if (cancel) { return; }

		const auto vendor_it = vendors.find(index.vendor());
		if (vendor_it == vendors.end()) {
			BOOST_LOG_TRIVIAL(warning) << "No such vendor: " << index.vendor();
			continue;
		}

		const VendorProfile &vendor = vendor_it->second;
		if (vendor.config_update_url.empty()) {
			BOOST_LOG_TRIVIAL(info) << "Vendor has no config_update_url: " << vendor.name;
			continue;
		}

		// Download a fresh index
		BOOST_LOG_TRIVIAL(info) << "Downloading index for vendor: " << vendor.name;
		const auto idx_url = vendor.config_update_url + "/" + INDEX_FILENAME;
		const std::string idx_path = (cache_path / (vendor.id + ".idx")).string();
		const std::string idx_path_temp = idx_path + "-update";
		//check if idx_url is leading to our site 
		if (! boost::starts_with(idx_url, "http://files.prusa3d.com/wp-content/uploads/repository/"))
		{
			BOOST_LOG_TRIVIAL(warning) << "unsafe url path for vendor \"" << vendor.name << "\" rejected: " << idx_url;
			continue;
		}
		if (!get_file(idx_url, idx_path_temp)) { continue; }
		if (cancel) { return; }

		// Load the fresh index up
		{
			Index new_index;
			try {
				new_index.load(idx_path_temp);
			} catch (const std::exception & /* err */) {
				BOOST_LOG_TRIVIAL(error) << format("Could not load downloaded index %1% for vendor %2%: invalid index?", idx_path_temp, vendor.name);
				continue;
			}
			if (new_index.version() < index.version()) {
				BOOST_LOG_TRIVIAL(warning) << format("The downloaded index %1% for vendor %2% is older than the active one. Ignoring the downloaded index.", idx_path_temp, vendor.name);
				continue;
			}
			Slic3r::rename_file(idx_path_temp, idx_path);
			//if we rename path we need to change it in Index object too or create the object again
			//index = std::move(new_index);
			try {
				index.load(idx_path);
			}
			catch (const std::exception& /* err */) {
				BOOST_LOG_TRIVIAL(error) << format("Could not load downloaded index %1% for vendor %2%: invalid index?", idx_path, vendor.name);
				continue;
			}
			if (cancel)
				return;
		}

		// See if a there's a new version to download
		const auto recommended_it = index.recommended();
		if (recommended_it == index.end()) {
			BOOST_LOG_TRIVIAL(error) << format("No recommended version for vendor: %1%, invalid index?", vendor.name);
			continue;
		}

		const auto recommended = recommended_it->config_version;

		BOOST_LOG_TRIVIAL(debug) << format("Got index for vendor: %1%: current version: %2%, recommended version: %3%",
			vendor.name,
			vendor.config_version.to_string(),
			recommended.to_string());

		if (vendor.config_version >= recommended) { continue; }

		// Download a fresh bundle
		BOOST_LOG_TRIVIAL(info) << "Downloading new bundle for vendor: " << vendor.name;
		const auto bundle_url = format("%1%/%2%.ini", vendor.config_update_url, recommended.to_string());
		const auto bundle_path = cache_path / (vendor.id + ".ini");
		if (! get_file(bundle_url, bundle_path)) { continue; }
		if (cancel) { return; }
	}
}

// Install indicies from resources. Only installs those that are either missing or older than in resources.
void PresetUpdater::priv::check_install_indices() const
{
	BOOST_LOG_TRIVIAL(info) << "Checking if indices need to be installed from resources...";

    for (auto &dir_entry : boost::filesystem::directory_iterator(rsrc_path))
		if (is_idx_file(dir_entry)) {
			const auto &path = dir_entry.path();
			const auto path_in_cache = cache_path / path.filename();

			if (! fs::exists(path_in_cache)) {
				BOOST_LOG_TRIVIAL(info) << "Install index from resources: " << path.filename();
				copy_file_fix(path, path_in_cache);
			} else {
				Index idx_rsrc, idx_cache;
				idx_rsrc.load(path);
				idx_cache.load(path_in_cache);

				if (idx_cache.version() < idx_rsrc.version()) {
					BOOST_LOG_TRIVIAL(info) << "Update index from resources: " << path.filename();
					copy_file_fix(path, path_in_cache);
				}
			}
		}
}

// Generates a list of bundle updates that are to be performed.
// Version of slic3r that was running the last time and which was read out from PrusaSlicer.ini is provided
// as a parameter.
Updates PresetUpdater::priv::get_config_updates(const Semver &old_slic3r_version) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << "Checking for cached configuration updates...";

	// Over all indices from the cache directory:
	for (const auto idx : index_db) {
		auto bundle_path = vendor_path / (idx.vendor() + ".ini");
		auto bundle_path_idx = vendor_path / idx.path().filename();

		if (! fs::exists(bundle_path)) {
			BOOST_LOG_TRIVIAL(info) << format("Confing bundle not installed for vendor %1%, skipping: ", idx.vendor());
			continue;
		}

		// Perform a basic load and check the version of the installed preset bundle.
		auto vp = VendorProfile::from_ini(bundle_path, false);

		// Getting a recommended version from the latest index, wich may have been downloaded
		// from the internet, or installed / updated from the installation resources.
		auto recommended = idx.recommended();
		if (recommended == idx.end()) {
			BOOST_LOG_TRIVIAL(error) << format("No recommended version for vendor: %1%, invalid index? Giving up.", idx.vendor());
			// XXX: what should be done here?
			continue;
		}

		const auto ver_current = idx.find(vp.config_version);
		const bool ver_current_found = ver_current != idx.end();

		BOOST_LOG_TRIVIAL(debug) << format("Vendor: %1%, version installed: %2%%3%, version cached: %4%",
			vp.name,
			vp.config_version.to_string(),
			(ver_current_found ? "" : " (not found in index!)"),
			recommended->config_version.to_string());

		if (! ver_current_found) {
			// Any published config shall be always found in the latest config index.
			auto message = format("Preset bundle `%1%` version not found in index: %2%", idx.vendor(), vp.config_version.to_string());
			BOOST_LOG_TRIVIAL(error) << message;
			GUI::show_error(nullptr, message);
			continue;
		}

		bool current_not_supported = false; //if slcr is incompatible but situation is not downgrade, we do forced updated and this bool is information to do it 

		if (ver_current_found && !ver_current->is_current_slic3r_supported()){
			if(ver_current->is_current_slic3r_downgrade()) {
				// "Reconfigure" situation.
				BOOST_LOG_TRIVIAL(warning) << "Current Slic3r incompatible with installed bundle: " << bundle_path.string();
				updates.incompats.emplace_back(std::move(bundle_path), *ver_current, vp.name);
				continue;
			}
		current_not_supported = true;
		}

		if (recommended->config_version < vp.config_version) {
			BOOST_LOG_TRIVIAL(warning) << format("Recommended config version for the currently running PrusaSlicer is older than the currently installed config for vendor %1%. This should not happen.", idx.vendor());
			continue;
		}

		if (recommended->config_version == vp.config_version) {
			// The recommended config bundle is already installed.
			continue;
		}

		// Config bundle update situation. The recommended config bundle version for this PrusaSlicer version from the index from the cache is newer
		// than the version of the currently installed config bundle.

		// The config index inside the cache directory (given by idx.path()) is one of the following:
		// 1) The last config index downloaded by any previously running PrusaSlicer instance
		// 2) The last config index installed by any previously running PrusaSlicer instance (older or newer) from its resources.
		// 3) The last config index installed by the currently running PrusaSlicer instance from its resources.
		// The config index is always the newest one (given by its newest config bundle referenced), and older config indices shall fully contain
		// the content of the older config indices.

		// Config bundle inside the cache directory.
		fs::path path_in_cache 		= cache_path / (idx.vendor() + ".ini");
		// Config bundle inside the resources directory.
		fs::path path_in_rsrc 		= rsrc_path  / (idx.vendor() + ".ini");
		// Config index inside the resources directory.
		fs::path path_idx_in_rsrc 	= rsrc_path  / (idx.vendor() + ".idx");

		// Search for a valid config bundle in the cache directory.
		bool 		found = false;
		Update    	new_update;
		fs::path 	bundle_path_idx_to_install;
		if (fs::exists(path_in_cache)) {
			try {
				VendorProfile new_vp = VendorProfile::from_ini(path_in_cache, false);
				if (new_vp.config_version == recommended->config_version) {
					// The config bundle from the cache directory matches the recommended version of the index from the cache directory.
					// This is the newest known recommended config. Use it.
					new_update = Update(std::move(path_in_cache), std::move(bundle_path), *recommended, vp.name, vp.changelog_url, current_not_supported);
					// and install the config index from the cache into vendor's directory.
					bundle_path_idx_to_install = idx.path();
					found = true;
				}
			} catch (const std::exception &ex) {
				BOOST_LOG_TRIVIAL(info) << format("Failed to load the config bundle `%1%`: %2%", path_in_cache.string(), ex.what());
			}
		}

		// Keep the rsrc_idx outside of the next block, as we will reference the "recommended" version by an iterator.
		Index rsrc_idx;
		if (! found && fs::exists(path_in_rsrc) && fs::exists(path_idx_in_rsrc)) {
			// Trying the config bundle from resources (from the installation).
			// In that case, the recommended version number has to be compared against the recommended version reported by the config index from resources as well, 
			// as the config index in the cache directory may already be newer, recommending a newer config bundle than available in cache or resources.
			VendorProfile rsrc_vp;
			try {
				rsrc_vp = VendorProfile::from_ini(path_in_rsrc, false);
			} catch (const std::exception &ex) {
				BOOST_LOG_TRIVIAL(info) << format("Cannot load the config bundle at `%1%`: %2%", path_in_rsrc.string(), ex.what());
			}
			if (rsrc_vp.valid()) {
				try {
					rsrc_idx.load(path_idx_in_rsrc);
				} catch (const std::exception &ex) {
					BOOST_LOG_TRIVIAL(info) << format("Cannot load the config index at `%1%`: %2%", path_idx_in_rsrc.string(), ex.what());
				}
				recommended = rsrc_idx.recommended();
				if (recommended != rsrc_idx.end() && recommended->config_version == rsrc_vp.config_version && recommended->config_version > vp.config_version) {
					new_update = Update(std::move(path_in_rsrc), std::move(bundle_path), *recommended, vp.name, vp.changelog_url, current_not_supported);
					bundle_path_idx_to_install = path_idx_in_rsrc;
					found = true;
				} else {
					BOOST_LOG_TRIVIAL(warning) << format("The recommended config version for vendor `%1%` in resources does not match the recommended\n"
			                                             " config version for this version of PrusaSlicer. Corrupted installation?", idx.vendor());
				}
			}
		}

		if (found) {
			// Load 'installed' idx, if any.
			// 'Installed' indices are kept alongside the bundle in the `vendor` subdir
			// for bookkeeping to remember a cancelled update and not offer it again.
			if (fs::exists(bundle_path_idx)) {
				Index existing_idx;
				try {
					existing_idx.load(bundle_path_idx);
					// Find a recommended config bundle version for the slic3r version last executed. This makes sure that a config bundle update will not be missed
					// when upgrading an application. On the other side, the user will be bugged every time he will switch between slic3r versions.
                    /*const auto existing_recommended = existing_idx.recommended(old_slic3r_version);
                    if (existing_recommended != existing_idx.end() && recommended->config_version == existing_recommended->config_version) {
						// The user has already seen (and presumably rejected) this update
						BOOST_LOG_TRIVIAL(info) << format("Downloaded index for `%1%` is the same as installed one, not offering an update.",idx.vendor());
						continue;
					}*/
				} catch (const std::exception &err) {
					BOOST_LOG_TRIVIAL(error) << format("Cannot load the installed index at `%1%`: %2%", bundle_path_idx, err.what());
				}
			}

			// Check if the update is already present in a snapshot
			if(!current_not_supported)
			{
				const auto recommended_snap = SnapshotDB::singleton().snapshot_with_vendor_preset(vp.name, recommended->config_version);
				if (recommended_snap != SnapshotDB::singleton().end()) {
					BOOST_LOG_TRIVIAL(info) << format("Bundle update %1% %2% already found in snapshot %3%, skipping...",
						vp.name,
						recommended->config_version.to_string(),
						recommended_snap->id);
					continue;
				}
			}

			updates.updates.emplace_back(std::move(new_update));
			// 'Install' the index in the vendor directory. This is used to memoize
			// offered updates and to not offer the same update again if it was cancelled by the user.
			copy_file_fix(bundle_path_idx_to_install, bundle_path_idx);
		} else {
			BOOST_LOG_TRIVIAL(warning) << format("Index for vendor %1% indicates update (%2%) but the new bundle was found neither in cache nor resources",
				idx.vendor(),
				recommended->config_version.to_string());
		}
	}

	return updates;
}

void PresetUpdater::priv::perform_updates(Updates &&updates, bool snapshot) const
{
	if (updates.incompats.size() > 0) {
		if (snapshot) {
			BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
			SnapshotDB::singleton().take_snapshot(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_DOWNGRADE);
		}
		
		BOOST_LOG_TRIVIAL(info) << format("Deleting %1% incompatible bundles", updates.incompats.size());

		for (auto &incompat : updates.incompats) {
			BOOST_LOG_TRIVIAL(info) << '\t' << incompat;
			incompat.remove();
		}

		
	} else if (updates.updates.size() > 0) {
		
		if (snapshot) {
			BOOST_LOG_TRIVIAL(info) << "Taking a snapshot...";
			SnapshotDB::singleton().take_snapshot(*GUI::wxGetApp().app_config, Snapshot::SNAPSHOT_UPGRADE);
		}

		BOOST_LOG_TRIVIAL(info) << format("Performing %1% updates", updates.updates.size());

		for (const auto &update : updates.updates) {
			BOOST_LOG_TRIVIAL(info) << '\t' << update;

			update.install();

			PresetBundle bundle;
			bundle.load_configbundle(update.source.string(), PresetBundle::LOAD_CFGBNDLE_SYSTEM);

			BOOST_LOG_TRIVIAL(info) << format("Deleting %1% conflicting presets", bundle.prints.size() + bundle.filaments.size() + bundle.printers.size());

			auto preset_remover = [](const Preset &preset) {
				BOOST_LOG_TRIVIAL(info) << '\t' << preset.file;
				fs::remove(preset.file);
			};

			for (const auto &preset : bundle.prints)    { preset_remover(preset); }
			for (const auto &preset : bundle.filaments) { preset_remover(preset); }
			for (const auto &preset : bundle.printers)  { preset_remover(preset); }

			// Also apply the `obsolete_presets` property, removing obsolete ini files

			BOOST_LOG_TRIVIAL(info) << format("Deleting %1% obsolete presets",
				bundle.obsolete_presets.prints.size() + bundle.obsolete_presets.filaments.size() + bundle.obsolete_presets.printers.size());

			auto obsolete_remover = [](const char *subdir, const std::string &preset) {
				auto path = fs::path(Slic3r::data_dir()) / subdir / preset;
				path += ".ini";
				BOOST_LOG_TRIVIAL(info) << '\t' << path.string();
				fs::remove(path);
			};

			for (const auto &name : bundle.obsolete_presets.prints)    { obsolete_remover("print", name); }
			for (const auto &name : bundle.obsolete_presets.filaments) { obsolete_remover("filament", name); }
			for (const auto &name : bundle.obsolete_presets.sla_prints) { obsolete_remover("sla_print", name); } 
			for (const auto &name : bundle.obsolete_presets.sla_materials/*filaments*/) { obsolete_remover("sla_material", name); } 
			for (const auto &name : bundle.obsolete_presets.printers)  { obsolete_remover("printer", name); }
		}
	}
}

void PresetUpdater::priv::set_waiting_updates(Updates u)
{
	waiting_updates = u;
	has_waiting_updates = true;
}

PresetUpdater::PresetUpdater() :
	p(new priv())
{}


// Public

PresetUpdater::~PresetUpdater()
{
	if (p && p->thread.joinable()) {
		// This will stop transfers being done by the thread, if any.
		// Cancelling takes some time, but should complete soon enough.
		p->cancel = true;
		p->thread.join();
	}
}

void PresetUpdater::sync(PresetBundle *preset_bundle)
{
	p->set_download_prefs(GUI::wxGetApp().app_config);
	if (!p->enabled_version_check && !p->enabled_config_update) { return; }

	// Copy the whole vendors data for use in the background thread
	// Unfortunatelly as of C++11, it needs to be copied again
	// into the closure (but perhaps the compiler can elide this).
	VendorMap vendors = preset_bundle->vendors;

	p->thread = std::move(std::thread([this, vendors]() {
		this->p->prune_tmps();
		this->p->sync_version();
		this->p->sync_config(std::move(vendors));
	}));
}

void PresetUpdater::slic3r_update_notify()
{
	if (! p->enabled_version_check) { return; }

	auto* app_config = GUI::wxGetApp().app_config;
	const auto ver_online_str = app_config->get("version_online");
	const auto ver_online = Semver::parse(ver_online_str);
	const auto ver_online_seen = Semver::parse(app_config->get("version_online_seen"));

	if (ver_online) {
		// Only display the notification if the version available online is newer AND if we haven't seen it before
		if (*ver_online > Slic3r::SEMVER && (! ver_online_seen || *ver_online_seen < *ver_online)) {
			GUI::MsgUpdateSlic3r notification(Slic3r::SEMVER, *ver_online);
			notification.ShowModal();
			if (notification.disable_version_check()) {
				app_config->set("version_check", "0");
				p->enabled_version_check = false;
			}
		}

		app_config->set("version_online_seen", ver_online_str);
	}
}

PresetUpdater::UpdateResult PresetUpdater::config_update(const Semver& old_slic3r_version, bool no_notification) const
{
 	if (! p->enabled_config_update) { return R_NOOP; }

	auto updates = p->get_config_updates(old_slic3r_version);
	if (updates.incompats.size() > 0) {
		BOOST_LOG_TRIVIAL(info) << format("%1% bundles incompatible. Asking for action...", updates.incompats.size());

		std::unordered_map<std::string, wxString> incompats_map;
		for (const auto &incompat : updates.incompats) {
			const auto min_slic3r = incompat.version.min_slic3r_version;
			const auto max_slic3r = incompat.version.max_slic3r_version;
			wxString restrictions;
			if (min_slic3r != Semver::zero() && max_slic3r != Semver::inf()) {
                restrictions = GUI::format_wxstr(_L("requires min. %s and max. %s"),
                    min_slic3r.to_string(),
                    max_slic3r.to_string());
			} else if (min_slic3r != Semver::zero()) {
				restrictions = GUI::format_wxstr(_L("requires min. %s"), min_slic3r.to_string());
				BOOST_LOG_TRIVIAL(debug) << "Bundle is not downgrade, user will now have to do whole wizard. This should not happen.";
			} else {
                restrictions = GUI::format_wxstr(_L("requires max. %s"), max_slic3r.to_string());
			}

			incompats_map.emplace(std::make_pair(incompat.vendor, std::move(restrictions)));
		}

		GUI::MsgDataIncompatible dlg(std::move(incompats_map));
		const auto res = dlg.ShowModal();
		if (res == wxID_REPLACE) {
			BOOST_LOG_TRIVIAL(info) << "User wants to re-configure...";

			// This effectively removes the incompatible bundles:
			// (snapshot is taken beforehand)
			p->perform_updates(std::move(updates));

			if (!GUI::wxGetApp().run_wizard(GUI::ConfigWizard::RR_DATA_INCOMPAT)) {
				return R_INCOMPAT_EXIT;
			}

			return R_INCOMPAT_CONFIGURED;
		}
		else {
			BOOST_LOG_TRIVIAL(info) << "User wants to exit Slic3r, bye...";
			return R_INCOMPAT_EXIT;
		}

	} else if (updates.updates.size() > 0) {

		bool incompatible_version = false;
		for (const auto& update : updates.updates) {
			incompatible_version = (update.forced_update ? true : incompatible_version);
			//td::cout << update.forced_update << std::endl;
			//BOOST_LOG_TRIVIAL(info) << format("Update requires higher version.");
		}

		//forced update
		if(incompatible_version)
		{
			BOOST_LOG_TRIVIAL(info) << format("Update of %1% bundles available. At least one requires higher version of Slicer.", updates.updates.size());

			std::vector<GUI::MsgUpdateForced::Update> updates_msg;
			for (const auto& update : updates.updates) {
				std::string changelog_url = update.version.config_version.prerelease() == nullptr ? update.changelog_url : std::string();
				updates_msg.emplace_back(update.vendor, update.version.config_version, update.version.comment, std::move(changelog_url));
			}

			GUI::MsgUpdateForced dlg(updates_msg);

			const auto res = dlg.ShowModal();
			if (res == wxID_OK) {
				BOOST_LOG_TRIVIAL(info) << "User wants to update...";

				p->perform_updates(std::move(updates));

				// Reload global configuration
				auto* app_config = GUI::wxGetApp().app_config;
				GUI::wxGetApp().preset_bundle->load_presets(*app_config);
				GUI::wxGetApp().load_current_presets();
				GUI::wxGetApp().plater()->set_bed_shape();
				return R_UPDATE_INSTALLED;
			}
			else {
				BOOST_LOG_TRIVIAL(info) << "User wants to exit Slic3r, bye...";
				return R_INCOMPAT_EXIT;
			}
		}

		// regular update
		if (no_notification) {
			BOOST_LOG_TRIVIAL(info) << format("Update of %1% bundles available. Asking for confirmation ...", p->waiting_updates.updates.size());

			std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
			for (const auto& update : updates.updates) {
				std::string changelog_url = update.version.config_version.prerelease() == nullptr ? update.changelog_url : std::string();
				updates_msg.emplace_back(update.vendor, update.version.config_version, update.version.comment, std::move(changelog_url));
			}

			GUI::MsgUpdateConfig dlg(updates_msg);

			const auto res = dlg.ShowModal();
			if (res == wxID_OK) {
				BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
				p->perform_updates(std::move(updates));

				// Reload global configuration
				auto* app_config = GUI::wxGetApp().app_config;
				GUI::wxGetApp().preset_bundle->load_presets(*app_config);
				GUI::wxGetApp().load_current_presets();
				return R_UPDATE_INSTALLED;
			}
			else {
				BOOST_LOG_TRIVIAL(info) << "User refused the update";
				return R_UPDATE_REJECT;
			}
		} else {
			p->set_waiting_updates(updates);
			GUI::wxGetApp().plater()->get_notification_manager()->push_notification(GUI::NotificationType::PresetUpdateAviable, *(GUI::wxGetApp().plater()->get_current_canvas3D()));
		}
		
		// MsgUpdateConfig will show after the notificaation is clicked
	} else {
		BOOST_LOG_TRIVIAL(info) << "No configuration updates available.";
	}

	return R_NOOP;
}

void PresetUpdater::install_bundles_rsrc(std::vector<std::string> bundles, bool snapshot) const
{
	Updates updates;

	BOOST_LOG_TRIVIAL(info) << format("Installing %1% bundles from resources ...", bundles.size());

	for (const auto &bundle : bundles) {
		auto path_in_rsrc = (p->rsrc_path / bundle).replace_extension(".ini");
		auto path_in_vendors = (p->vendor_path / bundle).replace_extension(".ini");
		updates.updates.emplace_back(std::move(path_in_rsrc), std::move(path_in_vendors), Version(), "", "");
	}

	p->perform_updates(std::move(updates), snapshot);
}

void PresetUpdater::on_update_notification_confirm()
{
	if (!p->has_waiting_updates)
		return;
	BOOST_LOG_TRIVIAL(info) << format("Update of %1% bundles available. Asking for confirmation ...", p->waiting_updates.updates.size());

	std::vector<GUI::MsgUpdateConfig::Update> updates_msg;
	for (const auto& update : p->waiting_updates.updates) {
		std::string changelog_url = update.version.config_version.prerelease() == nullptr ? update.changelog_url : std::string();
		updates_msg.emplace_back(update.vendor, update.version.config_version, update.version.comment, std::move(changelog_url));
	}

	GUI::MsgUpdateConfig dlg(updates_msg);

	const auto res = dlg.ShowModal();
	if (res == wxID_OK) {
		BOOST_LOG_TRIVIAL(debug) << "User agreed to perform the update";
		p->perform_updates(std::move(p->waiting_updates));

		// Reload global configuration
		auto* app_config = GUI::wxGetApp().app_config;
		GUI::wxGetApp().preset_bundle->load_presets(*app_config);
		GUI::wxGetApp().load_current_presets();
		p->has_waiting_updates = false;
		//return R_UPDATE_INSTALLED;
	}
	else {
		BOOST_LOG_TRIVIAL(info) << "User refused the update";
		//return R_UPDATE_REJECT;
	}
	
}

}
