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

void PrintConfigDef::init_fff_internal_feature_flags()
{
    ConfigOptionDef* def;


    def = this->add("brim_ears", coBool);
    def->label = L("Brim ears");
    def->category = L("Support");
    def->tooltip = L("Only draw brim over the sharp edges of the model.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def        = this->add("volumetric_speed_coefficients", coStrings);
    def->label = L("Max volumetric speed multinomial coefficients");
    def->set_default_value(new ConfigOptionStrings{""});

    //def = this->add("adaptive_layer_height", coBool);
    //def->label = L("Adaptive layer height");
    //def->category = L("Quality");
    //def->tooltip = L("Enabling this option means the height of every layer except the first will be automatically calculated "
    //    "during slicing according to the slope of the model’s surface.\n"
    //    "Note that this option only takes effect if no prime tower is generated in current plate.");
    //def->set_default_value(new ConfigOptionBool(0));

    def = this->add("has_filament_switcher", coBool);
    def->label = L("Has filament switcher");
    def->tooltip = L("Printer has a filament switcher hardware (e.g., AMS).");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));
}

} // namespace Slic3r
