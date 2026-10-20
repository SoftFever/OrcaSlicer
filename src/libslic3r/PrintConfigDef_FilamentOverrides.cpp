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

void PrintConfigDef::init_fff_filament_overrides()
{
    ConfigOptionDef* def;


    // Filament ironing overrides
    def = this->add("filament_ironing_flow", coPercents);
    def->label = L("Ironing flow");
    def->category = L("Quality");
    def->tooltip = L("Filament-specific override for ironing flow. This allows you to customize the ironing flow "
                     "for each filament type. Too high value results in overextrusion on the surface.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionPercentsNullable{ ConfigOptionPercentsNullable::nil_value() });

    def = this->add("filament_ironing_spacing", coFloats);
    def->label = L("Ironing line spacing");
    def->category = L("Quality");
    def->tooltip = L("Filament-specific override for ironing line spacing. This allows you to customize the spacing "
                     "between ironing lines for each filament type.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ ConfigOptionFloatsNullable::nil_value() });

    def = this->add("filament_ironing_inset", coFloats);
    def->label = L("Ironing inset");
    def->category = L("Quality");
    def->tooltip = L("Filament-specific override for ironing inset. This allows you to customize the distance to keep "
                     "from the edges when ironing for each filament type.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ ConfigOptionFloatsNullable::nil_value() });

    def = this->add("filament_ironing_speed", coFloats);
    def->label = L("Ironing speed");
    def->category = L("Speed");
    def->tooltip = L("Filament-specific override for ironing speed. This allows you to customize the print speed "
                     "of ironing lines for each filament type.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 1;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{ ConfigOptionFloatsNullable::nil_value() });

    def = this->add("retraction_minimum_travel", coFloats);
    def->label = L("Travel distance threshold");
    def->tooltip = L("Only trigger retraction when the travel distance is longer than this threshold.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("retract_before_wipe", coPercents);
    def->label = L("Retract amount before wipe");
    def->tooltip = L("This is the length of fast retraction before a wipe, relative to retraction length.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents { 100 });

    // Orca:
    def = this->add("retract_after_wipe", coPercents);
    def->label = L("Retract amount after wipe");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("This is the length of fast retraction after wipe, relative to retraction length.\n"
                     "The value will be clamped by 100% minus the retract amount before the wipe value.");
    def->sidetext = "%";
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercents { 0 });

    def = this->add("retract_when_changing_layer", coBools);
    def->label = L("Retract on layer change");
    def->tooltip = L("This forces a retraction on layer changes.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("retraction_length", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length");
    def->tooltip = L("Some amount of material in extruder is pulled back to avoid ooze during long travel. "
                     "Set zero to disable retraction.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 0.8 });

    def = this->add("z_hop", coFloats);
    def->label = L("Z-hop height");
    def->tooltip = L("Whenever there is a retraction, the nozzle is lifted a little to create clearance between the nozzle and the print. This prevents the nozzle from hitting the print when traveling more. Using spiral lines to lift Z can prevent stringing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comSimple;
    def->min = 0;
    def->max = 5;
    def->set_default_value(new ConfigOptionFloats { 0.4 });

    def             = this->add("retract_lift_above", coFloats);
    def->label      = L("Z-hop lower boundary");
    def->tooltip    = L("Z-hop will only come into effect when Z is above this value and is below the parameter: \"Z-hop upper boundary\".");
    def->sidetext   = L("mm");	// millimeters, CIS languages need translation
    def->mode       = comAdvanced;
    def->min        = 0;
    def->set_default_value(new ConfigOptionFloats{0.});

    def             = this->add("retract_lift_below", coFloats);
    def->label      = L("Z-hop upper boundary");
    def->tooltip    = L("If this value is positive, Z-hop will only come into effect when Z is above the parameter: \"Z-hop lower boundary\" and is below this value.");
    def->sidetext   = L("mm");	// millimeters, CIS languages need translation
    def->mode       = comAdvanced;
    def->min        = 0;
    def->set_default_value(new ConfigOptionFloats{0.});

    def = this->add("z_hop_types", coEnums);
    def->label = L("Z-hop type");
    def->tooltip = L("Type of Z-hop.");
    def->enum_keys_map = &ConfigOptionEnum<ZHopType>::get_enum_values();
    def->enum_values.push_back("Auto Lift");
    def->enum_values.push_back("Normal Lift");
    def->enum_values.push_back("Slope Lift");
    def->enum_values.push_back("Spiral Lift");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Normal"));
    def->enum_labels.push_back(L("Slope"));
    def->enum_labels.push_back(L("Spiral"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{ ZHopType::zhtSlope });

    def = this->add("retract_lift_above", coFloats);
    def->label = L("Only lift Z above");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified absolute Z.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{0.});

    def = this->add("retract_lift_below", coFloats);
    def->label = L("Only lift Z below");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below the specified absolute Z.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{0.});

    def = this->add("retract_lift_enforce", coEnums);
    def->label = L("On surfaces");
    def->tooltip = L("Enforce Z-Hop behavior. This setting is impacted by the above settings (Only lift Z above/below).");
    def->enum_keys_map = &ConfigOptionEnum<RetractLiftEnforceType>::get_enum_values();
    def->enum_values.push_back("All Surfaces");
    def->enum_values.push_back("Top Only");
    def->enum_values.push_back("Bottom Only");
    def->enum_values.push_back("Top and Bottom");
    def->enum_labels.push_back(L("All Surfaces"));
    def->enum_labels.push_back(L("Top Only"));
    def->enum_labels.push_back(L("Bottom Only"));
    def->enum_labels.push_back(L("Top and Bottom"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{RetractLiftEnforceType ::rletAllSurfaces});

    def = this->add("retract_restart_extra", coFloats);
    def->label = L("Extra length on restart");
    def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
                  "this additional amount of filament. This setting is rarely needed.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retraction_speed", coFloats);
    def->label = L("Retraction speed");
    def->full_label = L("Retraction speed");
    def->tooltip = L("Speed for retracting filament from the nozzle.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 30. });

    def = this->add("deretraction_speed", coFloats);
    def->label = L("Deretraction speed");
    def->full_label = L("Deretraction speed");
    def->tooltip = L("Speed for reloading filament into the nozzle. Zero means same speed of retraction.");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("wipe", coBools);
    def->label = L("Wipe while retracting");
    def->tooltip = L("This moves the nozzle along the last extrusion path when retracting to clean any leaked material on the nozzle. This can minimize blobs when printing a new part after traveling.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("wipe_distance", coFloats);
    def->label = L("Wipe distance");
    def->tooltip = L("Describe how long the nozzle will move along the last path when retracting.\n\n"
                     "Depending on how long the wipe operation lasts, how fast and long the extruder/filament retraction settings are, "
                     "a retraction move may be needed to retract the remaining filament.\n\n"
                     "Setting a value in the retract amount before wipe setting below will perform any excess retraction before the wipe, else it will be performed after.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 1. });
}

} // namespace Slic3r
