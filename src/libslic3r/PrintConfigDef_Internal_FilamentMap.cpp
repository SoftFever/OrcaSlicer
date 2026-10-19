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

void PrintConfigDef::init_fff_internal_filament_map()
{
    ConfigOptionDef* def;


    def = this->add("filament_map", coInts);
    def->label = L("Filament map to extruder");
    def->tooltip = L("Filament map to extruder.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{1});

    // Multi-nozzle: per-filament map to the config slot identified by (extruder, nozzle_volume_type).
    // Engine-internal per-filament slot map: recomputed at apply time from filament_map (plus the
    // per-filament volume map when an extruder exposes several volume types) and used to key the
    // retract-override fallback; never a user input and never part of the exported full config.
    // Internal use only, no translation.
    def = this->add("filament_map_2", coInts);
    def->label = "Filament map plus for multi nozzle";
    def->tooltip = "Filament map to the index identified by extruder and nozzle_volume_type";
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{1});

    // Per-filament nozzle-volume-type override (multi-volume/Hybrid). Round-trips through .3mf
    // plate metadata (filament_volume_maps) and steers per-filament slot resolution when an
    // extruder exposes several volume types. Producers: the GUI full-config composition injects
    // a filament-count-sized map (plate map, else per-extruder defaults) and the project config
    // keeps it sized; expansion trusts the map only when its size matches the filament count
    // (CLI runs may still carry the 1-element default).
    def = this->add("filament_volume_map", coInts);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{(int)(NozzleVolumeType::nvtStandard)});

    def = this->add("physical_extruder_map",coInts);
    // internal use only, don't need translation
    def->label = "Map the logical extruder to physical extruder";
    def->tooltip = "Map the logical extruder to physical extruder.";
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("filament_map_mode", coEnum);
    // internal use only, don't need translation
    def->label = "filament mapping mode";
    def->tooltip = "Filament mapping mode used as plate param.";
    def->enum_keys_map = &ConfigOptionEnum<FilamentMapMode>::get_enum_values();
    def->enum_values.push_back("Auto For Flush");
    def->enum_values.push_back("Auto For Match");
    def->enum_values.push_back("Manual");
    def->enum_values.push_back("Nozzle Manual");
    def->enum_values.push_back("Default");
    def->enum_labels.push_back(L("Auto For Flush"));
    def->enum_labels.push_back(L("Auto For Match"));
    def->enum_labels.push_back(L("Manual"));
    def->enum_labels.push_back(L("Nozzle Manual"));
    def->enum_labels.push_back(L("Default"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<FilamentMapMode>(fmmAutoForFlush));

    // Per-filament -> physical-nozzle assignment. Written back by the grouping engine after
    // slicing (all non-sequential modes) and read back to the plate config; a user input only
    // in fully-manual mode (fmmNozzleManual). Dumped in the g-code header for diagnostics.
    // Internal use only, no translation.
    def = this->add("filament_nozzle_map", coInts);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInts{1});

    def = this->add("enable_filament_dynamic_map", coBool);
    def->label = L("Enable filament dynamic map");
    def->tooltip = L("Enable dynamic filament mapping during print.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("filament_self_index", coInts);
    // internal use only, don't need translation
    def->label = "Filament self index";
    def->tooltip = "Filament self index.";
    def->set_default_value(new ConfigOptionInts { 1 });
    def->cli = ConfigOptionDef::nocli;
}

} // namespace Slic3r
