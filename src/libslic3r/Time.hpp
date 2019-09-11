#ifndef slic3r_Utils_Time_hpp_
#define slic3r_Utils_Time_hpp_

#include <string>
#include <ctime>

namespace Slic3r {
namespace Utils {

// Utilities to convert an UTC time_t to/from an ISO8601 time format,
// useful for putting timestamps into file and directory names.
// Returns (time_t)-1 on error.
time_t parse_time_ISO8601Z(const std::string &s);
std::string format_time_ISO8601Z(time_t time);

// Format the date and time from an UTC time according to the active locales and a local time zone.
// TODO: make sure time2str is a suitable replacement
std::string format_local_date_time(time_t time);

// There is no gmtime() on windows.
time_t get_current_time_utc();

const constexpr char *const SLIC3R_TIME_FMT = "%Y-%m-%d at %T";

enum class TimeZone { local, utc };

std::string time2str(const time_t &t, TimeZone zone, const char *fmt = SLIC3R_TIME_FMT);

inline std::string current_time2str(TimeZone zone, const char *fmt = SLIC3R_TIME_FMT)
{
    return time2str(get_current_time_utc(), zone, fmt);
}

inline std::string current_local_time2str(const char * fmt = SLIC3R_TIME_FMT)
{
    return current_time2str(TimeZone::local, fmt);    
}

inline std::string current_utc_time2str(const char * fmt = SLIC3R_TIME_FMT)
{
    return current_time2str(TimeZone::utc, fmt);
}

}; // namespace Utils
}; // namespace Slic3r

#endif /* slic3r_Utils_Time_hpp_ */
