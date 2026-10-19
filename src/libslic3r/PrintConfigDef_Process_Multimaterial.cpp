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

void PrintConfigDef::init_fff_process_multimaterial()
{
    ConfigOptionDef* def;

    const int max_temp = 1500;


    def = this->add("sparse_infill_filament_id", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Infill");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print internal sparse infill.\n\"Default\" uses the active object/part filament.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("interface_shells", coBool);
    def->label = L("Interface shells");
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                  "Useful for multi-extruder prints with translucent materials or manual soluble "
                  "support material.");
    def->category = L("Quality");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("mmu_segmented_region_max_width", coFloat);
    def->label    = L("Maximum width of a segmented region");
    def->tooltip  = L("Maximum width of a segmented region. A value of 0 disables this feature.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("mmu_segmented_region_interlocking_depth", coFloat);
    def->label    = L("Interlocking depth of a segmented region");
    def->tooltip  = L("Interlocking depth of a segmented region. On every other layer, the segmented region "
                    "is limited to this depth. It will be ignored if \"interlocking_beam\" is enabled. "
                    "Zero disables this feature.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation 
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("interlocking_beam", coBool);
    def->label    = L("Use beam interlocking");
    def->tooltip  = L("Generate interlocking beam structure at the locations where different filaments touch. This improves the adhesion between filaments, especially models printed in different materials.");
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("interlocking_beam_width", coFloat);
    def->label    = L("Interlocking beam width");
    def->tooltip  = L("The width of the interlocking structure beams.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0.01;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def           = this->add("interlocking_orientation", coFloat);
    def->label    = L("Interlocking direction");
    def->tooltip  = L("Orientation of interlock beams.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 0;
    def->max      = 360;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(22.5));

    def           = this->add("interlocking_beam_layer_count", coInt);
    def->label    = L("Interlocking beam layers");
    def->tooltip  = L("The height of the beams of the interlocking structure, measured in number of layers. Less layers is stronger, but more prone to defects.");
    def->min      = 1;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def           = this->add("interlocking_depth", coInt);
    def->label    = L("Interlocking depth");
    def->tooltip  = L("The distance from the boundary between filaments to generate interlocking structure, measured in cells. Too few cells will result in poor adhesion.");
    def->min      = 1;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def           = this->add("interlocking_boundary_avoidance", coInt);
    def->label    = L("Interlocking boundary avoidance");
    def->tooltip  = L("The distance from the outside of a model where interlocking structures will not be generated, measured in cells.");
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def = this->add("ooze_prevention", coBool);
    def->label = L("Enable");
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("outer_wall_filament_id", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Outer walls");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print outer walls.\n\"Default\" uses the active object/part filament.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("inner_wall_filament_id", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Inner walls");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print inner walls.\n\"Default\" uses the active object/part filament.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("internal_solid_filament_id", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Internal solid infill");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print internal solid infill.\n\"Default\" uses the active object/part filament.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("top_surface_filament_id", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Top surface");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print top surface.\n\"Default\" uses the active object/part filament.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("bottom_surface_filament_id", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Bottom surface");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print bottom surface.\n\"Default\" uses the active object/part filament.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("standby_temperature_delta", coInt);
    def->label = L("Temperature variation");
    // TRN PrintSettings : "Ooze prevention" > "Temperature variation"
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                     "The value is not used when 'idle_temperature' in filament settings "
                     "is set to non-zero value.");
    def->sidetext = L(u8"\u2206\u2103" /* ∆°C */);	// delta degrees Celsius, CIS languages need translation
    def->min = -max_temp;
    def->max = max_temp;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(-5));

    def = this->add("preheat_time", coFloat);
    def->label = L("Preheat time");
    def->tooltip = L("To reduce the waiting time after tool change, Orca can preheat the next tool while the current tool is still in use. "
                     "This setting specifies the time in seconds to preheat the next tool. Orca will insert a M104 command to preheat the tool in advance.");
    def->sidetext = L_CONTEXT("s", "second");	// seconds, CIS languages need translation
    def->min = 0;
    def->max = 120;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30.0));

    def = this->add("preheat_steps", coInt);
    def->label = L("Preheat steps");
    def->tooltip = L("Insert multiple preheat commands (e.g. M104.1). Only useful for Prusa XL. For other printers, please set it to 1.");
    //def->sidetext = "";
    def->min = 1;
    def->max = 10;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(1));


    def = this->add("wipe_tower_no_sparse_layers", coBool);
    def->label = L("No sparse layers (beta)");
    def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no tool changes. "
                    "On layers with a tool change, extruder will travel downward to print the wipe tower, "
                    "so the tower ends up below the model and the toolhead has to reach down to it. "
                    "Layouts where that would collide with an already printed object are rejected. "
                    "Has no effect with smooth timelapse or clumping detection, which need a tower on every layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_tower_sparse_layers_combination", coBool);
    def->label = L("Combine sparse layers");
    def->tooltip = L("If enabled, consecutive layers on which the prime tower has no filament change are printed as a single "
                     "thicker tower layer instead of one thin layer each, the same way infill combination merges sparse infill. "
                     "The merged layer is printed at the top of the run, at the height of everything it covers.\n\n"
                     "Only whole layers are merged, and never past the maximum layer height of the nozzle printing the tower "
                     "(three quarters of the nozzle diameter when that is left at 0). Two or more layers therefore have to fit "
                     "under that limit before anything changes at all: at a 0.2 mm layer height under a 0.3 mm maximum nothing "
                     "is merged, while at 0.1 mm three layers become one.\n\n"
                     "Unlike \"No sparse layers\" the tower keeps following the model, so the toolhead never has to reach down to it. "
                     "Has no effect with \"No sparse layers\", smooth timelapse or clumping detection, which need a tower on every layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("single_extruder_multi_material_priming", coBool);
    def->label = L("Prime all printing extruders");
    def->tooltip = L("If enabled, all printing extruders will be primed at the front edge of the print bed at the start of the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("toolchange_ordering", coEnum);
    def->label = L("Toolchange ordering");
    def->category = L("Advanced");
    def->tooltip = L(
        "Determines the order of tool changes on each layer.\n"
        "- Default: Starts with the last used extruder to minimize tool changes.\n"
        "- Cyclic: Uses a fixed tool sequence each layer. This sacrifices speed for better surface quality, as the extra toolchanges allow layers more time to cool."
    );
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<ToolChangeOrderingType>::get_enum_values();
    def->enum_values.emplace_back("default");
    def->enum_values.emplace_back("cyclic");
    def->enum_labels.emplace_back(L("Default"));
    def->enum_labels.emplace_back(L("Cyclic"));
    def->set_default_value(new ConfigOptionEnum<ToolChangeOrderingType>(ToolChangeOrderingType::Default));

    def = this->add("toolchange_cyclic_order", coString);
    def->label = L("Cyclic order");
    def->category = L("Advanced");
    def->tooltip = L(
        "Custom filament sequence used by the cyclic toolchange ordering, as filament numbers separated by commas (e.g. \"3,2,1,4\").\n"
        "Each layer prints its filaments following this sequence; filaments not listed are printed last, in ascending order.\n"
        "Leave empty to cycle through the filaments in ascending order."
    );
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("toolchange_cyclic_first_layer", coBool);
    def->label = L("Apply cyclic order to first layer");
    def->category = L("Advanced");
    def->tooltip = L(
        "Applies the cyclic toolchange order to the first layer as well.\n"
        "By default this is disabled, because the first layer is instead ordered for the best bed "
        "adhesion: filaments that print small, fragile first-layer features are printed last, so the "
        "following tool changes and travel moves are less likely to knock those weakly anchored parts "
        "loose. This first-layer order also honors a custom first layer filament sequence when one is set. "
        "The cyclic order's benefit (extra tool changes give each layer more time to cool) does not apply "
        "to the first layer, which is printed slowly and hot for adhesion.\n"
        "Enable this only if you need the exact same tool sequence on every layer, including the first, at "
        "the cost of that adhesion optimization."
    );
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("belt_purge_tower_width", coFloat);
    def->label = L("Belt purge tower width");
    def->category = L("Printable space");
    def->tooltip = L("Width (machine X, across the belt) of the purge prism that is automatically "
                     "generated on belt printers when the belt purge tower is enabled and multiple "
                     "filaments are used. Filament-change purging is routed into this prism's "
                     "extrusions instead of a classic wipe tower. Its height is computed "
                     "automatically from the worst-case purge volume per layer: a wider prism "
                     "results in a shorter one.");
    def->sidetext = L("mm");
    def->min = 1.;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(35.));

    def = this->add("enable_prime_tower", coBool);
    def->label = L("Enable");
    def->tooltip = L("The wiping tower can be used to clean up residue on the nozzle and stabilize the chamber pressure inside the nozzle in order to avoid appearance defects when printing objects.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("prime_tower_enable_framework", coBool);
    def->label = L("Internal ribs");
    def->tooltip = L("Enable internal ribs to increase the stability of the prime tower.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    // Orca: used by the generic (Type2) wipe tower; also the fallback for filament_prime_volume on Type1.
    def = this->add("prime_volume", coFloat);
    def->label = L("Prime volume");
    def->tooltip = L("This is the volume of material to prime the extruder with on the tower.");
    def->sidetext = L(u8"mm³");	// cubic millimeters, CIS languages need translation
    def->min = 1.0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(45.));

    def = this->add("prime_tower_width", coFloat);
    def->label = L("Width");
    def->tooltip = L("This is the width of prime towers.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 2.0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(60.));

    def = this->add("wipe_tower_rotation_angle", coFloat);
    def->label = L("Wipe tower rotation angle");
    def->tooltip = L("Wipe tower rotation angle with respect to X axis.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("prime_tower_brim_width", coFloat);
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Brim width");
    def->tooltip = L("Brim width of prime tower, negative number means auto calculated width based on the height of prime tower.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->min = -1;
    def->enum_values.push_back("-1");
    def->enum_labels.push_back(L("Auto"));
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("wipe_tower_cone_angle", coFloat);
    def->label = L("Stabilization cone apex angle");
    def->tooltip = L("Angle at the apex of the cone that is used to stabilize the wipe tower. "
                     "Larger angle means wider base.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->mode = comAdvanced;
    def->min = 0.;
    def->max = 90.;
    def->set_default_value(new ConfigOptionFloat(30.0));
    
    def = this->add("wipe_tower_max_purge_speed", coFloat);
    def->label = L("Maximum wipe tower print speed");
    def->tooltip = L("The maximum print speed when purging in the wipe tower and printing the wipe tower sparse layers. "
                     "When purging, if the sparse infill speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.\n\n"
                     "When printing the sparse layers, if the sparse infill speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.\n\n"
                     "Increasing this speed may affect the tower's stability as well as increase the force with which the nozzle collides with any blobs that may have formed on the wipe tower.\n\n"
                     "Before increasing this parameter beyond the default of 90 mm/s, make sure your printer can reliably bridge at the increased speeds and that ooze when tool changing is well controlled.\n\n"
                     "For the wipe tower external perimeters the internal perimeter speed is used, also limited by this setting.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->mode = comAdvanced;
    def->min = 10;
    def->set_default_value(new ConfigOptionFloat(90.));

    def = this->add("wipe_tower_wall_type", coEnum);
    def->label = L("Wall type");
    def->tooltip = L("Wipe tower outer wall type.\n"
                    "1. Rectangle: A rectangle with fixed width and height.\n"
                    "2. Cone: A cone with a fillet at the bottom to help stabilize the wipe tower.\n"
                    "3. Rib: The default wall type, which adds four ribs to the tower wall for enhanced stability.");
    def->enum_keys_map = &ConfigOptionEnum<WipeTowerWallType>::get_enum_values();
    def->enum_values.emplace_back("rectangle");
    def->enum_values.emplace_back("cone");
    def->enum_values.emplace_back("rib");
    def->enum_labels.emplace_back(L("Rectangle"));
    def->enum_labels.emplace_back(L("Cone"));
    def->enum_labels.emplace_back(L("Rib"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WipeTowerWallType>(wtwRib));

    def           = this->add("wipe_tower_extra_rib_length", coFloat);
    def->label    = L("Extra rib length");
    def->tooltip  = L("Positive values can increase the size of the rib wall, while negative values can reduce the size. "
                       "However, the size of the rib wall can not be smaller than that determined by the cleaning volume.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->max      = 300;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("wipe_tower_rib_width", coFloat);
    def->label    = L("Rib width");
    def->tooltip  = L("Rib width is always less than half the prime tower side length.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode     = comAdvanced;
    def->min      = 0;
    def->max      = 300;
    def->set_default_value(new ConfigOptionFloat(8));

    def          = this->add("wipe_tower_fillet_wall", coBool);
    def->label   = L("Fillet wall");
    def->tooltip = L("The wall of prime tower will fillet.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));


    def = this->add("wipe_tower_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Wipe tower");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing perimeter of the wipe tower. "
                     "Set to 0 to use the one that is available (non-soluble would be preferred).");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def          = this->add("prime_tower_skip_points", coBool);
    def->label   = L("Skip points");
    def->tooltip = L("The wall of prime tower will skip the start points of wipe path.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("enable_tower_interface_features", coBool);
    def->label = L("Enable tower interface features");
    def->tooltip = L("Enable optimized prime tower interface behavior when different materials meet.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("enable_tower_interface_cooldown_during_tower", coBool);
    def->label = L("Cool down from interface boost during prime tower");
    def->tooltip = L("When interface-layer temperature boost is active, set the nozzle back to print temperature at the start of the prime tower so it cools down during the tower.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("prime_tower_infill_gap", coPercent);
    def->label    = L("Infill gap");
    def->tooltip  = L("Infill gap.");
    def->sidetext = "%";
    def->mode     = comAdvanced;
    def->min      = 100;
    def->set_default_value(new ConfigOptionPercent(150));

    def = this->add("flush_into_infill", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into objects' infill");
    def->tooltip = L("Purging after filament change will be done inside objects\' infills. This may lower the amount of waste and decrease the print time. If the walls are printed with transparent filament, the mixed color infill will be visible. It will not take effect unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("flush_into_support", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into objects' support");
    def->tooltip = L("Purging after filament change will be done inside objects\' support. This may lower the amount of waste and decrease the print time. It will not take effect unless a prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("flush_into_objects", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into this object");
    def->tooltip = L("This object will be used to purge the nozzle after a filament change to save filament and decrease the print time. "
        "Colors of the objects will be mixed as a result. "
        "It will not take effect unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_tower_bridging", coFloat);
    def->label = L("Maximal bridging distance");
    def->tooltip = L("Maximal distance between supports on sparse infill sections.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("wipe_tower_extra_spacing", coPercent);
    def->label = L("Wipe tower purge lines spacing");
    def->tooltip = L("Spacing of purge lines on the wipe tower.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 100.;
    def->max = 300.;
    def->set_default_value(new ConfigOptionPercent(100.));

    def = this->add("wipe_tower_extra_flow", coPercent);
    def->label = L("Extra flow for purging");
    def->tooltip = L("Extra flow used for the purging lines on the wipe tower. This makes the purging lines thicker "
                     "than they normally would be. The spacing is adjusted automatically.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 100.;
    def->max = 300.;
    def->set_default_value(new ConfigOptionPercent(100.));
}

} // namespace Slic3r
