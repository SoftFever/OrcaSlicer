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

void PrintConfigDef::init_fff_internal_retraction()
{
    ConfigOptionDef* def;


    def = this->add("enable_long_retraction_when_cut",coInt);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt {0});

    // Per-(extruder,variant) deretraction speed at an extruder change. This value
    // ships in multi-extruder machine profiles but has no slicer consumer — it is a device/firmware-facing
    // profile key. Registered here (ConfigDef-only, no FullPrintConfig static member) so X2D/P2S profiles
    // that already carry it — and the H2D/A2L leaves — validate as a known key; unread by the slicer.
    // nullable so absent leaves stay nil (excluded from the g-code config block).
    def = this->add("deretract_speed_extruder_change", coFloats);
    def->label = L("Deretraction speed (extruder change)");
    def->tooltip = L("Speed for reloading filament into the nozzle when switching extruder.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0. });
}

} // namespace Slic3r
