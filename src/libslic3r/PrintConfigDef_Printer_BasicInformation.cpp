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

void PrintConfigDef::init_fff_printer_basic_information()
{
    ConfigOptionDef* def;


    // Orca: already carried by the BBL/Qidi/Geeetech/Eryone machine profiles, which inherited it from
    // the BambuStudio import; without a definition here it was parsed as an unknown key and dropped.
    def = this->add("extruder_clearance_dist_to_rod", coFloat);
    def->label = L("Distance to rod");
    def->tooltip = L("Horizontal distance of the nozzle tip to the rod's farther edge. Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    def = this->add("extruder_clearance_height_to_rod", coFloat);
    def->label = L("Height to rod");
    def->tooltip = L("Distance from the nozzle tip to the lower rod. Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    // BBS
    def = this->add("extruder_clearance_height_to_lid", coFloat);
    def->label = L("Height to lid");
    def->tooltip = L("Distance from the nozzle tip to the lid. Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(120));

    def = this->add("extruder_clearance_radius", coFloat);
    def->label = L("Radius");
    def->tooltip = L("Clearance radius around extruder: used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    def          = this->add("bed_mesh_min", coPoint);
    def->label   = L("Bed mesh min");
    def->tooltip = L(
        "This option sets the min point for the allowed bed mesh area. Due to the probe's XY offset, most printers are unable to probe the "
        "entire bed. To ensure the probe point does not go outside the bed area, the minimum and maximum points of the bed mesh should be "
        "set appropriately. OrcaSlicer ensures that adaptive_bed_mesh_min/adaptive_bed_mesh_max values do not exceed these min/max "
        "points. This information can usually be obtained from your printer manufacturer. The default setting is (-99999, -99999), which "
        "means there are no limits, thus allowing probing across the entire bed.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(-99999, -99999)));

    def          = this->add("bed_mesh_max", coPoint);
    def->label   = L("Bed mesh max");
    def->tooltip = L(
        "This option sets the max point for the allowed bed mesh area. Due to the probe's XY offset, most printers are unable to probe the "
        "entire bed. To ensure the probe point does not go outside the bed area, the minimum and maximum points of the bed mesh should be "
        "set appropriately. OrcaSlicer ensures that adaptive_bed_mesh_min/adaptive_bed_mesh_max values do not exceed these min/max "
        "points. This information can usually be obtained from your printer manufacturer. The default setting is (99999, 99999), which "
        "means there are no limits, thus allowing probing across the entire bed.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(99999, 99999)));

    def          = this->add("bed_mesh_probe_distance", coPoint);
    def->label   = L("Probe point distance");
    def->tooltip = L("This option sets the preferred distance between probe points (grid size) for the X and Y directions, with the "
                     "default being 50mm for both X and Y.");
    def->min     = 0;
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(50, 50)));

    def          = this->add("adaptive_bed_mesh_margin", coFloat);
    def->label   = L("Mesh margin");
    def->tooltip = L("This option determines the additional distance by which the adaptive bed mesh area should be expanded in the XY directions.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    // BBS
    def = this->add("scan_first_layer", coBool);
    def->label = L("Scan first layer");
    def->tooltip = L("Enable this to allow the camera on the printer to check the quality of the first layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    // Orca
    def = this->add("enable_power_loss_recovery", coEnum);
    def->label = L("Power Loss Recovery");
    def->tooltip = L("Choose how to control power loss recovery. When set to Printer configuration, the slicer will not emit power loss recovery G-code and will leave the printer's configuration unchanged. Applicable to Bambu Lab or Marlin 2 firmware based printers.");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<PowerLossRecoveryMode>::get_enum_values();
    def->enum_values.push_back("printer_configuration");
    def->enum_values.push_back("enable");
    def->enum_values.push_back("disable");
    def->enum_labels.push_back(L("Printer configuration"));
    def->enum_labels.push_back(L("Enable"));
    def->enum_labels.push_back(L("Disable"));
    def->set_default_value(new ConfigOptionEnum<PowerLossRecoveryMode>(PowerLossRecoveryMode::PrinterConfiguration));

    //BBS
    // def = this->add("spaghetti_detector", coBool);
    // def->label = L("Enable spaghetti detector");
    // def->tooltip = L("Enable the camera on printer to check spaghetti.");
    // def->mode = comSimple;
    // def->set_default_value(new ConfigOptionBool(false));


    def                = this->add("nozzle_hrc", coInt);
    def->label         = L("Nozzle HRC");
    def->tooltip       = L("The nozzle\'s hardness. Zero means no checking for nozzle hardness during slicing.");
    def->sidetext      = L("HRC");
    def->min           = 0;
    def->max           = 500;
    def->mode          = comDevelop;
    def->set_default_value(new ConfigOptionInt{0});

    def = this->add("printer_structure", coEnum);
    def->label = L("Printer structure");
    def->tooltip = L("The physical arrangement and components of a printing device.");
    def->enum_keys_map = &ConfigOptionEnum<PrinterStructure>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("corexy");
    def->enum_values.push_back("i3");
    def->enum_values.push_back("hbot");
    def->enum_values.push_back("delta");
    def->enum_labels.push_back(L("Undefined"));
    def->enum_labels.push_back(L("CoreXY"));
    def->enum_labels.push_back(L("I3"));
    def->enum_labels.push_back(L("Hbot"));
    def->enum_labels.push_back(L("Delta"));
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<PrinterStructure>(psUndefine));

    def = this->add("best_object_pos", coPoint);
    def->label = L("Best object position");
    def->tooltip = L("Best auto arranging position in range [0,1] w.r.t. bed shape.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(0.5, 0.5)));

    def = this->add("auxiliary_fan", coBool);
    def->label = L("Auxiliary part cooling fan");
    def->tooltip = L("Enable this option if machine has auxiliary part cooling fan. G-code command: M106 P2 S(0-255).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("fan_direction", coEnum);
    def->label = L("Fan direction");
    def->tooltip = L("Cooling fan direction of the printer");
    def->enum_keys_map = &ConfigOptionEnum<FanDirection>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("left");
    def->enum_values.push_back("right");
    def->enum_values.push_back("both");
    def->enum_labels.push_back(L("Undefined"));
    def->enum_labels.push_back(L("Left"));
    def->enum_labels.push_back(L("Right"));
    def->enum_labels.push_back(L("Both"));
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<FanDirection>(fdUndefine));

    def = this->add("fan_speedup_time", coFloat);
	// Label is set in Tab.cpp in the Line object.
    //def->label = L("Fan speed-up time");
    def->tooltip = L("Start the fan this number of seconds earlier than its target start time (you can use fractional seconds)."
        " It assumes infinite acceleration for this time estimation, and will only take into account G1 and G0 moves (arc fitting"
        " is unsupported)."
        "\nIt won't move fan commands from custom G-code (they act as a sort of 'barrier')."
        "\nIt won't move fan commands into the start G-code if the 'only custom start G-code' is activated."
        "\nUse 0 to deactivate.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("fan_speedup_overhangs", coBool);
    def->label = L("Only overhangs");
    def->tooltip = L("Will only take into account the delay for the cooling of overhangs.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("fan_kickstart", coFloat);
    def->label = L("Fan kick-start time");
    def->tooltip = L("Emit a max fan speed command for this amount of seconds before reducing to target speed to kick-start the cooling fan."
                    "\nThis is useful for fans where a low PWM/power may be insufficient to get the fan started spinning from a stop, or to "
                    "get the fan up to speed faster."
                    "\nSet to 0 to deactivate.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    // ORCA: minimum non-zero part cooling fan speed.
    def = this->add("part_cooling_fan_min_pwm", coInt);
    def->label = L("Minimum non-zero part cooling fan speed");
    def->tooltip = L("Some part-cooling fans cannot start spinning when commanded below a certain PWM duty cycle. "
                     "When set above 0, any non-zero part-cooling fan command will be raised to at least this percentage "
                     "so the fan reliably starts. A fan command of 0 (fan off) is always honoured exactly. "
                     "This clamp is applied after every other fan calculation (first-layer ramp, layer-time interpolation, "
                     "overhang/bridge/support-interface/ironing overrides), so scaling still operates within the range "
                     "[this value, 100%]."
                     "\nIf your firmware already disables the fan below a threshold (for example Klipper's "
                     "[fan] off_below: 0.10 shuts the fan off whenever the commanded duty cycle is below 10%), "
                     "this option and the firmware threshold should ideally be set to the same value. Matching them "
                     "(e.g. off_below: 0.10 in Klipper and 10% here) guarantees the slicer never emits a non-zero "
                     "value that the firmware would silently drop, and the fan never receives a value below the one "
                     "you know it can actually spool at."
                     "\nSet to 0 to deactivate.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));


    def = this->add("time_cost", coFloat);
    def->label = L("Time cost");
    def->tooltip = L("The printer cost per hour.");
    def->sidetext = L("money/h");
    def->min     = 0;
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    // Orca: may remove this option later
    def =this->add("support_chamber_temp_control",coBool);
    def->label=L("Support controlling chamber temperature");
    def->tooltip=L("This option is enabled if machine support controlling chamber temperature\nG-code command: M141 S(0-255)");
    def->mode=comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
    def->readonly=false;

    def =this->add("support_air_filtration",coBool);
    def->label=L("Support air filtration");
    def->tooltip=L("Enable this if printer support air filtration\nG-code command: M106 P3 S(0-255)");
    def->mode=comDevelop;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("cooling_filter_enabled", coBool);
    def->label = L("Use cooling filter");
    def->tooltip = L("Enable this if printer support cooling filter");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcode_flavor", coEnum);
    def->label = L("G-code flavor");
    def->tooltip = L("What kind of G-code the printer is compatible with.");
    def->enum_keys_map = &ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("marlin");
    def->enum_values.push_back("klipper");
    def->enum_values.push_back("reprapfirmware");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("marlin2");
    //def->enum_values.push_back("reprap");
    //def->enum_values.push_back("teacup");
    //def->enum_values.push_back("makerware");
    //def->enum_values.push_back("sailfish");
    //def->enum_values.push_back("mach3");
    //def->enum_values.push_back("machinekit");
    //def->enum_values.push_back("smoothie");
    //def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back(L("Marlin (legacy)"));
    def->enum_labels.push_back(L("Klipper"));
    def->enum_labels.push_back("RepRapFirmware");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("Marlin 2");
    //def->enum_labels.push_back("RepRap/Sprinter");
    //def->enum_labels.push_back("Teacup");
    //def->enum_labels.push_back("MakerWare (MakerBot)");
    //def->enum_labels.push_back("Sailfish (MakerBot)");
    //def->enum_labels.push_back("Mach3/LinuxCNC");
    //def->enum_labels.push_back("Machinekit");
    //def->enum_labels.push_back("Smoothie");
    //def->enum_labels.push_back(L("No extrusion"));
    def->mode = comAdvanced;
    def->readonly = false;
    def->set_default_value(new ConfigOptionEnum<GCodeFlavor>(gcfMarlinLegacy));

    def = this->add("gcode_skip_config_block", coBool);
    def->label = L("Skip G-code config block");
    def->tooltip = L("Do not write the CONFIG_BLOCK (slicer configuration key/value pairs) into the G-code file. "
                   "This can help with printers whose firmware crashes when parsing these comment lines "
                   "(e.g. Anycubic go-klipper). Note: the G-code file will no longer contain slicer settings, "
                   "so importing it back into OrcaSlicer will not restore the configuration.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def          = this->add("pellet_modded_printer", coBool);
    def->label   = L("Pellet Modded Printer");
    def->tooltip = L("Enable this option if your printer uses pellets instead of filaments.");
    def->mode    = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_multi_bed_types", coBool);
    def->label = L("Support multi bed types");
    def->tooltip = L("Enable this option if you want to use multiple bed types.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wrapping_exclude_area", coPoints);
    def->label   = L("Probing exclude area of clumping");
    def->tooltip = L("Probing exclude area of clumping.");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints());

    def = this->add("use_firmware_retraction", coBool);
    def->label = L("Use firmware retraction");
    def->tooltip = L("This experimental setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. This is only supported in recent Marlin.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("disable_m73", coBool);
    def->label = L("Disable set remaining print time");
    def->tooltip = L("Disable generating of the M73: Set remaining print time in the final G-code.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("z_offset", coFloat);
    def->label = L("Z offset");
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("build_plate_tilt_x", coFloat);
    def->label = L("Build plate tilt X");
    def->category = L("Support");
    def->tooltip = L("Tilt angle of the build plate along the X axis. "
                     "A positive value tilts the plate so the +X side is higher, shifting gravity toward -X and increasing overhangs on the +X side. "
                     "A negative value tilts the -X side higher. Set to 0 for no X-axis tilt. "
                     "In belt printer mode, this is automatically synced to the belt angle.");
    def->sidetext = u8"\u00B0";
    def->min = -89;
    def->max = 89;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("build_plate_tilt_y", coFloat);
    def->label = L("Build plate tilt Y");
    def->category = L("Support");
    def->tooltip = L("Tilt angle of the build plate along the Y axis. "
                     "A positive value tilts the plate so the +Y side is higher, shifting gravity toward -Y and increasing overhangs on the +Y side. "
                     "A negative value tilts the -Y side higher. Set to 0 for no Y-axis tilt.");
    def->sidetext = u8"\u00B0";
    def->min = -89;
    def->max = 89;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("belt_printer", coBool);
    def->label = L("Enable belt printing");
    def->category = L("Printable space");
    def->tooltip = L("Enable belt printer mode. Belt printers use a conveyor belt as the build surface, "
                     "tilted at an angle (typically 45 degrees). The slicer will rotate the slicing plane "
                     "and transform G-code coordinates for the tilted build surface.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("belt_printer_infinite_y", coBool);
    def->label = L("Infinite Y axis");
    def->category = L("Printable space");
    def->tooltip = L("Enable infinite Y axis for belt printers. "
                     "When enabled, the Y axis build volume limit is effectively removed, "
                     "allowing objects of any length to be printed along the belt direction.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    // Mesh rotation applied before slicing — the sole mesh-side belt transform AND
    // the single source of truth for the physical belt tilt (bed rendering, support
    // gravity tilt and bed-exclusion projection all derive their angle from this).
    def = this->add("belt_slice_rotation", coEnum);
    def->label = L("Belt tilt axis");
    def->category = L("Printable space");
    def->tooltip = L("Axis the mesh is rotated about before slicing. This is the belt "
                     "printer's tilt: an isometric (no distortion) rotation that also "
                     "drives bed rendering and support gravity tilt, and that the G-code "
                     "back-transform inverts before the machine-frame shear/scale and remap. "
                     "X is the typical gantry tilt (belt travels along Y).");
    def->enum_keys_map = &ConfigOptionEnum<BeltRotationAxis>::get_enum_values();
    def->enum_values  = {"none", "x", "y", "z"};
    def->enum_labels  = {L("None"), L("X"), L("Y"), L("Z")};
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<BeltRotationAxis>(BeltRotationAxis::X));

    def = this->add("belt_slice_rotation_angle", coFloat);
    def->label = L("Belt tilt angle");
    def->category = L("Printable space");
    def->tooltip = L("Tilt angle of the belt surface, in degrees. Most belt printers use "
                     "45°. Positive values rotate counter-clockwise looking down the "
                     "positive tilt axis; the magnitude is also the physical belt tilt "
                     "used for bed rendering and support gravity.");
    def->sidetext = L("°");
    def->min = -180.;
    def->max = 180.;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45.));

    def = this->add("belt_frame_tilt_decouple", coBool);
    def->label = L("Decouple machine-frame tilt");
    def->category = L("Printable space");
    def->tooltip = L("Expert override: set the machine-frame (G-code shear/scale) tilt angle "
                     "independently of the pre-slice rotation angle. When disabled, the "
                     "machine-frame transform is derived from the belt tilt angle, so a single "
                     "angle drives both stages. Enable only to compensate for a machine whose "
                     "physical gantry tilt differs from the slicing rotation.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("belt_frame_tilt_angle", coFloat);
    def->label = L("Machine-frame tilt angle");
    def->category = L("Printable space");
    def->tooltip = L("Tilt angle (degrees) used to derive the machine-frame shear (cot) and "
                     "scale (1/|sin|) applied to G-code. Only used when 'Decouple machine-frame "
                     "tilt' is enabled; otherwise the belt tilt angle is used.");
    def->sidetext = L("°");
    def->min = -89.9;
    def->max = 89.9;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(45.));

    // G-code axis remap with sign. Each field is its own row in the settings tab.  The
    // labels and tooltips are literals in L() so they are extracted for translation.
    auto add_belt_remap = [this](const char *key, const std::string &label, const std::string &tooltip,
                                  RemapAxis default_axis, ConfigOptionMode mode) {
        auto def = this->add(key, coEnum);
        def->label = label;
        def->category = L("Printable space");
        def->tooltip = tooltip;
        def->enum_keys_map = &ConfigOptionEnum<RemapAxis>::get_enum_values();
        def->enum_values  = {"pos_x", "pos_y", "pos_z", "neg_x", "neg_y", "neg_z", "rev_x", "rev_y", "rev_z"};
        def->enum_labels  = {L("+X"), L("+Y"), L("+Z"), L("-X"), L("-Y"), L("-Z"), L("Rev X"), L("Rev Y"), L("Rev Z")};
        def->mode = mode;  // Visibility may also be gated by toggle_line in Tab.cpp
        def->set_default_value(new ConfigOptionEnum<RemapAxis>(default_axis));
    };
    add_belt_remap("gcode_remap_x", L("G-code remap X"),
                   L("Which slicing axis maps to machine X in G-code output. Applied AFTER slicing, during G-code generation."),
                   RemapAxis::PosX, comDevelop);
    add_belt_remap("gcode_remap_y", L("G-code remap Y"),
                   L("Which slicing axis maps to machine Y in G-code output. Applied AFTER slicing, during G-code generation."),
                   RemapAxis::PosY, comDevelop);
    add_belt_remap("gcode_remap_z", L("G-code remap Z"),
                   L("Which slicing axis maps to machine Z in G-code output. Applied AFTER slicing, during G-code generation."),
                   RemapAxis::PosZ, comDevelop);

    // The machine-frame G-code transform (shear + scale) is no longer configured
    // by per-axis keys: it is derived from the belt tilt (belt_slice_rotation axis
    // + angle, or belt_frame_tilt_angle when decoupled) in MachineFrameTransform.

    // Belt support floor debug controls
    def = this->add("belt_support_floor_offset", coFloat);
    def->label = L("Support Floor Z offset");
    def->category = L("Printable space");
    def->tooltip = L("Shifts the computed belt floor up or down (mm). Negative values lower the floor, allowing more supports to survive. Use this to diagnose belt floor formula issues.");
    def->sidetext = L("mm");
    def->min = -500;
    def->max = 500;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("thumbnails", coString);
    def->label = L("G-code thumbnails");
    def->tooltip = L("Picture sizes to be stored into a .gcode and .sl1 / .sl1s files, in the following format: \"XxY, XxY, ...\"");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString("48x48/PNG,300x300/PNG"));

    def = this->add("thumbnails_format", coEnum);
    def->label = L("Format of G-code thumbnails");
    def->tooltip = L("Format of G-code thumbnails: PNG for best quality, JPG for smallest size, QOI for low memory firmware.");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<GCodeThumbnailsFormat>::get_enum_values();
    def->enum_values.push_back("PNG");
    def->enum_values.push_back("JPG");
    def->enum_values.push_back("QOI");
    def->enum_values.push_back("BTT_TFT");
    def->enum_values.push_back("COLPIC");
    def->enum_labels.push_back("PNG");
    def->enum_labels.push_back("JPG");
    def->enum_labels.push_back("QOI");
    def->enum_labels.push_back("BTT TT");
    def->enum_labels.push_back("ColPic");
    def->set_default_value(new ConfigOptionEnum<GCodeThumbnailsFormat>(GCodeThumbnailsFormat::PNG));

    def = this->add("use_relative_e_distances", coBool);
    def->label = L("Use relative E distances");
    def->tooltip = L("Relative extrusion is recommended when using \"label_objects\" option. "
                   "Some extruders work better with this option unchecked (absolute extrusion mode). "
                   "Wipe tower is only compatible with relative mode. It is recommended on "
                   "most printers. Default is checked.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
}

} // namespace Slic3r
