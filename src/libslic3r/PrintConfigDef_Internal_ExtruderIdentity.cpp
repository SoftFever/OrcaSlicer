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


void PrintConfigDef::init_fff_internal_extruder_identity()
{
    ConfigOptionDef* def;


    def = this->add("printer_extruder_id", coInts);
    // internal use only, don't need translation
    def->label = "Printer extruder id";
    def->tooltip = "Printer extruder id.";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_extruder_variant", coStrings);
    // internal use only, don't need translation
    def->label = "Printer's extruder variant";
    def->tooltip = "Printer's extruder variant.";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("master_extruder_id", coInt);
    // internal use only, don't need translation
    def->label = "Master extruder id";
    def->tooltip = "Default extruder id to place filament.";
    def->set_default_value(new ConfigOptionInt{ 1 });

    def = this->add("print_extruder_id", coInts);
    // internal use only, don't need translation
    def->label = "Print extruder id";
    def->tooltip = "Print extruder id.";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_extruder_variant", coStrings);
    // internal use only, don't need translation
    def->label = "Print's extruder variant";
    def->tooltip = "Print's extruder variant.";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    /*def = this->add("filament_extruder_id", coInts);
    def->label = "Filament extruder id";
    def->tooltip = "Filament extruder id.";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;*/

    def = this->add("filament_extruder_variant", coStrings);
    // internal use only, don't need translation
    def->label = "Filament's extruder variant";
    def->tooltip = "Filament's extruder variant.";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;
}

} // namespace Slic3r
