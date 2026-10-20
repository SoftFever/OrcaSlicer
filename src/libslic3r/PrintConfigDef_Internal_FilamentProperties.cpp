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

void PrintConfigDef::init_fff_internal_filament_properties()
{
    ConfigOptionDef* def;



    def = this->add("filament_multi_colour", coStrings);
    def->set_default_value(new ConfigOptionStrings{""});

    // 0: gradient color, 1: default color(single or multi color)
    def = this->add("filament_colour_type", coStrings);
    def->set_default_value(new ConfigOptionStrings{"1"}); // Init as default color

    // defined in bits
    // 0 means cannot support, 1 means support
    // 0 bit: can support in left extruder
    // 1 bit: can support in right extruder
    def          = this->add("filament_printable", coInts);
    def->label   = L("Filament printable");
    def->tooltip = L("The filament is printable in extruder.");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionInts{3});

    // A single 32-bit int encodes the compatibility level of a filament across all extruders (up to 10).
    // Every 3 bits represent one extruder: bits [3*i, 3*i+2] -> extruder i.
    // Compatibility levels: 0 = printable, 1 = error, 2 = critical warning, 3 = warning (4-7 reserved).
    // Default 0 (printable) keeps the key inert for filaments/printers that do not declare it.
    def          = this->add("filament_extruder_compatibility", coInts);
    def->label   = L("Filament-extruder compatibility");
    def->tooltip = L("A single 32-bit int encoding the compatibility level of a filament across all extruders (up to 10). "
                     "Every 3 bits represent one extruder (bits [3*i, 3*i+2] for extruder i). "
                     "0: printable, 1: error, 2: critical warning, 3: warning, 4-7: reserved.");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});
}

} // namespace Slic3r
