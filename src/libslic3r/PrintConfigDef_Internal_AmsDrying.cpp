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


void PrintConfigDef::init_fff_internal_ams_drying()
{
    ConfigOptionDef* def;


    def = this->add("filament_dev_ams_drying_ams_limitations", coStrings);
    def->set_default_value(new ConfigOptionStrings{""});

    def = this->add("filament_dev_ams_drying_temperature", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("filament_dev_ams_drying_time", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("filament_dev_ams_drying_heat_distortion_temperature", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("filament_dev_chamber_drying_bed_temperature", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("filament_dev_chamber_drying_time", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("filament_dev_drying_softening_temperature", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    def = this->add("filament_dev_drying_cooling_temperature", coFloats);
    def->set_default_value(new ConfigOptionFloats{0});

    // Declare retract values for filament profile, overriding the printer's extruder profile.
    for (auto& opt_key : filament_extruder_override_keys) {
        const std::string filament_prefix = "filament_";
        std::string extruder_raw_key = opt_key.substr(opt_key.find(filament_prefix) + filament_prefix.length());
        auto it_opt = options.find(extruder_raw_key);
        assert(it_opt != options.end());
        def = this->add_nullable(opt_key, it_opt->second.type);
        def->label 		= it_opt->second.label;
        def->full_label = it_opt->second.full_label;
        def->tooltip 	= it_opt->second.tooltip;
        def->sidetext   = it_opt->second.sidetext;
        def->enum_keys_map = it_opt->second.enum_keys_map;
        def->enum_labels   = it_opt->second.enum_labels;
        def->enum_values   = it_opt->second.enum_values;
        def->min        = it_opt->second.min;
        def->max        = it_opt->second.max;
        //BBS: shown specific filament retract config because we hide the machine retract into comDevelop mode
        if (opt_key =="filament_retraction_length"||
            opt_key=="filament_z_hop" ||
            opt_key== "filament_long_retractions_when_cut" ||
            opt_key=="filament_retraction_distances_when_cut")
            def->mode       = comSimple;
        else
            def->mode       = comAdvanced;
        switch (def->type) {
        case coFloats   : def->set_default_value(new ConfigOptionFloatsNullable  (static_cast<const ConfigOptionFloats*  >(it_opt->second.default_value.get())->values)); break;
        case coPercents : def->set_default_value(new ConfigOptionPercentsNullable(static_cast<const ConfigOptionPercents*>(it_opt->second.default_value.get())->values)); break;
        case coBools    : def->set_default_value(new ConfigOptionBoolsNullable   (static_cast<const ConfigOptionBools*   >(it_opt->second.default_value.get())->values)); break;
        case coEnums    : def->set_default_value(new ConfigOptionEnumsGenericNullable(static_cast<const ConfigOptionEnumsGeneric*   >(it_opt->second.default_value.get())->values)); break;
        default: assert(false);
        }
    }
}

} // namespace Slic3r
