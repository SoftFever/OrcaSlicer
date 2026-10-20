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

// Note: the L() macro is already used for LABEL and TOOLTIP
#define new_def(OPT_KEY, TYPE, LABEL, TOOLTIP)         def = this->add(OPT_KEY, TYPE);         def->label = L(LABEL);         def->tooltip = L(TOOLTIP);

ReadOnlySlicingStatesConfigDef::ReadOnlySlicingStatesConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("zhop", coFloat);
    def->label = L("Current Z-hop");
    def->tooltip = L("Contains Z-hop present at the beginning of the custom G-code block.");
}

ReadWriteSlicingStatesConfigDef::ReadWriteSlicingStatesConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("position", coFloats);
    def->label = L("Position");
    def->tooltip = L("Position of the extruder at the beginning of the custom G-code block. If the custom G-code travels somewhere else, "
                     "it should write to this variable so OrcaSlicer knows where it travels from when it gets control back.");

    def = this->add("e_retracted", coFloats);
    def->label = L("Retraction");
    def->tooltip = L("Retraction state at the beginning of the custom G-code block. If the custom G-code moves the extruder axis, "
                     "it should write to this variable so OrcaSlicer de-retracts correctly when it gets control back.");

    def = this->add("e_restart_extra", coFloats);
    def->label = L("Extra de-retraction");
    def->tooltip = L("Currently planned extra extruder priming after de-retraction.");

   def = this->add("e_position", coFloats);
   def->label = L("Absolute E position");
   def->tooltip = L("Current position of the extruder axis. Only used with absolute extruder addressing.");
}

OtherSlicingStatesConfigDef::OtherSlicingStatesConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("current_extruder", coInt);
    def->label = L("Current extruder");
    def->tooltip = L("Zero-based index of currently used extruder.");

    def = this->add("current_object_idx", coInt);
    def->label = L("Current object index");
    def->tooltip = L("Specific for sequential printing. Zero-based index of currently printed object.");

    def = this->add("has_wipe_tower", coBool);
    def->label = L("Has wipe tower");
    def->tooltip = L("Whether or not wipe tower is being generated in the print.");

    def = this->add("initial_extruder", coInt);
    def->label = L("Initial extruder");
    def->tooltip = L("Zero-based index of the first extruder used in the print. Same as initial_tool.");

    def = this->add("initial_tool", coInt);
    def->label = L("Initial tool");
    def->tooltip = L("Zero-based index of the first extruder used in the print. Same as initial_extruder.");

    def = this->add("is_extruder_used", coBools);
    def->label = L("Is extruder used?");
    def->tooltip = L("Vector of booleans stating whether a given extruder is used in the print.");

    def = this->add("num_extruders", coInt);
    def->label   = L("Number of extruders");
    def->tooltip = L("Total number of extruders, regardless of whether they are used in the current print.");

    // Options from PS not used in Orca
    //    def = this->add("initial_filament_type", coString);
    //    def->label = L("Initial filament type");
    //    def->tooltip = L("String containing filament type of the first used extruder.");

    def          = this->add("has_single_extruder_multi_material_priming", coBool);
    def->label   = L("Has single extruder MM priming");
    def->tooltip = L("Are the extra multi-material priming regions used in this print?");

    new_def("initial_no_support_extruder", coInt, L("Initial no support extruder"), L("Zero-based index of the first extruder used for printing without support. Same as initial_no_support_tool."));
    new_def("in_head_wrap_detect_zone", coBool, L("In head wrap detect zone"), L("Indicates if the first layer overlaps with the head wrap zone."));
    new_def("curr_bed_type", coString, L("Current bed type"), L("Name of the currently selected bed plate type (e.g. 'Textured PEI Plate', 'Smooth High Temp Plate')."));
    new_def("imex_mode", coString, L("IDEX/IQEX active mode"), L("Name of the active IDEX/IQEX parallel print mode for this plate (e.g. 'primary', 'mirror', 'copy'). Empty string if IDEX/IQEX is not enabled."));
    new_def("imex_mode_index", coInt, L("IDEX/IQEX active mode index"), L("Zero-based index of the active IDEX/IQEX parallel print mode within imex_mode_names."));
    new_def("imex_mode_gcode", coString, L("IDEX/IQEX active mode G-code"), L("The raw mode G-code template for the active IDEX/IQEX parallel print mode, after placeholder evaluation. Globals defined here flow into machine_start_gcode."));
}

} // namespace Slic3r