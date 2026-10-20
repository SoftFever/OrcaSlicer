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

void PrintConfigDef::init_fff_filament_multimaterial()
{
    ConfigOptionDef* def;

    const int max_temp = 1500;


    def = this->add("filament_flush_temp", coInts);
    def->label = L("Flush temperature");
    def->tooltip = L("Temperature when flushing filament. 0 indicates the upper bound of the recommended nozzle temperature range.");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = max_temp;
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->set_default_value(new ConfigOptionIntsNullable{0});

    def = this->add("filament_flush_volumetric_speed", coFloats);
    def->label = L("Flush volumetric speed");
    def->tooltip = L("Volumetric speed when flushing filament. 0 indicates the max volumetric speed.");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = 200;
    def->sidetext = L(u8"mm³/s");	// cubic millimeters per second, CIS languages need translation
    def->set_default_value(new ConfigOptionFloatsNullable{ 0 });

    def = this->add("filament_loading_speed", coFloats);
    def->label = L("Loading speed");
    def->tooltip = L("Speed used for loading the filament on the wipe tower.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 28. });

    def = this->add("filament_loading_speed_start", coFloats);
    def->label = L("Loading speed at the start");
    def->tooltip = L("Speed used at the very beginning of loading phase.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 3. });

    def = this->add("filament_unloading_speed", coFloats);
    def->label = L("Unloading speed");
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect initial part of unloading just after ramming).");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 90. });

    def = this->add("filament_unloading_speed_start", coFloats);
    def->label = L("Unloading speed at the start");
    def->tooltip = L("Speed used for unloading the tip of the filament immediately after ramming.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 100. });

    def = this->add("filament_toolchange_delay", coFloats);
    def->label = L("Delay after unloading");
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable tool changes with flexible materials "
                   "that may need more time to shrink to original dimensions.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_cooling_moves", coInts);
    def->label = L("Number of cooling moves");
    def->tooltip = L("Filament is cooled by being moved back and forth in the "
                   "cooling tubes. Specify desired number of these moves.");
    def->min = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 4 });

    def = this->add("filament_stamping_loading_speed", coFloats);
    def->label = L("Stamping loading speed");
    def->tooltip = L("Speed used for stamping.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_stamping_distance", coFloats);
    def->label = L("Stamping distance measured from the center of the cooling tube");
    def->tooltip = L("If set to non-zero value, filament is moved toward the nozzle between the individual cooling moves (\"stamping\"). "
                     "This option configures how long this movement should be before the filament is retracted again.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_cooling_initial_speed", coFloats);
    def->label = L("Speed of the first cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating beginning at this speed.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2.2 });

    def = this->add("filament_minimal_purge_on_wipe_tower", coFloats);
    def->label = L("Minimal purge on wipe tower");
    def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
                     "the nozzle may not be known, and the filament pressure is likely not yet stable. "
                     "Before purging the print head into an infill or a sacrificial object, Orca Slicer will always prime "
                     "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->sidetext = L(u8"mm³");	// cubic millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 15. });

    def = this->add("filament_tower_interface_pre_extrusion_dist", coFloats);
    def->label = L("Interface layer pre-extrusion distance");
    def->tooltip = L("Pre-extrusion distance for prime tower interface layer (where different materials meet).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("filament_tower_interface_pre_extrusion_length", coFloats);
    def->label = L("Interface layer pre-extrusion length");
    def->tooltip = L("Pre-extrusion length for prime tower interface layer (where different materials meet).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_tower_ironing_area", coFloats);
    def->label = L("Tower ironing area");
    def->tooltip = L("Ironing area for prime tower interface layer (where different materials meet).");
    def->sidetext = L(u8"mm²");	// square millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 4. });

    def = this->add("filament_tower_interface_purge_volume", coFloats);
    def->label = L("Interface layer purge length");
    def->tooltip = L("Purge length for prime tower interface layer (where different materials meet).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 20. });

    def = this->add("filament_tower_interface_print_temp", coInts);
    def->label = L("Interface layer print temperature");
    def->tooltip = L("Print temperature for prime tower interface layer (where different materials meet). If set to -1, use max recommended nozzle temperature.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = -1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { -1 });

    def = this->add("filament_cooling_final_speed", coFloats);
    def->label = L("Speed of the last cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating towards this speed.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 3.4 });

    def = this->add("filament_multitool_ramming", coBools);
    def->label = L("Enable ramming for multi-tool setups");
    def->tooltip = L("Perform ramming when using multi-tool printer (i.e. when the 'Single Extruder Multimaterial' in Printer Settings is unchecked). "
                     "When checked, a small amount of filament is rapidly extruded on the wipe tower just before the tool change. "
                     "This option is only used when the wipe tower is enabled.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_multitool_ramming_volume", coFloats);
    def->label = L("Multi-tool ramming volume");
    def->tooltip = L("The volume to be rammed before the tool change.");
    def->sidetext = L(u8"mm³");	// cubic millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("filament_multitool_ramming_flow", coFloats);
    def->label = L("Multi-tool ramming flow");
    def->tooltip = L("Flow used for ramming the filament before the tool change.");
    def->sidetext = L(u8"mm³/s");	// cubic millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("long_retractions_when_ec", coBools);
    def->label = L("Long retraction when extruder change");
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable {false});

    def = this->add("retraction_distances_when_ec", coFloats);
    def->label = L("Retraction distance when extruder change");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = 10;
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->set_default_value(new ConfigOptionFloatsNullable{10});
}

} // namespace Slic3r
