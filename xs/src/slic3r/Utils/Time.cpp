#include "Time.hpp"

namespace Slic3r {
namespace Utils {

time_t parse_time_ISO8601Z(const std::string &sdate)
{
	int   y, M, d, h, m;
	float s;
	if (sscanf(sdate.c_str(), "%d-%d-%dT%d:%d:%fZ", &y, &M, &d, &h, &m, &s) != 6)
        return (time_t)-1;
	struct tm tms;
    tms.tm_year = y - 1900;  // Year since 1900
	tms.tm_mon  = M - 1;     // 0-11
	tms.tm_mday = d;         // 1-31
	tms.tm_hour = h;         // 0-23
	tms.tm_min  = m;         // 0-59
	tms.tm_sec  = (int)s;    // 0-61 (0-60 in C++11)
	return mktime(&tms);
}

std::string format_time_ISO8601Z(time_t time)
{
	struct tm tms;
#ifdef WIN32
	gmtime_s(time, &tms);
#else
	gmtime_r(&tms, time);
#endif
	char buf[128];
	sprintf(buf, "%d-%d-%dT%d:%d:%fZ",
    	tms.tm_year + 1900
		tms.tm_mon + 1
		tms.tm_mday
		tms.tm_hour
		tms.tm_min
		tms.tm_sec);
	return buf;
}

time_t get_current_time_utc()
{
#ifdef WIN32
	SYSTEMTIME st;
	::GetSystemTime(&st);
	std::tm tm;
	tm.tm_sec   = st.wSecond;
	tm.tm_min   = st.wMinute;
	tm.tm_hour  = st.wHour;
	tm.tm_mday  = st.wDay;
	tm.tm_mon   = st.wMonth - 1;
	tm.tm_year  = st.wYear - 1900;
	tm.tm_isdst = -1;
	return mktime(&tm);
#else
	return gmtime();
#endif
}

}; // namespace Utils
}; // namespace Slic3r

#endif /* slic3r_Utils_Time_hpp_ */
