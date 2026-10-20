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

void PrintConfigDef::init_fff_internal_filament_mix()
{
    ConfigOptionDef* def;


    // Mixed-color filament. A slot flagged here is virtual: it is not loaded into any
    // physical extruder, but resolved at slicing time into the physical filaments listed
    // in filament_mixed_components, blended either by splitting each layer into
    // sub-layers or by alternating whole layers (see enable_mixed_color_sublayer).
    def          = this->add("filament_is_mixed", coBools);
    def->label   = L("Is mixed filament");
    def->tooltip = L("Whether this filament slot is a mixed filament composed of multiple physical filaments");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionBools{false});

    def          = this->add("filament_mixed_components", coStrings);
    def->label   = L("Mixed filament components");
    def->tooltip = L("Comma-separated 1-based indices of component filaments, e.g. \"1,3\"");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionStrings{""});

    def          = this->add("filament_mixed_sublayer_ratios", coStrings);
    def->label   = L("Mixed filament sublayer ratios");
    def->tooltip = L("Comma-separated ratio values summing to 1.0, e.g. \"0.7,0.3\"");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionStrings{""});

    def          = this->add("filament_mixed_gradient", coBools);
    def->label   = L("Mixed filament gradient");
    def->tooltip = L("Enable Z-direction gradient mode for mixed filament sub-layers. "
                     "When enabled, the sub-layer ratios vary linearly across layers.");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionBools{false});

    def          = this->add("filament_mixed_gradient_range", coStrings);
    def->label   = L("Mixed filament gradient range");
    def->tooltip = L("Start and end ratios for the first component in gradient mode. "
                     "Comma-separated pair, e.g. \"0.10,0.90\" means 10% to 90%.");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionStrings{""});

    def          = this->add("filament_mixed_gradient_curve", coStrings);
    def->label   = L("Mixed filament gradient curve");
    def->tooltip = L("Optional Photoshop-style custom curve mapping Z progress to the first "
                     "component ratio. Encoded as pipe-separated control points, "
                     "either \"x,y\" (legacy) or \"x,y,m_in,m_out\" when a tangent override "
                     "is needed (empty token or \"nan\" means use PCHIP default). "
                     "x in [0,1]; y is clamped to the configured ratio range, "
                     "e.g. \"0,0.15|0.5,0.50|1,0.85\". When empty, the linear "
                     "gradient_range is used instead.");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionStrings{""});

    def          = this->add("filament_mixed_gradient_per_part", coBools);
    def->label   = L("Mixed filament per-part gradient");
    def->tooltip = L("When gradient mode is enabled, apply the gradient to each part of an "
                     "assembly independently rather than treating the whole assembly as one "
                     "Z range.");
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionBools{false});
}

} // namespace Slic3r
