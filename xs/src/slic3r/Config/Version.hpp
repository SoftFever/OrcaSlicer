#ifndef slic3r_GUI_ConfigIndex_
#define slic3r_GUI_ConfigIndex_

#include <string>
#include <vector>

#include "../../libslic3r/FileParserError.hpp"
#include "../Utils/Semver.hpp"

namespace Slic3r { 
namespace GUI {
namespace Config {

// Configuration bundle version.
struct Version
{
	// Version of this config.
	Semver 		config_version     = Semver::invalid();
	// Minimum Slic3r version, for which this config is applicable.
	Semver 		min_slic3r_version = Semver::zero();
	// Maximum Slic3r version, for which this config is recommended.
	// Slic3r should read older configuration and upgrade to a newer format,
	// but likely there has been a better configuration published, using the new features.
	Semver 		max_slic3r_version = Semver::inf();
	// Single comment line.
	std::string comment;

	bool 		is_slic3r_supported(const Semver &slicer_version) const { return slicer_version.in_range(min_slic3r_version, max_slic3r_version); }
	bool 		is_current_slic3r_supported() const;
};

// Index of vendor specific config bundle versions and Slic3r compatibilities.
// The index is being downloaded from the internet, also an initial version of the index 
// is contained in the Slic3r installation.
// 
// The index has a simple format:
//
// min_sic3r_version = 
// max_slic3r_version = 
// config_version "comment"
// config_version "comment"
// ...
// min_slic3r_version = 
// max_slic3r_version = 
// config_version comment
// config_version comment
// ...
//
// The min_slic3r_version, max_slic3r_version keys are applied to the config versions below,
// empty slic3r version means an open interval.
class Index
{
public:
	typedef std::vector<Version>::const_iterator const_iterator;
	// Read a config index file in the simple format described in the Index class comment.
	// Throws Slic3r::file_parser_error and the standard std file access exceptions.
	size_t						load(const std::string &path);

	const_iterator				begin()   const { return m_configs.begin(); }
	const_iterator				end()     const { return m_configs.end(); }
	const std::vector<Version>& configs() const { return m_configs; }
	// Finds a recommended config to be installed for the current Slic3r version.
	// Returns configs().end() if such version does not exist in the index. This shall never happen
	// if the index is valid.
	const_iterator				recommended() const;

private:
	std::vector<Version>		m_configs;
};

} // namespace Config
} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_GUI_ConfigIndex_ */
