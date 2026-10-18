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

void PrintConfigDef::init_fff_internal_prime_volume()
{
    ConfigOptionDef* def;


    def = this->add("silent_mode", coBool);
    def->label = L("Silent Mode");
    def->tooltip = L("Whether the machine supports silent mode in which machine uses lower acceleration to print more quietly");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    // Used by the Type1 wipe tower: filament_prime_volume on a filament change,
    // filament_prime_volume_nc on a hotend/nozzle change. Type2 uses prime_volume instead.
    def           = this->add("filament_prime_volume", coFloats);
    def->label    = L("Filament change");
    def->tooltip  = L("The volume of material required to prime the extruder on the tower, excluding a hotend change.");
    def->sidetext = L("mm³");
    def->min      = 1.0;
    def->mode     = comSimple;
    def->set_default_value(new ConfigOptionFloats{45.});

    def           = this->add("filament_prime_volume_nc", coFloats);
    def->label    = L("Hotend change");
    def->tooltip  = L("The volume of material required to prime the extruder for a hotend change on the tower.");
    def->sidetext = L("mm³");
    def->min      = 1.0;
    def->mode     = comSimple;
    def->set_default_value(new ConfigOptionFloats{60.});

    def = this->add("filament_preheat_temperature_delta", coFloats);
    def->label = L("Preheat temperature delta");
    def->tooltip = L("Temperature delta applied during pre-heating before tool change.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{0});
}

} // namespace Slic3r
