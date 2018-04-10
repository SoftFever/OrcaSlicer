#include "Snapshot.hpp"
#include "../GUI/AppConfig.hpp"
#include "../Utils/Time.hpp"

#include <time.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Config.hpp"
#include "../../libslic3r/FileParserError.hpp"
#include "../../libslic3r/Utils.hpp"

#define SLIC3R_SNAPSHOTS_DIR "snapshots"
#define SLIC3R_SNAPSHOT_FILE "snapshot.ini"

namespace Slic3r { 
namespace GUI {
namespace Config {

void Snapshot::clear()
{
	this->id.clear();
	this->time_captured = 0;
	this->slic3r_version_captured = Semver::invalid();
	this->comment.clear();
	this->reason = SNAPSHOT_UNKNOWN;
	this->print.clear();
	this->filaments.clear();
	this->printer.clear();
}

void Snapshot::load_ini(const std::string &path)
{
	this->clear();

	auto throw_on_parse_error = [&path](const std::string &msg) {
		throw file_parser_error(std::string("Failed loading the snapshot file. Reason: ") + msg, path);
	};

	// Load the snapshot.ini file.
    boost::property_tree::ptree tree;
    try {
        boost::nowide::ifstream ifs(path);
        boost::property_tree::read_ini(ifs, tree);
    } catch (const std::ifstream::failure &err) {
        throw file_parser_error(std::string("The snapshot file cannot be loaded. Reason: ") + err.what(), path);
    } catch (const std::runtime_error &err) {
        throw_on_parse_error(err.what());
    }

    // Parse snapshot.ini
    std::string group_name_vendor = "Vendor:";
	std::string key_filament = "filament";
    for (auto &section : tree) {
    	if (section.first == "snapshot") {
    		// Parse the common section.
            for (auto &kvp : section.second) {
                if (kvp.first == "id")
                    this->id = kvp.second.data();
                else if (kvp.first == "time_captured") {
                	this->time_captured = Slic3r::Utils::parse_time_ISO8601Z(kvp.second.data());
					if (this->time_captured == (time_t)-1)
				        throw_on_parse_error("invalid timestamp");
                } else if (kvp.first == "slic3r_version_captured") {
                	auto semver = Semver::parse(kvp.second.data());
                	if (! semver)
                		throw_on_parse_error("invalid slic3r_version_captured semver");
                	this->slic3r_version_captured = *semver;
                } else if (kvp.first == "comment") {
                	this->comment = kvp.second.data();
                } else if (kvp.first == "reason") {
                	std::string rsn = kvp.second.data();
                	if (rsn == "upgrade")
                		this->reason = SNAPSHOT_UPGRADE;
                	else if (rsn == "downgrade")
                		this->reason = SNAPSHOT_DOWNGRADE;
                	else if (rsn == "user")
                		this->reason = SNAPSHOT_USER;
                	else
                		this->reason = SNAPSHOT_UNKNOWN;
                }
            }
    	} else if (section.first == "presets") {
            // Load the names of the active presets.
            for (auto &kvp : section.second) {
                if (kvp.first == "print") {
                    this->print = kvp.second.data();
                } else if (boost::starts_with(kvp.first, "filament")) {
                    int idx = 0;
                    if (kvp.first == "filament" || sscanf(kvp.first.c_str(), "filament_%d", &idx) == 1) {
                        if (int(this->filaments.size()) <= idx)
                            this->filaments.resize(idx + 1, std::string());
                        this->filaments[idx] = kvp.second.data();
                    }
                } else if (kvp.first == "printer") {
                    this->printer = kvp.second.data();
                }
            }
    	} else if (boost::starts_with(section.first, group_name_vendor) && section.first.size() > group_name_vendor.size()) {
    		// Vendor specific section.
            VendorConfig vc;
            vc.name = section.first.substr(group_name_vendor.size());
            for (auto &kvp : section.second) {
            	if (boost::starts_with(kvp.first, "model_")) {
            		//model:MK2S = 0.4;xxx
					//model:MK3 = 0.4;xxx
            	} else if (kvp.first == "version" || kvp.first == "min_slic3r_version" || kvp.first == "max_slic3r_version") {
            		// Version of the vendor specific config bundle bundled with this snapshot.            		
                	auto semver = Semver::parse(kvp.second.data());
                	if (! semver)
                		throw_on_parse_error("invalid " + kvp.first + " format for " + section.first);
					if (kvp.first == "version")
                		vc.version = *semver;
                	else if (kvp.first == "min_slic3r_version")
                		vc.min_slic3r_version = *semver;
                	else
                		vc.max_slic3r_version = *semver;
            	}
			}
        }
    }
}

void Snapshot::save_ini(const std::string &path)
{
	boost::nowide::ofstream c;
    c.open(path, std::ios::out | std::ios::trunc);
    c << "# " << Slic3r::header_slic3r_generated() << std::endl;

    // Export the common "snapshot".
	c << std::endl << "[snapshot]" << std::endl;
	c << "id = " << this->id << std::endl;
	c << "time_captured = " << Slic3r::Utils::format_time_ISO8601Z(this->time_captured) << std::endl;
	c << "slic3r_version_captured = " << this->slic3r_version_captured.to_string() << std::endl;
	c << "comment = " << this->comment << std::endl;
	c << "reason = " << this->reason << std::endl;

    // Export the active presets at the time of the snapshot.
	c << std::endl << "[presets]" << std::endl;
	c << "print = " << this->print << std::endl;
	c << "filament = " << this->filaments.front() << std::endl;
	for (size_t i = 1; i < this->filaments.size(); ++ i)
		c << "filament_" << std::to_string(i) << " = " << this->filaments[i] << std::endl;
	c << "printer = " << this->printer << std::endl;

    // Export the vendor configs.
    for (const VendorConfig &vc : this->vendor_configs) {
		c << std::endl << "[Vendor:" << vc.name << "]" << std::endl;
		c << "version = " << vc.version.to_string() << std::endl;
		c << "min_slic3r_version = " << vc.min_slic3r_version.to_string() << std::endl;
		c << "max_slic3r_version = " << vc.max_slic3r_version.to_string() << std::endl;
    }
    c.close();
}

void Snapshot::export_selections(AppConfig &config) const
{
    assert(filaments.size() >= 1);
    config.clear_section("presets");
    config.set("presets", "print",    print);
    config.set("presets", "filament", filaments.front());
	for (int i = 1; i < filaments.size(); ++i) {
        char name[64];
        sprintf(name, "filament_%d", i);
        config.set("presets", name, filaments[i]);
    }
    config.set("presets", "printer",  printer);
}

size_t SnapshotDB::load_db()
{
	boost::filesystem::path snapshots_dir = SnapshotDB::create_db_dir();

	m_snapshots.clear();

    // Walk over the snapshot directories and load their index.
    std::string errors_cummulative;
	for (auto &dir_entry : boost::filesystem::directory_iterator(snapshots_dir))
        if (boost::filesystem::is_directory(dir_entry.status())) {
        	// Try to read "snapshot.ini".
            boost::filesystem::path path_ini = dir_entry.path() / SLIC3R_SNAPSHOT_FILE;
            Snapshot 			    snapshot;
            try {
            	snapshot.load_ini(path_ini.string());
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
                continue;
			}
            // Check that the name of the snapshot directory matches the snapshot id stored in the snapshot.ini file.
            if (dir_entry.path().filename().string() != snapshot.id) {
            	errors_cummulative += std::string("Snapshot ID ") + snapshot.id + " does not match the snapshot directory " + dir_entry.path().filename().string() + "\n";
            	continue;
            }
            m_snapshots.emplace_back(std::move(snapshot));
        }

    if (! errors_cummulative.empty())
        throw std::runtime_error(errors_cummulative);
    return m_snapshots.size();
}

static void copy_config_dir_single_level(const boost::filesystem::path &path_src, const boost::filesystem::path &path_dst)
{
    if (! boost::filesystem::is_directory(path_dst) && 
        ! boost::filesystem::create_directory(path_dst))
        throw std::runtime_error(std::string("Slic3r was unable to create a directory at ") + path_dst.string());

	for (auto &dir_entry : boost::filesystem::directory_iterator(path_src))
        if (boost::filesystem::is_regular_file(dir_entry.status()) && boost::algorithm::iends_with(dir_entry.path().filename().string(), ".ini"))
		    boost::filesystem::copy_file(dir_entry.path(), path_dst / dir_entry.path().filename(), boost::filesystem::copy_option::overwrite_if_exists);
}

static void delete_existing_ini_files(const boost::filesystem::path &path)
{
    if (! boost::filesystem::is_directory(path))
    	return;
	for (auto &dir_entry : boost::filesystem::directory_iterator(path))
        if (boost::filesystem::is_regular_file(dir_entry.status()) && boost::algorithm::iends_with(dir_entry.path().filename().string(), ".ini"))
		    boost::filesystem::remove(dir_entry.path());
}

const Snapshot&	SnapshotDB::make_snapshot(const AppConfig &app_config, Snapshot::Reason reason, const std::string &comment)
{
	boost::filesystem::path data_dir        = boost::filesystem::path(Slic3r::data_dir());
	boost::filesystem::path snapshot_db_dir = SnapshotDB::create_db_dir();

	// 1) Prepare the snapshot structure.
	Snapshot snapshot;
	// Snapshot header.
	snapshot.time_captured 			 = Slic3r::Utils::get_current_time_utc();
	snapshot.id 					 = Slic3r::Utils::format_time_ISO8601Z(snapshot.time_captured);
	snapshot.slic3r_version_captured = *Semver::parse(SLIC3R_VERSION);
	snapshot.comment 				 = comment;
	snapshot.reason 				 = reason;
	// Active presets at the time of the snapshot.
    snapshot.print   = app_config.get("presets", "print");
    snapshot.filaments.emplace_back(app_config.get("presets", "filament"));
    snapshot.printer = app_config.get("presets", "printer");
    for (unsigned int i = 1; i < 1000; ++ i) {
        char name[64];
        sprintf(name, "filament_%d", i);
        if (! app_config.has("presets", name))
            break;
	    snapshot.filaments.emplace_back(app_config.get("presets", name));
    }
    // Vendor specific config bundles and installed printers.

    // Backup the presets.
    boost::filesystem::path snapshot_dir = snapshot_db_dir / snapshot.id;
    for (const char *subdir : { "print", "filament", "printer", "vendor" })
    	copy_config_dir_single_level(data_dir / subdir, snapshot_dir / subdir);
	snapshot.save_ini((snapshot_dir / "snapshot.ini").string());
    m_snapshots.emplace_back(std::move(snapshot));
    return m_snapshots.back();
}

void SnapshotDB::restore_snapshot(const std::string &id, AppConfig &app_config)
{
	for (const Snapshot &snapshot : m_snapshots)
		if (snapshot.id == id) {
			this->restore_snapshot(snapshot, app_config);
			return;
		}
	throw std::runtime_error(std::string("Snapshot with id " + id + " was not found."));
}

void SnapshotDB::restore_snapshot(const Snapshot &snapshot, AppConfig &app_config)
{
	boost::filesystem::path data_dir        = boost::filesystem::path(Slic3r::data_dir());
	boost::filesystem::path snapshot_db_dir = SnapshotDB::create_db_dir();
    boost::filesystem::path snapshot_dir 	= snapshot_db_dir / snapshot.id;

    // Remove existing ini files and restore the ini files from the snapshot.
    for (const char *subdir : { "print", "filament", "printer", "vendor" }) {
		delete_existing_ini_files(data_dir / subdir);
    	copy_config_dir_single_level(snapshot_dir / subdir, data_dir / subdir);
    }

    // Update app_config from the snapshot.
    snapshot.export_selections(app_config);

    // Store information about the snapshot.

}

boost::filesystem::path SnapshotDB::create_db_dir()
{
    boost::filesystem::path data_dir 	  = boost::filesystem::path(Slic3r::data_dir());
    boost::filesystem::path snapshots_dir = data_dir / SLIC3R_SNAPSHOTS_DIR;
    for (const boost::filesystem::path &path : { data_dir, snapshots_dir }) {
		boost::filesystem::path subdir = path;
        subdir.make_preferred();
        if (! boost::filesystem::is_directory(subdir) && 
            ! boost::filesystem::create_directory(subdir))
            throw std::runtime_error(std::string("Slic3r was unable to create a directory at ") + subdir.string());
    }
    return snapshots_dir;
}

} // namespace Config
} // namespace GUI
} // namespace Slic3r
