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

void PrintConfigDef::init_fff_filament_cooling()
{
    ConfigOptionDef* def;


    def = this->add("enable_overhang_bridge_fan", coBools);
    def->label = L("Force cooling for overhangs and bridges");
    def->tooltip = L("Enable this option to allow adjustment of the part cooling fan speed for specifically for overhangs, internal and external "
                     "bridges. Setting the fan speed specifically for these features can improve overall print quality and reduce warping.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{ true });

    def = this->add("overhang_fan_speed", coInts);
    def->label = L("Overhangs and external bridges fan speed");
    def->tooltip = L("Use this part cooling fan speed when printing bridges or overhang walls with an overhang threshold that exceeds "
                     "the value set in the 'Overhangs cooling threshold' parameter above. Increasing the cooling specifically for overhangs "
                     "and bridges can improve the overall print quality of these features.\n\n"
                     "Please note, this fan speed is clamped on the lower end by the minimum fan speed threshold set above. It is also adjusted "
                     "upwards up to the maximum fan speed threshold when the minimum layer time threshold is not met.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("overhang_fan_threshold", coEnums);
    def->label = L("Overhang cooling activation threshold");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("When the overhang exceeds this specified threshold, force the cooling fan to run at the 'Overhang Fan Speed' set below. "
                     "This threshold is expressed as a percentage, indicating the portion of each line's width that is unsupported by the layer "
                     "beneath it. Setting this value to 0% forces the cooling fan to run for all outer walls, regardless of the overhang degree.");
    //def->sidetext = "";
    def->enum_keys_map = &ConfigOptionEnum<OverhangFanThreshold>::get_enum_values();
    def->mode = comAdvanced;
    def->enum_values.emplace_back("0%");
    def->enum_values.emplace_back("10%");
    def->enum_values.emplace_back("25%");
    def->enum_values.emplace_back("50%");
    def->enum_values.emplace_back("75%");
    def->enum_values.emplace_back("95%");
    def->enum_labels.emplace_back("0%");
    def->enum_labels.emplace_back("10%");
    def->enum_labels.emplace_back("25%");
    def->enum_labels.emplace_back("50%");
    def->enum_labels.emplace_back("75%");
    def->enum_labels.emplace_back("95%");
    def->set_default_value(new ConfigOptionEnumsGeneric{ (int)Overhang_threshold_bridge });

    def = this->add("slow_down_for_layer_cooling", coBools);
    def->label = L("Slow printing down for better layer cooling");
    def->tooltip = L("Enable this option to slow printing speed down to ensure that the final layer time is not shorter than the layer time threshold in \"Max fan speed threshold\", so that the layer can be cooled for a longer time. This can improve the quality for small details.");
    def->set_default_value(new ConfigOptionBools { true });

    def = this->add("activate_air_filtration",coBools);
    def->label = L("Activate air filtration");
    def->tooltip = L("Activate for better air filtration. G-code command: M106 P3 S(0-255)");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{false});
    

    def = this->add("activate_air_filtration_during_print", coBools);
    def->full_label = L("During print");
    def->tooltip=L("Enable this to override the fan speed set in custom G-code during print.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{true});

    def = this->add("activate_air_filtration_on_completion", coBools);
    def->full_label = L("On completion");
    def->tooltip=L("Enable this to override the fan speed set in custom G-code after print completion.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{true});

    def = this->add("during_print_exhaust_fan_speed", coInts);
    def->label   = L("Fan speed");
    def->tooltip=L("Speed of exhaust fan during printing. This speed will override the speed in filament custom G-code.");
    def->sidetext = "%";
    def->min=0;
    def->max=100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{60});

    def = this->add("complete_print_exhaust_fan_speed", coInts);
    def->label = L("Fan speed");
    def->tooltip=L("Speed of exhaust fan after printing completes.");
    def->sidetext = "%";
    def->min=0;
    def->max=100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{80});

    def = this->add("close_fan_the_first_x_layers", coInts);
    def->label = L("No cooling for the first");
    def->tooltip = L("Turn off all cooling fans for the first few layers. "
                     "This can be used to improve build plate adhesion.");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 1 });

    def = this->add("reduce_fan_stop_start_freq", coBools);
    def->label = L("Keep fan always on");
    def->tooltip = L("Enabling this setting means that part cooling fan will never stop entirely and will instead run at least at minimum speed to reduce the frequency of starting and stopping.");
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("dont_slow_down_outer_wall", coBools);
    def->label = L("Don't slow down outer walls");
    def->tooltip = L("If enabled, this setting will ensure external perimeters are not slowed down to meet the minimum layer time. "
                     "This is particularly helpful in the below scenarios:\n"
                     "1. To avoid changes in shine when printing glossy filaments\n"
                     "2. To avoid changes in external wall speed which may create slight wall artifacts that appear like Z banding\n"
                     "3. To avoid printing at speeds which cause VFAs (fine artifacts) on the external walls");
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("fan_cooling_layer_time", coFloats);
    def->label = L("Layer time");
    def->tooltip = L("The part cooling fan will be enabled for layers where the estimated time is shorter than this value. Fan speed is interpolated between the minimum and maximum fan speeds according to layer printing time.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{ 60.0f });

    def = this->add("full_fan_speed_layer", coInts);
    def->label = L("Full fan speed at layer");
    def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"close_fan_the_first_x_layers\" "
                  "to maximum at layer \"full_fan_speed_layer\". "
                  "\"full_fan_speed_layer\" will be ignored if lower than \"close_fan_the_first_x_layers\", in which case "
                  "the fan will be running at maximum allowed speed at layer \"close_fan_the_first_x_layers\" + 1.");
    def->sidetext = L("layer"); // ORCA add side text
    def->min = 0;
    def->max = 1000;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 0 });

    // ORCA: explicit override for the part cooling fan speed on the first printed layer.
    def = this->add("initial_layer_fan_speed", coInts);
    def->label = L("First layer fan speed");
    def->tooltip = L("Sets an exact fan speed for the first layer, overriding all other cooling settings. "
                     "Useful for protecting 3D-printed toolhead parts (e.g. Voron-style ABS/ASA ducts) from "
                     "a hot bed. A small amount of airflow cools the ducts down, without using full cooling that "
                     "may in certain conditions hurt first-layer adhesion."
                     "\nFrom the second layer onwards, normal cooling resumes."
                     "\nIf \"Full fan speed at layer\" is also set, the fan ramps smoothly from this value "
                     "on the first layer up to your target by the chosen layer."
                     "\nOnly available when \"No cooling for the first\" is 0."
                     "\nSet to -1 to disable it.");
    def->sidetext = "%";
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { -1 });

    def = this->add("support_material_interface_fan_speed", coInts);
    def->label = L("Support interface fan speed");
    def->tooltip = L("This part cooling fan speed is applied when printing support interfaces. Setting this parameter to a higher than regular speed "
                     "reduces the layer binding strength between supports and the supported part, making them easier to separate."
                    "\nSet to -1 to disable it."
                     "\nThis setting is overridden by disable_fan_first_layers.");
    def->sidetext = "%";
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts{ -1 });
    
    // ORCA: Add support for separate internal bridge fan speed control
    def = this->add("internal_bridge_fan_speed", coInts);
    def->label = L("Internal bridges fan speed");
    def->tooltip = L("The part cooling fan speed used for all internal bridges. Set to -1 to use the overhang fan speed settings instead.\n\n"
                     "Reducing the internal bridges fan speed, compared to your regular fan speed, can help reduce part warping due to excessive "
                     "cooling applied over a large surface for a prolonged period of time.");
    def->sidetext = "%";
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts{ -1 });
    
    def = this->add("ironing_fan_speed", coInts);
    def->label = L("Ironing fan speed");
    def->tooltip = L("This part cooling fan speed is applied when ironing. Setting this parameter to a lower than regular speed "
                     "reduces possible nozzle clogging due to the low volumetric flow rate, making the interface smoother."
                    "\nSet to -1 to disable it.");
    def->sidetext = "%";
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts{ -1 });

    def = this->add("fan_max_speed", coFloats);
    def->label = L("Fan speed");
    def->tooltip = L("The part cooling fan speed may be increased when auto cooling is enabled. This is the maximum speed for the part cooling fan.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 100 });


    def = this->add("fan_min_speed", coFloats);
    def->label = L("Fan speed");
    def->tooltip = L("Minimum speed for part cooling fan.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 20 });

    def = this->add("additional_cooling_fan_speed", coInts);
    def->label = L("Fan speed");
    def->tooltip = L("Speed of auxiliary part cooling fan. Auxiliary fan will run at this speed during printing except the first several layers "
                     "which is defined by no cooling layers.\nPlease enable auxiliary_fan in printer settings to use this feature. G-code command: M106 P2 S(0-255)");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("slow_down_min_speed", coFloats);
    def->label = L("Min print speed");
    def->tooltip = L("The minimum print speed to which the printer slows down to maintain the minimum layer time defined above "
                     "when the slowdown for better layer cooling is enabled.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("slow_down_layer_time", coFloats);
    def->label = L("Layer time");
    def->tooltip = L("The printing speed in exported G-code will be slowed down when the estimated layer time is "
                     "shorter than this value in order to get better cooling for these layers.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 5.0f });
}

} // namespace Slic3r
