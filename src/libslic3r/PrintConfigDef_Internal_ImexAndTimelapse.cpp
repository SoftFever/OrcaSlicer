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

void PrintConfigDef::init_fff_internal_imex_and_timelapse()
{
    ConfigOptionDef* def;


    // Farthest-point timelapse. Corexy-only refinement layered on top of the existing
    // timelapse_type: when enabled the traditional-mode snapshot is
    // taken at the extrusion point farthest from the camera (0,0) instead of traveling to a safe/chute
    // position. Gated in GCode::process_layer by timelapse_type==tlTraditional && printer_structure!=psI3,
    // so it is inert for i3 (A1/A2L) printers and whenever the toggle is off (default false → every
    // existing printer's g-code is byte-identical). Only H2C/H2D machine profiles set it =1.
    def = this->add("farthest_point_timelapse", coBool);
    def->label = L("Farthest point timelapse");
    def->tooltip = L("When enabled, the timelapse snapshot is taken at the farthest point from camera "
                     "instead of traveling to the wipe tower or excess chute. "
                     "Only effective in traditional timelapse mode on non-I3 printers.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("imex_parallel_mode", coString);
    def->label = L("IDEX/IQEX Print Mode");
    def->tooltip = L("Name of the active IDEX/IQEX parallel print mode, or \"primary\" for single-carriage printing. Requires IDEX/IQEX Printer enabled in the Printer preset (Printer \u2192 Multimaterial \u2192 IDEX/IQEX Configuration).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("primary"));

    def = this->add("imex_head_filament_map", coString);
    def->label = L("IDEX/IQEX head filament map");
    def->tooltip = L("Per-plate override mapping physical heads to filament slots "
                     "(1-based). Empty means each head uses the first filament that the "
                     "printer's physical extruder map routes to it.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("imex_mode_names", coStrings);
    def->label = L("IDEX/IQEX Mode Names");
    def->tooltip = L("Display names for each user-defined IDEX/IQEX parallel print mode.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("imex_mode_active_tools", coStrings);
    def->label = L("IDEX/IQEX Mode Active Tools");
    def->tooltip = L("Tool role assignments for each mode. Format: \"idx:P,idx:C,idx:M\" where P=Primary, C=Copy, M=Mirror (e.g. \"0:P,1:C,2:M,3:M\"). Managed by the IDEX/IQEX Modes editor in the Printer preset.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("imex_mode_gcodes", coStrings);
    def->label = L("IDEX/IQEX Mode G-codes");
    def->tooltip = L("G-code or macro call to inject at print start for each mode.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());
}

} // namespace Slic3r
