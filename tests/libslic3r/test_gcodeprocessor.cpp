#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/GCode/GCodeProcessor.hpp"

#include <utility>

TEST_CASE("G-code result assignment preserves the Z offset", "[GCodeProcessor]")
{
    const bool move = GENERATE(false, true);
    Slic3r::GCodeProcessorResult source{};
    source.z_offset = 0.75f;
    Slic3r::GCodeProcessorResult target{};
    target.z_offset = -1.0f;

    if (move)
        target = std::move(source);
    else
        target = source;

    CHECK_THAT(target.z_offset, Catch::Matchers::WithinAbs(0.75f, 1e-6));
}
