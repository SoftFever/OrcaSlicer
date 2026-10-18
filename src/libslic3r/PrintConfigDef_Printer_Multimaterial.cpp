#include "PrintConfig.hpp"
#include "CommonDefs.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintConfigConstants.hpp"
#include "BeltTransform.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "Geometry.hpp"
#include "FilamentMixer.hpp"
#include "MaterialType.hpp"
#include "I18N.hpp"
#include "enum_bitmask.hpp"
#include "format.hpp"

#include "GCode/Thumbnails.hpp"
#include <numeric>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <iterator>
#include <cstdlib>
#include "libslic3r.h"
#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <boost/algorithm/string/constants.hpp>
#include <limits>
#include <memory>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <cstdint>
#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <float.h>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <unordered_map>


namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)

void PrintConfigDef::init_fff_printer_multimaterial()
{
    ConfigOptionDef* def;


    def = this->add("machine_load_filament_time", coFloat);
    def->label = L("Filament load time");
    def->tooltip = L("Time to load new filament when switch filament. It's usually applicable for single-extruder multi-material machines. "
                     "For tool changers or multi-tool machines, it's typically 0. For statistics only.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("machine_unload_filament_time", coFloat);
    def->label = L("Filament unload time");
    def->tooltip = L("Time to unload old filament when switch filament. It's usually applicable for single-extruder multi-material machines. "
                     "For tool changers or multi-tool machines, it's typically 0. For statistics only.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("machine_tool_change_time", coFloat);
    def->label = L("Tool change time");
    def->tooltip = L("Time taken to switch tools. It's usually applicable for tool changers or multi-tool machines. "
                     "For single-extruder multi-material machines, it's typically 0. For statistics only.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat { 0. });

    def = this->add("bed_temperature_formula", coEnum);
    def->label = L("Bed temperature type");
    def->tooltip = L("This option determines how the bed temperature is set during slicing: based on the temperature of the first filament or the highest temperature of the printed filaments.");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<BedTempFormula>::get_enum_values();
    def->enum_values.push_back("by_first_filament");
    def->enum_values.push_back("by_highest_temp");
    def->enum_labels.push_back(L("By First filament"));
    def->enum_labels.push_back(L("By Highest Temp"));
    def->set_default_value(new ConfigOptionEnum<BedTempFormula>(BedTempFormula::btfHighestTemp));

    def = this->add("cooling_tube_retraction", coFloat);
    def->label = L("Cooling tube position");
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(91.5));

    def = this->add("cooling_tube_length", coFloat);
    def->label = L("Cooling tube length");
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("high_current_on_filament_swap", coBool);
    def->label = L("High extruder current on filament swap");
    def->tooltip = L("It may be beneficial to increase the extruder motor current during the filament exchange"
                   " sequence to allow for rapid ramming feed rates and to overcome resistance when loading"
                   " a filament with an ugly shaped tip.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("parking_pos_retraction", coFloat);
    def->label = L("Filament parking position");
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(92.));

    def = this->add("extra_loading_move", coFloat);
    def->label = L("Extra loading distance");
    def->tooltip = L("When set to zero, the distance the filament is moved from parking position during load "
                      "is exactly the same as it was moved back during unload. When positive, it is loaded further, "
                      "if negative, the loading move is shorter than unloading.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-2.));

    def = this->add("single_extruder_multi_material", coBool);
    def->label = L("Single Extruder Multi Material");
    def->tooltip = L("Use single nozzle to print multi filament.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    // IDEX/IQEX (independent X extruder) — parallel printing support for IDEX/IQEX printers.
    // Every key in this group is read at slice time from m_config, and the two per-plate process
    // keys (imex_parallel_mode, imex_head_filament_map) round-trip through the 3MF's
    // model_settings.config plate metadata, so none of them needs to appear in the exported
    // g-code. They all carry non-nil defaults, so emitting them would add a line to every
    // printer's dump. Kept out of the g-code config block (banned_keys).
    def = this->add("is_imex", coBool);
    def->label = L("IDEX/IQEX Printer");
    def->tooltip = L("Enable parallel printing for printers with multiple independent carriages (IDEX, IQEX, and similar).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    // Firmware-managed zones: slice the primary zone's contents at bed origin (0,0)
    // so a center-origin printer firmware can apply its own copy/mirror offsets to
    // each toolhead. The slicer's zone overlay, placement check, and mode signaling
    // are unchanged; only the gcode emission frame shifts.
    def = this->add("imex_firmware_managed_zones", coBool);
    def->label = L("Firmware-managed zones");
    def->tooltip = L("When enabled, the slicer emits G-code with the primary zone's "
                     "contents centered on bed origin (0,0). The printer firmware is "
                     "responsible for placing copies/mirrors at each physical zone "
                     "position. Enable only on printers whose firmware applies its own "
                     "zone offsets in copy/mirror mode (e.g. RepRapFirmware IDEX "
                     "duplication mode). Has no effect in Primary mode.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("imex_gantry_count", coInt);
    def->label = L("Gantry Count");
    def->tooltip = L("Number of independent Y-axis gantries. 1 for IDEX/single-rail IQEX. 2 for dual-gantry systems (divides bed into rows).");
    def->min = 1;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("imex_tools_per_gantry", coInt);
    def->label = L("Tools per Gantry");
    def->tooltip = L("Number of independent toolheads per gantry along X. 2 for IDEX-style.");
    def->min = 1;
    def->max = 4;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def = this->add("imex_tool_layout", coEnum);
    def->label = L("Tool 0 Position");
    def->tooltip = L("Physical position on the bed where tool T0 (index 0) is located. "
                     "Determines how tool indices map to bed zones. "
                     "For single-gantry setups (IDEX) only left/right matters; "
                     "for dual-gantry setups (IQEX) all four corners are selectable "
                     "(front = lower Y / near the operator, rear = higher Y / back of machine).");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<ImexToolLayout>::get_enum_values();
    def->enum_values.push_back("front-left");
    def->enum_values.push_back("front-right");
    def->enum_values.push_back("rear-left");
    def->enum_values.push_back("rear-right");
    def->enum_labels.push_back(L("Front-left"));
    def->enum_labels.push_back(L("Front-right"));
    def->enum_labels.push_back(L("Rear-left"));
    def->enum_labels.push_back(L("Rear-right"));
    def->set_default_value(new ConfigOptionEnum<ImexToolLayout>(ImexToolLayout::FrontLeft));

    def = this->add("imex_nozzle_clearance_x", coFloat);
    def->label = L("Nozzle Clearance X");
    def->tooltip = L("Distance (mm) from the nozzle to the collision-side carriage edge in X. Used to calculate the width of the collision exclusion strip at each X boundary.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30.0));

    def = this->add("imex_nozzle_clearance_y", coFloat);
    def->label = L("Nozzle Clearance Y");
    def->tooltip = L("Distance (mm) from the nozzle to the collision-side carriage edge in Y. Used to calculate the width of the collision exclusion strip at each Y boundary.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30.0));

    def = this->add("imex_carriage_margin", coFloat);
    def->label = L("Safety Margin");
    def->tooltip = L("Non-blocking advisory clearance strip (mm) drawn inside the primary zone at each carriage boundary. Parts placed within this strip will still slice; it is a visual reminder to leave extra clearance near the zone edge.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 200;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("imex_viz_theme", coEnum);
    def->label = L("Visualization Theme");
    def->tooltip = L("Color theme for IDEX/IQEX bed zone visualization. Choose a colorblind-friendly theme if needed.");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<ImexVizTheme>::get_enum_values();
    def->enum_values.push_back("standard");
    def->enum_values.push_back("deuteranopia");
    def->enum_values.push_back("tritanopia");
    def->enum_values.push_back("high_contrast");
    def->enum_labels.push_back(L("Standard"));
    def->enum_labels.push_back(L("Deuteranopia / Protanopia (red-green)"));
    def->enum_labels.push_back(L("Tritanopia (blue-yellow)"));
    def->enum_labels.push_back(L("High Contrast"));
    def->set_default_value(new ConfigOptionEnum<ImexVizTheme>(ImexVizTheme::Standard));

    def = this->add("manual_filament_change", coBool);
    def->label = L("Manual Filament Change");
    def->tooltip = L("Enable this option to omit the custom Change filament G-code only at the beginning of the print. "
                    "The tool change command (e.g., T0) will be skipped throughout the entire print. "
                    "This is useful for manual multi-material printing, where we use M600/PAUSE to trigger the manual filament change action.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_tower_type", coEnum);
    def->label = L("Wipe tower type");
    def->tooltip = L("Choose the wipe tower implementation for multi-material prints. Type 1 is recommended for Bambu and Qidi printers with a filament cutter. Type 2 offers better compatibility with multi-tool and MMU printers and provide overall better compatibility.");
    def->enum_keys_map = &ConfigOptionEnum<WipeTowerType>::get_enum_values();
    def->enum_values.emplace_back("type1");
    def->enum_values.emplace_back("type2");
    def->enum_labels.emplace_back(L("Type 1"));
    def->enum_labels.emplace_back(L("Type 2"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WipeTowerType>(WipeTowerType::Type2));

    def = this->add("purge_in_prime_tower", coBool);
    def->label = L("Purge in prime tower");
    def->tooltip = L("Purge remaining filament into prime tower.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("enable_filament_ramming", coBool);
    def->label = L("Enable filament ramming");
    def->tooltip = L("Enable filament ramming");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("tool_change_on_wipe_tower", coBool);
    def->label = L("Tool change on wipe tower");
    def->tooltip = L("Force the toolhead to travel to the wipe tower before issuing the tool change command (Tx). "
                     "Only relevant for multi-extruder (multi-toolhead) printers using a Type 2 wipe tower. "
                     "By default Orca skips the travel on multi-toolhead machines because the firmware handles the head swap, "
                     "which can result in the Tx command being issued above the printed part. "
                     "Enable this option if you want the tool change to always be issued above the wipe tower instead.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wait_for_temp_on_wipe_tower", coBool);
    def->label = L("Wait for temperature on wipe tower");
    def->tooltip = L("Pick up the new tool without waiting for it to reach printing temperature, travel to the wipe "
                     "tower, and wait for the temperature there, right before purging. Ooze from the heat-up lands on "
                     "the tower instead of the model, and the travel overlaps with the heating. "
                     "Only relevant for multi-extruder (multi-toolhead) printers using a Type 2 wipe tower. "
                     "The firmware or tool change macro must not wait for the temperature itself. "
                     "When disabled, the temperature wait is issued right after the tool change command.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("enable_belt_purge_tower", coBool);
    def->label = L("Enable belt purge tower");
    def->category = L("Multimaterial");
    def->tooltip = L("Belt-printer replacement for the wipe/prime tower. When enabled on a belt "
                     "printer, a purge prism is automatically generated next to the printed parts "
                     "and filament-change purging is routed into it (the classic wipe tower cannot "
                     "be used on belt printers because its G-code bypasses the belt transform). "
                     "Only available on belt printers.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
}

} // namespace Slic3r
