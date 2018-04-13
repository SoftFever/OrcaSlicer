#include "PrintConfig.hpp"

#include <set>
#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

#include <float.h>

namespace Slic3r {

//! macro used to mark string used at localization, 
//! return same string
#define L(s) s

PrintConfigDef::PrintConfigDef()
{
    t_optiondef_map &Options = this->options;
    
    ConfigOptionDef* def;

    // Maximum extruder temperature, bumped to 1500 to support printing of glass.
    const int max_temp = 1500;

	def = this->add("avoid_crossing_perimeters", coBool);
    def->label = L("Avoid crossing perimeters");
	def->tooltip = L("Optimize travel moves in order to minimize the crossing of perimeters. "
                   "This is mostly useful with Bowden extruders which suffer from oozing. "
                   "This feature slows down both the print and the G-code generation.");
    def->cli = "avoid-crossing-perimeters!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("bed_shape", coPoints);
	def->label = L("Bed shape");
    def->default_value = new ConfigOptionPoints { Pointf(0,0), Pointf(200,0), Pointf(200,200), Pointf(0,200) };
    
    def = this->add("bed_temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers after the first one. "
                   "Set this to zero to disable bed temperature control commands in the output.");
    def->cli = "bed-temperature=i@";
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->default_value = new ConfigOptionInts { 0 };

    def = this->add("before_layer_gcode", coString);
    def->label = L("Before layer change G-code");
    def->tooltip = L("This custom code is inserted at every layer change, right before the Z move. "
                   "Note that you can use placeholder variables for all Slic3r settings as well "
                   "as [layer_num] and [layer_z].");
    def->cli = "before-layer-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("between_objects_gcode", coString);
    def->label = L("Between objects G-code");
    def->tooltip = L("This code is inserted between objects when using sequential printing. By default extruder and bed temperature are reset using non-wait command; however if M104, M109, M140 or M190 are detected in this custom code, Slic3r will not add temperature commands. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->cli = "between-objects-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionString("");

    def = this->add("bottom_solid_layers", coInt);
    def->label = L("Bottom");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Number of solid layers to generate on bottom surfaces.");
    def->cli = "bottom-solid-layers=i";
    def->full_label = L("Bottom solid layers");
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("bridge_acceleration", coFloat);
    def->label = L("Bridge");
    def->tooltip = L("This is the acceleration your printer will use for bridges. "
                   "Set zero to disable acceleration control for bridges.");
    def->sidetext = L("mm/s²");
    def->cli = "bridge-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("bridge_angle", coFloat);
    def->label = L("Bridging angle");
    def->category = L("Infill");
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
                   "automatically. Otherwise the provided angle will be used for all bridges. "
                   "Use 180° for zero angle.");
	def->sidetext = L("°");
    def->cli = "bridge-angle=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0.);

    def = this->add("bridge_fan_speed", coInts);
    def->label = L("Bridges fan speed");
    def->tooltip = L("This fan speed is enforced during all bridges and overhangs.");
    def->sidetext = L("%");
    def->cli = "bridge-fan-speed=i@";
    def->min = 0;
    def->max = 100;
    def->default_value = new ConfigOptionInts { 100 };

    def = this->add("bridge_flow_ratio", coFloat);
    def->label = L("Bridge flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of plastic for bridging. "
                   "You can decrease it slightly to pull the extrudates and prevent sagging, "
                   "although default settings are usually good and you should experiment "
                   "with cooling (use a fan) before tweaking this.");
    def->cli = "bridge-flow-ratio=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(1);

    def = this->add("bridge_speed", coFloat);
    def->label = L("Bridges");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing bridges.");
    def->sidetext = L("mm/s");
    def->cli = "bridge-speed=f";
    def->aliases.push_back("bridge_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(60);

    def = this->add("brim_width", coFloat);
    def->label = L("Brim width");
    def->tooltip = L("Horizontal width of the brim that will be printed around each object on the first layer.");
    def->sidetext = L("mm");
    def->cli = "brim-width=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("clip_multipart_objects", coBool);
    def->label = L("Clip multi-part objects");
    def->tooltip = L("When printing multi-material objects, this settings will make slic3r "
                   "to clip the overlapping object parts one by the other "
                   "(2nd part will be clipped by the 1st, 3rd part will be clipped by the 1st and 2nd etc).");
    def->cli = "clip-multipart-objects!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("compatible_printers", coStrings);
    def->label = L("Compatible printers");
    def->default_value = new ConfigOptionStrings();

    def = this->add("compatible_printers_condition", coString);
    def->label = L("Compatible printers condition");
    def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active printer profile.");
    def->default_value = new ConfigOptionString();

    def = this->add("complete_objects", coBool);
    def->label = L("Complete individual objects");
    def->tooltip = L("When printing multiple objects or copies, this feature will complete "
                   "each object before moving onto next one (and starting it from its bottom layer). "
                   "This feature is useful to avoid the risk of ruined prints. "
                   "Slic3r should warn and prevent you from extruder collisions, but beware.");
    def->cli = "complete-objects!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("cooling", coBools);
    def->label = L("Enable auto cooling");
    def->tooltip = L("This flag enables the automatic cooling logic that adjusts print speed "
                   "and fan speed according to layer printing time.");
    def->cli = "cooling!";
    def->default_value = new ConfigOptionBools { true };

    def = this->add("cooling_tube_retraction", coFloat);
    def->label = L("Cooling tube position");
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip ");
    def->sidetext = L("mm");
    def->cli = "cooling_tube_retraction=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(91.5f);

    def = this->add("cooling_tube_length", coFloat);
    def->label = L("Cooling tube length");
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it ");
    def->sidetext = L("mm");
    def->cli = "cooling_tube_length=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(5.f);

    def = this->add("default_acceleration", coFloat);
    def->label = L("Default");
    def->tooltip = L("This is the acceleration your printer will be reset to after "
                   "the role-specific acceleration values are used (perimeter/infill). "
                   "Set zero to prevent resetting acceleration at all.");
    def->sidetext = L("mm/s²");
    def->cli = "default-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("default_filament_profile", coStrings);
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile associated with the current printer profile. "
                   "On selection of the current printer profile, this filament profile will be activated.");
    def->default_value = new ConfigOptionStrings();

    def = this->add("default_print_profile", coString);
    def->label = L("Default print profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->default_value = new ConfigOptionString();

    def = this->add("disable_fan_first_layers", coInts);
    def->label = L("Disable fan for the first");
    def->tooltip = L("You can set this to a positive value to disable fan at all "
                   "during the first layers, so that it does not make adhesion worse.");
    def->sidetext = L("layers");
    def->cli = "disable-fan-first-layers=i@";
    def->min = 0;
    def->max = 1000;
    def->default_value = new ConfigOptionInts { 3 };

    def = this->add("dont_support_bridges", coBool);
    def->label = L("Don't support bridges");
    def->category = L("Support material");
    def->tooltip = L("Experimental option for preventing support material from being generated "
                   "under bridged areas.");
    def->cli = "dont-support-bridges!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("duplicate_distance", coFloat);
    def->label = L("Distance between copies");
    def->tooltip = L("Distance used for the auto-arrange feature of the plater.");
    def->sidetext = L("mm");
    def->cli = "duplicate-distance=f";
    def->aliases.push_back("multiply_distance");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(6);

    def = this->add("elefant_foot_compensation", coFloat);
    def->label = L("Elephant foot compensation");
    def->category = L("Advanced");
    def->tooltip = L("The first layer will be shrunk in the XY plane by the configured value "
                   "to compensate for the 1st layer squish aka an Elephant Foot effect.");
    def->sidetext = L("mm");
    def->cli = "elefant-foot-compensation=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("end_gcode", coString);
    def->label = L("End G-code");
    def->tooltip = L("This end procedure is inserted at the end of the output file. "
                   "Note that you can use placeholder variables for all Slic3r settings.");
    def->cli = "end-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n");

    def = this->add("end_filament_gcode", coStrings);
    def->label = L("End G-code");
    def->tooltip = L("This end procedure is inserted at the end of the output file, before the printer end gcode. "
                   "Note that you can use placeholder variables for all Slic3r settings. "
                   "If you have multiple extruders, the gcode is processed in extruder order.");
    def->cli = "end-filament-gcode=s@";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionStrings { "; Filament-specific end gcode \n;END gcode for filament\n" };

    def = this->add("ensure_vertical_shell_thickness", coBool);
    def->label = L("Ensure vertical shell thickness");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Add solid infill near sloping surfaces to guarantee the vertical shell thickness "
                   "(top+bottom solid layers).");
    def->cli = "ensure-vertical-shell-thickness!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("external_fill_pattern", coEnum);
    def->label = L("Top/bottom fill pattern");
    def->category = L("Infill");
    def->tooltip = L("Fill pattern for top/bottom infill. This only affects the external visible layer, "
                   "and not its adjacent solid shells.");
    def->cli = "external-fill-pattern|solid-fill-pattern=s";
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back("Rectilinear");
    def->enum_labels.push_back("Concentric");
    def->enum_labels.push_back("Hilbert Curve");
    def->enum_labels.push_back("Archimedean Chords");
    def->enum_labels.push_back("Octagram Spiral");
    // solid_fill_pattern is an obsolete equivalent to external_fill_pattern.
    def->aliases.push_back("solid_fill_pattern");
    def->default_value = new ConfigOptionEnum<InfillPattern>(ipRectilinear);

    def = this->add("external_perimeter_extrusion_width", coFloatOrPercent);
    def->label = L("External perimeters");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for external perimeters. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%), it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "external-perimeter-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("external_perimeter_speed", coFloatOrPercent);
    def->label = L("External perimeters");
    def->category = L("Speed");
    def->tooltip = L("This separate setting will affect the speed of external perimeters (the visible ones). "
                   "If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->cli = "external-perimeter-speed=s";
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(50, true);

    def = this->add("external_perimeters_first", coBool);
    def->label = L("External perimeters first");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Print contour perimeters from the outermost one to the innermost one "
                   "instead of the default inverse order.");
    def->cli = "external-perimeters-first!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("extra_perimeters", coBool);
    def->label = L("Extra perimeters if needed");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Add more perimeters when needed for avoiding gaps in sloping walls. "
                   "Slic3r keeps adding perimeters, until more than 70% of the loop immediately above "
                   "is supported.");
    def->cli = "extra-perimeters!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("extruder", coInt);
    def->gui_type = "i_enum_open";
    def->label = L("Extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
                   "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->cli = "extruder=i";
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back("default");  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");

    def = this->add("extruder_clearance_height", coFloat);
    def->label = L("Height");
    def->tooltip = L("Set this to the vertical distance between your nozzle tip and (usually) the X carriage rods. "
                   "In other words, this is the height of the clearance cylinder around your extruder, "
                   "and it represents the maximum depth the extruder can peek before colliding with "
                   "other printed objects.");
    def->sidetext = L("mm");
    def->cli = "extruder-clearance-height=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(20);

    def = this->add("extruder_clearance_radius", coFloat);
    def->label = L("Radius");
    def->tooltip = L("Set this to the clearance radius around your extruder. "
                   "If the extruder is not centered, choose the largest value for safety. "
                   "This setting is used to check for collisions and to display the graphical preview "
                   "in the plater.");
    def->sidetext = L("mm");
    def->cli = "extruder-clearance-radius=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(20);

    def = this->add("extruder_colour", coStrings);
    def->label = L("Extruder Color");
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->cli = "extruder-color=s@";
    def->gui_type = "color";
    // Empty string means no color assigned yet.
    def->default_value = new ConfigOptionStrings { "" };

    def = this->add("extruder_offset", coPoints);
    def->label = L("Extruder offset");
    def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
                   "to take it into account. This option lets you specify the displacement of each extruder "
                   "with respect to the first one. It expects positive coordinates (they will be subtracted "
                   "from the XY coordinate).");
    def->sidetext = L("mm");
    def->cli = "extruder-offset=s@";
    def->default_value = new ConfigOptionPoints { Pointf(0,0) };

    def = this->add("extrusion_axis", coString);
    def->label = L("Extrusion axis");
    def->tooltip = L("Use this option to set the axis letter associated to your printer's extruder "
                   "(usually E but some printers use A).");
    def->cli = "extrusion-axis=s";
    def->default_value = new ConfigOptionString("E");

    def = this->add("extrusion_multiplier", coFloats);
    def->label = L("Extrusion multiplier");
    def->tooltip = L("This factor changes the amount of flow proportionally. You may need to tweak "
                   "this setting to get nice surface finish and correct single wall widths. "
                   "Usual values are between 0.9 and 1.1. If you think you need to change this more, "
                   "check filament diameter and your firmware E steps.");
    def->cli = "extrusion-multiplier=f@";
    def->default_value = new ConfigOptionFloats { 1. };
    
    def = this->add("extrusion_width", coFloatOrPercent);
    def->label = L("Default extrusion width");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to allow a manual extrusion width. "
                   "If left to zero, Slic3r derives extrusion widths from the nozzle diameter "
                   "(see the tooltips for perimeter extrusion width, infill extrusion width etc). "
                   "If expressed as percentage (for example: 230%), it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for auto)");
    def->cli = "extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("fan_always_on", coBools);
    def->label = L("Keep fan always on");
    def->tooltip = L("If this is enabled, fan will never be disabled and will be kept running at least "
                   "at its minimum speed. Useful for PLA, harmful for ABS.");
    def->cli = "fan-always-on!";
    def->default_value = new ConfigOptionBools { false };

    def = this->add("fan_below_layer_time", coInts);
    def->label = L("Enable fan if layer print time is below");
    def->tooltip = L("If layer print time is estimated below this number of seconds, fan will be enabled "
                   "and its speed will be calculated by interpolating the minimum and maximum speeds.");
    def->sidetext = L("approximate seconds");
    def->cli = "fan-below-layer-time=i@";
    def->width = 60;
    def->min = 0;
    def->max = 1000;
    def->default_value = new ConfigOptionInts { 60 };

    def = this->add("filament_colour", coStrings);
    def->label = L("Color");
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->cli = "filament-color=s@";
    def->gui_type = "color";
    def->default_value = new ConfigOptionStrings { "#29B2B2" };

    def = this->add("filament_notes", coStrings);
    def->label = L("Filament notes");
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->cli = "filament-notes=s@";
    def->multiline = true;
    def->full_width = true;
    def->height = 130;
    def->default_value = new ConfigOptionStrings { "" };

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = L("Max volumetric speed");
    def->tooltip = L("Maximum volumetric speed allowed for this filament. Limits the maximum volumetric "
                   "speed of a print to the minimum of print and filament volumetric speed. "
                   "Set to zero for no limit.");
    def->sidetext = L("mm³/s");
    def->cli = "filament-max-volumetric-speed=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("filament_loading_speed", coFloats);
    def->label = L("Loading speed");
    def->tooltip = L("Speed used for loading the filament on the wipe tower. ");
    def->sidetext = L("mm/s");
    def->cli = "filament-loading-speed=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 28. };

    def = this->add("filament_unloading_speed", coFloats);
    def->label = L("Unloading speed");
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect "
                      " initial part of unloading just after ramming). ");
    def->sidetext = L("mm/s");
    def->cli = "filament-unloading-speed=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 90. };

    def = this->add("filament_toolchange_delay", coFloats);
    def->label = L("Delay after unloading");
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable toolchanges with flexible materials "
                   "that may need more time to shrink to original dimensions. ");
    def->sidetext = L("s");
    def->cli = "filament-toolchange-delay=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 0. };
    
    def = this->add("filament_cooling_time", coFloats);
    def->label = L("Cooling time");
    def->tooltip = L("The filament is slowly moved back and forth after retraction into the cooling tube "
                   "for this amount of time.");
    def->cli = "filament_cooling_time=i@";
    def->sidetext = L("s");
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 14.f };

    def = this->add("filament_ramming_parameters", coStrings);
    def->label = L("Ramming parameters");
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters ");
    def->cli = "filament-ramming-parameters=s@";
    def->default_value = new ConfigOptionStrings { "120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
	   " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6" };

    def = this->add("filament_diameter", coFloats);
    def->label = L("Diameter");
    def->tooltip = L("Enter your filament diameter here. Good precision is required, so use a caliper "
                   "and do multiple measurements along the filament, then compute the average.");
    def->sidetext = L("mm");
    def->cli = "filament-diameter=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 3. };

    def = this->add("filament_density", coFloats);
    def->label = L("Density");
    def->tooltip = L("Enter your filament density here. This is only for statistical information. "
                   "A decent way is to weigh a known length of filament and compute the ratio "
                   "of the length to volume. Better is to calculate the volume directly through displacement.");
    def->sidetext = L("g/cm³");
    def->cli = "filament-density=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("filament_type", coStrings);
    def->label = L("Filament type");
    def->tooltip = L("If you want to process the output G-code through custom scripts, just list their "
                   "absolute paths here. Separate multiple scripts with a semicolon. Scripts will be passed "
                   "the absolute path to the G-code file as the first argument, and they can access "
                   "the Slic3r config settings by reading environment variables.");
    def->cli = "filament_type=s@";
    def->gui_type = "f_enum_open";
    def->gui_flags = "show_value";
    def->enum_values.push_back("PLA");
    def->enum_values.push_back("ABS");
    def->enum_values.push_back("PET");
    def->enum_values.push_back("HIPS");
    def->enum_values.push_back("FLEX");
    def->enum_values.push_back("SCAFF");
    def->enum_values.push_back("EDGE");
    def->enum_values.push_back("NGEN");
    def->enum_values.push_back("PVA");
    def->default_value = new ConfigOptionStrings { "PLA" };

    def = this->add("filament_soluble", coBools);
    def->label = L("Soluble material");
    def->tooltip = L("Soluble material is most likely used for a soluble support.");
    def->cli = "filament-soluble!";
    def->default_value = new ConfigOptionBools { false };

    def = this->add("filament_cost", coFloats);
    def->label = L("Cost");
    def->tooltip = L("Enter your filament cost per kg here. This is only for statistical information.");
    def->sidetext = L("money/kg");
    def->cli = "filament-cost=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 0. };
    
    def = this->add("filament_settings_id", coStrings);
    def->default_value = new ConfigOptionStrings { "" };

    def = this->add("fill_angle", coFloat);
    def->label = L("Fill angle");
    def->category = L("Infill");
    def->tooltip = L("Default base angle for infill orientation. Cross-hatching will be applied to this. "
                   "Bridges will be infilled using the best direction Slic3r can detect, so this setting "
                   "does not affect them.");
    def->sidetext = L("°");
    def->cli = "fill-angle=f";
    def->min = 0;
    def->max = 360;
    def->default_value = new ConfigOptionFloat(45);

    def = this->add("fill_density", coPercent);
    def->gui_type = "f_enum_open";
    def->gui_flags = "show_value";
    def->label = L("Fill density");
    def->category = L("Infill");
    def->tooltip = L("Density of internal infill, expressed in the range 0% - 100%.");
    def->sidetext = L("%");
    def->cli = "fill-density=s";
    def->min = 0;
    def->max = 100;
    def->enum_values.push_back("0");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("15");
    def->enum_values.push_back("20");
    def->enum_values.push_back("25");
    def->enum_values.push_back("30");
    def->enum_values.push_back("40");
    def->enum_values.push_back("50");
    def->enum_values.push_back("60");
    def->enum_values.push_back("70");
    def->enum_values.push_back("80");
    def->enum_values.push_back("90");
    def->enum_values.push_back("100");
    def->enum_labels.push_back("0%");
    def->enum_labels.push_back("5%");
    def->enum_labels.push_back("10%");
    def->enum_labels.push_back("15%");
    def->enum_labels.push_back("20%");
    def->enum_labels.push_back("25%");
    def->enum_labels.push_back("30%");
    def->enum_labels.push_back("40%");
    def->enum_labels.push_back("50%");
    def->enum_labels.push_back("60%");
    def->enum_labels.push_back("70%");
    def->enum_labels.push_back("80%");
    def->enum_labels.push_back("90%");
    def->enum_labels.push_back("100%");
    def->default_value = new ConfigOptionPercent(20);

    def = this->add("fill_pattern", coEnum);
    def->label = L("Fill pattern");
    def->category = L("Infill");
    def->tooltip = L("Fill pattern for general low-density infill.");
    def->cli = "fill-pattern=s";
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("stars");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("line");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("3dhoneycomb");
    def->enum_values.push_back("gyroid");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back("Rectilinear");
    def->enum_labels.push_back("Grid");
    def->enum_labels.push_back("Triangles");
    def->enum_labels.push_back("Stars");
    def->enum_labels.push_back("Cubic");
    def->enum_labels.push_back("Line");
    def->enum_labels.push_back("Concentric");
    def->enum_labels.push_back("Honeycomb");
    def->enum_labels.push_back("3D Honeycomb");
    def->enum_labels.push_back("Gyroid");
    def->enum_labels.push_back("Hilbert Curve");
    def->enum_labels.push_back("Archimedean Chords");
    def->enum_labels.push_back("Octagram Spiral");
    def->default_value = new ConfigOptionEnum<InfillPattern>(ipStars);

    def = this->add("first_layer_acceleration", coFloat);
    def->label = L("First layer");
    def->tooltip = L("This is the acceleration your printer will use for first layer. Set zero "
                   "to disable acceleration control for first layer.");
    def->sidetext = L("mm/s²");
    def->cli = "first-layer-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("first_layer_bed_temperature", coInts);
    def->label = L("First layer");
    def->tooltip = L("Heated build plate temperature for the first layer. Set this to zero to disable "
                   "bed temperature control commands in the output.");
    def->cli = "first-layer-bed-temperature=i@";
    def->max = 0;
    def->max = 300;
    def->default_value = new ConfigOptionInts { 0 };

    def = this->add("first_layer_extrusion_width", coFloatOrPercent);
    def->label = L("First layer");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for first layer. "
                   "You can use this to force fatter extrudates for better adhesion. If expressed "
                   "as percentage (for example 120%) it will be computed over first layer height. "
                   "If set to zero, it will use the default extrusion width.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "first-layer-extrusion-width=s";
    def->ratio_over = "first_layer_height";
    def->default_value = new ConfigOptionFloatOrPercent(200, true);

    def = this->add("first_layer_height", coFloatOrPercent);
    def->label = L("First layer height");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("When printing with very low layer heights, you might still want to print a thicker "
                   "bottom layer to improve adhesion and tolerance for non perfect build plates. "
                   "This can be expressed as an absolute value or as a percentage (for example: 150%) "
                   "over the default layer height.");
    def->sidetext = L("mm or %");
    def->cli = "first-layer-height=s";
    def->ratio_over = "layer_height";
    def->default_value = new ConfigOptionFloatOrPercent(0.35, false);

    def = this->add("first_layer_speed", coFloatOrPercent);
    def->label = L("First layer speed");
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first layer, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->sidetext = L("mm/s or %");
    def->cli = "first-layer-speed=s";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(30, false);

    def = this->add("first_layer_temperature", coInts);
    def->label = L("First layer");
    def->tooltip = L("Extruder temperature for first layer. If you want to control temperature manually "
                   "during print, set this to zero to disable temperature control commands in the output file.");
    def->cli = "first-layer-temperature=i@";
    def->min = 0;
    def->max = max_temp;
    def->default_value = new ConfigOptionInts { 200 };
    
    def = this->add("gap_fill_speed", coFloat);
    def->label = L("Gap fill");
    def->category = L("Speed");
    def->tooltip = L("Speed for filling small gaps using short zigzag moves. Keep this reasonably low "
                   "to avoid too much shaking and resonance issues. Set zero to disable gaps filling.");
    def->sidetext = L("mm/s");
    def->cli = "gap-fill-speed=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(20);

    def = this->add("gcode_comments", coBool);
    def->label = L("Verbose G-code");
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
                   "If you print from SD card, the additional weight of the file could make your firmware "
                   "slow down.");
    def->cli = "gcode-comments!";
    def->default_value = new ConfigOptionBool(0);

    def = this->add("gcode_flavor", coEnum);
    def->label = L("G-code flavor");
    def->tooltip = L("Some G/M-code commands, including temperature control and others, are not universal. "
                   "Set this option to your printer's firmware to get a compatible output. "
                   "The \"No extrusion\" flavor prevents Slic3r from exporting any extrusion value at all.");
    def->cli = "gcode-flavor=s";
    def->enum_keys_map = &ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("reprap");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("teacup");
    def->enum_values.push_back("makerware");
    def->enum_values.push_back("marlin");
    def->enum_values.push_back("sailfish");
    def->enum_values.push_back("mach3");
    def->enum_values.push_back("machinekit");
    def->enum_values.push_back("smoothie");
    def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back("RepRap/Sprinter");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("Teacup");
    def->enum_labels.push_back("MakerWare (MakerBot)");
    def->enum_labels.push_back("Marlin");
    def->enum_labels.push_back("Sailfish (MakerBot)");
    def->enum_labels.push_back("Mach3/LinuxCNC");
    def->enum_labels.push_back("Machinekit");
    def->enum_labels.push_back("Smoothie");
    def->enum_labels.push_back("No extrusion");
    def->default_value = new ConfigOptionEnum<GCodeFlavor>(gcfMarlin);

    def = this->add("infill_acceleration", coFloat);
    def->label = L("Infill");
    def->tooltip = L("This is the acceleration your printer will use for infill. Set zero to disable "
                   "acceleration control for infill.");
    def->sidetext = L("mm/s²");
    def->cli = "infill-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("infill_every_layers", coInt);
    def->label = L("Combine infill every");
    def->category = L("Infill");
    def->tooltip = L("This feature allows to combine infill and speed up your print by extruding thicker "
                   "infill layers while preserving thin perimeters, thus accuracy.");
    def->sidetext = L("layers");
    def->cli = "infill-every-layers=i";
    def->full_label = L("Combine infill every n layers");
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("infill_extruder", coInt);
    def->label = L("Infill extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing infill.");
    def->cli = "infill-extruder=i";
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("infill_extrusion_width", coFloatOrPercent);
    def->label = L("Infill");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "You may want to use fatter extrudates to speed up the infill and make your parts stronger. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "infill-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("infill_first", coBool);
    def->label = L("Infill before perimeters");
    def->tooltip = L("This option will switch the print order of perimeters and infill, making the latter first.");
    def->cli = "infill-first!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("infill_only_where_needed", coBool);
    def->label = L("Only infill where needed");
    def->category = L("Infill");
    def->tooltip = L("This option will limit infill to the areas actually needed for supporting ceilings "
                   "(it will act as internal support material). If enabled, slows down the G-code generation "
                   "due to the multiple checks involved.");
    def->cli = "infill-only-where-needed!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("infill_overlap", coFloatOrPercent);
    def->label = L("Infill/perimeters overlap");
    def->category = L("Advanced");
    def->tooltip = L("This setting applies an additional overlap between infill and perimeters for better bonding. "
                   "Theoretically this shouldn't be needed, but backlash might cause gaps. If expressed "
                   "as percentage (example: 15%) it is calculated over perimeter extrusion width.");
    def->sidetext = L("mm or %");
    def->cli = "infill-overlap=s";
    def->ratio_over = "perimeter_extrusion_width";
    def->default_value = new ConfigOptionFloatOrPercent(25, true);

    def = this->add("infill_speed", coFloat);
    def->label = L("Infill");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing the internal fill. Set to zero for auto.");
    def->sidetext = L("mm/s");
    def->cli = "infill-speed=f";
    def->aliases.push_back("print_feed_rate");
    def->aliases.push_back("infill_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(80);

    def = this->add("inherits", coString);
    def->label = L("Inherits profile");
    def->tooltip = L("Name of the profile, from which this profile inherits.");
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("interface_shells", coBool);
    def->label = L("Interface shells");
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                   "Useful for multi-extruder prints with translucent materials or manual soluble "
                   "support material.");
    def->cli = "interface-shells!";
    def->category = L("Layers and Perimeters");
    def->default_value = new ConfigOptionBool(false);

    def = this->add("layer_gcode", coString);
    def->label = L("After layer change G-code");
    def->tooltip = L("This custom code is inserted at every layer change, right after the Z move "
                   "and before the extruder moves to the first layer point. Note that you can use "
                   "placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].");
    def->cli = "after-layer-gcode|layer-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("layer_height", coFloat);
    def->label = L("Layer height");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("This setting controls the height (and thus the total number) of the slices/layers. "
                   "Thinner layers give better accuracy but take more time to print.");
    def->sidetext = L("mm");
    def->cli = "layer-height=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0.3);

    def = this->add("max_fan_speed", coInts);
    def->label = L("Max");
    def->tooltip = L("This setting represents the maximum speed of your fan.");
    def->sidetext = L("%");
    def->cli = "max-fan-speed=i@";
    def->min = 0;
    def->max = 100;
    def->default_value = new ConfigOptionInts { 100 };

    def = this->add("max_layer_height", coFloats);
    def->label = L("Max");
    def->tooltip = L("This is the highest printable layer height for this extruder, used to cap "
                   "the variable layer height and support layer height. Maximum recommended layer height "
                   "is 75% of the extrusion width to achieve reasonable inter-layer adhesion. "
                   "If set to 0, layer height is limited to 75% of the nozzle diameter.");
    def->sidetext = L("mm");
    def->cli = "max-layer-height=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("max_print_height", coFloat);
    def->label = L("Max print height");
    def->tooltip = L("Set this to the maximum height that can be reached by your extruder while printing.");
    def->sidetext = L("mm");
    def->cli = "max-print-height=f";
    def->default_value = new ConfigOptionFloat(200.0);

    def = this->add("max_print_speed", coFloat);
    def->label = L("Max print speed");
    def->tooltip = L("When setting other speed settings to 0 Slic3r will autocalculate the optimal speed "
                   "in order to keep constant extruder pressure. This experimental setting is used "
                   "to set the highest print speed you want to allow.");
    def->sidetext = L("mm/s");
    def->cli = "max-print-speed=f";
    def->min = 1;
    def->default_value = new ConfigOptionFloat(80);

    def = this->add("max_volumetric_speed", coFloat);
    def->label = L("Max volumetric speed");
    def->tooltip = L("This experimental setting is used to set the maximum volumetric speed your "
                   "extruder supports.");
    def->sidetext = L("mm³/s");
    def->cli = "max-volumetric-speed=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("max_volumetric_extrusion_rate_slope_positive", coFloat);
    def->label = L("Max volumetric slope positive");
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->sidetext = L("mm³/s²");
    def->cli = "max-volumetric-extrusion-rate-slope-positive=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("max_volumetric_extrusion_rate_slope_negative", coFloat);
    def->label = L("Max volumetric slope negative");
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->sidetext = L("mm³/s²");
    def->cli = "max-volumetric-extrusion-rate-slope-negative=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("min_fan_speed", coInts);
    def->label = L("Min");
    def->tooltip = L("This setting represents the minimum PWM your fan needs to work.");
    def->sidetext = L("%");
    def->cli = "min-fan-speed=i@";
    def->min = 0;
    def->max = 100;
    def->default_value = new ConfigOptionInts { 35 };

    def = this->add("min_layer_height", coFloats);
    def->label = L("Min");
    def->tooltip = L("This is the lowest printable layer height for this extruder and limits "
                   "the resolution for variable layer height. Typical values are between 0.05 mm and 0.1 mm.");
    def->sidetext = L("mm");
    def->cli = "min-layer-height=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 0.07 };

    def = this->add("min_print_speed", coFloats);
    def->label = L("Min print speed");
    def->tooltip = L("Slic3r will not scale speed down below this speed.");
    def->sidetext = L("mm/s");
    def->cli = "min-print-speed=f@";
    def->min = 0;
    def->default_value = new ConfigOptionFloats { 10. };

    def = this->add("min_skirt_length", coFloat);
    def->label = L("Minimum extrusion length");
    def->tooltip = L("Generate no less than the number of skirt loops required to consume "
                   "the specified amount of filament on the bottom layer. For multi-extruder machines, "
                   "this minimum applies to each extruder.");
    def->sidetext = L("mm");
    def->cli = "min-skirt-length=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("notes", coString);
    def->label = L("Configuration notes");
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->cli = "notes=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 130;
    def->default_value = new ConfigOptionString("");

    def = this->add("nozzle_diameter", coFloats);
    def->label = L("Nozzle diameter");
    def->tooltip = L("This is the diameter of your extruder nozzle (for example: 0.5, 0.35 etc.)");
    def->sidetext = L("mm");
    def->cli = "nozzle-diameter=f@";
    def->default_value = new ConfigOptionFloats { 0.5 };

    def = this->add("octoprint_apikey", coString);
    def->label = L("API Key");
    def->tooltip = L("Slic3r can upload G-code files to OctoPrint. This field should contain "
                   "the API Key required for authentication.");
    def->cli = "octoprint-apikey=s";
    def->default_value = new ConfigOptionString("");
    
    def = this->add("octoprint_cafile", coString);
    def->label = "HTTPS CA file";
    def->tooltip = "Custom CA certificate file can be specified for HTTPS OctoPrint connections, in crt/pem format. "
                   "If left blank, the default OS CA certificate repository is used.";
    def->cli = "octoprint-cafile=s";
    def->default_value = new ConfigOptionString("");

    def = this->add("octoprint_host", coString);
    def->label = L("Hostname, IP or URL");
    def->tooltip = L("Slic3r can upload G-code files to OctoPrint. This field should contain "
                   "the hostname, IP address or URL of the OctoPrint instance.");
    def->cli = "octoprint-host=s";
    def->default_value = new ConfigOptionString("");

    def = this->add("only_retract_when_crossing_perimeters", coBool);
    def->label = L("Only retract when crossing perimeters");
    def->tooltip = L("Disables retraction when the travel path does not exceed the upper layer's perimeters "
                   "(and thus any ooze will be probably invisible).");
    def->cli = "only-retract-when-crossing-perimeters!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("ooze_prevention", coBool);
    def->label = L("Enable");
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing. "
                   "It will enable a tall skirt automatically and move extruders outside such "
                   "skirt when changing temperatures.");
    def->cli = "ooze-prevention!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("output_filename_format", coString);
    def->label = L("Output filename format");
    def->tooltip = L("You can use all configuration options as variables inside this template. "
                   "For example: [layer_height], [fill_density] etc. You can also use [timestamp], "
                   "[year], [month], [day], [hour], [minute], [second], [version], [input_filename], "
                   "[input_filename_base].");
    def->cli = "output-filename-format=s";
    def->full_width = true;
    def->default_value = new ConfigOptionString("[input_filename_base].gcode");

    def = this->add("overhangs", coBool);
    def->label = L("Detect bridging perimeters");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Experimental option to adjust flow for overhangs (bridge flow will be used), "
                   "to apply bridge speed to them and enable fan.");
    def->cli = "overhangs!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("parking_pos_retraction", coFloat);
    def->label = L("Filament parking position");
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware. ");
    def->sidetext = L("mm");
    def->cli = "parking_pos_retraction=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(92.f);

    def = this->add("perimeter_acceleration", coFloat);
    def->label = L("Perimeters");
    def->tooltip = L("This is the acceleration your printer will use for perimeters. "
                   "A high value like 9000 usually gives good results if your hardware is up to the job. "
                   "Set zero to disable acceleration control for perimeters.");
    def->sidetext = L("mm/s²");
    def->cli = "perimeter-acceleration=f";
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("perimeter_extruder", coInt);
    def->label = L("Perimeter extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing perimeters and brim. First extruder is 1.");
    def->cli = "perimeter-extruder=i";
    def->aliases.push_back("perimeters_extruder");
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("perimeter_extrusion_width", coFloatOrPercent);
    def->label = L("Perimeters");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for perimeters. "
                   "You may want to use thinner extrudates to get more accurate surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%) it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "perimeter-extrusion-width=s";
    def->aliases.push_back("perimeters_extrusion_width");
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("perimeter_speed", coFloat);
    def->label = L("Perimeters");
    def->category = L("Speed");
    def->tooltip = L("Speed for perimeters (contours, aka vertical shells). Set to zero for auto.");
    def->sidetext = L("mm/s");
    def->cli = "perimeter-speed=f";
    def->aliases.push_back("perimeter_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(60);

    def = this->add("perimeters", coInt);
    def->label = L("Perimeters");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("This option sets the number of perimeters to generate for each layer. "
                   "Note that Slic3r may increase this number automatically when it detects "
                   "sloping surfaces which benefit from a higher number of perimeters "
                   "if the Extra Perimeters option is enabled.");
    def->sidetext = L("(minimum)");
    def->cli = "perimeters=i";
    def->aliases.push_back("perimeter_offsets");
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("post_process", coStrings);
    def->label = L("Post-processing scripts");
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Slic3r config settings by reading environment variables.");
    def->cli = "post-process=s@";
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
	def->height = 60;
	def->default_value = new ConfigOptionStrings();

    def = this->add("printer_model", coString);
    def->label = L("Printer type");
    def->tooltip = L("Type of the printer.");
    def->default_value = new ConfigOptionString();

    def = this->add("printer_notes", coString);
    def->label = L("Printer notes");
    def->tooltip = L("You can put your notes regarding the printer here.");
    def->cli = "printer-notes=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 130;
    def->default_value = new ConfigOptionString("");

    def = this->add("printer_vendor", coString);
    def->label = L("Printer vendor");
    def->tooltip = L("Name of the printer vendor.");
    def->default_value = new ConfigOptionString();

    def = this->add("printer_variant", coString);
    def->label = L("Printer variant");
    def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->default_value = new ConfigOptionString();

    def = this->add("print_settings_id", coString);
    def->default_value = new ConfigOptionString("");
    
    def = this->add("printer_settings_id", coString);
    def->default_value = new ConfigOptionString("");

    def = this->add("raft_layers", coInt);
    def->label = L("Raft layers");
    def->category = L("Support material");
    def->tooltip = L("The object will be raised by this number of layers, and support material "
                   "will be generated under it.");
    def->sidetext = L("layers");
    def->cli = "raft-layers=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("resolution", coFloat);
    def->label = L("Resolution");
    def->tooltip = L("Minimum detail resolution, used to simplify the input file for speeding up "
                   "the slicing job and reducing memory usage. High-resolution models often carry "
                   "more detail than printers can render. Set to zero to disable any simplification "
                   "and use full resolution from input.");
    def->sidetext = L("mm");
    def->cli = "resolution=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("retract_before_travel", coFloats);
    def->label = L("Minimum travel after retraction");
    def->tooltip = L("Retraction is not triggered when travel moves are shorter than this length.");
    def->sidetext = L("mm");
    def->cli = "retract-before-travel=f@";
    def->default_value = new ConfigOptionFloats { 2. };

    def = this->add("retract_before_wipe", coPercents);
    def->label = L("Retract amount before wipe");
    def->tooltip = L("With bowden extruders, it may be wise to do some amount of quick retract "
                   "before doing the wipe movement.");
    def->sidetext = L("%");
    def->cli = "retract-before-wipe=s@";
    def->default_value = new ConfigOptionPercents { 0. };
    
    def = this->add("retract_layer_change", coBools);
    def->label = L("Retract on layer change");
    def->tooltip = L("This flag enforces a retraction whenever a Z move is done.");
    def->cli = "retract-layer-change!";
    def->default_value = new ConfigOptionBools { false };

    def = this->add("retract_length", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length");
    def->tooltip = L("When retraction is triggered, filament is pulled back by the specified amount "
                   "(the length is measured on raw filament, before it enters the extruder).");
    def->sidetext = L("mm (zero to disable)");
    def->cli = "retract-length=f@";
    def->default_value = new ConfigOptionFloats { 2. };

    def = this->add("retract_length_toolchange", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length (Toolchange)");
    def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
                   "by the specified amount (the length is measured on raw filament, before it enters "
                   "the extruder).");
    def->sidetext = L("mm (zero to disable)");
    def->cli = "retract-length-toolchange=f@";
    def->default_value = new ConfigOptionFloats { 10. };

    def = this->add("retract_lift", coFloats);
    def->label = L("Lift Z");
    def->tooltip = L("If you set this to a positive value, Z is quickly raised every time a retraction "
                   "is triggered. When using multiple extruders, only the setting for the first extruder "
                   "will be considered.");
    def->sidetext = L("mm");
    def->cli = "retract-lift=f@";
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("retract_lift_above", coFloats);
    def->label = L("Above Z");
    def->full_label = L("Only lift Z above");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified "
                   "absolute Z. You can tune this setting for skipping lift on the first layers.");
    def->sidetext = L("mm");
    def->cli = "retract-lift-above=f@";
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("retract_lift_below", coFloats);
    def->label = L("Below Z");
    def->full_label = L("Only lift Z below");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below "
                   "the specified absolute Z. You can tune this setting for limiting lift "
                   "to the first layers.");
    def->sidetext = L("mm");
    def->cli = "retract-lift-below=f@";
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("retract_restart_extra", coFloats);
    def->label = L("Extra length on restart");
    def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
                   "this additional amount of filament. This setting is rarely needed.");
    def->sidetext = L("mm");
    def->cli = "retract-restart-extra=f@";
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("retract_restart_extra_toolchange", coFloats);
    def->label = L("Extra length on restart");
    def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
                   "this additional amount of filament.");
    def->sidetext = L("mm");
    def->cli = "retract-restart-extra-toolchange=f@";
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("retract_speed", coFloats);
    def->label = L("Retraction Speed");
    def->full_label = L("Retraction Speed");
    def->tooltip = L("The speed for retractions (it only applies to the extruder motor).");
    def->sidetext = L("mm/s");
    def->cli = "retract-speed=f@";
    def->default_value = new ConfigOptionFloats { 40. };

    def = this->add("deretract_speed", coFloats);
    def->label = L("Deretraction Speed");
    def->full_label = L("Deretraction Speed");
    def->tooltip = L("The speed for loading of a filament into extruder after retraction "
                   "(it only applies to the extruder motor). If left to zero, the retraction speed is used.");
    def->sidetext = L("mm/s");
    def->cli = "retract-speed=f@";
    def->default_value = new ConfigOptionFloats { 0. };

    def = this->add("seam_position", coEnum);
    def->label = L("Seam position");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Position of perimeters starting points.");
    def->cli = "seam-position=s";
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("random");
    def->enum_values.push_back("nearest");
    def->enum_values.push_back("aligned");
    def->enum_values.push_back("rear");
    def->enum_labels.push_back("Random");
    def->enum_labels.push_back("Nearest");
    def->enum_labels.push_back("Aligned");
    def->enum_labels.push_back("Rear"); 
    def->default_value = new ConfigOptionEnum<SeamPosition>(spAligned);

#if 0
    def = this->add("seam_preferred_direction", coFloat);
//    def->gui_type = "slider";
    def->label = L("Direction");
    def->sidetext = L("°");
    def->full_label = L("Preferred direction of the seam");
    def->tooltip = L("Seam preferred direction");
    def->cli = "seam-preferred-direction=f";
    def->min = 0;
    def->max = 360;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("seam_preferred_direction_jitter", coFloat);
//    def->gui_type = "slider";
    def->label = L("Jitter");
    def->sidetext = L("°");
    def->full_label = L("Seam preferred direction jitter");
    def->tooltip = L("Preferred direction of the seam - jitter");
    def->cli = "seam-preferred-direction-jitter=f";
    def->min = 0;
    def->max = 360;
    def->default_value = new ConfigOptionFloat(30);
#endif

    def = this->add("serial_port", coString);
    def->gui_type = "select_open";
    def->label = "";
    def->full_label = L("Serial port");
    def->tooltip = L("USB/serial port for printer connection.");
    def->cli = "serial-port=s";
    def->width = 200;
    def->default_value = new ConfigOptionString("");

    def = this->add("serial_speed", coInt);
    def->gui_type = "i_enum_open";
    def->label = L("Speed");
    def->full_label = L("Serial port speed");
    def->tooltip = L("Speed (baud) of USB/serial port for printer connection.");
    def->cli = "serial-speed=i";
    def->min = 1;
    def->max = 300000;
    def->enum_values.push_back("115200");
    def->enum_values.push_back("250000");
    def->default_value = new ConfigOptionInt(250000);

    def = this->add("skirt_distance", coFloat);
    def->label = L("Distance from object");
    def->tooltip = L("Distance between skirt and object(s). Set this to zero to attach the skirt "
                   "to the object(s) and get a brim for better adhesion.");
    def->sidetext = L("mm");
    def->cli = "skirt-distance=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(6);

    def = this->add("skirt_height", coInt);
    def->label = L("Skirt height");
    def->tooltip = L("Height of skirt expressed in layers. Set this to a tall value to use skirt "
                   "as a shield against drafts.");
    def->sidetext = L("layers");
    def->cli = "skirt-height=i";
    def->default_value = new ConfigOptionInt(1);

    def = this->add("skirts", coInt);
    def->label = L("Loops (minimum)");
    def->full_label = L("Skirt Loops");
    def->tooltip = L("Number of loops for the skirt. If the Minimum Extrusion Length option is set, "
                   "the number of loops might be greater than the one configured here. Set this to zero "
                   "to disable skirt completely.");
    def->cli = "skirts=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(1);
    
    def = this->add("slowdown_below_layer_time", coInts);
    def->label = L("Slow down if layer print time is below");
    def->tooltip = L("If layer print time is estimated below this number of seconds, print moves "
                   "speed will be scaled down to extend duration to this value.");
    def->sidetext = L("approximate seconds");
    def->cli = "slowdown-below-layer-time=i@";
    def->width = 60;
    def->min = 0;
    def->max = 1000;
    def->default_value = new ConfigOptionInts { 5 };

    def = this->add("small_perimeter_speed", coFloatOrPercent);
    def->label = L("Small perimeters");
    def->category = L("Speed");
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= 6.5mm "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->cli = "small-perimeter-speed=s";
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(15, false);

    def = this->add("solid_infill_below_area", coFloat);
    def->label = L("Solid infill threshold area");
    def->category = L("Infill");
    def->tooltip = L("Force solid infill for regions having a smaller area than the specified threshold.");
    def->sidetext = L("mm²");
    def->cli = "solid-infill-below-area=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(70);

    def = this->add("solid_infill_extruder", coInt);
    def->label = L("Solid infill extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing solid infill.");
    def->cli = "solid-infill-extruder=i";
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("solid_infill_every_layers", coInt);
    def->label = L("Solid infill every");
    def->category = L("Infill");
    def->tooltip = L("This feature allows to force a solid layer every given number of layers. "
                   "Zero to disable. You can set this to any value (for example 9999); "
                   "Slic3r will automatically choose the maximum possible number of layers "
                   "to combine according to nozzle diameter and layer height.");
    def->sidetext = L("layers");
    def->cli = "solid-infill-every-layers=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("solid_infill_extrusion_width", coFloatOrPercent);
    def->label = L("Solid infill");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for solid surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "solid-infill-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("solid_infill_speed", coFloatOrPercent);
    def->label = L("Solid infill");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing solid regions (top/bottom/internal horizontal shells). "
                   "This can be expressed as a percentage (for example: 80%) over the default "
                   "infill speed above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->cli = "solid-infill-speed=s";
    def->ratio_over = "infill_speed";
    def->aliases.push_back("solid_infill_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(20, false);

    def = this->add("solid_layers", coInt);
    def->label = L("Solid layers");
    def->tooltip = L("Number of solid layers to generate on top and bottom surfaces.");
    def->cli = "solid-layers=i";
    def->shortcut.push_back("top_solid_layers");
    def->shortcut.push_back("bottom_solid_layers");
    def->min = 0;

    def = this->add("spiral_vase", coBool);
    def->label = L("Spiral vase");
    def->tooltip = L("This feature will raise Z gradually while printing a single-walled object "
                   "in order to remove any visible seam. This option requires a single perimeter, "
                   "no infill, no top solid layers and no support material. You can still set "
                   "any number of bottom solid layers as well as skirt/brim loops. "
                   "It won't work when printing more than an object.");
    def->cli = "spiral-vase!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("standby_temperature_delta", coInt);
    def->label = L("Temperature variation");
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                   "Enables a full-height \"sacrificial\" skirt on which the nozzles are periodically wiped.");
	def->sidetext = "∆°C";
    def->cli = "standby-temperature-delta=i";
    def->min = -max_temp;
    def->max = max_temp;
    def->default_value = new ConfigOptionInt(-5);

    def = this->add("start_gcode", coString);
    def->label = L("Start G-code");
    def->tooltip = L("This start procedure is inserted at the beginning, after bed has reached "
                   "the target temperature and extruder just started heating, and before extruder "
                   "has finished heating. If Slic3r detects M104 or M190 in your custom codes, "
                   "such commands will not be prepended automatically so you're free to customize "
                   "the order of heating commands and other custom actions. Note that you can use "
                   "placeholder variables for all Slic3r settings, so you can put "
                   "a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->cli = "start-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n");

    def = this->add("start_filament_gcode", coStrings);
    def->label = L("Start G-code");
    def->tooltip = L("This start procedure is inserted at the beginning, after any printer start gcode. "
                   "This is used to override settings for a specific filament. If Slic3r detects "
                   "M104, M109, M140 or M190 in your custom codes, such commands will "
                   "not be prepended automatically so you're free to customize the order "
                   "of heating commands and other custom actions. Note that you can use placeholder variables "
                   "for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command "
                   "wherever you want. If you have multiple extruders, the gcode is processed "
                   "in extruder order.");
    def->cli = "start-filament-gcode=s@";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionStrings { "; Filament gcode\n" };

    def = this->add("single_extruder_multi_material", coBool);
    def->label = L("Single Extruder Multi Material");
    def->tooltip = L("The printer multiplexes filaments into a single hot end.");
    def->cli = "single-extruder-multi-material!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material", coBool);
    def->label = L("Generate support material");
    def->category = L("Support material");
    def->tooltip = L("Enable support material generation.");
    def->cli = "support-material!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_xy_spacing", coFloatOrPercent);
    def->label = L("XY separation between an object and its support");
    def->category = L("Support material");
    def->tooltip = L("XY separation between an object and its support. If expressed as percentage "
                   "(for example 50%), it will be calculated over external perimeter width.");
    def->sidetext = L("mm or %");
    def->cli = "support-material-xy-spacing=s";
    def->ratio_over = "external_perimeter_extrusion_width";
    def->min = 0;
    // Default is half the external perimeter width.
    def->default_value = new ConfigOptionFloatOrPercent(50, true);

    def = this->add("support_material_angle", coFloat);
    def->label = L("Pattern angle");
    def->category = L("Support material");
    def->tooltip = L("Use this setting to rotate the support material pattern on the horizontal plane.");
    def->sidetext = L("°");
    def->cli = "support-material-angle=f";
    def->min = 0;
    def->max = 359;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("support_material_buildplate_only", coBool);
    def->label = L("Support on build plate only");
    def->category = L("Support material");
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->cli = "support-material-buildplate-only!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_contact_distance", coFloat);
    def->gui_type = "f_enum_open";
    def->label = L("Contact Z distance");
    def->category = L("Support material");
    def->tooltip = L("The vertical distance between object and support material interface. "
                   "Setting this to 0 will also prevent Slic3r from using bridge flow and speed "
                   "for the first object layer.");
    def->sidetext = L("mm");
    def->cli = "support-material-contact-distance=f";
    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("0.2");
    def->enum_labels.push_back("0 (soluble)");
    def->enum_labels.push_back("0.2 (detachable)");
    def->default_value = new ConfigOptionFloat(0.2);

    def = this->add("support_material_enforce_layers", coInt);
    def->label = L("Enforce support for the first");
    def->category = L("Support material");
    def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
                   "regardless of whether normal support material is enabled or not and regardless "
                   "of any angle threshold. This is useful for getting more adhesion of objects "
                   "having a very thin or poor footprint on the build plate.");
    def->sidetext = L("layers");
    def->cli = "support-material-enforce-layers=f";
    def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("support_material_extruder", coInt);
    def->label = L("Support material/raft/skirt extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing support material, raft and skirt "
                   "(1+, 0 to use the current extruder to minimize tool changes).");
    def->cli = "support-material-extruder=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("support_material_extrusion_width", coFloatOrPercent);
    def->label = L("Support material");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for support material. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "support-material-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("support_material_interface_contact_loops", coBool);
    def->label = L("Interface loops");
    def->category = L("Support material");
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->cli = "support-material-interface-contact-loops!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_interface_extruder", coInt);
    def->label = L("Support material/raft interface extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing support material interface "
                   "(1+, 0 to use the current extruder to minimize tool changes). This affects raft too.");
    def->cli = "support-material-interface-extruder=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("support_material_interface_layers", coInt);
    def->label = L("Interface layers");
    def->category = L("Support material");
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material.");
    def->sidetext = L("layers");
    def->cli = "support-material-interface-layers=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("support_material_interface_spacing", coFloat);
    def->label = L("Interface pattern spacing");
    def->category = L("Support material");
    def->tooltip = L("Spacing between interface lines. Set zero to get a solid interface.");
    def->sidetext = L("mm");
    def->cli = "support-material-interface-spacing=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("support_material_interface_speed", coFloatOrPercent);
    def->label = L("Support material interface");
    def->category = L("Support material");
    def->tooltip = L("Speed for printing support material interface layers. If expressed as percentage "
                   "(for example 50%) it will be calculated over support material speed.");
    def->sidetext = L("mm/s or %");
    def->cli = "support-material-interface-speed=s";
    def->ratio_over = "support_material_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(100, true);

    def = this->add("support_material_pattern", coEnum);
    def->label = L("Pattern");
    def->category = L("Support material");
    def->tooltip = L("Pattern used to generate support material.");
    def->cli = "support-material-pattern=s";
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_labels.push_back("rectilinear");
    def->enum_labels.push_back("rectilinear grid");
    def->enum_labels.push_back("honeycomb");
    def->default_value = new ConfigOptionEnum<SupportMaterialPattern>(smpRectilinear);

    def = this->add("support_material_spacing", coFloat);
    def->label = L("Pattern spacing");
    def->category = L("Support material");
    def->tooltip = L("Spacing between support material lines.");
    def->sidetext = L("mm");
    def->cli = "support-material-spacing=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(2.5);

    def = this->add("support_material_speed", coFloat);
    def->label = L("Support material");
    def->category = L("Support material");
    def->tooltip = L("Speed for printing support material.");
    def->sidetext = L("mm/s");
    def->cli = "support-material-speed=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(60);

    def = this->add("support_material_synchronize_layers", coBool);
    def->label = L("Synchronize with object layers");
    def->category = L("Support material");
    def->tooltip = L("Synchronize support layers with the object print layers. This is useful "
                   "with multi-material printers, where the extruder switch is expensive.");
    def->cli = "support-material-synchronize-layers!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_threshold", coInt);
    def->label = L("Overhang threshold");
    def->category = L("Support material");
    def->tooltip = L("Support material will not be generated for overhangs whose slope angle "
                   "(90° = vertical) is above the given threshold. In other words, this value "
                   "represent the most horizontal slope (measured from the horizontal plane) "
                   "that you can print without support material. Set to zero for automatic detection "
                   "(recommended).");
    def->sidetext = L("°");
    def->cli = "support-material-threshold=i";
    def->min = 0;
    def->max = 90;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("support_material_with_sheath", coBool);
    def->label = L("With sheath around the support");
    def->category = L("Support material");
    def->tooltip = L("Add a sheath (a single perimeter line) around the base support. This makes "
                   "the support more reliable, but also more difficult to remove.");
    def->cli = "support-material-with-sheath!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Extruder temperature for layers after the first one. Set this to zero to disable "
                   "temperature control commands in the output.");
    def->cli = "temperature=i@";
    def->full_label = L("Temperature");
    def->max = 0;
    def->max = max_temp;
    def->default_value = new ConfigOptionInts { 200 };
    
    def = this->add("thin_walls", coBool);
    def->label = L("Detect thin walls");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Detect single-width walls (parts where two extrusions don't fit and we need "
                   "to collapse them into a single trace).");
    def->cli = "thin-walls!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("threads", coInt);
    def->label = L("Threads");
    def->tooltip = L("Threads are used to parallelize long-running tasks. Optimal threads number "
                   "is slightly above the number of available cores/processors.");
    def->cli = "threads|j=i";
    def->readonly = true;
    def->min = 1;
    {
        int threads = (unsigned int)boost::thread::hardware_concurrency();
        def->default_value = new ConfigOptionInt(threads > 0 ? threads : 2);
    }
    
    def = this->add("toolchange_gcode", coString);
    def->label = L("Tool change G-code");
    def->tooltip = L("This custom code is inserted right before every extruder change. "
                   "Note that you can use placeholder variables for all Slic3r settings as well "
                   "as [previous_extruder] and [next_extruder].");
    def->cli = "toolchange-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("top_infill_extrusion_width", coFloatOrPercent);
    def->label = L("Top solid infill");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for top surfaces. "
                   "You may want to use thinner extrudates to fill all narrow regions and get a smoother finish. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or % (leave 0 for default)");
    def->cli = "top-infill-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("top_solid_infill_speed", coFloatOrPercent);
    def->label = L("Top solid infill");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing top solid layers (it only applies to the uppermost "
                   "external layers and not to their internal solid layers). You may want "
                   "to slow down this to get a nicer surface finish. This can be expressed "
                   "as a percentage (for example: 80%) over the solid infill speed above. "
                   "Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->cli = "top-solid-infill-speed=s";
    def->ratio_over = "solid_infill_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(15, false);

    def = this->add("top_solid_layers", coInt);
    def->label = L("Top");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Number of solid layers to generate on top surfaces.");
    def->cli = "top-solid-layers=i";
    def->full_label = L("Top solid layers");
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("travel_speed", coFloat);
    def->label = L("Travel");
    def->tooltip = L("Speed for travel moves (jumps between distant extrusion points).");
    def->sidetext = L("mm/s");
    def->cli = "travel-speed=f";
    def->aliases.push_back("travel_feed_rate");
    def->min = 1;
    def->default_value = new ConfigOptionFloat(130);

    def = this->add("use_firmware_retraction", coBool);
    def->label = L("Use firmware retraction");
    def->tooltip = L("This experimental setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. This is only supported in recent Marlin.");
    def->cli = "use-firmware-retraction!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("use_relative_e_distances", coBool);
    def->label = L("Use relative E distances");
    def->tooltip = L("If your firmware requires relative E values, check this, "
                   "otherwise leave it unchecked. Most firmwares use absolute values.");
    def->cli = "use-relative-e-distances!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("use_volumetric_e", coBool);
    def->label = L("Use volumetric E");
    def->tooltip = L("This experimental setting uses outputs the E values in cubic millimeters "
                   "instead of linear millimeters. If your firmware doesn't already know "
                   "filament diameter(s), you can put commands like 'M200 D[filament_diameter_0] T0' "
                   "in your start G-code in order to turn volumetric mode on and use the filament "
                   "diameter associated to the filament selected in Slic3r. This is only supported "
                   "in recent Marlin.");
    def->cli = "use-volumetric-e!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("variable_layer_height", coBool);
    def->label = L("Enable variable layer height feature");
    def->tooltip = L("Some printers or printer setups may have difficulties printing "
                   "with a variable layer height. Enabled by default.");
    def->cli = "variable-layer-height!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("wipe", coBools);
    def->label = L("Wipe while retracting");
    def->tooltip = L("This flag will move the nozzle while retracting to minimize the possible blob "
                   "on leaky extruders.");
    def->cli = "wipe!";
    def->default_value = new ConfigOptionBools { false };

    def = this->add("wipe_tower", coBool);
    def->label = L("Enable");
    def->tooltip = L("Multi material printers may need to prime or purge extruders on tool changes. "
                   "Extrude the excess material into the wipe tower.");
    def->cli = "wipe-tower!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("wiping_volumes_extruders", coFloats);
    def->label = L("Purging volumes - load/unload volumes");
    def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
                     "wipe tower. These values are used to simplify creation of the full purging "
                     "volumes below. ");
    def->cli = "wiping-volumes-extruders=f@";
    def->default_value = new ConfigOptionFloats { 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f  };

    def = this->add("wiping_volumes_matrix", coFloats);
    def->label = L("Purging volumes - matrix");
    def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
                     " new filament on the wipe tower for any given pair of tools. ");
    def->cli = "wiping-volumes-matrix=f@";
    def->default_value = new ConfigOptionFloats {   0.f, 140.f, 140.f, 140.f, 140.f,
                                                  140.f,   0.f, 140.f, 140.f, 140.f,
                                                  140.f, 140.f,   0.f, 140.f, 140.f,
                                                  140.f, 140.f, 140.f,   0.f, 140.f,
                                                  140.f, 140.f, 140.f, 140.f,   0.f };

    def = this->add("wipe_tower_x", coFloat);
    def->label = L("Position X");
    def->tooltip = L("X coordinate of the left front corner of a wipe tower");
    def->sidetext = L("mm");
    def->cli = "wipe-tower-x=f";
    def->default_value = new ConfigOptionFloat(180.);

    def = this->add("wipe_tower_y", coFloat);
    def->label = L("Position Y");
    def->tooltip = L("Y coordinate of the left front corner of a wipe tower");
    def->sidetext = L("mm");
    def->cli = "wipe-tower-y=f";
    def->default_value = new ConfigOptionFloat(140.);

    def = this->add("wipe_tower_width", coFloat);
    def->label = L("Width");
    def->tooltip = L("Width of a wipe tower");
    def->sidetext = L("mm");
    def->cli = "wipe-tower-width=f";
    def->default_value = new ConfigOptionFloat(60.);

    def = this->add("wipe_tower_rotation_angle", coFloat);
    def->label = L("Wipe tower rotation angle");
    def->tooltip = L("Wipe tower rotation angle with respect to x-axis ");
    def->sidetext = L("degrees");
    def->cli = "wipe-tower-rotation-angle=f";
    def->default_value = new ConfigOptionFloat(0.);
    
    def = this->add("wipe_tower_bridging", coFloat);
    def->label = L("Maximal bridging distance");
    def->tooltip = L("Maximal distance between supports on sparse infill sections. ");
    def->sidetext = L("mm");
    def->cli = "wipe-tower-bridging=f";
    def->default_value = new ConfigOptionFloat(10.);

    def = this->add("xy_size_compensation", coFloat);
    def->label = L("XY Size Compensation");
    def->category = L("Advanced");
    def->tooltip = L("The object will be grown/shrunk in the XY plane by the configured value "
                   "(negative = inwards, positive = outwards). This might be useful "
                   "for fine-tuning hole sizes.");
    def->sidetext = L("mm");
    def->cli = "xy-size-compensation=f";
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("z_offset", coFloat);
    def->label = L("Z offset");
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->sidetext = L("mm");
    def->cli = "z-offset=f";
    def->default_value = new ConfigOptionFloat(0);
}

void PrintConfigDef::handle_legacy(t_config_option_key &opt_key, std::string &value)
{
    // handle legacy options
    if (opt_key == "extrusion_width_ratio" || opt_key == "bottom_layer_speed_ratio"
        || opt_key == "first_layer_height_ratio") {
        boost::replace_first(opt_key, "_ratio", "");
        if (opt_key == "bottom_layer_speed") opt_key = "first_layer_speed";
        try {
            float v = boost::lexical_cast<float>(value);
            if (v != 0) 
                value = boost::lexical_cast<std::string>(v*100) + "%";
        } catch (boost::bad_lexical_cast &) {
            value = "0";
        }
    } else if (opt_key == "gcode_flavor" && value == "makerbot") {
        value = "makerware";
    } else if (opt_key == "fill_density" && value.find("%") == std::string::npos) {
        try {
            // fill_density was turned into a percent value
            float v = boost::lexical_cast<float>(value);
            value = boost::lexical_cast<std::string>(v*100) + "%";
        } catch (boost::bad_lexical_cast &) {}
    } else if (opt_key == "randomize_start" && value == "1") {
        opt_key = "seam_position";
        value = "random";
    } else if (opt_key == "bed_size" && !value.empty()) {
        opt_key = "bed_shape";
        ConfigOptionPoint p;
        p.deserialize(value);
        std::ostringstream oss;
        oss << "0x0," << p.value.x << "x0," << p.value.x << "x" << p.value.y << ",0x" << p.value.y;
        value = oss.str();
    } else if (opt_key == "octoprint_host" && !value.empty()) {
        opt_key = "print_host";
    } else if ((opt_key == "perimeter_acceleration" && value == "25")
        || (opt_key == "infill_acceleration" && value == "50")) {
        /*  For historical reasons, the world's full of configs having these very low values;
            to avoid unexpected behavior we need to ignore them. Banning these two hard-coded
            values is a dirty hack and will need to be removed sometime in the future, but it
            will avoid lots of complaints for now. */
        value = "0";
    } else if (opt_key == "support_material_pattern" && value == "pillars") {
        // Slic3r PE does not support the pillars. They never worked well.
        value = "rectilinear";
    } else if (opt_key == "support_material_threshold" && value == "0") {
        // 0 used to be automatic threshold, but we introduced percent values so let's
        // transform it into the default value
        value = "60%";
    }
    
    // Ignore the following obsolete configuration keys:
    static std::set<std::string> ignore = {
        "duplicate_x", "duplicate_y", "gcode_arcs", "multiply_x", "multiply_y",
        "support_material_tool", "acceleration", "adjust_overhang_flow", 
        "standby_temperature", "scale", "rotate", "duplicate", "duplicate_grid",
        "start_perimeters_at_concave_points", "start_perimeters_at_non_overhang", "randomize_start", 
        "seal_position", "vibration_limit", "bed_size", "octoprint_host",
        "print_center", "g0", "threads", "pressure_advance", "wipe_tower_per_color_wipe"
    };

    if (ignore.find(opt_key) != ignore.end()) {
        opt_key = "";
        return;
    }
    
    if (! print_config_def.has(opt_key)) {
        //printf("Unknown option %s\n", opt_key.c_str());
        opt_key = "";
        return;
    }
}

PrintConfigDef print_config_def;

DynamicPrintConfig* DynamicPrintConfig::new_from_defaults()
{
    return new_from_defaults_keys(FullPrintConfig::defaults().keys());
}

DynamicPrintConfig* DynamicPrintConfig::new_from_defaults_keys(const std::vector<std::string> &keys)
{
    auto *out = new DynamicPrintConfig();
    out->apply_only(FullPrintConfig::defaults(), keys);
    return out;
}

void DynamicPrintConfig::normalize()
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("infill_extruder"))
                this->option("infill_extruder", true)->setInt(extruder);
            if (!this->has("perimeter_extruder"))
                this->option("perimeter_extruder", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_material_extruder"))
            //     this->option("support_material_extruder", true)->setInt(extruder);
            // if (!this->has("support_material_interface_extruder"))
            //     this->option("support_material_interface_extruder", true)->setInt(extruder);
        }
    }
    
    if (!this->has("solid_infill_extruder") && this->has("infill_extruder"))
        this->option("solid_infill_extruder", true)->setInt(this->option("infill_extruder")->getInt());
    
    if (this->has("spiral_vase") && this->opt<ConfigOptionBool>("spiral_vase", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            ConfigOptionBools* opt = this->opt<ConfigOptionBools>("retract_layer_change", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
        }
        {
            this->opt<ConfigOptionInt>("perimeters", true)->value       = 1;
            this->opt<ConfigOptionInt>("top_solid_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("fill_density", true)->value  = 0;
        }
    }
}

std::string DynamicPrintConfig::validate()
{
    // Full print config is initialized from the defaults.
    FullPrintConfig fpc;
    fpc.apply(*this, true);
    // Verify this print options through the FullPrintConfig.
    return fpc.validate();
}

double PrintConfig::min_object_distance() const
{
    return PrintConfig::min_object_distance(static_cast<const ConfigBase*>(this));
}

double PrintConfig::min_object_distance(const ConfigBase *config)
{
    double extruder_clearance_radius = config->option("extruder_clearance_radius")->getFloat();
    double duplicate_distance = config->option("duplicate_distance")->getFloat();
    
    // min object distance is max(duplicate_distance, clearance_radius)
    return (config->option("complete_objects")->getBool() && extruder_clearance_radius > duplicate_distance)
        ? extruder_clearance_radius
        : duplicate_distance;
}

std::string FullPrintConfig::validate()
{
    // --layer-height
    if (this->get_abs_value("layer_height") <= 0)
        return "Invalid value for --layer-height";
    if (fabs(fmod(this->get_abs_value("layer_height"), SCALING_FACTOR)) > 1e-4)
        return "--layer-height must be a multiple of print resolution";

    // --first-layer-height
    if (this->get_abs_value("first_layer_height") <= 0)
        return "Invalid value for --first-layer-height";

    // --filament-diameter
    for (double fd : this->filament_diameter.values)
        if (fd < 1)
            return "Invalid value for --filament-diameter";

    // --nozzle-diameter
    for (double nd : this->nozzle_diameter.values)
        if (nd < 0.005)
            return "Invalid value for --nozzle-diameter";
    
    // --perimeters
    if (this->perimeters.value < 0)
        return "Invalid value for --perimeters";

    // --solid-layers
    if (this->top_solid_layers < 0)
        return "Invalid value for --top-solid-layers";
    if (this->bottom_solid_layers < 0)
        return "Invalid value for --bottom-solid-layers";
    
    if (this->use_firmware_retraction.value && 
        this->gcode_flavor.value != gcfSmoothie &&
        this->gcode_flavor.value != gcfRepRap &&
        this->gcode_flavor.value != gcfMarlin &&
        this->gcode_flavor.value != gcfMachinekit &&
        this->gcode_flavor.value != gcfRepetier)
        return "--use-firmware-retraction is only supported by Marlin, Smoothie, Repetier and Machinekit firmware";

    if (this->use_firmware_retraction.value)
        for (bool wipe : this->wipe.values)
             if (wipe)
                return "--use-firmware-retraction is not compatible with --wipe";
        
    // --gcode-flavor
    if (! print_config_def.get("gcode_flavor")->has_enum_value(this->gcode_flavor.serialize()))
        return "Invalid value for --gcode-flavor";
    
    // --fill-pattern
    if (! print_config_def.get("fill_pattern")->has_enum_value(this->fill_pattern.serialize()))
        return "Invalid value for --fill-pattern";
    
    // --external-fill-pattern
    if (! print_config_def.get("external_fill_pattern")->has_enum_value(this->external_fill_pattern.serialize()))
        return "Invalid value for --external-fill-pattern";

    // --fill-density
    if (fabs(this->fill_density.value - 100.) < EPSILON &&
        ! print_config_def.get("external_fill_pattern")->has_enum_value(this->fill_pattern.serialize()))
        return "The selected fill pattern is not supposed to work at 100% density";
    
    // --infill-every-layers
    if (this->infill_every_layers < 1)
        return "Invalid value for --infill-every-layers";

    // --skirt-height
    if (this->skirt_height < -1) // -1 means as tall as the object
        return "Invalid value for --skirt-height";
    
    // --bridge-flow-ratio
    if (this->bridge_flow_ratio <= 0)
        return "Invalid value for --bridge-flow-ratio";
    
    // extruder clearance
    if (this->extruder_clearance_radius <= 0)
        return "Invalid value for --extruder-clearance-radius";
    if (this->extruder_clearance_height <= 0)
        return "Invalid value for --extruder-clearance-height";

    // --extrusion-multiplier
    for (float em : this->extrusion_multiplier.values)
        if (em <= 0)
            return "Invalid value for --extrusion-multiplier";

    // --default-acceleration
    if ((this->perimeter_acceleration != 0. || this->infill_acceleration != 0. || this->bridge_acceleration != 0. || this->first_layer_acceleration != 0.) &&
        this->default_acceleration == 0.)
        return "Invalid zero value for --default-acceleration when using other acceleration settings";

    // --spiral-vase
    if (this->spiral_vase) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (this->perimeters > 1)
            return "Can't make more than one perimeter when spiral vase mode is enabled";
        else if (this->perimeters < 1)
            return "Can't make less than one perimeter when spiral vase mode is enabled";
        if (this->fill_density > 0)
            return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        if (this->top_solid_layers > 0)
            return "Spiral vase mode is not compatible with top solid layers";
        if (this->support_material || this->support_material_enforce_layers > 0)
            return "Spiral vase mode is not compatible with support material";
    }
    
    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        for (double dmr : this->nozzle_diameter.values)
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        const char *widths[] = { "external_perimeter", "perimeter", "infill", "solid_infill", "top_infill", "support_material", "first_layer" };
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            key += "_extrusion_width";
            if (this->get_abs_value(key, max_nozzle_diameter) > 10. * max_nozzle_diameter)
                return std::string("Invalid extrusion width (too large): ") + key;
        }
    }

    // Out of range validation of numeric values.
    for (const std::string &opt_key : this->keys()) {
        const ConfigOption      *opt    = this->optptr(opt_key);
        assert(opt != nullptr);
        const ConfigOptionDef   *optdef = print_config_def.get(opt_key);
        assert(optdef != nullptr);
        bool out_of_range = false;
        switch (opt->type()) {
        case coFloat:
        case coPercent:
        case coFloatOrPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloat*>(opt);
            out_of_range = fopt->value < optdef->min || fopt->value > optdef->max;
            break;
        }
        case coFloats:
        case coPercents:
            for (double v : static_cast<const ConfigOptionFloats*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        case coInt:
        {
            auto *iopt = static_cast<const ConfigOptionInt*>(opt);
            out_of_range = iopt->value < optdef->min || iopt->value > optdef->max;
            break;
        }
        case coInts:
            for (int v : static_cast<const ConfigOptionInts*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        default:;
        }
        if (out_of_range)
            return std::string("Value out of range: " + opt_key);
    }
    
    // The configuration is valid.
    return "";
}

// Declare the static caches for each StaticPrintConfig derived class.
StaticPrintConfig::StaticCache<class Slic3r::PrintObjectConfig> PrintObjectConfig::s_cache_PrintObjectConfig;
StaticPrintConfig::StaticCache<class Slic3r::PrintRegionConfig> PrintRegionConfig::s_cache_PrintRegionConfig;
StaticPrintConfig::StaticCache<class Slic3r::GCodeConfig>       GCodeConfig::s_cache_GCodeConfig;
StaticPrintConfig::StaticCache<class Slic3r::PrintConfig>       PrintConfig::s_cache_PrintConfig;
StaticPrintConfig::StaticCache<class Slic3r::HostConfig>        HostConfig::s_cache_HostConfig;
StaticPrintConfig::StaticCache<class Slic3r::FullPrintConfig>   FullPrintConfig::s_cache_FullPrintConfig;

}
