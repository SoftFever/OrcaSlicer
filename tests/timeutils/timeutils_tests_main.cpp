#include <gtest/gtest.h>
#include "libslic3r/Time.hpp"

#include <sstream>
#include <iomanip>
#include <locale>

namespace {

void test_time_fmt(Slic3r::Utils::TimeFormat fmt) {
    using namespace Slic3r::Utils;
    time_t t = get_current_time_utc();
    
    std::string tstr = time2str(t, TimeZone::local, fmt);
    time_t parsedtime = str2time(tstr, TimeZone::local, fmt);
    ASSERT_EQ(t, parsedtime);
    
    tstr = time2str(t, TimeZone::utc, fmt);
    parsedtime = str2time(tstr, TimeZone::utc, fmt);
    ASSERT_EQ(t, parsedtime);
    
    parsedtime = str2time("not valid string", TimeZone::local, fmt);
    ASSERT_EQ(parsedtime, time_t(-1));
    
    parsedtime = str2time("not valid string", TimeZone::utc, fmt);
    ASSERT_EQ(parsedtime, time_t(-1));
}
}

TEST(Timeutils, ISO8601Z) {
    test_time_fmt(Slic3r::Utils::TimeFormat::iso8601Z);
}

TEST(Timeutils, Slic3r_UTC_Time_Format) {
    test_time_fmt(Slic3r::Utils::TimeFormat::gcode);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
