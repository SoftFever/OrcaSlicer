#include "unistd.h"

#include <cstdint>
#include <chrono>
#include <thread>

#include <windows.h>


extern "C" {


int usleep(unsigned usec)
{
	std::this_thread::sleep_for(std::chrono::microseconds(usec));
	return 0;
}


// SO: https://stackoverflow.com/questions/10905892/equivalent-of-gettimeday-for-windows
int gettimeofday(struct timeval *tp, struct timezone *tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970 
    static const std::uint64_t EPOCH = ((std::uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    std::uint64_t    time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((std::uint64_t)file_time.dwLowDateTime);
    time += ((std::uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec  = (long)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
    return 0;
}



}
