#include <catch2/catch_all.hpp>

#include <catch2/catch_test_macros.hpp>
#include "libslic3r/Time.hpp"
#include "libslic3r/Utils.hpp"

#include <ctime>
#include <sstream>
#include <iomanip>
#include <locale>
#include <string>

static void test_time_fmt(Slic3r::Utils::TimeFormat fmt) {
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

TEST_CASE("ISO8601Z", "[Timeutils]") {
    test_time_fmt(Slic3r::Utils::TimeFormat::iso8601Z);
    
    std::string mydate = "20190710T085000Z";
    time_t t = Slic3r::Utils::parse_iso_utc_timestamp(mydate);
    std::string date = Slic3r::Utils::iso_utc_timestamp(t);
    
    REQUIRE(date == mydate);
}

TEST_CASE("Slic3r_UTC_Time_Format", "[Timeutils]") {
    using namespace Slic3r::Utils;
    test_time_fmt(TimeFormat::gcode);
    
    std::string mydate = "2019-07-10 at 08:50:00 UTC";
    time_t t = Slic3r::Utils::str2time(mydate, TimeZone::utc, TimeFormat::gcode);
    std::string date = Slic3r::Utils::utc_timestamp(t);
    
    REQUIRE(date == mydate);
}

TEST_CASE("format_clock_time", "[Timeutils]") {
    using Slic3r::format_clock_time;

    // 24-hour output is the unchanged default look.
    REQUIRE(format_clock_time(0, 5, false) == "00:05");
    REQUIRE(format_clock_time(13, 30, false) == "13:30");
    REQUIRE(format_clock_time(23, 59, false, 7) == "23:59:07");

    // 12-hour: midnight is 12 AM, noon is 12 PM.
    REQUIRE(format_clock_time(0, 5, true) == "12:05 AM");
    REQUIRE(format_clock_time(9, 0, true) == "09:00 AM");
    REQUIRE(format_clock_time(12, 0, true) == "12:00 PM");
    REQUIRE(format_clock_time(13, 30, true) == "01:30 PM");
    REQUIRE(format_clock_time(23, 59, true, 7) == "11:59:07 PM");
}

TEST_CASE("get_bbl_monitor_end_time_dhm", "[Timeutils]") {
    REQUIRE(Slic3r::get_bbl_monitor_end_time_dhm(0.f).empty());
    REQUIRE(Slic3r::get_bbl_monitor_end_time_dhm(0.f, true).empty());

    const std::string h24 = Slic3r::get_bbl_monitor_end_time_dhm(3600.f);
    const std::string h12 = Slic3r::get_bbl_monitor_end_time_dhm(3600.f, true);
    REQUIRE(h24.size() == 5);
    REQUIRE(h24[2] == ':');
    REQUIRE((h12.size() == 8 && (h12.substr(6) == "AM" || h12.substr(6) == "PM")));
}
