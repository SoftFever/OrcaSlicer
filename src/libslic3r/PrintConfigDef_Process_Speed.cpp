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

void PrintConfigDef::init_fff_process_speed()
{
    ConfigOptionDef* def;


    def = this->add("enable_overhang_speed", coBools);
    def->label = L("Slow down for overhangs");
    def->category = L("Speed");
    def->tooltip = L("Enable this option to slow down when printing overhangs. The speeds for different overhang percentages are set below.");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable{ true });
    
    def = this->add("slowdown_for_curled_perimeters", coBools);
    def->label = L("Slow down for curled perimeters");
    def->category = L("Speed");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enable this option to slow down printing in areas where perimeters may have curled upwards.\n"
                     "For example, additional slowdown will be applied when printing overhangs on sharp corners like the "
                     "front of the Benchy hull, reducing curling which compounds over multiple layers.\n\n"
                     "It is generally recommended to have this option switched on unless your printer cooling is powerful enough or the "
                     "print speed is slow enough that perimeter curling does not happen. \n"
                     "If printing with a high external perimeter speed, this parameter may introduce wall artifacts when slowing down, "
                     "due to the potentially large variance in print speeds causing the extruder to be unable to keep up with the requested flow change.\n"
                     "Root cause of these artifacts is most likely PA tuning being slightly off, especially when combined "
                     "with a high PA smooth time.\n\n"
                     "Recommendations when enabling this option:\n"
                     "1. Reduce Pressure Advance smooth time to 0.015 - 0.02 so the extruder reacts quickly to the speed changes.\n"
                     "2. Increase the minimum print speeds to limit the magnitude of the slowdown and reduce the variance between fast and slow segments.\n"
                     "3. If artifacts still appear, enable Extrusion Rate Smoothing (ERS) to further smooth the flow transitions.\n\n"
                     "Note: When this option is enabled, overhang perimeters are treated like overhangs, meaning the overhang speed is "
                     "applied even if the overhanging perimeter is part of a bridge.\n"
                     "For example, when the perimeters are 100% overhanging, with no wall supporting them from underneath, the 75% overhang speed will be applied, limited by the bridge speed.");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable{ false });

    def = this->add("overhang_1_4_speed", coFloatsOrPercents);
    def->label = "10%";
    def->category = L("Speed");
    def->full_label = "10%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 10% and 25% line width. "
    //                 "0 means using original wall speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(0, false)});

    def = this->add("overhang_2_4_speed", coFloatsOrPercents);
    def->label = "25%";
    def->category = L("Speed");
    def->full_label = "25%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 25% and 50% line width. "
    //                 "0 means using original wall speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(0, false)});

    def = this->add("overhang_3_4_speed", coFloatsOrPercents);
    def->label = "50%";
    def->category = L("Speed");
    def->full_label = "50%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 50% and 75% line width. "
    //                 "0 means using original wall speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(0, false)});

    def = this->add("overhang_4_4_speed", coFloatsOrPercents);
    def->label = "75%";
    def->category = L("Speed");
    def->full_label = "75%";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 75% and 100% line width. "
    //                 "0 means using original wall speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(0, false)});

    def = this->add("bridge_speed", coFloats);
    def->label = L("External");
    def->category = L("Speed");
    def->tooltip = L("Speed of the externally visible bridge extrusions.\n\n"
                     "In addition, if Slow down for curled perimeters is disabled, "
                     "it will be the print speed of overhang walls that are supported by less than 13%, "
                     "whether they are part of a bridge or an overhang.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{25});

    def = this->add("internal_bridge_speed", coFloatsOrPercents);
    def->label = L("Internal");
    def->category = L("Speed");
    def->tooltip = L("Speed of internal bridges. If the value is expressed as a percentage, it will be calculated based on the bridge_speed. Default value is 150%.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "bridge_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(150, true)});

    def = this->add("default_acceleration", coFloats);
    def->label = L("Normal printing");
    def->category = L("Speed");
    def->tooltip = L("This is the default acceleration for both normal printing and travel after the first layer.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500.0});

    def           = this->add("travel_acceleration", coFloats);
    def->label = L("Travel");
    def->category = L("Speed");
    def->tooltip = L("Acceleration of travel moves.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{10000.0});

    def = this->add("initial_layer_travel_acceleration", coFloatsOrPercents);
    def->label = L("First layer travel");
    def->tooltip = L("Travel acceleration of first layer.\nThe percentage value is relative to Travel Acceleration.");
    def->sidetext = L(u8"mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "travel_acceleration";
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(100, true)});

    def = this->add("bridge_acceleration", coFloatsOrPercents);
    def->label = L("Bridge");
    def->category = L("Speed");
    def->tooltip = L("Acceleration of bridges. If the value is expressed as a percentage (e.g. 50%), it will be calculated based on the outer wall acceleration.");
    def->sidetext = L(u8"mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "outer_wall_acceleration";
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(50,true)});

    def = this->add("outer_wall_speed", coFloats);
    def->label = L("Outer wall");
    def->category = L("Speed");
    def->tooltip = L("This is the printing speed for the outer walls of parts. These are generally printed slower than inner walls for higher quality.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{60});

    def = this->add("small_perimeter_speed", coFloatsOrPercents);
    def->label = L("Small perimeters");
    def->category = L("Speed");
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= small_perimeter_threshold "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the outer wall speed setting above.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(50, true)});

    def = this->add("small_perimeter_threshold", coFloats);
    def->label = L("Small perimeters threshold");
    def->category = L("Speed");
    def->tooltip = L("This sets the threshold for small perimeter length. Default threshold is 0mm.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("small_support_perimeter_speed", coFloatsOrPercents);
    def->label = L("Small support perimeters");
    def->category = L("Speed");
    def->tooltip = L("Same as \"Small perimeters\", but for supports. "
                    "This separate setting will affect the speed of support for areas <= `small_support_perimeter_threshold`. "
                    "If expressed as a percentage (for example: 80%), it will be calculated on the support or support interface speed setting above.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "support_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(50, true)});

    def = this->add("small_support_perimeter_threshold", coFloats);
    def->label = L("Small support perimeters threshold");
    def->category = L("Speed");
    def->tooltip = L("This sets the threshold for small support perimeter length. The default threshold is 0mm.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("top_surface_acceleration", coFloats);
    def->label = L("Top surface");
    def->category = L("Speed");
    def->tooltip = L("This is the acceleration of top surface infill. Using a lower value may improve top surface quality.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500});

    def = this->add("outer_wall_acceleration", coFloats);
    def->label = L("Outer wall");
    def->category = L("Speed");
    def->tooltip = L("Acceleration of outer wall: using a lower value can improve quality.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{500});

    def = this->add("inner_wall_acceleration", coFloats);
    def->label = L("Inner wall");
    def->category = L("Speed");
    def->tooltip = L("Acceleration of inner walls.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{10000});

    def             = this->add("sparse_infill_acceleration", coFloatsOrPercents);
    def->label = L("Sparse infill");
    def->category = L("Speed");
    def->tooltip = L("Acceleration of sparse infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.");
    def->sidetext = L(u8"mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "default_acceleration";
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(100, true)});

    def = this->add("internal_solid_infill_acceleration", coFloatsOrPercents);
    def->label = L("Internal solid infill");
    def->category = L("Speed");
    def->tooltip = L("Acceleration of internal solid infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.");
    def->sidetext = L(u8"mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "default_acceleration";
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(100, true)});

    def = this->add("initial_layer_acceleration", coFloats);
    def->label = L("First layer");
    def->category = L("Speed");
    def->tooltip = L("This is the printing acceleration for the first layer. Using limited acceleration can improve build plate adhesion.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{300});

    def = this->add("accel_to_decel_enable", coBool);
    def->label = L("Enable accel_to_decel");
    def->category = L("Speed");
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted automatically.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
    
    def = this->add("accel_to_decel_factor", coPercent);
    def->label = L("accel_to_decel");
    def->category = L("Speed");
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted to this %% of acceleration.");
    def->sidetext = "%";
    def->min = 1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));
    
    def = this->add("default_jerk", coFloats);
    def->label = L("Default");
    def->category = L("Speed");
    def->tooltip = L("Default jerk.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("default_junction_deviation", coFloats);
    def->label = L("Junction Deviation");
    def->category = L("Speed");
    def->tooltip = L("Marlin Firmware Junction Deviation (replaces the traditional XY Jerk setting).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0.f;
    def->max = 0.3f;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0.f});

    def = this->add("outer_wall_jerk", coFloats);
    def->label = L("Outer wall");
    def->category = L("Speed");
    def->tooltip = L("Jerk of outer walls.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{9});

    def = this->add("inner_wall_jerk", coFloats);
    def->label = L("Inner wall");
    def->category = L("Speed");
    def->tooltip = L("Jerk of inner walls.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{9});

    def = this->add("top_surface_jerk", coFloats);
    def->label = L("Top surface");
    def->category = L("Speed");
    def->nullable = true;
    def->tooltip = L("Jerk for top surface.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsNullable{9});

    def = this->add("infill_jerk", coFloats);
    def->label = L("Infill");
    def->category = L("Speed");
    def->tooltip = L("Jerk for infill.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{9});

    def = this->add("initial_layer_jerk", coFloats);
    def->label = L("First layer");
    def->category = L("Speed");
    def->tooltip = L("Jerk for the first layer.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{9});

    def = this->add("travel_jerk", coFloats);
    def->label = L("Travel");
    def->category = L("Speed");
    def->tooltip = L("Jerk for travel.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{12});

    def = this->add("initial_layer_travel_jerk", coFloatsOrPercents);
    def->label = L("First layer travel");
    def->tooltip = L("Travel jerk of first layer.\nThe percentage value is relative to Travel Jerk.");
    def->sidetext = L("mm/s or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "travel_jerk";
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(100, true)});

    def = this->add("initial_layer_speed", coFloats);
    def->label = L("First layer");
    def->tooltip = L("This is the speed for the walls and brim of the first layer.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{30});

    def = this->add("initial_layer_infill_speed", coFloats);
    def->label = L("First layer infill");
    def->tooltip = L("This is the speed for the first layer except for walls and brim, such as infill, gap fill and support.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{60.0});

    def = this->add("initial_layer_travel_speed", coFloatsOrPercents);
    def->label = L("First layer travel speed");
    def->tooltip = L("Travel speed of the first layer.");
    def->category = L("Speed");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "travel_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsOrPercentsNullable{FloatOrPercent(100, true)});

    def = this->add("slow_down_layers", coInt);
    def->label = L("Number of slow layers");
    def->tooltip = L("The first few layers are printed slower than normal. "
                     "The speed is gradually increased in a linear fashion over the specified number of layers.");
    def->category = L("Speed");
    def->sidetext = L("layers"); // ORCA add side text
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));
    
    def = this->add("gap_infill_speed", coFloats);
    def->label = L("Gap infill");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for gap infill. Gaps usually have irregular line width and should be printed more slowly.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{30});

    def = this->add("sparse_infill_speed", coFloats);
    def->label = L("Sparse infill");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for internal sparse infill.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{100});

    def = this->add("ironing_speed", coFloat);
    def->label = L("Ironing speed");
    def->category = L("Quality");
    def->tooltip = L("This is the print speed for ironing lines.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("max_volumetric_extrusion_rate_slope", coFloat);
    def->label = L("Extrusion rate smoothing");
    def->tooltip = L("This parameter smooths out sudden extrusion rate changes that happen when " 
                     "the printer transitions from printing a high flow (high speed/larger width) "
                     "extrusion to a lower flow (lower speed/smaller width) extrusion and vice versa.\n\n"
                     "It defines the maximum rate by which the extruded volumetric flow in mm³/s can change over time. "
                     "Higher values mean higher extrusion rate changes are allowed, resulting in faster speed transitions.\n\n" 
                     "A value of 0 disables the feature.\n\n"
                     "For a high speed, high flow direct drive printer (like the Bambu lab or Voron) this value is usually not needed. "
                     "However it can provide some marginal benefit in certain cases where feature speeds vary greatly. For example, "
                     "when there are aggressive slowdowns due to overhangs. In these cases a high value of around 300-350 mm³/s² is "
                     "recommended as this allows for just enough smoothing to assist pressure advance achieve a smoother flow transition.\n\n"
                     "For slower printers without pressure advance, the value should be set much lower. "
                     "A value of 10-15 mm³/s² is a good starting point for direct drive extruders and 5-10 mm³/s² for Bowden style.\n\n"
                     "This feature is known as Pressure Equalizer in Prusa slicer.\n\n"
                     "Note: this parameter disables arc fitting.");
    def->sidetext = L(u8"mm³/s²");	// cubic millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_volumetric_extrusion_rate_slope_segment_length", coFloat);
    def->label = L("Smoothing segment length");
    def->tooltip = L("A lower value results in smoother extrusion rate transitions. "
                     "However, this results in a significantly larger G-code file and more instructions for the printer to process.\n\n"
                     "Default value of 3 works well for most cases. If your printer is stuttering, increase this value to reduce the number of adjustments made.\n\n"
                     "Allowed values: 0.5-5");
    def->min = 0.5;
    def->max = 5;
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(3.0));
    
    def = this->add("extrusion_rate_smoothing_external_perimeter_only", coBool);
    def->label = L("Apply only on external features");
    def->tooltip = L("Applies extrusion rate smoothing only on external perimeters and overhangs. This can help reduce artefacts due to sharp speed transitions on externally visible "
                     "overhangs without impacting the print speed of features that will not be visible to the user.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("inner_wall_speed", coFloats);
    def->label = L("Inner wall");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for inner walls.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->aliases = { "perimeter_feed_rate" };
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{60});

    def = this->add("internal_solid_infill_speed", coFloats);
    def->label = L("Internal solid infill");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for internal solid infill, not including the top or bottom surface.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{100});

    def = this->add("support_interface_speed", coFloats);
    def->label = L("Support interface");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for support interfaces.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{80});

    def = this->add("support_speed", coFloats);
    def->label = L("Support");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for support.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ 80 });

    def = this->add("top_surface_speed", coFloats);
    def->label = L("Top surface");
    def->category = L("Speed");
    def->tooltip = L("This is the speed for solid top surface infill.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{100});

    def = this->add("travel_speed", coFloats);
    def->label = L("Travel");
    def->tooltip = L("This is the speed at which traveling is done.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{120});
}

} // namespace Slic3r
