#include <catch2/catch.hpp>

#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

SCENARIO("Placeholder parser scripting", "[PlaceholderParser]") {
	PlaceholderParser 	parser;
	auto 				config = DynamicPrintConfig::full_print_config();

	config.set_deserialize_strict( {
		{ "printer_notes", "  PRINTER_VENDOR_PRUSA3D  PRINTER_MODEL_MK2  " },
	    { "nozzle_diameter", "0.6;0.6;0.6;0.6" },
	    { "temperature", "357;359;363;378" }
	});
    // To test the "first_layer_extrusion_width" over "first_layer_heigth".
    // "first_layer_heigth" over "layer_height" is no more supported after first_layer_height was moved from PrintObjectConfig to PrintConfig.
//  config.option<ConfigOptionFloatOrPercent>("first_layer_height")->value = 150.;
//  config.option<ConfigOptionFloatOrPercent>("first_layer_height")->percent = true;
    config.option<ConfigOptionFloatOrPercent>("first_layer_height")->value = 1.5 * config.opt_float("layer_height");
    config.option<ConfigOptionFloatOrPercent>("first_layer_height")->percent = false;
    // To let the PlaceholderParser throw when referencing first_layer_speed if it is set to percent, as the PlaceholderParser does not know
    // a percent to what.
    config.option<ConfigOptionFloatOrPercent>("first_layer_speed")->value = 50.;
    config.option<ConfigOptionFloatOrPercent>("first_layer_speed")->percent = true;

    parser.apply_config(config);
	parser.set("foo", 0);
	parser.set("bar", 2);
	parser.set("num_extruders", 4);

    SECTION("nested config options (legacy syntax)") { REQUIRE(parser.process("[temperature_[foo]]") == "357"); }
    SECTION("array reference") { REQUIRE(parser.process("{temperature[foo]}") == "357"); }
    SECTION("whitespaces and newlines are maintained") { REQUIRE(parser.process("test [ temperature_ [foo] ] \n hu") == "test 357 \n hu"); }

    // Test the math expressions.
    SECTION("math: 2*3") { REQUIRE(parser.process("{2*3}") == "6"); }
    SECTION("math: 2*3/6") { REQUIRE(parser.process("{2*3/6}") == "1"); }
    SECTION("math: 2*3/12") { REQUIRE(parser.process("{2*3/12}") == "0"); }
    SECTION("math: 2.*3/12") { REQUIRE(std::stod(parser.process("{2.*3/12}")) == Approx(0.5)); }
    SECTION("math: 10 % 2.5") { REQUIRE(std::stod(parser.process("{10%2.5}")) == Approx(0.)); }
    SECTION("math: 11 % 2.5") { REQUIRE(std::stod(parser.process("{11%2.5}")) == Approx(1.)); }
    SECTION("math: 2*(3-12)") { REQUIRE(parser.process("{2*(3-12)}") == "-18"); }
    SECTION("math: 2*foo*(3-12)") { REQUIRE(parser.process("{2*foo*(3-12)}") == "0"); }
    SECTION("math: 2*bar*(3-12)") { REQUIRE(parser.process("{2*bar*(3-12)}") == "-36"); }
    SECTION("math: 2.5*bar*(3-12)") { REQUIRE(std::stod(parser.process("{2.5*bar*(3-12)}")) == Approx(-45)); }
    SECTION("math: min(12, 14)") { REQUIRE(parser.process("{min(12, 14)}") == "12"); }
    SECTION("math: max(12, 14)") { REQUIRE(parser.process("{max(12, 14)}") == "14"); }
    SECTION("math: min(13.4, -1238.1)") { REQUIRE(std::stod(parser.process("{min(13.4, -1238.1)}")) == Approx(-1238.1)); }
    SECTION("math: max(13.4, -1238.1)") { REQUIRE(std::stod(parser.process("{max(13.4, -1238.1)}")) == Approx(13.4)); }
    SECTION("math: int(13.4)") { REQUIRE(parser.process("{int(13.4)}") == "13"); }
    SECTION("math: int(-13.4)") { REQUIRE(parser.process("{int(-13.4)}") == "-13"); }

    // Test the "coFloatOrPercent" and "xxx_extrusion_width" substitutions.
    // first_layer_extrusion_width ratio_over first_layer_heigth.
    SECTION("perimeter_extrusion_width") { REQUIRE(std::stod(parser.process("{perimeter_extrusion_width}")) == Approx(0.67500001192092896)); }
    SECTION("first_layer_extrusion_width") { REQUIRE(std::stod(parser.process("{first_layer_extrusion_width}")) == Approx(0.9)); }
    SECTION("support_material_xy_spacing") { REQUIRE(std::stod(parser.process("{support_material_xy_spacing}")) == Approx(0.3375)); }
    // external_perimeter_speed over perimeter_speed
    SECTION("external_perimeter_speed") { REQUIRE(std::stod(parser.process("{external_perimeter_speed}")) == Approx(30.)); }
    // infill_overlap over perimeter_extrusion_width
    SECTION("infill_overlap") { REQUIRE(std::stod(parser.process("{infill_overlap}")) == Approx(0.16875)); }
    // If first_layer_speed is set to percent, then it is applied over respective extrusion types by overriding their respective speeds.
    // The PlaceholderParser has no way to know which extrusion type the caller has in mind, therefore it throws.
    SECTION("first_layer_speed") { REQUIRE_THROWS(parser.process("{first_layer_speed}")); }

    // Test the boolean expression parser.
    auto boolean_expression = [&parser](const std::string& templ) { return parser.evaluate_boolean_expression(templ, parser.config()); };

    SECTION("boolean expression parser: 12 == 12") { REQUIRE(boolean_expression("12 == 12")); }
    SECTION("boolean expression parser: 12 != 12") { REQUIRE(! boolean_expression("12 != 12")); }
    SECTION("boolean expression parser: regex matches") { REQUIRE(boolean_expression("\"has some PATTERN embedded\" =~ /.*PATTERN.*/")); }
    SECTION("boolean expression parser: regex does not match") { REQUIRE(! boolean_expression("\"has some PATTERN embedded\" =~ /.*PTRN.*/")); }
    SECTION("boolean expression parser: accessing variables, equal") { REQUIRE(boolean_expression("foo + 2 == bar")); }
    SECTION("boolean expression parser: accessing variables, not equal") { REQUIRE(! boolean_expression("foo + 3 == bar")); }
    SECTION("boolean expression parser: (12 == 12) and (13 != 14)") { REQUIRE(boolean_expression("(12 == 12) and (13 != 14)")); }
    SECTION("boolean expression parser: (12 == 12) && (13 != 14)") { REQUIRE(boolean_expression("(12 == 12) && (13 != 14)")); }
    SECTION("boolean expression parser: (12 == 12) or (13 == 14)") { REQUIRE(boolean_expression("(12 == 12) or (13 == 14)")); }
    SECTION("boolean expression parser: (12 == 12) || (13 == 14)") { REQUIRE(boolean_expression("(12 == 12) || (13 == 14)")); }
    SECTION("boolean expression parser: (12 == 12) and not (13 == 14)") { REQUIRE(boolean_expression("(12 == 12) and not (13 == 14)")); }
    SECTION("boolean expression parser: ternary true") { REQUIRE(boolean_expression("(12 == 12) ? (1 - 1 == 0) : (2 * 2 == 3)")); }
    SECTION("boolean expression parser: ternary false") { REQUIRE(! boolean_expression("(12 == 21/2) ? (1 - 1 == 0) : (2 * 2 == 3)")); }
    SECTION("boolean expression parser: ternary false 2") { REQUIRE(boolean_expression("(12 == 13) ? (1 - 1 == 3) : (2 * 2 == 4)")); }
    SECTION("boolean expression parser: ternary true 2") { REQUIRE(! boolean_expression("(12 == 2 * 6) ? (1 - 1 == 3) : (2 * 2 == 4)")); }
    SECTION("boolean expression parser: lower than - false") { REQUIRE(! boolean_expression("12 < 3")); }
    SECTION("boolean expression parser: lower than - true") { REQUIRE(boolean_expression("12 < 22")); }
    SECTION("boolean expression parser: greater than - true") { REQUIRE(boolean_expression("12 > 3")); }
    SECTION("boolean expression parser: greater than - false") { REQUIRE(! boolean_expression("12 > 22")); }
    SECTION("boolean expression parser: lower than or equal- false") { REQUIRE(! boolean_expression("12 <= 3")); }
    SECTION("boolean expression parser: lower than or equal - true") { REQUIRE(boolean_expression("12 <= 22")); }
    SECTION("boolean expression parser: greater than or equal - true") { REQUIRE(boolean_expression("12 >= 3")); }
    SECTION("boolean expression parser: greater than or equal - false") { REQUIRE(! boolean_expression("12 >= 22")); }
    SECTION("boolean expression parser: lower than or equal (same values) - true") { REQUIRE(boolean_expression("12 <= 12")); }
    SECTION("boolean expression parser: greater than or equal (same values) - true") { REQUIRE(boolean_expression("12 >= 12")); }
    SECTION("complex expression") { REQUIRE(boolean_expression("printer_notes=~/.*PRINTER_VENDOR_PRUSA3D.*/ and printer_notes=~/.*PRINTER_MODEL_MK2.*/ and nozzle_diameter[0]==0.6 and num_extruders>1")); }
    SECTION("complex expression2") { REQUIRE(boolean_expression("printer_notes=~/.*PRINTER_VEwerfNDOR_PRUSA3D.*/ or printer_notes=~/.*PRINTertER_MODEL_MK2.*/ or (nozzle_diameter[0]==0.6 and num_extruders>1)")); }
    SECTION("complex expression3") { REQUIRE(! boolean_expression("printer_notes=~/.*PRINTER_VEwerfNDOR_PRUSA3D.*/ or printer_notes=~/.*PRINTertER_MODEL_MK2.*/ or (nozzle_diameter[0]==0.3 and num_extruders>1)")); }
}
