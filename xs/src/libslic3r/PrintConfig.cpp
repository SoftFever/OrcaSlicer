#include "PrintConfig.hpp"
#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

namespace Slic3r {

PrintConfigDef::PrintConfigDef()
{
    t_optiondef_map &Options = this->options;
    
    ConfigOptionDef* def;
    
    def = this->add("avoid_crossing_perimeters", coBool);
    def->label = "Avoid crossing perimeters";
    def->tooltip = "Optimize travel moves in order to minimize the crossing of perimeters. This is mostly useful with Bowden extruders which suffer from oozing. This feature slows down both the print and the G-code generation.";
    def->cli = "avoid-crossing-perimeters!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("bed_shape", coPoints);
    def->label = "Bed shape";
    {
        ConfigOptionPoints* opt = new ConfigOptionPoints();
        opt->values.push_back(Pointf(0,0));
        opt->values.push_back(Pointf(200,0));
        opt->values.push_back(Pointf(200,200));
        opt->values.push_back(Pointf(0,200));
        def->default_value = opt;
    }
    
    def = this->add("bed_temperature", coInts);
    def->label = "Other layers";
    def->tooltip = "Bed temperature for layers after the first one. Set this to zero to disable bed temperature control commands in the output.";
    def->cli = "bed-temperature=i@";
    def->full_label = "Bed temperature";
    def->min = 0;
    def->max = 300;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("before_layer_gcode", coString);
    def->label = "Before layer change G-code";
    def->tooltip = "This custom code is inserted at every layer change, right before the Z move. Note that you can use placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].";
    def->cli = "before-layer-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("bottom_solid_layers", coInt);
    def->label = "Bottom";
    def->category = "Layers and Perimeters";
    def->tooltip = "Number of solid layers to generate on bottom surfaces.";
    def->cli = "bottom-solid-layers=i";
    def->full_label = "Bottom solid layers";
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("bridge_acceleration", coFloat);
    def->label = "Bridge";
    def->tooltip = "This is the acceleration your printer will use for bridges. Set zero to disable acceleration control for bridges.";
    def->sidetext = "mm/s²";
    def->cli = "bridge-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("bridge_angle", coFloat);
    def->label = "Bridging angle";
    def->category = "Infill";
    def->tooltip = "Bridging angle override. If left to zero, the bridging angle will be calculated automatically. Otherwise the provided angle will be used for all bridges. Use 180° for zero angle.";
    def->sidetext = "°";
    def->cli = "bridge-angle=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0.);

    def = this->add("bridge_fan_speed", coInts);
    def->label = "Bridges fan speed";
    def->tooltip = "This fan speed is enforced during all bridges and overhangs.";
    def->sidetext = "%";
    def->cli = "bridge-fan-speed=i@";
    def->min = 0;
    def->max = 100;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(100);
        def->default_value = opt;
    }

    def = this->add("bridge_flow_ratio", coFloat);
    def->label = "Bridge flow ratio";
    def->category = "Advanced";
    def->tooltip = "This factor affects the amount of plastic for bridging. You can decrease it slightly to pull the extrudates and prevent sagging, although default settings are usually good and you should experiment with cooling (use a fan) before tweaking this.";
    def->cli = "bridge-flow-ratio=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(1);

    def = this->add("bridge_speed", coFloat);
    def->label = "Bridges";
    def->category = "Speed";
    def->tooltip = "Speed for printing bridges.";
    def->sidetext = "mm/s";
    def->cli = "bridge-speed=f";
    def->aliases.push_back("bridge_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(60);

    def = this->add("brim_width", coFloat);
    def->label = "Brim width";
    def->tooltip = "Horizontal width of the brim that will be printed around each object on the first layer.";
    def->sidetext = "mm";
    def->cli = "brim-width=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("clip_multipart_objects", coBool);
    def->label = "Clip multi-part objects";
    def->tooltip = "When printing multi-material objects, this settings will make slic3r to clip the overlapping object parts one by the other (2nd part will be clipped by the 1st, 3rd part will be clipped by the 1st and 2nd etc).";
    def->cli = "clip-multipart-objects!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("compatible_printers", coStrings);
    def->label = "Compatible printers";
    def->default_value = new ConfigOptionStrings();

    def = this->add("complete_objects", coBool);
    def->label = "Complete individual objects";
    def->tooltip = "When printing multiple objects or copies, this feature will complete each object before moving onto next one (and starting it from its bottom layer). This feature is useful to avoid the risk of ruined prints. Slic3r should warn and prevent you from extruder collisions, but beware.";
    def->cli = "complete-objects!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("cooling", coBools);
    def->label = "Enable auto cooling";
    def->tooltip = "This flag enables the automatic cooling logic that adjusts print speed and fan speed according to layer printing time.";
    def->cli = "cooling!";
    {
        ConfigOptionBools* opt = new ConfigOptionBools();
        opt->values.push_back(true);
        def->default_value = opt;
    }

    def = this->add("default_acceleration", coFloat);
    def->label = "Default";
    def->tooltip = "This is the acceleration your printer will be reset to after the role-specific acceleration values are used (perimeter/infill). Set zero to prevent resetting acceleration at all.";
    def->sidetext = "mm/s²";
    def->cli = "default-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("disable_fan_first_layers", coInts);
    def->label = "Disable fan for the first";
    def->tooltip = "You can set this to a positive value to disable fan at all during the first layers, so that it does not make adhesion worse.";
    def->sidetext = "layers";
    def->cli = "disable-fan-first-layers=i@";
    def->min = 0;
    def->max = 1000;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(3);
        def->default_value = opt;
    }

    def = this->add("dont_support_bridges", coBool);
    def->label = "Don't support bridges";
    def->category = "Support material";
    def->tooltip = "Experimental option for preventing support material from being generated under bridged areas.";
    def->cli = "dont-support-bridges!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("duplicate_distance", coFloat);
    def->label = "Distance between copies";
    def->tooltip = "Distance used for the auto-arrange feature of the plater.";
    def->sidetext = "mm";
    def->cli = "duplicate-distance=f";
    def->aliases.push_back("multiply_distance");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(6);

    def = this->add("elefant_foot_compensation", coFloat);
    def->label = "Elefant foot compensation";
    def->category = "Advanced";
    def->tooltip = "The first layer will be shrunk in the XY plane by the configured value to compensate for the 1st layer squish aka an Elefant Foot effect.";
    def->sidetext = "mm";
    def->cli = "elefant-foot-compensation=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("end_gcode", coString);
    def->label = "End G-code";
    def->tooltip = "This end procedure is inserted at the end of the output file. Note that you can use placeholder variables for all Slic3r settings.";
    def->cli = "end-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n");

    def = this->add("end_filament_gcode", coStrings);
    def->label = "End G-code";
    def->tooltip = "This end procedure is inserted at the end of the output file, before the printer end gcode. Note that you can use placeholder variables for all Slic3r settings. If you have multiple extruders, the gcode is processed in extruder order.";
    def->cli = "end-filament-gcode=s@";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    {
        ConfigOptionStrings* opt = new ConfigOptionStrings();
        opt->values.push_back("; Filament-specific end gcode \n;END gcode for filament\n");
        def->default_value = opt;
    }

    def = this->add("ensure_vertical_shell_thickness", coBool);
    def->label = "Ensure vertical shell thickness";
    def->category = "Layers and Perimeters";
    def->tooltip = "Add solid infill near sloping surfaces to guarantee the vertical shell thickness (top+bottom solid layers).";
    def->cli = "ensure-vertical-shell-thickness!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("external_fill_pattern", coEnum);
    def->label = "Top/bottom fill pattern";
    def->category = "Infill";
    def->tooltip = "Fill pattern for top/bottom infill. This only affects the external visible layer, and not its adjacent solid shells.";
    def->cli = "external-fill-pattern|solid-fill-pattern=s";
    def->enum_keys_map = ConfigOptionEnum<InfillPattern>::get_enum_values();
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
    def->aliases.push_back("solid_fill_pattern");
    def->default_value = new ConfigOptionEnum<InfillPattern>(ipRectilinear);

    def = this->add("external_perimeter_extrusion_width", coFloatOrPercent);
    def->label = "External perimeters";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for external perimeters. If left zero, an automatic value will be used that maximizes accuracy of the external visible surfaces. If expressed as percentage (for example 200%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "external-perimeter-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("external_perimeter_speed", coFloatOrPercent);
    def->label = "External perimeters";
    def->category = "Speed";
    def->tooltip = "This separate setting will affect the speed of external perimeters (the visible ones). If expressed as percentage (for example: 80%) it will be calculated on the perimeters speed setting above. Set to zero for auto.";
    def->sidetext = "mm/s or %";
    def->cli = "external-perimeter-speed=s";
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(50, true);

    def = this->add("external_perimeters_first", coBool);
    def->label = "External perimeters first";
    def->category = "Layers and Perimeters";
    def->tooltip = "Print contour perimeters from the outermost one to the innermost one instead of the default inverse order.";
    def->cli = "external-perimeters-first!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("extra_perimeters", coBool);
    def->label = "Extra perimeters if needed";
    def->category = "Layers and Perimeters";
    def->tooltip = "Add more perimeters when needed for avoiding gaps in sloping walls. Slic3r keeps adding perimeters, until more than 70% of the loop immediately above is supported.";
    def->cli = "extra-perimeters!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("extruder", coInt);
    def->gui_type = "i_enum_open";
    def->label = "Extruder";
    def->category = "Extruders";
    def->tooltip = "The extruder to use (unless more specific extruder settings are specified). This value overrides perimeter and infill extruders, but not the support extruders.";
    def->cli = "extruder=i";
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back("default");  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");

    def = this->add("extruder_clearance_height", coFloat);
    def->label = "Height";
    def->tooltip = "Set this to the vertical distance between your nozzle tip and (usually) the X carriage rods. In other words, this is the height of the clearance cylinder around your extruder, and it represents the maximum depth the extruder can peek before colliding with other printed objects.";
    def->sidetext = "mm";
    def->cli = "extruder-clearance-height=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(20);

    def = this->add("extruder_clearance_radius", coFloat);
    def->label = "Radius";
    def->tooltip = "Set this to the clearance radius around your extruder. If the extruder is not centered, choose the largest value for safety. This setting is used to check for collisions and to display the graphical preview in the plater.";
    def->sidetext = "mm";
    def->cli = "extruder-clearance-radius=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(20);

    def = this->add("extruder_colour", coStrings);
    def->label = "Extruder Color";
    def->tooltip = "This is only used in the Slic3r interface as a visual help.";
    def->cli = "extruder-color=s@";
    def->gui_type = "color";
    {
        ConfigOptionStrings* opt = new ConfigOptionStrings();
        // Empty string means no color assigned yet.
//        opt->values.push_back("#FFFFFF");
        opt->values.push_back("");
        def->default_value = opt;
    }

    def = this->add("extruder_offset", coPoints);
    def->label = "Extruder offset";
    def->tooltip = "If your firmware doesn't handle the extruder displacement you need the G-code to take it into account. This option lets you specify the displacement of each extruder with respect to the first one. It expects positive coordinates (they will be subtracted from the XY coordinate).";
    def->sidetext = "mm";
    def->cli = "extruder-offset=s@";
    {
        ConfigOptionPoints* opt = new ConfigOptionPoints();
        opt->values.push_back(Pointf(0,0));
        def->default_value = opt;
    }

    def = this->add("extrusion_axis", coString);
    def->label = "Extrusion axis";
    def->tooltip = "Use this option to set the axis letter associated to your printer's extruder (usually E but some printers use A).";
    def->cli = "extrusion-axis=s";
    def->default_value = new ConfigOptionString("E");

    def = this->add("extrusion_multiplier", coFloats);
    def->label = "Extrusion multiplier";
    def->tooltip = "This factor changes the amount of flow proportionally. You may need to tweak this setting to get nice surface finish and correct single wall widths. Usual values are between 0.9 and 1.1. If you think you need to change this more, check filament diameter and your firmware E steps.";
    def->cli = "extrusion-multiplier=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(1);
        def->default_value = opt;
    }
    
    def = this->add("extrusion_width", coFloatOrPercent);
    def->label = "Default extrusion width";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width. If left to zero, Slic3r calculates a width automatically. If expressed as percentage (for example: 230%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for auto)";
    def->cli = "extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("fan_always_on", coBools);
    def->label = "Keep fan always on";
    def->tooltip = "If this is enabled, fan will never be disabled and will be kept running at least at its minimum speed. Useful for PLA, harmful for ABS.";
    def->cli = "fan-always-on!";
    {
        ConfigOptionBools* opt = new ConfigOptionBools();
        opt->values.push_back(false);
        def->default_value = opt;
    }

    def = this->add("fan_below_layer_time", coInts);
    def->label = "Enable fan if layer print time is below";
    def->tooltip = "If layer print time is estimated below this number of seconds, fan will be enabled and its speed will be calculated by interpolating the minimum and maximum speeds.";
    def->sidetext = "approximate seconds";
    def->cli = "fan-below-layer-time=i@";
    def->width = 60;
    def->min = 0;
    def->max = 1000;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(60);
        def->default_value = opt;
    }

    def = this->add("filament_colour", coStrings);
    def->label = "Color";
    def->tooltip = "This is only used in the Slic3r interface as a visual help.";
    def->cli = "filament-color=s@";
    def->gui_type = "color";
    {
        ConfigOptionStrings* opt = new ConfigOptionStrings();
        opt->values.push_back("#FFFFFF");
        def->default_value = opt;
    }

    def = this->add("filament_notes", coStrings);
    def->label = "Filament notes";
    def->tooltip = "You can put your notes regarding the filament here.";
    def->cli = "filament-notes=s@";
    def->multiline = true;
    def->full_width = true;
    def->height = 130;
    {
        ConfigOptionStrings* opt = new ConfigOptionStrings();
        opt->values.push_back("");
        def->default_value = opt;
    }

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = "Max volumetric speed";
    def->tooltip = "Maximum volumetric speed allowed for this filament. Limits the maximum volumetric speed of a print to the minimum of print and filament volumetric speed. Set to zero for no limit.";
    def->sidetext = "mm³/s";
    def->cli = "filament-max-volumetric-speed=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0.f);
        def->default_value = opt;
    }

    def = this->add("filament_diameter", coFloats);
    def->label = "Diameter";
    def->tooltip = "Enter your filament diameter here. Good precision is required, so use a caliper and do multiple measurements along the filament, then compute the average.";
    def->sidetext = "mm";
    def->cli = "filament-diameter=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(3);
        def->default_value = opt;
    }

    def = this->add("filament_density", coFloats);
    def->label = "Density";
    def->tooltip = "Enter your filament density here. This is only for statistical information. A decent way is to weigh a known length of filament and compute the ratio of the length to volume. Better is to calculate the volume directly through displacement.";
    def->sidetext = "g/cm^3";
    def->cli = "filament-density=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("filament_type", coStrings);
    def->label = "Filament type";
    def->tooltip = "If you want to process the output G-code through custom scripts, just list their absolute paths here. Separate multiple scripts with a semicolon. Scripts will be passed the absolute path to the G-code file as the first argument, and they can access the Slic3r config settings by reading environment variables.";
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
    {
        ConfigOptionStrings* opt = new ConfigOptionStrings();
        opt->values.push_back("PLA");
        def->default_value = opt;
    }

    def = this->add("filament_soluble", coBools);
    def->label = "Soluble material";
    def->tooltip = "Soluble material is most likely used for a soluble support.";
    def->cli = "filament-soluble!";
    {
        ConfigOptionBools* opt = new ConfigOptionBools();
        opt->values.push_back(false);
        def->default_value = opt;
    }

    def = this->add("filament_cost", coFloats);
    def->label = "Cost";
    def->tooltip = "Enter your filament cost per kg here. This is only for statistical information.";
    def->sidetext = "money/kg";
    def->cli = "filament-cost=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }
    
    def = this->add("filament_settings_id", coString);
    def->default_value = new ConfigOptionString("");
    
    def = this->add("fill_angle", coFloat);
    def->label = "Fill angle";
    def->category = "Infill";
    def->tooltip = "Default base angle for infill orientation. Cross-hatching will be applied to this. Bridges will be infilled using the best direction Slic3r can detect, so this setting does not affect them.";
    def->sidetext = "°";
    def->cli = "fill-angle=f";
    def->min = 0;
    def->max = 360;
    def->default_value = new ConfigOptionFloat(45);

    def = this->add("fill_density", coPercent);
    def->gui_type = "f_enum_open";
    def->gui_flags = "show_value";
    def->label = "Fill density";
    def->category = "Infill";
    def->tooltip = "Density of internal infill, expressed in the range 0% - 100%.";
    def->sidetext = "%";
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
    def->label = "Fill pattern";
    def->category = "Infill";
    def->tooltip = "Fill pattern for general low-density infill.";
    def->cli = "fill-pattern=s";
    def->enum_keys_map = ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("stars");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("line");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("3dhoneycomb");
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
    def->enum_labels.push_back("Hilbert Curve");
    def->enum_labels.push_back("Archimedean Chords");
    def->enum_labels.push_back("Octagram Spiral");
    def->default_value = new ConfigOptionEnum<InfillPattern>(ipStars);

    def = this->add("first_layer_acceleration", coFloat);
    def->label = "First layer";
    def->tooltip = "This is the acceleration your printer will use for first layer. Set zero to disable acceleration control for first layer.";
    def->sidetext = "mm/s²";
    def->cli = "first-layer-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("first_layer_bed_temperature", coInts);
    def->label = "First layer";
    def->tooltip = "Heated build plate temperature for the first layer. Set this to zero to disable bed temperature control commands in the output.";
    def->cli = "first-layer-bed-temperature=i@";
    def->max = 0;
    def->max = 300;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("first_layer_extrusion_width", coFloatOrPercent);
    def->label = "First layer";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for first layer. You can use this to force fatter extrudates for better adhesion. If expressed as percentage (for example 120%) it will be computed over first layer height. If set to zero, it will use the Default Extrusion Width.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "first-layer-extrusion-width=s";
    def->ratio_over = "first_layer_height";
    def->default_value = new ConfigOptionFloatOrPercent(200, true);

    def = this->add("first_layer_height", coFloatOrPercent);
    def->label = "First layer height";
    def->category = "Layers and Perimeters";
    def->tooltip = "When printing with very low layer heights, you might still want to print a thicker bottom layer to improve adhesion and tolerance for non perfect build plates. This can be expressed as an absolute value or as a percentage (for example: 150%) over the default layer height.";
    def->sidetext = "mm or %";
    def->cli = "first-layer-height=s";
    def->ratio_over = "layer_height";
    def->default_value = new ConfigOptionFloatOrPercent(0.35, false);

    def = this->add("first_layer_speed", coFloatOrPercent);
    def->label = "First layer speed";
    def->tooltip = "If expressed as absolute value in mm/s, this speed will be applied to all the print moves of the first layer, regardless of their type. If expressed as a percentage (for example: 40%) it will scale the default speeds.";
    def->sidetext = "mm/s or %";
    def->cli = "first-layer-speed=s";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(30, false);

    def = this->add("first_layer_temperature", coInts);
    def->label = "First layer";
    def->tooltip = "Extruder temperature for first layer. If you want to control temperature manually during print, set this to zero to disable temperature control commands in the output file.";
    def->cli = "first-layer-temperature=i@";
    def->min = 0;
    def->max = 500;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(200);
        def->default_value = opt;
    }
    
    def = this->add("gap_fill_speed", coFloat);
    def->label = "Gap fill";
    def->category = "Speed";
    def->tooltip = "Speed for filling small gaps using short zigzag moves. Keep this reasonably low to avoid too much shaking and resonance issues. Set zero to disable gaps filling.";
    def->sidetext = "mm/s";
    def->cli = "gap-fill-speed=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(20);

    def = this->add("gcode_comments", coBool);
    def->label = "Verbose G-code";
    def->tooltip = "Enable this to get a commented G-code file, with each line explained by a descriptive text. If you print from SD card, the additional weight of the file could make your firmware slow down.";
    def->cli = "gcode-comments!";
    def->default_value = new ConfigOptionBool(0);

    def = this->add("gcode_flavor", coEnum);
    def->label = "G-code flavor";
    def->tooltip = "Some G/M-code commands, including temperature control and others, are not universal. Set this option to your printer's firmware to get a compatible output. The \"No extrusion\" flavor prevents Slic3r from exporting any extrusion value at all.";
    def->cli = "gcode-flavor=s";
    def->enum_keys_map = ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("reprap");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("teacup");
    def->enum_values.push_back("makerware");
    def->enum_values.push_back("sailfish");
    def->enum_values.push_back("mach3");
    def->enum_values.push_back("machinekit");
    def->enum_values.push_back("smoothie");
    def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back("RepRap (Marlin/Sprinter)");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("Teacup");
    def->enum_labels.push_back("MakerWare (MakerBot)");
    def->enum_labels.push_back("Sailfish (MakerBot)");
    def->enum_labels.push_back("Mach3/LinuxCNC");
    def->enum_labels.push_back("Machinekit");
    def->enum_labels.push_back("Smoothie");
    def->enum_labels.push_back("No extrusion");
    def->default_value = new ConfigOptionEnum<GCodeFlavor>(gcfRepRap);

    def = this->add("infill_acceleration", coFloat);
    def->label = "Infill";
    def->tooltip = "This is the acceleration your printer will use for infill. Set zero to disable acceleration control for infill.";
    def->sidetext = "mm/s²";
    def->cli = "infill-acceleration=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("infill_every_layers", coInt);
    def->label = "Combine infill every";
    def->category = "Infill";
    def->tooltip = "This feature allows to combine infill and speed up your print by extruding thicker infill layers while preserving thin perimeters, thus accuracy.";
    def->sidetext = "layers";
    def->cli = "infill-every-layers=i";
    def->full_label = "Combine infill every n layers";
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("infill_extruder", coInt);
    def->label = "Infill extruder";
    def->category = "Extruders";
    def->tooltip = "The extruder to use when printing infill.";
    def->cli = "infill-extruder=i";
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("infill_extrusion_width", coFloatOrPercent);
    def->label = "Infill";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for infill. You may want to use fatter extrudates to speed up the infill and make your parts stronger. If expressed as percentage (for example 90%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "infill-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("infill_first", coBool);
    def->label = "Infill before perimeters";
    def->tooltip = "This option will switch the print order of perimeters and infill, making the latter first.";
    def->cli = "infill-first!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("infill_only_where_needed", coBool);
    def->label = "Only infill where needed";
    def->category = "Infill";
    def->tooltip = "This option will limit infill to the areas actually needed for supporting ceilings (it will act as internal support material). If enabled, slows down the G-code generation due to the multiple checks involved.";
    def->cli = "infill-only-where-needed!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("infill_overlap", coFloatOrPercent);
    def->label = "Infill/perimeters overlap";
    def->category = "Advanced";
    def->tooltip = "This setting applies an additional overlap between infill and perimeters for better bonding. Theoretically this shouldn't be needed, but backlash might cause gaps. If expressed as percentage (example: 15%) it is calculated over perimeter extrusion width.";
    def->sidetext = "mm or %";
    def->cli = "infill-overlap=s";
    def->ratio_over = "perimeter_extrusion_width";
    def->default_value = new ConfigOptionFloatOrPercent(25, true);

    def = this->add("infill_speed", coFloat);
    def->label = "Infill";
    def->category = "Speed";
    def->tooltip = "Speed for printing the internal fill. Set to zero for auto.";
    def->sidetext = "mm/s";
    def->cli = "infill-speed=f";
    def->aliases.push_back("print_feed_rate");
    def->aliases.push_back("infill_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(80);

    def = this->add("interface_shells", coBool);
    def->label = "Interface shells";
    def->tooltip = "Force the generation of solid shells between adjacent materials/volumes. Useful for multi-extruder prints with translucent materials or manual soluble support material.";
    def->cli = "interface-shells!";
    def->category = "Layers and Perimeters";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("layer_gcode", coString);
    def->label = "After layer change G-code";
    def->tooltip = "This custom code is inserted at every layer change, right after the Z move and before the extruder moves to the first layer point. Note that you can use placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].";
    def->cli = "after-layer-gcode|layer-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("layer_height", coFloat);
    def->label = "Layer height";
    def->category = "Layers and Perimeters";
    def->tooltip = "This setting controls the height (and thus the total number) of the slices/layers. Thinner layers give better accuracy but take more time to print.";
    def->sidetext = "mm";
    def->cli = "layer-height=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0.3);

    def = this->add("max_fan_speed", coInts);
    def->label = "Max";
    def->tooltip = "This setting represents the maximum speed of your fan.";
    def->sidetext = "%";
    def->cli = "max-fan-speed=i@";
    def->min = 0;
    def->max = 100;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(100);
        def->default_value = opt;
    }

    def = this->add("max_layer_height", coFloats);
    def->label = "Max";
    def->tooltip = "This is the highest printable layer height for this extruder, used to cap the variable layer height and support layer height. Maximum recommended layer height is 75% of the extrusion width to achieve reasonable inter-layer adhesion. If set to 0, layer height is limited to 75% of the nozzle diameter.";
    def->sidetext = "mm";
    def->cli = "max-layer-height=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("max_print_speed", coFloat);
    def->label = "Max print speed";
    def->tooltip = "When setting other speed settings to 0 Slic3r will autocalculate the optimal speed in order to keep constant extruder pressure. This experimental setting is used to set the highest print speed you want to allow.";
    def->sidetext = "mm/s";
    def->cli = "max-print-speed=f";
    def->min = 1;
    def->default_value = new ConfigOptionFloat(80);

    def = this->add("max_volumetric_speed", coFloat);
    def->label = "Max volumetric speed";
    def->tooltip = "This experimental setting is used to set the maximum volumetric speed your extruder supports.";
    def->sidetext = "mm³/s";
    def->cli = "max-volumetric-speed=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("max_volumetric_extrusion_rate_slope_positive", coFloat);
    def->label = "Max volumetric slope positive";
    def->tooltip = "This experimental setting is used to limit the speed of change in extrusion rate. A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.";
    def->sidetext = "mm³/s²";
    def->cli = "max-volumetric-extrusion-rate-slope-positive=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("max_volumetric_extrusion_rate_slope_negative", coFloat);
    def->label = "Max volumetric slope negative";
    def->tooltip = "This experimental setting is used to limit the speed of change in extrusion rate. A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.";
    def->sidetext = "mm³/s²";
    def->cli = "max-volumetric-extrusion-rate-slope-negative=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("min_fan_speed", coInts);
    def->label = "Min";
    def->tooltip = "This setting represents the minimum PWM your fan needs to work.";
    def->sidetext = "%";
    def->cli = "min-fan-speed=i@";
    def->min = 0;
    def->max = 100;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(35);
        def->default_value = opt;
    }

    def = this->add("min_layer_height", coFloats);
    def->label = "Min";
    def->tooltip = "This is the lowest printable layer height for this extruder and limits the resolution for variable layer height. Typical values are between 0.05 mm and 0.1 mm.";
    def->sidetext = "mm";
    def->cli = "min-layer-height=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0.07);
        def->default_value = opt;
    }

    def = this->add("min_print_speed", coFloats);
    def->label = "Min print speed";
    def->tooltip = "Slic3r will not scale speed down below this speed.";
    def->sidetext = "mm/s";
    def->cli = "min-print-speed=f@";
    def->min = 0;
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(10.);
        def->default_value = opt;
    }

    def = this->add("min_skirt_length", coFloat);
    def->label = "Minimum extrusion length";
    def->tooltip = "Generate no less than the number of skirt loops required to consume the specified amount of filament on the bottom layer. For multi-extruder machines, this minimum applies to each extruder.";
    def->sidetext = "mm";
    def->cli = "min-skirt-length=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("notes", coString);
    def->label = "Configuration notes";
    def->tooltip = "You can put here your personal notes. This text will be added to the G-code header comments.";
    def->cli = "notes=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 130;
    def->default_value = new ConfigOptionString("");

    def = this->add("nozzle_diameter", coFloats);
    def->label = "Nozzle diameter";
    def->tooltip = "This is the diameter of your extruder nozzle (for example: 0.5, 0.35 etc.)";
    def->sidetext = "mm";
    def->cli = "nozzle-diameter=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0.5);
        def->default_value = opt;
    }

    def = this->add("octoprint_apikey", coString);
    def->label = "API Key";
    def->tooltip = "Slic3r can upload G-code files to OctoPrint. This field should contain the API Key required for authentication.";
    def->cli = "octoprint-apikey=s";
    def->default_value = new ConfigOptionString("");
    
    def = this->add("octoprint_host", coString);
    def->label = "Host or IP";
    def->tooltip = "Slic3r can upload G-code files to OctoPrint. This field should contain the hostname or IP address of the OctoPrint instance.";
    def->cli = "octoprint-host=s";
    def->default_value = new ConfigOptionString("");

    def = this->add("only_retract_when_crossing_perimeters", coBool);
    def->label = "Only retract when crossing perimeters";
    def->tooltip = "Disables retraction when the travel path does not exceed the upper layer's perimeters (and thus any ooze will be probably invisible).";
    def->cli = "only-retract-when-crossing-perimeters!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("ooze_prevention", coBool);
    def->label = "Enable";
    def->tooltip = "This option will drop the temperature of the inactive extruders to prevent oozing. It will enable a tall skirt automatically and move extruders outside such skirt when changing temperatures.";
    def->cli = "ooze-prevention!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("output_filename_format", coString);
    def->label = "Output filename format";
    def->tooltip = "You can use all configuration options as variables inside this template. For example: [layer_height], [fill_density] etc. You can also use [timestamp], [year], [month], [day], [hour], [minute], [second], [version], [input_filename], [input_filename_base].";
    def->cli = "output-filename-format=s";
    def->full_width = true;
    def->default_value = new ConfigOptionString("[input_filename_base].gcode");

    def = this->add("overhangs", coBool);
    def->label = "Detect bridging perimeters";
    def->category = "Layers and Perimeters";
    def->tooltip = "Experimental option to adjust flow for overhangs (bridge flow will be used), to apply bridge speed to them and enable fan.";
    def->cli = "overhangs!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("perimeter_acceleration", coFloat);
    def->label = "Perimeters";
    def->tooltip = "This is the acceleration your printer will use for perimeters. A high value like 9000 usually gives good results if your hardware is up to the job. Set zero to disable acceleration control for perimeters.";
    def->sidetext = "mm/s²";
    def->cli = "perimeter-acceleration=f";
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("perimeter_extruder", coInt);
    def->label = "Perimeter extruder";
    def->category = "Extruders";
    def->tooltip = "The extruder to use when printing perimeters and brim. First extruder is 1.";
    def->cli = "perimeter-extruder=i";
    def->aliases.push_back("perimeters_extruder");
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("perimeter_extrusion_width", coFloatOrPercent);
    def->label = "Perimeters";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for perimeters. You may want to use thinner extrudates to get more accurate surfaces. If expressed as percentage (for example 200%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "perimeter-extrusion-width=s";
    def->aliases.push_back("perimeters_extrusion_width");
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("perimeter_speed", coFloat);
    def->label = "Perimeters";
    def->category = "Speed";
    def->tooltip = "Speed for perimeters (contours, aka vertical shells). Set to zero for auto.";
    def->sidetext = "mm/s";
    def->cli = "perimeter-speed=f";
    def->aliases.push_back("perimeter_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloat(60);

    def = this->add("perimeters", coInt);
    def->label = "Perimeters";
    def->category = "Layers and Perimeters";
    def->tooltip = "This option sets the number of perimeters to generate for each layer. Note that Slic3r may increase this number automatically when it detects sloping surfaces which benefit from a higher number of perimeters if the Extra Perimeters option is enabled.";
    def->sidetext = "(minimum)";
    def->cli = "perimeters=i";
    def->aliases.push_back("perimeter_offsets");
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("post_process", coStrings);
    def->label = "Post-processing scripts";
    def->tooltip = "If you want to process the output G-code through custom scripts, just list their absolute paths here. Separate multiple scripts with a semicolon. Scripts will be passed the absolute path to the G-code file as the first argument, and they can access the Slic3r config settings by reading environment variables.";
    def->cli = "post-process=s@";
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 60;

    def = this->add("printer_notes", coString);
    def->label = "Printer notes";
    def->tooltip = "You can put your notes regarding the printer here.";
    def->cli = "printer-notes=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 130;
    def->default_value = new ConfigOptionString("");

    def = this->add("print_settings_id", coString);
    def->default_value = new ConfigOptionString("");
    
    def = this->add("printer_settings_id", coString);
    def->default_value = new ConfigOptionString("");

    def = this->add("raft_layers", coInt);
    def->label = "Raft layers";
    def->category = "Support material";
    def->tooltip = "The object will be raised by this number of layers, and support material will be generated under it.";
    def->sidetext = "layers";
    def->cli = "raft-layers=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("resolution", coFloat);
    def->label = "Resolution";
    def->tooltip = "Minimum detail resolution, used to simplify the input file for speeding up the slicing job and reducing memory usage. High-resolution models often carry more detail than printers can render. Set to zero to disable any simplification and use full resolution from input.";
    def->sidetext = "mm";
    def->cli = "resolution=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("retract_before_travel", coFloats);
    def->label = "Minimum travel after retraction";
    def->tooltip = "Retraction is not triggered when travel moves are shorter than this length.";
    def->sidetext = "mm";
    def->cli = "retract-before-travel=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(2);
        def->default_value = opt;
    }

    def = this->add("retract_before_wipe", coPercents);
    def->label = "Retract amount before wipe";
    def->tooltip = "With bowden extruders, it may be wise to do some amount of quick retract before doing the wipe movement.";
    def->sidetext = "%";
    def->cli = "retract-before-wipe=s@";
    {
        ConfigOptionPercents* opt = new ConfigOptionPercents();
        opt->values.push_back(0.f);
        def->default_value = opt;
    }
    
    def = this->add("retract_layer_change", coBools);
    def->label = "Retract on layer change";
    def->tooltip = "This flag enforces a retraction whenever a Z move is done.";
    def->cli = "retract-layer-change!";
    {
        ConfigOptionBools* opt = new ConfigOptionBools();
        opt->values.push_back(false);
        def->default_value = opt;
    }

    def = this->add("retract_length", coFloats);
    def->label = "Length";
    def->full_label = "Retraction Length";
    def->tooltip = "When retraction is triggered, filament is pulled back by the specified amount (the length is measured on raw filament, before it enters the extruder).";
    def->sidetext = "mm (zero to disable)";
    def->cli = "retract-length=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(2);
        def->default_value = opt;
    }

    def = this->add("retract_length_toolchange", coFloats);
    def->label = "Length";
    def->full_label = "Retraction Length (Toolchange)";
    def->tooltip = "When retraction is triggered before changing tool, filament is pulled back by the specified amount (the length is measured on raw filament, before it enters the extruder).";
    def->sidetext = "mm (zero to disable)";
    def->cli = "retract-length-toolchange=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(10);
        def->default_value = opt;
    }

    def = this->add("retract_lift", coFloats);
    def->label = "Lift Z";
    def->tooltip = "If you set this to a positive value, Z is quickly raised every time a retraction is triggered. When using multiple extruders, only the setting for the first extruder will be considered.";
    def->sidetext = "mm";
    def->cli = "retract-lift=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("retract_lift_above", coFloats);
    def->label = "Above Z";
    def->full_label = "Only lift Z above";
    def->tooltip = "If you set this to a positive value, Z lift will only take place above the specified absolute Z. You can tune this setting for skipping lift on the first layers.";
    def->sidetext = "mm";
    def->cli = "retract-lift-above=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("retract_lift_below", coFloats);
    def->label = "Below Z";
    def->full_label = "Only lift Z below";
    def->tooltip = "If you set this to a positive value, Z lift will only take place below the specified absolute Z. You can tune this setting for limiting lift to the first layers.";
    def->sidetext = "mm";
    def->cli = "retract-lift-below=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("retract_restart_extra", coFloats);
    def->label = "Extra length on restart";
    def->tooltip = "When the retraction is compensated after the travel move, the extruder will push this additional amount of filament. This setting is rarely needed.";
    def->sidetext = "mm";
    def->cli = "retract-restart-extra=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("retract_restart_extra_toolchange", coFloats);
    def->label = "Extra length on restart";
    def->tooltip = "When the retraction is compensated after changing tool, the extruder will push this additional amount of filament.";
    def->sidetext = "mm";
    def->cli = "retract-restart-extra-toolchange=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("retract_speed", coFloats);
    def->label = "Retraction Speed";
    def->full_label = "Retraction Speed";
    def->tooltip = "The speed for retractions (it only applies to the extruder motor).";
    def->sidetext = "mm/s";
    def->cli = "retract-speed=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(40);
        def->default_value = opt;
    }

    def = this->add("deretract_speed", coFloats);
    def->label = "Deretraction Speed";
    def->full_label = "Deretraction Speed";
    def->tooltip = "The speed for loading of a filament into extruder after retraction (it only applies to the extruder motor). If left to zero, the retraction speed is used.";
    def->sidetext = "mm/s";
    def->cli = "retract-speed=f@";
    {
        ConfigOptionFloats* opt = new ConfigOptionFloats();
        opt->values.push_back(0);
        def->default_value = opt;
    }

    def = this->add("seam_position", coEnum);
    def->label = "Seam position";
    def->category = "Layers and Perimeters";
    def->tooltip = "Position of perimeters starting points.";
    def->cli = "seam-position=s";
    def->enum_keys_map = ConfigOptionEnum<SeamPosition>::get_enum_values();
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
    def->label = "Direction";
    def->sidetext = "°";
    def->full_label = "Preferred direction of the seam";
    def->tooltip = "Seam preferred direction";
    def->cli = "seam-preferred-direction=f";
    def->min = 0;
    def->max = 360;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("seam_preferred_direction_jitter", coFloat);
//    def->gui_type = "slider";
    def->label = "Jitter";
    def->sidetext = "°";
    def->full_label = "Seam preferred direction jitter";
    def->tooltip = "Preferred direction of the seam - jitter";
    def->cli = "seam-preferred-direction-jitter=f";
    def->min = 0;
    def->max = 360;
    def->default_value = new ConfigOptionFloat(30);
#endif

    def = this->add("serial_port", coString);
    def->gui_type = "select_open";
    def->label = "";
    def->full_label = "Serial port";
    def->tooltip = "USB/serial port for printer connection.";
    def->cli = "serial-port=s";
    def->width = 200;
    def->default_value = new ConfigOptionString("");

    def = this->add("serial_speed", coInt);
    def->gui_type = "i_enum_open";
    def->label = "Speed";
    def->full_label = "Serial port speed";
    def->tooltip = "Speed (baud) of USB/serial port for printer connection.";
    def->cli = "serial-speed=i";
    def->min = 1;
    def->max = 300000;
    def->enum_values.push_back("115200");
    def->enum_values.push_back("250000");
    def->default_value = new ConfigOptionInt(250000);

    def = this->add("skirt_distance", coFloat);
    def->label = "Distance from object";
    def->tooltip = "Distance between skirt and object(s). Set this to zero to attach the skirt to the object(s) and get a brim for better adhesion.";
    def->sidetext = "mm";
    def->cli = "skirt-distance=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(6);

    def = this->add("skirt_height", coInt);
    def->label = "Skirt height";
    def->tooltip = "Height of skirt expressed in layers. Set this to a tall value to use skirt as a shield against drafts.";
    def->sidetext = "layers";
    def->cli = "skirt-height=i";
    def->default_value = new ConfigOptionInt(1);

    def = this->add("skirts", coInt);
    def->label = "Loops (minimum)";
    def->full_label = "Skirt Loops";
    def->tooltip = "Number of loops for the skirt. If the Minimum Extrusion Length option is set, the number of loops might be greater than the one configured here. Set this to zero to disable skirt completely.";
    def->cli = "skirts=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(1);
    
    def = this->add("slowdown_below_layer_time", coInts);
    def->label = "Slow down if layer print time is below";
    def->tooltip = "If layer print time is estimated below this number of seconds, print moves speed will be scaled down to extend duration to this value.";
    def->sidetext = "approximate seconds";
    def->cli = "slowdown-below-layer-time=i@";
    def->width = 60;
    def->min = 0;
    def->max = 1000;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(5);
        def->default_value = opt;
    }

    def = this->add("small_perimeter_speed", coFloatOrPercent);
    def->label = "Small perimeters";
    def->category = "Speed";
    def->tooltip = "This separate setting will affect the speed of perimeters having radius <= 6.5mm (usually holes). If expressed as percentage (for example: 80%) it will be calculated on the perimeters speed setting above. Set to zero for auto.";
    def->sidetext = "mm/s or %";
    def->cli = "small-perimeter-speed=s";
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(15, false);

    def = this->add("solid_infill_below_area", coFloat);
    def->label = "Solid infill threshold area";
    def->category = "Infill";
    def->tooltip = "Force solid infill for regions having a smaller area than the specified threshold.";
    def->sidetext = "mm²";
    def->cli = "solid-infill-below-area=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(70);

    def = this->add("solid_infill_extruder", coInt);
    def->label = "Solid infill extruder";
    def->category = "Extruders";
    def->tooltip = "The extruder to use when printing solid infill.";
    def->cli = "solid-infill-extruder=i";
    def->min = 1;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("solid_infill_every_layers", coInt);
    def->label = "Solid infill every";
    def->category = "Infill";
    def->tooltip = "This feature allows to force a solid layer every given number of layers. Zero to disable. You can set this to any value (for example 9999); Slic3r will automatically choose the maximum possible number of layers to combine according to nozzle diameter and layer height.";
    def->sidetext = "layers";
    def->cli = "solid-infill-every-layers=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("solid_infill_extrusion_width", coFloatOrPercent);
    def->label = "Solid infill";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for infill for solid surfaces. If expressed as percentage (for example 90%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "solid-infill-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("solid_infill_speed", coFloatOrPercent);
    def->label = "Solid infill";
    def->category = "Speed";
    def->tooltip = "Speed for printing solid regions (top/bottom/internal horizontal shells). This can be expressed as a percentage (for example: 80%) over the default infill speed above. Set to zero for auto.";
    def->sidetext = "mm/s or %";
    def->cli = "solid-infill-speed=s";
    def->ratio_over = "infill_speed";
    def->aliases.push_back("solid_infill_feed_rate");
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(20, false);

    def = this->add("solid_layers", coInt);
    def->label = "Solid layers";
    def->tooltip = "Number of solid layers to generate on top and bottom surfaces.";
    def->cli = "solid-layers=i";
    def->shortcut.push_back("top_solid_layers");
    def->shortcut.push_back("bottom_solid_layers");
    def->min = 0;

    def = this->add("spiral_vase", coBool);
    def->label = "Spiral vase";
    def->tooltip = "This feature will raise Z gradually while printing a single-walled object in order to remove any visible seam. This option requires a single perimeter, no infill, no top solid layers and no support material. You can still set any number of bottom solid layers as well as skirt/brim loops. It won't work when printing more than an object.";
    def->cli = "spiral-vase!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("standby_temperature_delta", coInt);
    def->label = "Temperature variation";
    def->tooltip = "Temperature difference to be applied when an extruder is not active.  Enables a full-height \"sacrificial\" skirt on which the nozzles are periodically wiped.";
    def->sidetext = "∆°C";
    def->cli = "standby-temperature-delta=i";
    def->min = -500;
    def->max = 500;
    def->default_value = new ConfigOptionInt(-5);

    def = this->add("start_gcode", coString);
    def->label = "Start G-code";
    def->tooltip = "This start procedure is inserted at the beginning, after bed has reached the target temperature and extruder just started heating, and before extruder has finished heating. If Slic3r detects M104 or M190 in your custom codes, such commands will not be prepended automatically so you're free to customize the order of heating commands and other custom actions. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want.";
    def->cli = "start-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->default_value = new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n");

    def = this->add("start_filament_gcode", coStrings);
    def->label = "Start G-code";
    def->tooltip = "This start procedure is inserted at the beginning, after any printer start gcode. This is used to override settings for a specific filament. If Slic3r detects M104, M109, M140 or M190 in your custom codes, such commands will not be prepended automatically so you're free to customize the order of heating commands and other custom actions. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want. If you have multiple extruders, the gcode is processed in extruder order.";
    def->cli = "start-filament-gcode=s@";
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    {
        ConfigOptionStrings* opt = new ConfigOptionStrings();
        opt->values.push_back("; Filament gcode\n");
        def->default_value = opt;
    }

    def = this->add("single_extruder_multi_material", coBool);
    def->label = "Single Extruder Multi Material";
    def->tooltip = "The printer multiplexes filaments into a single hot end.";
    def->cli = "single-extruder-multi-material!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material", coBool);
    def->label = "Generate support material";
    def->category = "Support material";
    def->tooltip = "Enable support material generation.";
    def->cli = "support-material!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_xy_spacing", coFloatOrPercent);
    def->label = "XY separation between an object and its support";
    def->category = "Support material";
    def->tooltip = "XY separation between an object and its support. If expressed as percentage (for example 50%), it will be calculated over external perimeter width.";
    def->sidetext = "mm or %";
    def->cli = "support-material-xy-spacing=s";
    def->ratio_over = "external_perimeter_extrusion_width";
    def->min = 0;
    // Default is half the external perimeter width.
    def->default_value = new ConfigOptionFloatOrPercent(50, true);

    def = this->add("support_material_angle", coFloat);
    def->label = "Pattern angle";
    def->category = "Support material";
    def->tooltip = "Use this setting to rotate the support material pattern on the horizontal plane.";
    def->sidetext = "°";
    def->cli = "support-material-angle=f";
    def->min = 0;
    def->max = 359;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("support_material_buildplate_only", coBool);
    def->label = "Support on build plate only";
    def->category = "Support material";
    def->tooltip = "Only create support if it lies on a build plate. Don't create support on a print.";
    def->cli = "support-material-buildplate-only!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_contact_distance", coFloat);
    def->gui_type = "f_enum_open";
    def->label = "Contact Z distance";
    def->category = "Support material";
    def->tooltip = "The vertical distance between object and support material interface. Setting this to 0 will also prevent Slic3r from using bridge flow and speed for the first object layer.";
    def->sidetext = "mm";
    def->cli = "support-material-contact-distance=f";
    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("0.2");
    def->enum_labels.push_back("0 (soluble)");
    def->enum_labels.push_back("0.2 (detachable)");
    def->default_value = new ConfigOptionFloat(0.2);

    def = this->add("support_material_enforce_layers", coInt);
    def->label = "Enforce support for the first";
    def->category = "Support material";
    def->tooltip = "Generate support material for the specified number of layers counting from bottom, regardless of whether normal support material is enabled or not and regardless of any angle threshold. This is useful for getting more adhesion of objects having a very thin or poor footprint on the build plate.";
    def->sidetext = "layers";
    def->cli = "support-material-enforce-layers=f";
    def->full_label = "Enforce support for the first n layers";
    def->min = 0;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("support_material_extruder", coInt);
    def->label = "Support material/raft/skirt extruder";
    def->category = "Extruders";
    def->tooltip = "The extruder to use when printing support material, raft and skirt (1+, 0 to use the current extruder to minimize tool changes).";
    def->cli = "support-material-extruder=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("support_material_extrusion_width", coFloatOrPercent);
    def->label = "Support material";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for support material. If expressed as percentage (for example 90%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "support-material-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("support_material_interface_contact_loops", coBool);
    def->label = "Interface loops";
    def->category = "Support material";
    def->tooltip = "Cover the top contact layer of the supports with loops. Disabled by default.";
    def->cli = "support-material-interface-contact-loops!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_interface_extruder", coInt);
    def->label = "Support material/raft interface extruder";
    def->category = "Extruders";
    def->tooltip = "The extruder to use when printing support material interface (1+, 0 to use the current extruder to minimize tool changes). This affects raft too.";
    def->cli = "support-material-interface-extruder=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(1);

    def = this->add("support_material_interface_layers", coInt);
    def->label = "Interface layers";
    def->category = "Support material";
    def->tooltip = "Number of interface layers to insert between the object(s) and support material.";
    def->sidetext = "layers";
    def->cli = "support-material-interface-layers=i";
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("support_material_interface_spacing", coFloat);
    def->label = "Interface pattern spacing";
    def->category = "Support material";
    def->tooltip = "Spacing between interface lines. Set zero to get a solid interface.";
    def->sidetext = "mm";
    def->cli = "support-material-interface-spacing=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("support_material_interface_speed", coFloatOrPercent);
    def->label = "Support material interface";
    def->category = "Support material";
    def->tooltip = "Speed for printing support material interface layers. If expressed as percentage (for example 50%) it will be calculated over support material speed.";
    def->sidetext = "mm/s or %";
    def->cli = "support-material-interface-speed=s";
    def->ratio_over = "support_material_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(100, true);

    def = this->add("support_material_pattern", coEnum);
    def->label = "Pattern";
    def->category = "Support material";
    def->tooltip = "Pattern used to generate support material.";
    def->cli = "support-material-pattern=s";
    def->enum_keys_map = ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("pillars");
    def->enum_labels.push_back("rectilinear");
    def->enum_labels.push_back("rectilinear grid");
    def->enum_labels.push_back("honeycomb");
    def->enum_labels.push_back("pillars");
    def->default_value = new ConfigOptionEnum<SupportMaterialPattern>(smpPillars);

    def = this->add("support_material_spacing", coFloat);
    def->label = "Pattern spacing";
    def->category = "Support material";
    def->tooltip = "Spacing between support material lines.";
    def->sidetext = "mm";
    def->cli = "support-material-spacing=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(2.5);

    def = this->add("support_material_speed", coFloat);
    def->label = "Support material";
    def->category = "Support material";
    def->tooltip = "Speed for printing support material.";
    def->sidetext = "mm/s";
    def->cli = "support-material-speed=f";
    def->min = 0;
    def->default_value = new ConfigOptionFloat(60);

    def = this->add("support_material_synchronize_layers", coBool);
    def->label = "Synchronize with object layers";
    def->category = "Support material";
    def->tooltip = "Synchronize support layers with the object print layers. This is useful with multi-material printers, where the extruder switch is expensive.";
    def->cli = "support-material-synchronize-layers!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("support_material_threshold", coInt);
    def->label = "Overhang threshold";
    def->category = "Support material";
    def->tooltip = "Support material will not be generated for overhangs whose slope angle (90° = vertical) is above the given threshold. In other words, this value represent the most horizontal slope (measured from the horizontal plane) that you can print without support material. Set to zero for automatic detection (recommended).";
    def->sidetext = "°";
    def->cli = "support-material-threshold=i";
    def->min = 0;
    def->max = 90;
    def->default_value = new ConfigOptionInt(0);

    def = this->add("support_material_with_sheath", coBool);
    def->label = "With sheath around the support";
    def->category = "Support material";
    def->tooltip = "Add a sheath (a single perimeter line) around the base support. This makes the support more reliable, but also more difficult to remove.";
    def->cli = "support-material-with-sheath!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("temperature", coInts);
    def->label = "Other layers";
    def->tooltip = "Extruder temperature for layers after the first one. Set this to zero to disable temperature control commands in the output.";
    def->cli = "temperature=i@";
    def->full_label = "Temperature";
    def->max = 0;
    def->max = 500;
    {
        ConfigOptionInts* opt = new ConfigOptionInts();
        opt->values.push_back(200);
        def->default_value = opt;
    }
    
    def = this->add("thin_walls", coBool);
    def->label = "Detect thin walls";
    def->category = "Layers and Perimeters";
    def->tooltip = "Detect single-width walls (parts where two extrusions don't fit and we need to collapse them into a single trace).";
    def->cli = "thin-walls!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("threads", coInt);
    def->label = "Threads";
    def->tooltip = "Threads are used to parallelize long-running tasks. Optimal threads number is slightly above the number of available cores/processors.";
    def->cli = "threads|j=i";
    def->readonly = true;
    def->min = 1;
    {
        unsigned int threads = boost::thread::hardware_concurrency();
        def->default_value = new ConfigOptionInt(threads > 0 ? threads : 2);
    }
    
    def = this->add("toolchange_gcode", coString);
    def->label = "Tool change G-code";
    def->tooltip = "This custom code is inserted right before every extruder change. Note that you can use placeholder variables for all Slic3r settings as well as [previous_extruder] and [next_extruder].";
    def->cli = "toolchange-gcode=s";
    def->multiline = true;
    def->full_width = true;
    def->height = 50;
    def->default_value = new ConfigOptionString("");

    def = this->add("top_infill_extrusion_width", coFloatOrPercent);
    def->label = "Top solid infill";
    def->category = "Extrusion Width";
    def->tooltip = "Set this to a non-zero value to set a manual extrusion width for infill for top surfaces. You may want to use thinner extrudates to fill all narrow regions and get a smoother finish. If expressed as percentage (for example 90%) it will be computed over layer height.";
    def->sidetext = "mm or % (leave 0 for default)";
    def->cli = "top-infill-extrusion-width=s";
    def->default_value = new ConfigOptionFloatOrPercent(0, false);

    def = this->add("top_solid_infill_speed", coFloatOrPercent);
    def->label = "Top solid infill";
    def->category = "Speed";
    def->tooltip = "Speed for printing top solid layers (it only applies to the uppermost external layers and not to their internal solid layers). You may want to slow down this to get a nicer surface finish. This can be expressed as a percentage (for example: 80%) over the solid infill speed above. Set to zero for auto.";
    def->sidetext = "mm/s or %";
    def->cli = "top-solid-infill-speed=s";
    def->ratio_over = "solid_infill_speed";
    def->min = 0;
    def->default_value = new ConfigOptionFloatOrPercent(15, false);

    def = this->add("top_solid_layers", coInt);
    def->label = "Top";
    def->category = "Layers and Perimeters";
    def->tooltip = "Number of solid layers to generate on top surfaces.";
    def->cli = "top-solid-layers=i";
    def->full_label = "Top solid layers";
    def->min = 0;
    def->default_value = new ConfigOptionInt(3);

    def = this->add("travel_speed", coFloat);
    def->label = "Travel";
    def->tooltip = "Speed for travel moves (jumps between distant extrusion points).";
    def->sidetext = "mm/s";
    def->cli = "travel-speed=f";
    def->aliases.push_back("travel_feed_rate");
    def->min = 1;
    def->default_value = new ConfigOptionFloat(130);

    def = this->add("use_firmware_retraction", coBool);
    def->label = "Use firmware retraction";
    def->tooltip = "This experimental setting uses G10 and G11 commands to have the firmware handle the retraction. This is only supported in recent Marlin.";
    def->cli = "use-firmware-retraction!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("use_relative_e_distances", coBool);
    def->label = "Use relative E distances";
    def->tooltip = "If your firmware requires relative E values, check this, otherwise leave it unchecked. Most firmwares use absolute values.";
    def->cli = "use-relative-e-distances!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("use_volumetric_e", coBool);
    def->label = "Use volumetric E";
    def->tooltip = "This experimental setting uses outputs the E values in cubic millimeters instead of linear millimeters. If your firmware doesn't already know filament diameter(s), you can put commands like 'M200 D[filament_diameter_0] T0' in your start G-code in order to turn volumetric mode on and use the filament diameter associated to the filament selected in Slic3r. This is only supported in recent Marlin.";
    def->cli = "use-volumetric-e!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("variable_layer_height", coBool);
    def->label = "Enable variable layer height feature";
    def->tooltip = "Some printers or printer setups may have difficulties printing with a variable layer height. Enabled by default.";
    def->cli = "variable-layer-height!";
    def->default_value = new ConfigOptionBool(true);

    def = this->add("wipe", coBools);
    def->label = "Wipe while retracting";
    def->tooltip = "This flag will move the nozzle while retracting to minimize the possible blob on leaky extruders.";
    def->cli = "wipe!";
    {
        ConfigOptionBools* opt = new ConfigOptionBools();
        opt->values.push_back(false);
        def->default_value = opt;
    }

    def = this->add("wipe_tower", coBool);
    def->label = "Enable";
    def->tooltip = "Multi material printers may need to prime or purge extruders on tool changes. Extrude the excess material into the wipe tower.";
    def->cli = "wipe-tower!";
    def->default_value = new ConfigOptionBool(false);

    def = this->add("wipe_tower_x", coFloat);
    def->label = "Position X";
    def->tooltip = "X coordinate of the left front corner of a wipe tower";
    def->sidetext = "mm";
    def->cli = "wipe-tower-x=f";
    def->default_value = new ConfigOptionFloat(180.);

    def = this->add("wipe_tower_y", coFloat);
    def->label = "Position Y";
    def->tooltip = "Y coordinate of the left front corner of a wipe tower";
    def->sidetext = "mm";
    def->cli = "wipe-tower-y=f";
    def->default_value = new ConfigOptionFloat(140.);

    def = this->add("wipe_tower_width", coFloat);
    def->label = "Width";
    def->tooltip = "Width of a wipe tower";
    def->sidetext = "mm";
    def->cli = "wipe-tower-width=f";
    def->default_value = new ConfigOptionFloat(60.);

    def = this->add("wipe_tower_per_color_wipe", coFloat);
    def->label = "Per color change depth";
    def->tooltip = "Depth of a wipe color per color change. For N colors, there will be maximum (N-1) tool switches performed, therefore the total depth of the wipe tower will be (N-1) times this value.";
    def->sidetext = "mm";
    def->cli = "wipe-tower-per-color-wipe=f";
    def->default_value = new ConfigOptionFloat(15.);

    def = this->add("xy_size_compensation", coFloat);
    def->label = "XY Size Compensation";
    def->category = "Advanced";
    def->tooltip = "The object will be grown/shrunk in the XY plane by the configured value (negative = inwards, positive = outwards). This might be useful for fine-tuning hole sizes.";
    def->sidetext = "mm";
    def->cli = "xy-size-compensation=f";
    def->default_value = new ConfigOptionFloat(0);

    def = this->add("z_offset", coFloat);
    def->label = "Z offset";
    def->tooltip = "This value will be added (or subtracted) from all the Z coordinates in the output G-code. It is used to compensate for bad Z endstop position: for example, if your endstop zero actually leaves the nozzle 0.3mm far from the print bed, set this to -0.3 (or fix your endstop).";
    def->sidetext = "mm";
    def->cli = "z-offset=f";
    def->default_value = new ConfigOptionFloat(0);
}

PrintConfigDef print_config_def;

void
DynamicPrintConfig::normalize() {
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


bool PrintConfigBase::set_deserialize(t_config_option_key opt_key, std::string str, bool append)
{
    this->_handle_legacy(opt_key, str);
    return opt_key.empty() ? 
        // ignore option
        true :
        ConfigBase::set_deserialize(opt_key, str, append);
}

void PrintConfigBase::_handle_legacy(t_config_option_key &opt_key, std::string &value) const
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
    } else if (opt_key == "support_material_threshold" && value == "0") {
        // 0 used to be automatic threshold, but we introduced percent values so let's
        // transform it into the default value
        value = "60%";
    }
    
    // cemetery of old config settings
    if (opt_key == "duplicate_x" || opt_key == "duplicate_y" || opt_key == "multiply_x" 
        || opt_key == "multiply_y" || opt_key == "support_material_tool" 
        || opt_key == "acceleration" || opt_key == "adjust_overhang_flow" 
        || opt_key == "standby_temperature" || opt_key == "scale" || opt_key == "rotate" 
        || opt_key == "duplicate" || opt_key == "duplicate_grid" || opt_key == "rotate" 
        || opt_key == "scale"  || opt_key == "duplicate_grid" 
        || opt_key == "start_perimeters_at_concave_points" 
        || opt_key == "start_perimeters_at_non_overhang" || opt_key == "randomize_start" 
        || opt_key == "seal_position" || opt_key == "bed_size" || opt_key == "octoprint_host" 
        || opt_key == "print_center" || opt_key == "g0" || opt_key == "threads")
    {
        opt_key = "";
        return;
    }
    
    if (!this->def->has(opt_key)) {
        //printf("Unknown option %s\n", opt_key.c_str());
        opt_key = "";
        return;
    }
}

double PrintConfigBase::min_object_distance() const
{
    double extruder_clearance_radius = this->option("extruder_clearance_radius")->getFloat();
    double duplicate_distance = this->option("duplicate_distance")->getFloat();
    
    // min object distance is max(duplicate_distance, clearance_radius)
    return (this->option("complete_objects")->getBool() && extruder_clearance_radius > duplicate_distance)
        ? extruder_clearance_radius
        : duplicate_distance;
}

}
