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

void PrintConfigDef::init_fff_printer_motion_ability()
{
    ConfigOptionDef* def;


    def = this->add("emit_machine_limits_to_gcode", coBool);
    def->label = L("Emit limits to G-code");
    def->category = L("Machine limits");
    def->tooltip = L("If enabled, the machine limits will be emitted to G-code file.\nThis option will be ignored if the G-code flavor is "
        "set to Klipper.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    // resonance avoidance ported over from qidi slicer
    def          = this->add("resonance_avoidance", coBool);
    def->label   = L("Resonance avoidance");
    def->tooltip = L("By reducing the speed of the outer wall to avoid the resonance zone of the printer, ringing on the surface of the "
                     "model are avoided.\n"
                     "Please turn this option off when testing ringing.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("min_resonance_avoidance_speed", coFloat);
    def->label    = L("Min");
    def->tooltip  = L("Minimum speed of resonance avoidance.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(70));

    def           = this->add("max_resonance_avoidance_speed", coFloat);
    def->label    = L("Max");
    def->tooltip  = L("Maximum speed of resonance avoidance.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(120));

    // Orca: Input Shaping support
    def          = this->add("input_shaping_emit", coBool);
    def->label   = L("Emit input shaping");
    def->tooltip = L("Override firmware input shaping settings.\nIf disabled, firmware settings are used.");
    def->mode    = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def               = this->add("input_shaping_type", coEnum);
    def->label        = L("Input shaper type");
    def->tooltip      = L("Choose the input shaper algorithm.\nDefault uses the firmware default settings.\nDisable turns off input shaping in the firmware.");
    def->enum_keys_map = &ConfigOptionEnum<InputShaperType>::get_enum_values();
    def->enum_values  = {"Default", "MZV", "ZV", "ZVD", "ZVDD", "ZVDDD", "EI", "EI2", "2HUMP_EI", "EI3", "3HUMP_EI", "DAA", "Disable"};
    def->enum_labels  = {L("Default"), L("MZV"), L("ZV"), L("ZVD"), L("ZVDD"), L("ZVDDD"), L("EI"), L("EI2"), L("2HUMP_EI"), L("EI3"), L("3HUMP_EI"), L("DAA"), L("Disable")};
    def->mode         = comExpert;
    def->set_default_value(new ConfigOptionEnum<InputShaperType>(InputShaperType::Default));

    def           = this->add("input_shaping_freq_x", coFloat);
    def->label    = L_CONTEXT("X", "Axis");
    def->tooltip  = L("Resonant frequency for the X axis input shaper.\nZero will use the firmware frequency.\nTo disable input shaping, use the Disable type.\nRRF: X and Y values are equal.");
    def->sidetext = L("Hz");	// Hertz, CIS languages need translation
    def->min      = 0;
    def->max      = 1000;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("input_shaping_freq_y", coFloat);
    def->label    = L_CONTEXT("Y", "Axis");
    def->tooltip  = L("Resonant frequency for the Y axis input shaper.\nZero will use the firmware frequency.\nTo disable input shaping, use the Disable type.");
    def->sidetext = L("Hz");	// Hertz, CIS languages need translation
    def->min      = 0;
    def->max      = 1000;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def          = this->add("input_shaping_damp_x", coFloat);
    def->label   = L_CONTEXT("X", "Axis");
    def->tooltip = L("Damping ratio for the X axis input shaper.\nZero will use the firmware damping ratio.\nTo disable input shaping, use the Disable type.\nRRF: X and Y values are equal.");
    def->min     = 0;
    def->max     = 1;
    def->mode    = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def          = this->add("input_shaping_damp_y", coFloat);
    def->label   = L_CONTEXT("Y", "Axis");
    def->tooltip = L("Damping ratio for the Y axis input shaper.\nZero will use the firmware damping ratio.\nTo disable input shaping, use the Disable type.");
    def->min     = 0;
    def->max     = 1;
    def->mode    = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.1));
}

} // namespace Slic3r
