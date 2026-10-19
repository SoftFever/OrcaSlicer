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

void PrintConfigDef::init_fff_internal_filament_change()
{
    ConfigOptionDef* def;


    def = this->add("filament_ramming_parameters", coStrings);
    def->label = L("Ramming parameters");
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
       " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6" });

    // filament_max_temperature_drop_when_ec: inert (default {0}, passed into the injector context but
    // never consumed). Registered inert-only (comDevelop); not wired into any estimator read, and not
    // added to any per-variant expansion list.
    def = this->add("filament_max_temperature_drop_when_ec", coFloats);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats{0});

    def           = this->add("filament_change_length_nc", coFloats);
    def->label    = L("Hotend change");
    def->tooltip  = L("When changing the hotend, it is recommended to extrude a certain length of filament from the original nozzle. This helps minimize nozzle oozing.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{10});

    def          = this->add("filament_ramming_travel_time", coFloats);
    def->label   = L("Extruder change");
    def->tooltip = L("To prevent oozing, the nozzle will perform a reverse travel movement for a certain period after "
                     "the ramming is complete. The setting define the travel time.");
    def->mode    = comAdvanced;
    def->sidetext = "s";
    def->min      = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def           = this->add("filament_pre_cooling_temperature", coInts);
    def->label    = L("Extruder change");
    def->tooltip  = L("To prevent oozing, the nozzle temperature will be cooled during ramming. Therefore, the ramming time must be greater than the cooldown time. 0 means disabled.");
    def->mode     = comAdvanced;
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min      = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable{0});

    def           = this->add("filament_ramming_volumetric_speed", coFloats);
    def->label    = L("Extruder change");
    def->tooltip  = L("The maximum volumetric speed for ramming before extruder change, where -1 means using the maximum volumetric speed.");
    def->sidetext = L("mm³/s");
    def->min      = -1;
    def->max      = 200;
    def->mode     = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{-1});

    def           = this->add("filament_ramming_travel_time_nc", coFloats);
    def->label    = L("Hotend change");
    def->tooltip  = L("To prevent oozing, the nozzle will perform a reverse travel movement for a certain period after "
                       "the ramming is complete. The setting define the travel time.");
    def->mode     = comAdvanced;
    def->sidetext = "s";
    def->min      = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def          = this->add("filament_pre_cooling_temperature_nc", coInts);
    def->label   = L("Hotend change");
    def->tooltip = L(
        "To prevent oozing, the nozzle temperature will be cooled during ramming. Note: only a cooldown command and fan activation are triggered, reaching the target temperature is not guaranteed. 0 means disabled.");
    def->mode     = comAdvanced;
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min      = 0;
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable{0});

    def           = this->add("filament_ramming_volumetric_speed_nc", coFloats);
    def->label    = L("Hotend change");
    def->tooltip  = L("The maximum volumetric speed for ramming before a hotend change, where -1 means using the maximum volumetric speed.");
    def->sidetext = L("mm³/s");
    def->min      = -1;
    def->max      = 200;
    def->mode     = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{-1});

    def = this->add("filament_retract_length_nc", coFloats);
    def->label = L("length when change hotend");
    def->tooltip = L("When this retraction value is modified, it will be used as the amount of filament retracted "
                   "inside the hotend before changing hotends.");
    def->sidetext = L("mm");
    def->mode = comDevelop;
    def->nullable = true;
    def->min = 0;
    def->max = 18;
    def->set_default_value(new ConfigOptionFloatsNullable { 10. });
}

} // namespace Slic3r
