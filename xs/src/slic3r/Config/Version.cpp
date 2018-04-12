#include "Version.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/nowide/fstream.hpp>

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Config.hpp"
#include "../../libslic3r/FileParserError.hpp"
#include "../../libslic3r/Utils.hpp"

namespace Slic3r { 
namespace GUI {
namespace Config {

static boost::optional<Semver> s_current_slic3r_semver = Semver::parse(SLIC3R_VERSION);

bool Version::is_current_slic3r_supported() const
{
	return this->is_slic3r_supported(*s_current_slic3r_semver);
}

inline char* left_trim(char *c)
{
	for (; *c == ' ' || *c == '\t'; ++ c);
	return c;
}

inline char* right_trim(char *start)
{
	char *end = start + strlen(start) - 1;
	for (; end >= start && (*end == ' ' || *end == '\t'); -- end);
	*(++ end) = 0;
	return end;
}

inline std::string unquote_value(char *value, char *end, const std::string &path, int idx_line)
{
	std::string svalue;
	if (value == end) {
		// Empty string is a valid string.
	} else if (*value == '"') {
		if (++ value > -- end || *end != '"')
			throw file_parser_error("String not enquoted correctly", path, idx_line);
		*end = 0;
		if (! unescape_string_cstyle(value, svalue))
			throw file_parser_error("Invalid escape sequence inside a quoted string", path, idx_line);
	} else
		svalue.assign(value, end);
	return svalue;
}

inline std::string unquote_version_comment(char *value, char *end, const std::string &path, int idx_line)
{
	std::string svalue;
	if (value == end) {
		// Empty string is a valid string.
	} else if (*value == '"') {
		if (++ value > -- end || *end != '"')
			throw file_parser_error("Version comment not enquoted correctly", path, idx_line);
		*end = 0;
		if (! unescape_string_cstyle(value, svalue))
			throw file_parser_error("Invalid escape sequence inside a quoted version comment", path, idx_line);
	} else
		svalue.assign(value, end);
	return svalue;
}

size_t Index::load(const boost::filesystem::path &path)
{
	m_configs.clear();
	m_vendor = path.stem().string();

    boost::nowide::ifstream ifs(path.string());
    std::string line;
    size_t idx_line = 0;
    Version ver;
    while (std::getline(ifs, line)) {
    	++ idx_line;
    	// Skip the initial white spaces.
    	char *key = left_trim(const_cast<char*>(line.data()));
		if (*key == '#')
			// Skip a comment line.
			continue;
		// Right trim the line.
		char *end = right_trim(key);
        if (key == end)
            // Skip an empty line.
            continue;
		// Keyword may only contain alphanumeric characters. Semantic version may in addition contain "+.-".
    	char *key_end = key;
    	bool  maybe_semver = true;
		for (; *key_end != 0; ++ key_end) {
			if (std::isalnum(*key_end) || strchr("+.-", *key_end) != nullptr) {
				// It may be a semver.
			} else if (*key_end == '_') {
				// Cannot be a semver, but it may be a key.
				maybe_semver = false;
			} else
				// End of semver or keyword.
				break;
    	}
    	if (*key_end != 0 && *key_end != ' ' && *key_end != '\t' && *key_end != '=')
    		throw file_parser_error("Invalid keyword or semantic version", path, idx_line);
		char *value = left_trim(key_end);
		bool  key_value_pair = *value == '=';
		if (key_value_pair)
			value = left_trim(value + 1);
		*key_end = 0;
    	boost::optional<Semver> semver;
    	if (maybe_semver)
    		semver = Semver::parse(key);
		if (key_value_pair) {
    		if (semver)
    			throw file_parser_error("Key cannot be a semantic version", path, idx_line);\
    		// Verify validity of the key / value pair.
			std::string svalue = unquote_value(value, end, path.string(), idx_line);
    		if (strcmp(key, "min_slic3r_version") == 0 || strcmp(key, "max_slic3r_version") == 0) {
    			if (! svalue.empty())
					semver = Semver::parse(svalue);
		    	if (! semver)
		    		throw file_parser_error(std::string(key) + " must referece a valid semantic version", path, idx_line);
				if (strcmp(key, "min_slic3r_version") == 0)
    				ver.min_slic3r_version = *semver;
    			else
    				ver.max_slic3r_version = *semver;
    		} else {
    			// Ignore unknown keys, as there may come new keys in the future.
    		}
			continue;
    	}
		if (! semver)
			throw file_parser_error("Invalid semantic version", path, idx_line);
		ver.config_version = *semver;
		ver.comment = (end <= key_end) ? "" : unquote_version_comment(value, end, path.string(), idx_line);
		m_configs.emplace_back(ver);
    }

    // Sort the configs by their version.
    std::sort(m_configs.begin(), m_configs.end(), [](const Version &v1, const Version &v2) { return v1.config_version < v2.config_version; });
    return m_configs.size();
}

Index::const_iterator Index::find(const Semver &ver) const
{ 
	Version key;
	key.config_version = ver;
	auto it = std::lower_bound(m_configs.begin(), m_configs.end(), key, 
		[](const Version &v1, const Version &v2) { return v1.config_version < v2.config_version; });
	return (it == m_configs.end() || it->config_version == ver) ? it : m_configs.end();
}

Index::const_iterator Index::recommended() const
{
	int idx = -1;
	const_iterator highest = m_configs.end();
	for (const_iterator it = this->begin(); it != this->end(); ++ it)
		if (it->is_current_slic3r_supported() &&
			(highest == this->end() || highest->max_slic3r_version < it->max_slic3r_version))
			highest = it;
	return highest;
}

std::vector<Index> Index::load_db()
{
    boost::filesystem::path cache_dir = boost::filesystem::path(Slic3r::data_dir()) / "cache";

    std::vector<Index> index_db;
    std::string errors_cummulative;
	for (auto &dir_entry : boost::filesystem::directory_iterator(cache_dir))
        if (boost::filesystem::is_regular_file(dir_entry.status()) && boost::algorithm::iends_with(dir_entry.path().filename().string(), ".idx")) {
        	Index idx;
            try {
            	idx.load(dir_entry.path());
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
                continue;
			}
            index_db.emplace_back(std::move(idx));
        }

    if (! errors_cummulative.empty())
        throw std::runtime_error(errors_cummulative);
    return index_db;
}

} // namespace Config
} // namespace GUI
} // namespace Slic3r
