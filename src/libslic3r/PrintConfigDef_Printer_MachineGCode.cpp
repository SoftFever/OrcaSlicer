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

void PrintConfigDef::init_fff_printer_machine_g_code()
{
    ConfigOptionDef* def;


    def = this->add("before_layer_change_gcode", coString);
    def->label = L("Before layer change G-code");
    def->tooltip = L("This G-code is inserted at every layer change before the Z lift.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("machine_end_gcode", coString);
    def->label = L("End G-code");
    def->tooltip = L("Add end G-code when finishing the entire print.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n"));

    def             = this->add("printing_by_object_gcode", coString);
    def->label      = L("Between Object G-code");
    def->tooltip    = L("Insert G-code between objects. This parameter will only come into effect when you print your models object by object.");
    def->multiline  = true;
    def->full_width = true;
    def->height     = 12;
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("layer_change_gcode", coString);
    def->label = L("Layer change G-code");
    def->tooltip = L("This G-code is inserted at every layer change after the Z lift.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("time_lapse_gcode",coString);
    def->label = L("Timelapse G-code");
    def->multiline = true;
    def->full_width = true;
    def->height =5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def             = this->add("wrapping_detection_gcode", coString);
    def->label      = L("Clumping detection G-code");
    def->multiline  = true;
    def->full_width = true;
    def->height     = 5;
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("machine_pause_gcode", coString);
    def->label = L("Pause G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print. Users can insert pause G-code in the G-code viewer.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("template_custom_gcode", coString);
    def->label = L("Custom G-code");
    def->tooltip = L("This G-code will be used as a custom code.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());


    def = this->add("file_start_gcode", coString);
    def->label = L("File header G-code");
    def->tooltip = L("G-code written at the very top of the output file, before any other content. "
                     "Useful for adding metadata that printer firmware reads from the first lines of the file "
                     "(e.g. estimated print time, filament usage). "
                     "Supports placeholders like {print_time_total_sec}, {print_time_day}, {print_time_hour}, {print_time_minute}, {print_time_sec} and {used_filament_length}.");
    def->multiline = true;
    def->full_width = true;
    def->height = 8;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("machine_start_gcode", coString);
    def->label = L("Start G-code");
    def->tooltip = L("G-code added when starting a print.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n"));

    def = this->add("change_filament_gcode", coString);
    def->label = L("Change filament G-code");
    def->tooltip = L("This G-code is inserted when filament is changed, including T commands to trigger tool change.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());
}

} // namespace Slic3r
