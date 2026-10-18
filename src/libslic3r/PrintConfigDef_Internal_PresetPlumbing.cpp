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

void PrintConfigDef::init_fff_internal_preset_plumbing()
{
    ConfigOptionDef* def;


    // Orca: allow profile maker to set default bed type in machine profile
    // This option won't be shown in the UI
    def = this->add("default_bed_type", coString);
    def->label = L("Default bed type");
    def->tooltip = L("Default bed type for the printer (supports both numeric and string format).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("default_filament_profile", coStrings);
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile when switching to this machine profile.");
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_print_profile", coString);
    def->label = L("Default process profile");
    def->tooltip = L("Default process profile when switching to this machine profile.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_settings_id", coStrings);
    def->set_default_value(new ConfigOptionStrings { "" });
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_ids", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("inherits", coString);
    def->label = L("Inherits profile");
    def->tooltip = L("Name of parent profile.");
    def->full_width = true;
    def->height = 5;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = this->add("inherits_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("host_type", coEnum);
    def->label = L("Host Type");
    def->tooltip = L("Orca Slicer can upload G-code files to a printer host. This field must contain "
                   "the kind of the host.");
    def->enum_keys_map = &ConfigOptionEnum<PrintHostType>::get_enum_values();
    def->enum_values.push_back("prusalink");
    def->enum_values.push_back("prusaconnect");
    def->enum_values.push_back("octoprint");
    def->enum_values.push_back("duet");
    def->enum_values.push_back("ultimaker");
    def->enum_values.push_back("flashair");
    def->enum_values.push_back("astrobox");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("mks");
    def->enum_values.push_back("esp3d");
    def->enum_values.push_back("crealityprint");
    def->enum_values.push_back("obico");
    def->enum_values.push_back("flashforge");
    def->enum_values.push_back("simplyprint");
    def->enum_values.push_back("elegoolink");
    def->enum_values.push_back("3dprinteros");
    def->enum_values.push_back("moonraker");
    def->enum_labels.push_back("PrusaLink");
    def->enum_labels.push_back("PrusaConnect");
    def->enum_labels.push_back("Octo/Klipper");
    def->enum_labels.push_back("Duet");
    def->enum_labels.push_back("UltiMaker");
    def->enum_labels.push_back("FlashAir");
    def->enum_labels.push_back("AstroBox");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("MKS");
    def->enum_labels.push_back("ESP3D");
    def->enum_labels.push_back("CrealityPrint");
    def->enum_labels.push_back("Obico");
    def->enum_labels.push_back("Flashforge");
    def->enum_labels.push_back("SimplyPrint");
    def->enum_labels.push_back("Elegoo Link");
    def->enum_labels.push_back("3DPrinterOS");
    def->enum_labels.push_back("Moonraker (Klipper)");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionEnum<PrintHostType>(htOctoPrint));

    def = this->add("start_end_points", coPoints);
    def->label = L("Start end points");
    def->tooltip  = L("The start and end points which are from the cutter area to the excess chute.");
    def->mode     = comDevelop;
    def->readonly = true;
    // start and end point is from the change_filament_gcode
    def->set_default_value(new ConfigOptionPoints{Vec2d(30, -3), Vec2d(54, 245)});

    def = this->add("plugins", coStrings);
    def->label = L("Plugins Used");
    def->tooltip = L("Plugin capabilities referenced by this preset, stored as name;uuid;capability.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printer_model", coString);
    def->label = L("Printer type");
    def->tooltip = L("Type of the printer.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;
    
    def = this->add("printer_variant", coString);
    def->label = L("Printer variant");
    //def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_settings_id", coString);
    def->set_default_value(new ConfigOptionString());
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString());
    //BBS: open this option to command line

    def = this->add("default_nozzle_volume_type", coEnums);
    // internal use only, don't need translation
    def->label = "Default Nozzle Volume Type.";
    def->tooltip = "Default Nozzle volume type for extruders in this printer.";
    def->enum_keys_map = &ConfigOptionEnum<NozzleVolumeType>::get_enum_values();
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
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnumsGeneric{ NozzleVolumeType::nvtStandard });
}

} // namespace Slic3r
