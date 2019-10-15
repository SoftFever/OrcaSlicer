#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "libslic3r/libslic3r.h"

namespace {

TEST_CASE("sort_remove_duplicates", "[utils]") {
	std::vector<int> data_src = { 3, 0, 2, 1, 15, 3, 5, 6, 3, 1, 0 };
	std::vector<int> data_dst = { 0, 1, 2, 3, 5, 6, 15 };
	Slic3r::sort_remove_duplicates(data_src);
    REQUIRE(data_src == data_dst);
}

}
