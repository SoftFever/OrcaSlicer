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

void PrintConfigDef::init_fff_internal_extruder_variants()
{
    ConfigOptionDef* def;


    def = this->add("extruder", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Extruder");
    def->category = L("Extruders");
    //def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
    //               "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back(L("default"));  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5");
    def->mode = comAdvanced;

    def = this->add("nozzle_height", coFloat);
    def->label = L("Nozzle height");
    def->tooltip = L("The height of nozzle tip.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def = this->add("grab_length",coFloats);
    def->label = L("Grab length");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats({0}));

    def = this->add("extruder_colour", coStrings);
    def->label = L("Extruder Color");
    def->tooltip = L("Only used as a visual help on UI.");
    def->gui_type = ConfigOptionDef::GUIType::color;
    // Empty string means no color assigned yet.
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "" });

    def = this->add("nozzle_flush_dataset", coInts);
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable{0});

    def = this->add("extruder_type", coEnums);
    // internal use only, don't need translation
    def->label = "Type";
    def->tooltip = "This setting is only used for initial value of manual calibration of pressure advance. Bowden extruder usually has larger PA value. This setting doesn't influence normal slicing.";
    def->enum_keys_map = &ConfigOptionEnum<ExtruderType>::get_enum_values();
    def->enum_values.push_back("Direct Drive");
    def->enum_values.push_back("Bowden");
    def->enum_labels.push_back(L("Direct Drive"));
    def->enum_labels.push_back(L("Bowden"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{ ExtruderType::etDirectDrive });

    def = this->add("extruder_variant_list", coStrings);
    // internal use only, don't need translation
    def->label = "Extruder variant list";
    def->tooltip = "Extruder variant list.";
    def->set_default_value(new ConfigOptionStrings { "Direct Drive Standard" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("extruder_ams_count", coStrings);
    // internal use only, don't need translation
    def->label = "Extruder AMS count";
    def->tooltip = "AMS counts per extruder.";
    def->set_default_value(new ConfigOptionStrings { });

    // Multi-nozzle: per-extruder physical nozzle inventory by volume type,
    // "<volume_type>#<count>" tokens joined by "|". Internal use only, no translation.
    def = this->add("extruder_nozzle_stats", coStrings);
    def->label = "Extruder nozzle stats";
    def->tooltip = "Physical nozzle counts per extruder, keyed by nozzle volume type.";
    def->set_default_value(new ConfigOptionStrings { });

    // Multi-nozzle: number of physical nozzles per extruder. Forward-compat-only registration
    // with no slicing consumer: the nozzle-assignment engine derives its per-extruder physical nozzle
    // inventory from the richer `extruder_nozzle_stats` key, not from this scalar count, so nothing in
    // src/ reads it. Kept registered so an H2C project/config that carries the key loads without an
    // unknown-option substitution warning. Default {1} == today's single-nozzle-per-extruder assumption,
    // so absent/default is a no-op for every shipping printer. Internal use only, no translation.
    def = this->add("extruder_nozzle_count", coInts);
    def->label = "extruder nozzle count";
    def->tooltip = "extruder nozzle count";
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{1});

    // Per-nozzle volume type. Forward-compat-only registration with no slicing consumer — nothing in
    // src/ reads it; the engine resolves per-nozzle volume types from `extruder_nozzle_stats` tokens
    // instead. Kept registered so a project/config carrying it loads without an unknown-option
    // substitution warning. Registers the physical types only (no Hybrid).
    // Internal use only, no translation.
    def = this->add("extruder_nozzle_volume_type", coEnums);
    def->label = "Extruder nozzle volume type";
    def->tooltip = "Nozzle volume type per physical nozzle of an extruder.";
    def->enum_keys_map = &ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
    def->enum_values.push_back("Standard");
    def->enum_values.push_back("High Flow");
    def->enum_values.push_back("TPU High Flow");
    def->enum_values.push_back("E3D High Flow");
    def->enum_values.push_back("Extra High Flow");
    def->enum_labels.push_back("Standard");
    def->enum_labels.push_back("High Flow");
    def->enum_labels.push_back("TPU High Flow");
    def->enum_labels.push_back("E3D High Flow");
    def->enum_labels.push_back("Extra High Flow");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnumsGeneric{ NozzleVolumeType::nvtStandard });

    def = this->add("extruder_max_nozzle_count", coInts);
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionIntsNullable{ 1 });
}

} // namespace Slic3r
