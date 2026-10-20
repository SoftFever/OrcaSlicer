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

void PrintConfigDef::init_fff_internal_fans()
{
    ConfigOptionDef* def;


    def = this->add("filament_cooling_before_tower", coFloats);
    def->label = L("Wipe tower cooling");
    def->tooltip = L("Temperature drop before entering filament tower");
    def->sidetext = L(u8"\u2103" /* °C */); // degrees Celsius, CIS languages need translation
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 10. });

    def = this->add("close_additional_fan_first_x_layers", coInts);
    def->label = L("For the first");
    def->tooltip = L("Set special auxiliary cooling fan for the first certain layers.");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 1 });

    def = this->add("additional_fan_full_speed_layer", coInts);
    def->label = L("Full fan speed at layer");
    def->tooltip = L("Auxiliary fan speed will be ramped up linearly from layer \"For the first\" to maximum at layer \"Full fan speed at layer\".\n"
                     "\"Full fan speed at layer\" will be ignored if lower than \"For the first\", in which case the fan will run at maximum allowed speed at layer \"For the first\" + 1.");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("first_x_layer_fan_speed", coFloats);
    def->label = L("Fan speed");
    def->tooltip = L("Special auxiliary cooling fan speed, effective only for the first x layers.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 0 });
}

} // namespace Slic3r
