#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

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
    REQUIRE(t == parsedtime);
    
    tstr = time2str(t, TimeZone::utc, fmt);
    parsedtime = str2time(tstr, TimeZone::utc, fmt);
    REQUIRE(t == parsedtime);
    
    parsedtime = str2time("not valid string", TimeZone::local, fmt);
    REQUIRE(parsedtime == time_t(-1));
    
    parsedtime = str2time("not valid string", TimeZone::utc, fmt);
    REQUIRE(parsedtime == time_t(-1));
}
}

TEST_CASE("ISO8601Z", "[Timeutils]") {
    test_time_fmt(Slic3r::Utils::TimeFormat::iso8601Z);
}

TEST_CASE("Slic3r_UTC_Time_Format", "[Timeutils]") {
    test_time_fmt(Slic3r::Utils::TimeFormat::gcode);
}
