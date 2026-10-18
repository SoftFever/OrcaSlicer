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

void PrintConfigDef::init_fff_filament_advanced()
{
    ConfigOptionDef* def;


    def = this->add("filament_end_gcode", coStrings);
    def->label = L("End G-code");
    def->tooltip = L("Add end G-code when finishing the printing of this filament.");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { " " });

    def = this->add("filament_start_gcode", coStrings);
    def->label = L("Start G-code");
    def->tooltip = L("G-code added when the printer starts using this filament");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { " " });

    def = this->add("change_extrusion_role_gcode", coString);
    def->label = L("Change extrusion role G-code");
    def->tooltip = L("This G-code is inserted when the extrusion role is changed.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("filament_change_extrusion_role_gcode", coStrings);
    def->label = L("Change extrusion role G-code (filament)");
    def->tooltip = L("This G-code is inserted when the extrusion role is changed for the active filament.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{ "" });
}

} // namespace Slic3r
