#ifndef slic3r_Utils_Time_hpp_
#define slic3r_Utils_Time_hpp_

#include <string>
#include <time.h>

namespace Slic3r {
namespace Utils {

// Utilities to convert an UTC time_t to/from an ISO8601 time format,
// useful for putting timestamps into file and directory names.
// Returns (time_t)-1 on error.
extern time_t parse_time_ISO8601Z(const std::string &s);
extern std::string format_time_ISO8601Z(time_t time);

// Format the date and time from an UTC time according to the active locales and a local time zone.
extern std::string format_local_date_time(time_t time);

// There is no gmtime() on windows.
extern time_t get_current_time_utc();

}; // namespace Utils
}; // namespace Slic3r

#endif /* slic3r_Utils_Time_hpp_ */
