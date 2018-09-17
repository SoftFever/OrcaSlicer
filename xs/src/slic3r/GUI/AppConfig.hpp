#ifndef slic3r_AppConfig_hpp_
#define slic3r_AppConfig_hpp_

#include <set>
#include <map>
#include <string>

#include "libslic3r/Config.hpp"
#include "slic3r/Utils/Semver.hpp"

namespace Slic3r {

class AppConfig
{
public:
	AppConfig() :
		m_dirty(false),
		m_orig_version(Semver::invalid()),
		m_legacy_datadir(false)
	{
		this->reset();
	}

	// Clear and reset to defaults.
	void 			   	reset();
	// Override missing or keys with their defaults.
	void 			   	set_defaults();

	// Load the slic3r.ini from a user profile directory (or a datadir, if configured).
	void 			   	load();
	// Store the slic3r.ini into a user profile directory (or a datadir, if configured).
	void 			   	save();

	// Does this config need to be saved?
	bool 				dirty() const { return m_dirty; }

	// Const accessor, it will return false if a section or a key does not exist.
	bool get(const std::string &section, const std::string &key, std::string &value) const
	{
		value.clear();
		auto it = m_storage.find(section);
		if (it == m_storage.end())
			return false;
		auto it2 = it->second.find(key);
		if (it2 == it->second.end()) 
			return false;
		value = it2->second;
		return true;
	}
	std::string 		get(const std::string &section, const std::string &key) const
		{ std::string value; this->get(section, key, value); return value; }
	std::string 		get(const std::string &key) const
		{ std::string value; this->get("", key, value); return value; }
	void			    set(const std::string &section, const std::string &key, const std::string &value)
	{ 
		std::string &old = m_storage[section][key];
		if (old != value) {
			old = value;
			m_dirty = true;
		}
	}
	void			    set(const std::string &key, const std::string &value)
		{ this->set("", key, value);  }
	bool				has(const std::string &section, const std::string &key) const
	{
		auto it = m_storage.find(section);
		if (it == m_storage.end())
			return false;
		auto it2 = it->second.find(key);
		return it2 != it->second.end() && ! it2->second.empty();
	}
	bool				has(const std::string &key) const
		{ return this->has("", key); }

	void				erase(const std::string &section, const std::string &key)
	{
		auto it = m_storage.find(section);
		if (it != m_storage.end()) {
			it->second.erase(key);
		}
	}

	void 				clear_section(const std::string &section)
		{ m_storage[section].clear(); }

	typedef std::map<std::string, std::map<std::string, std::set<std::string>>> VendorMap;
	bool                get_variant(const std::string &vendor, const std::string &model, const std::string &variant) const;
	void                set_variant(const std::string &vendor, const std::string &model, const std::string &variant, bool enable);
	void                set_vendors(const AppConfig &from);
	void 				set_vendors(const VendorMap &vendors) { m_vendors = vendors; m_dirty = true; }
	void 				set_vendors(VendorMap &&vendors) { m_vendors = std::move(vendors); m_dirty = true; }
	const VendorMap&    vendors() const { return m_vendors; }

	// return recent/skein_directory or recent/config_directory or empty string.
	std::string 		get_last_dir() const;
	void 				update_config_dir(const std::string &dir);
	void 				update_skein_dir(const std::string &dir);

	std::string 		get_last_output_dir(const std::string &alt) const;
	void                update_last_output_dir(const std::string &dir);

	// reset the current print / filament / printer selections, so that 
	// the  PresetBundle::load_selections(const AppConfig &config) call will select
	// the first non-default preset when called.
    void                reset_selections();

	// Get the default config path from Slic3r::data_dir().
	static std::string  config_path();

	// Returns true if the user's data directory comes from before Slic3r 1.40.0 (no updating)
	bool legacy_datadir() const { return m_legacy_datadir; }
	void set_legacy_datadir(bool value) { m_legacy_datadir = value; }

	// Get the Slic3r version check url.
	// This returns a hardcoded string unless it is overriden by "version_check_url" in the ini file.
	std::string version_check_url() const;

	// Returns the original Slic3r version found in the ini file before it was overwritten
	// by the current version
	Semver orig_version() const { return m_orig_version; }

	// Does the config file exist?
	static bool 		exists();

private:
	// Map of section, name -> value
	std::map<std::string, std::map<std::string, std::string>> 	m_storage;
	// Map of enabled vendors / models / variants
	VendorMap                                                   m_vendors;
	// Has any value been modified since the config.ini has been last saved or loaded?
	bool														m_dirty;
	// Original version found in the ini file before it was overwritten
	Semver                                                      m_orig_version;
	// Whether the existing version is before system profiles & configuration updating
	bool                                                        m_legacy_datadir;
};

}; // namespace Slic3r

#endif /* slic3r_AppConfig_hpp_ */
