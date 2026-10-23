#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "slic3r/Utils/CrealityPrint.hpp"

using namespace Slic3r;
using json = nlohmann::json;

namespace {

const std::string gcode_path = "/usr/data/printer_data/gcodes/part.gcode";

// A colorMatch_<n> value as the send dialog writes it: "toolId\ttype\tcolor\tboxId\tmaterialId"
std::string slot(const std::string& tool, int box_id, int material_id)
{
    return tool + "\tPLA\t#FF0000\t" + std::to_string(box_id) + "\t" + std::to_string(material_id);
}

std::vector<json> commands(bool multi_color, const std::map<std::string, std::string>& extended_info)
{
    std::vector<json> parsed;
    for (const std::string& cmd : CrealityPrint::start_print_commands(multi_color, gcode_path, extended_info))
        parsed.push_back(json::parse(cmd));
    return parsed;
}

} // namespace

TEST_CASE("A CFS-capable printer that listed no slots starts from the spool holder", "[CrealityPrint][Creality]")
{
    // The dialog maps filaments only onto slots the printer listed in boxsInfo;
    // a K1-family printer without a CFS lists none.
    auto cmds = commands(true, {{"enableSelfTest", "1"}});
    REQUIRE(cmds.size() == 1);
    const json& params = cmds[0]["params"];
    CHECK(params["opGcodeFile"] == "printprt:" + gcode_path);
    CHECK(params["enableSelfTest"] == 1);
    CHECK_FALSE(params.contains("colorMatch"));
}

TEST_CASE("A filament mapped onto the spool holder starts a plain print", "[CrealityPrint][Creality]")
{
    auto cmds = commands(true, {{"enableSelfTest", "0"}, {"colorMatch_0", slot("T1A", 0, 0)}});
    REQUIRE(cmds.size() == 1);
    const json& params = cmds[0]["params"];
    CHECK(params["opGcodeFile"] == "printprt:" + gcode_path);
    CHECK(params["enableSelfTest"] == 0);
}

TEST_CASE("A mapping that includes the spool holder starts a plain print even when other filaments map to CFS slots", "[CrealityPrint][Creality]")
{
    auto cmds = commands(true, {{"enableSelfTest", "0"},
                                {"colorMatch_0", slot("T1A", 1, 2)},
                                {"colorMatch_1", slot("T1B", 0, 0)}});
    REQUIRE(cmds.size() == 1);
    CHECK(cmds[0]["params"]["opGcodeFile"] == "printprt:" + gcode_path);
    CHECK_FALSE(cmds[0]["params"].contains("colorMatch"));
}

TEST_CASE("Filaments mapped onto CFS slots send the colour match before the multi-colour start", "[CrealityPrint][Creality]")
{
    auto cmds = commands(true, {{"enableSelfTest", "1"},
                                {"colorMatch_0", slot("T1A", 1, 0)},
                                {"colorMatch_1", slot("T1B", 1, 3)}});
    REQUIRE(cmds.size() == 2);

    const json& match = cmds[0]["params"]["colorMatch"];
    CHECK(match["path"] == gcode_path);
    REQUIRE(match["list"].size() == 2);
    CHECK(match["list"][0]["id"] == "T1A");
    CHECK(match["list"][0]["boxId"] == 1);
    CHECK(match["list"][0]["materialId"] == 0);
    CHECK(match["list"][1]["id"] == "T1B");
    CHECK(match["list"][1]["materialId"] == 3);
    CHECK(match["list"][1]["type"] == "PLA");
    CHECK(match["list"][1]["color"] == "#FF0000");

    const json& start = cmds[1]["params"]["multiColorPrint"];
    CHECK(start["gcode"] == gcode_path);
    CHECK(start["enableSelfTest"] == 1);
}

TEST_CASE("A printer without CFS support gets the plain start command", "[CrealityPrint][Creality]")
{
    auto cmds = commands(false, {{"enableSelfTest", "1"}});
    REQUIRE(cmds.size() == 1);
    const json& params = cmds[0]["params"];
    CHECK(params["opGcodeFile"] == "printprt:" + gcode_path);
    CHECK_FALSE(params.contains("enableSelfTest"));
}
