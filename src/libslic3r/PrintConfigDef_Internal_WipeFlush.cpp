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

void PrintConfigDef::init_fff_internal_wipe_flush()
{
    ConfigOptionDef* def;

    const int max_temp = 1500;


    // Fast-purge flush temperature: used only when prime_volume_mode==Fast.
    // Default 0 (fall back to the recommended-range upper bound, identical to filament_flush_temp),
    // so the key is inert for the shipping fleet. Kept out of the g-code config block (banned_keys).
    def = this->add("filament_flush_temp_fast", coInts);
    def->label = L("Flush temperature");
    def->tooltip = L("Flush temperature used in fast purge mode.");
    def->mode = comAdvanced;
    def->nullable = true;
    def->min = 0;
    def->max = max_temp;
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->set_default_value(new ConfigOptionIntsNullable{0});


    def = this->add("support_object_skip_flush", coBool);
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("flush_volumes_vector", coFloats);
    // BBS: remove _L()
    def->label = ("Purging volumes - load/unload volumes");
    //def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
    //                 "wipe tower. These values are used to simplify creation of the full purging "
    //                 "volumes below.");

    // BBS: change 70.f => 140.f
    def->set_default_value(new ConfigOptionFloats { 140.f, 140.f, 140.f, 140.f, 140.f, 140.f, 140.f, 140.f });

    def = this->add("flush_volumes_matrix", coFloats);
    def->label = L("Purging volumes");
    //def->tooltip = L("This matrix describes volumes (in cubic millimeters) required to purge the"
    //                 " new filament on the wipe tower for any given pair of tools.");
    // BBS: change 140.f => 280.f
    def->set_default_value(new ConfigOptionFloats {   0.f, 280.f, 280.f, 280.f,
                                                    280.f,   0.f, 280.f, 280.f,
                                                    280.f, 280.f,   0.f, 280.f,
                                                    280.f, 280.f, 280.f,   0.f });

    def = this->add("flush_multiplier", coFloats);
    def->label = L("Flush multiplier");
    def->tooltip = L("The actual flushing volumes is equal to the flush multiplier value multiplied by the flushing volumes in the table.");
    //def->sidetext = "";
    def->set_default_value(new ConfigOptionFloats{0.3});

    // Fast-purge mode: the flush multiplier used when prime_volume_mode==Fast.
    // Only consumed on the pvmFast branch (default prime_volume_mode==Default reads flush_multiplier),
    // so this key is inert for the shipping fleet. Kept out of the g-code config block (banned_keys).
    def = this->add("flush_multiplier_fast", coFloats);
    def->label = L("Flush multiplier (Fast mode)");
    def->tooltip = L("The flush multiplier used in fast purge mode.");
    def->set_default_value(new ConfigOptionFloats{1.2});

    // Dual-extruder purge control: Default reproduces current behaviour
    // (per-extruder flush_multiplier + filament_prime_volume); Saving reduces the prime volume,
    // Fast selects flush_multiplier_fast + filament_flush_temp_fast. Default pvmDefault = inert.
    def = this->add("prime_volume_mode", coEnum);
    def->label = L("Prime volume mode");
    def->tooltip = L("Selects how the wipe-tower prime and flush volumes are computed on multi-extruder printers.");
    def->enum_keys_map = &ConfigOptionEnum<PrimeVolumeMode>::get_enum_values();
    def->enum_values.push_back("Default");
    def->enum_values.push_back("Saving");
    def->enum_values.push_back("Fast");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Saving"));
    def->enum_labels.push_back(L("Fast"));
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<PrimeVolumeMode>(pvmDefault));

    def = this->add("wipe_tower_x", coFloats);
    //def->label = L("Position X");
    //def->tooltip = L("X coordinate of the left front corner of a wipe tower.");
    //def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comDevelop;
    // BBS: change data type to floats to add partplate logic
    def->set_default_value(new ConfigOptionFloats{ 15. });

    def = this->add("wipe_tower_y", coFloats);
    //def->label = L("Position Y");
    //def->tooltip = L("Y coordinate of the left front corner of a wipe tower.");
    //def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comDevelop;
    // BBS: change data type to floats to add partplate logic
    def->set_default_value(new ConfigOptionFloats{ 220. });

    def = this->add("wiping_volumes_extruders", coFloats);
    def->label = L("Purging volumes - load/unload volumes");
    def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
                     "wipe tower. These values are used to simplify creation of the full purging "
                     "volumes below.");
    def->set_default_value(new ConfigOptionFloats { 70., 70., 70., 70., 70., 70., 70., 70., 70., 70.  });

    def      = this->add("prime_tower_flat_ironing", coBool);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    // Internal marker (not shown in any settings tab): identifies the auto-generated
    // belt purge prism so it can be updated/removed by the auto-manager and aligned
    // to the object layer grid by the backend. Persisted to 3mf like any per-object key.
    def = this->add("belt_purge_tower_object", coBool);
    def->category = L("Flush options");
    def->label = L("Belt purge tower object");
    def->tooltip = L("Marks the auto-generated belt purge prism. Managed automatically; do not set manually.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    // Printer flag gating the fast-purge mode selector in the Multi-Filament
    // UI. comDevelop, default false; profiles of printers that support fast purge
    // enable it, which shows the purge-mode selector for those printers.
    def = this->add("support_fast_purge_mode", coBool);
    def->label = L("Support fast purge mode");
    def->tooltip = L("Whether this printer supports fast purge mode with optimized temperature and multiplier.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));
}

} // namespace Slic3r
