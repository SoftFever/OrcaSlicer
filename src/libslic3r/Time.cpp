#include "Time.hpp"

#include <iomanip>
#include <sstream>
#include <chrono>

//#include <boost/date_time/local_time/local_time.hpp>
//#include <boost/chrono.hpp>


#ifdef WIN32
	#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
	#undef WIN32_LEAN_AND_MEAN
#endif /* WIN32 */

namespace Slic3r {
namespace Utils {

namespace  {

// FIXME: after we switch to gcc > 4.9 on the build server, please remove me
#if defined(__GNUC__) && __GNUC__ <= 4
std::string put_time(const std::tm *tm, const char *fmt)
{
    static const constexpr int MAX_CHARS = 200;
    char out[MAX_CHARS];
    std::strftime(out, MAX_CHARS, fmt, tm);
    return out;
}
#else
auto put_time(const std::tm *tm, const char *fmt) -> decltype (std::put_time(tm, fmt))
{
    return std::put_time(tm, fmt);
}
#endif

}

time_t parse_time_ISO8601Z(const std::string &sdate)
{
	int y, M, d, h, m, s;
	if (sscanf(sdate.c_str(), "%04d%02d%02dT%02d%02d%02dZ", &y, &M, &d, &h, &m, &s) != 6)
        return time_t(-1);
	struct tm tms;
    tms.tm_year = y - 1900;  // Year since 1900
	tms.tm_mon  = M - 1;     // 0-11
	tms.tm_mday = d;         // 1-31
	tms.tm_hour = h;         // 0-23
	tms.tm_min  = m;         // 0-59
	tms.tm_sec  = s;         // 0-61 (0-60 in C++11)
#ifdef WIN32
	return _mkgmtime(&tms);
#else /* WIN32 */
	return timegm(&tms);
#endif /* WIN32 */
}

std::string format_time_ISO8601Z(time_t time)
{
	struct tm tms;
#ifdef WIN32
	gmtime_s(&tms, &time);
#else
	gmtime_r(&time, &tms);
#endif
	char buf[128];
	sprintf(buf, "%04d%02d%02dT%02d%02d%02dZ",
    	tms.tm_year + 1900,
		tms.tm_mon + 1,
		tms.tm_mday,
		tms.tm_hour,
		tms.tm_min,
		tms.tm_sec);
	return buf;
}

std::string format_local_date_time(time_t time)
{
	struct tm tms;
#ifdef WIN32
	// Converts a time_t time value to a tm structure, and corrects for the local time zone.
	localtime_s(&tms, &time);
#else
	localtime_r(&time, &tms);
#endif
    char buf[80];
 	strftime(buf, 80, "%x %X", &tms);
    return buf;
}

time_t get_current_time_utc()
{    
    using clk = std::chrono::system_clock;
    return clk::to_time_t(clk::now());
}

static std::string tm2str(const std::tm *tm, const char *fmt)
{
    std::stringstream ss;
    ss << put_time(tm, fmt);
    return ss.str();
}

std::string time2str(const time_t &t, TimeZone zone, const char *fmt)
{
    std::string ret;
    
    switch (zone) {
    case TimeZone::local: ret = tm2str(std::localtime(&t), fmt); break;
    case TimeZone::utc:   ret = tm2str(std::gmtime(&t), fmt) + " UTC"; break;
    }
    
    return ret;
}

}; // namespace Utils
}; // namespace Slic3r
