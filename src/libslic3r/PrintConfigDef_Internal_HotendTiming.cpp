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

void PrintConfigDef::init_fff_internal_hotend_timing()
{
    ConfigOptionDef* def;


    // ---- Multi-nozzle + pre-heat + nozzle-change (nc) config keys ----
    // These back 6-nozzle cluster grouping, the pre-heat/pre-cool time model and wipe-tower
    // nozzle-change handling. Defaults are no-ops for existing single-nozzle printers (all new
    // readers gate on extruder_max_nozzle_count > 1).
    def           = this->add("machine_hotend_change_time", coFloat);
    def->label    = L("Hotend change time");
    def->tooltip  = L("Time to change hotend.");
    def->sidetext = L_CONTEXT("s", "second");
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    // Printer-owned toggle selecting the time-aware grouping objective (flush + change/print
    // time) over the flush-only objective. Default false == today's flush-only grouping, so
    // absent/default is inert. Consumed by the grouping solver (SpeedInfo).
    // Orca: pinned to comDevelop (rather than the default comSimple) to match the sibling multi-nozzle
    // dev keys and keep it out of user selectors.
    def = this->add("group_algo_with_time", coBool);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("enable_pre_heating", coBool);
    def->set_default_value(new ConfigOptionBool(false));

    // Pre-heat injector context key. handle_hotend_as_extruder is read defensively by the injector;
    // registered here (comDevelop, default off) as a known inert key.
    def = this->add("handle_hotend_as_extruder", coBool);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("hotend_cooling_rate", coFloats);
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{2});

    def = this->add("hotend_heating_rate", coFloats);
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{2});
}

} // namespace Slic3r
