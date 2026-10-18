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

void PrintConfigDef::init_fff_printer_extruder()
{
    ConfigOptionDef* def;


    def = this->add("extruder_offset", coPoints);
    def->label = L("Extruder offset");
    //def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
    //               "to take it into account. This option lets you specify the displacement of each extruder "
    //               "with respect to the first one. It expects positive coordinates (they will be subtracted "
    //               "from the XY coordinate).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints { Vec2d(0,0) });

    def = this->add("nozzle_type", coEnums);
    def->label = L("Nozzle type");
    def->tooltip = L("The metallic material of the nozzle: This determines the abrasive resistance of the nozzle and what kind of filament can be printed.");
    def->enum_keys_map = &ConfigOptionEnum<NozzleType>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("hardened_steel");
    def->enum_values.push_back("stainless_steel");
    def->enum_values.push_back("tungsten_carbide");
    def->enum_values.push_back("brass");
    def->enum_labels.push_back(L("Undefined"));
    def->enum_labels.push_back(L("Hardened steel"));
    def->enum_labels.push_back(L("Stainless steel"));
    def->enum_labels.push_back(L("Tungsten carbide"));
    def->enum_labels.push_back(L("Brass"));
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionEnumsGenericNullable({ ntUndefine }));

    def = this->add("max_layer_height", coFloats);
    def->label = L("Max");
    def->tooltip = L("The highest printable layer height for the extruder. "
                     "Used to limit the maximum layer height when enable adaptive layer height.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("min_layer_height", coFloats);
    def->label = L("Min");
    def->tooltip = L("The lowest printable layer height for the extruder. "
                     "Used to limit the minimum layer height when enable adaptive layer height.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.07 });

    def = this->add("nozzle_diameter", coFloats);
    def->label = L("Nozzle diameter");
    def->tooltip = L("The diameter of nozzle.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->max = 100;
    def->set_default_value(new ConfigOptionFloats { 0.4 });

    def = this->add("nozzle_volume", coFloats);
    def->label = L("Nozzle volume");
    def->tooltip = L("Volume of nozzle between the filament cutter and the end of the nozzle");
    def->sidetext = L(u8"mm³");	// cubic millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->readonly = false;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 0.0 });

    def = this->add("long_retractions_when_cut", coBools);
    def->label = L("Long retraction when cut (beta)");
    def->tooltip = L("Experimental feature: Retracting and cutting off the filament at a longer distance during changes to minimize purge. "
                     "While this reduces flush significantly, it may also raise the risk of nozzle clogs or other printing problems.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBools {false});

    def = this->add("retraction_distances_when_cut",coFloats);
    def->label = L("Retraction distance when cut");
    def->tooltip = L("Experimental feature: Retraction length before cutting off during filament change.");
    def->mode = comDevelop;
    def->min = 10;
    def->max = 18;
    def->set_default_value(new ConfigOptionFloats {18});

    def = this->add("retract_length_toolchange", coFloats);
    def->label = L("Retraction Length (Toolchange)");
    def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
                  "by the specified amount (the length is measured on raw filament, before it enters "
                  "the extruder).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    //BBS
    def = this->add("nozzle_volume_type", coEnums);
    // internal use only, don't need translation
    def->label = "Nozzle Volume Type";
    def->tooltip = "Nozzle volume type for extruders.";
    def->enum_keys_map = &ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
    // Listed in display order. A position is not the enum value (E3D High Flow is 5, after the reserved 4),
    // so map a position to its NozzleVolumeType through enum_keys_map.
    def->enum_values.push_back(L("Standard"));
    def->enum_values.push_back(L("High Flow"));
    def->enum_values.push_back(L("Hybrid"));
    def->enum_values.push_back(L("TPU High Flow"));
    def->enum_values.push_back(L("E3D High Flow"));
    def->enum_values.push_back(L("Extra High Flow"));
    def->enum_labels.push_back(L("Standard"));
    def->enum_labels.push_back(L("High Flow"));
    def->enum_labels.push_back(L("Hybrid"));
    def->enum_labels.push_back(L("TPU High Flow"));
    def->enum_labels.push_back(L("E3D High Flow"));
    def->enum_labels.push_back(L("Extra High Flow"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnumsGeneric{ NozzleVolumeType::nvtStandard });

    def = this->add("retract_restart_extra_toolchange", coFloats);
    def->label = L("Extra length on restart (Toolchange)");
    def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
                  "this additional amount of filament.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });
}

} // namespace Slic3r
