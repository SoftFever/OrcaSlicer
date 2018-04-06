#ifndef slic3r_GUI_Snapshot_
#define slic3r_GUI_Snapshot_

#include <string>
#include <vector>

#include <boost/filesystem.hpp>

#include "../Utils/Semver.hpp"

namespace Slic3r { 

class AppConfig;

namespace GUI {
namespace Config {

// A snapshot contains:
// 		Slic3r.ini
// 		vendor/
// 		print/
// 		filament/
//		printer/
class Snapshot
{
public:
	enum Reason {
		SNAPSHOT_UNKNOWN,
		SNAPSHOT_UPGRADE,
		SNAPSHOT_DOWNGRADE,
		SNAPSHOT_USER,
	};

	Snapshot() { clear(); }

	void 		clear();
	void 		load_ini(const std::string &path);
	void 		save_ini(const std::string &path);

	// Export the print / filament / printer selections to be activated into the AppConfig.
	void 		export_selections(AppConfig &config) const;

	// ID of a snapshot should equal to the name of the snapshot directory.
	// The ID contains the date/time, reason and comment to be human readable.
	std::string					id;
	std::time_t					time_captured;
	// Which Slic3r version captured this snapshot?
	Semver		 				slic3r_version_captured = Semver::invalid();
	// Comment entered by the user at the start of the snapshot capture.
	std::string 				comment;
	Reason						reason;

	// Active presets at the time of the snapshot.
	std::string 				print;
	std::vector<std::string>	filaments;
	std::string					printer;

	// Annotation of the vendor configuration stored in the snapshot.
	// This information is displayed to the user and used to decide compatibility
	// of the configuration stored in the snapshot with the running Slic3r version.
	struct VendorConfig {
		// Name of the vendor contained in this snapshot.
		std::string name;
		// Version of the vendor config contained in this snapshot.
		Semver 		version 		   = Semver::invalid();
		// Minimum Slic3r version compatible with this vendor configuration.
		Semver		min_slic3r_version = Semver::zero();
		// Maximum Slic3r version compatible with this vendor configuration, or empty.
		Semver 		max_slic3r_version = Semver::inf();
	};
	// List of vendor configs contained in this snapshot.
	std::vector<VendorConfig> 	vendor_configs;
};

class SnapshotDB
{
public:
	typedef std::vector<Snapshot>::const_iterator const_iterator;

	// Load the snapshot database from the snapshots directory.
	// If the snapshot directory or its parent does not exist yet, it will be created.
	// Returns a number of snapshots loaded.
	size_t 							load_db();

	// Create a snapshot directory, copy the vendor config bundles, user print/filament/printer profiles,
	// create an index.
	const Snapshot&					make_snapshot(const AppConfig &app_config, Snapshot::Reason reason, const std::string &comment);
	void 							restore_snapshot(const std::string &id, AppConfig &app_config);
	void 							restore_snapshot(const Snapshot &snapshot, AppConfig &app_config);

	const_iterator					begin()     const { return m_snapshots.begin(); }
	const_iterator					end()       const { return m_snapshots.end(); }
	const std::vector<Snapshot>& 	snapshots() const { return m_snapshots; }

private:
	// Create the snapshots directory if it does not exist yet.
	static boost::filesystem::path	create_db_dir();

	std::vector<Snapshot>			m_snapshots;
};

} // namespace Config
} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_GUI_Snapshot_ */
