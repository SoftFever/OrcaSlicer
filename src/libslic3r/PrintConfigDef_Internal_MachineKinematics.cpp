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

void PrintConfigDef::init_fff_internal_machine_kinematics()
{
    ConfigOptionDef* def;


    def = this->add("has_scarf_joint_seam", coBool);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    {
        struct AxisDefault {
            std::string         name;
            std::vector<double> max_feedrate;
            std::vector<double> max_acceleration;
            std::vector<double> max_jerk;
        };
        std::vector<AxisDefault> axes {
            // name, max_feedrate,  max_acceleration, max_jerk
            { "x", { 500., 200. }, {  1000., 1000. }, { 10. , 10.  } },
            { "y", { 500., 200. }, {  1000., 1000. }, { 10. , 10.  } },
            { "z", {  12.,  12. }, {   500.,  200. }, {  0.2,  0.4 } },
            { "e", { 120., 120. }, {  5000., 5000. }, {  2.5,  2.5 } }
        };
        for (const AxisDefault &axis : axes) {
            std::string axis_upper = boost::to_upper_copy<std::string>(axis.name);
            // Add the machine feedrate limits for XYZE axes. (M203)
            def = this->add("machine_max_speed_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum speed %1%") % axis_upper).str();
            (void)L("Maximum speed X");
            (void)L("Maximum speed Y");
            (void)L("Maximum speed Z");
            (void)L("Maximum speed E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum speed of the %1% axis") % axis_upper).str();
            (void)L("Maximum speed of the X axis");
            (void)L("Maximum speed of the Y axis");
            (void)L("Maximum speed of the Z axis");
            (void)L("Maximum speed of the E axis");
            def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
            def->min = 0;
            def->mode = comSimple;
            def->set_default_value(new ConfigOptionFloats(axis.max_feedrate));
            // Add the machine acceleration limits for XYZE axes (M201)
            def = this->add("machine_max_acceleration_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
            def->min = 0;
            def->mode = comSimple;
            def->set_default_value(new ConfigOptionFloats(axis.max_acceleration));
            // Add the machine jerk limits for XYZE axes (M205)
            def = this->add("machine_max_jerk_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
            def->min = 0;
            def->mode = comSimple;
            def->set_default_value(new ConfigOptionFloats(axis.max_jerk));
        }
    }
    // M205 J... [mm] machine junction deviation limits
    def = this->add("machine_max_junction_deviation", coFloats);
    def->full_label = L("Maximum Junction Deviation");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum junction deviation (M205 J, only apply if JD > 0 for Marlin Firmware\nIf your Marlin 2 printer uses Classic Jerk set this value to 0.)");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0.f;
    def->max = 0.3f;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0.01f });

    // M205 S... [mm/sec]
    def = this->add("machine_min_extruding_rate", coFloats);
    def->full_label = L("Minimum speed for extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum speed for extruding (M205 S)");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M205 T... [mm/sec]
    def = this->add("machine_min_travel_rate", coFloats);
    def->full_label = L("Minimum travel speed");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum travel speed (M205 T)");
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // Bedslinger mass/force limits.
    // Consumed by GCode::mass_load_limited_machine_acceleration (curr_y_acceleration_limit)
    // and the printed-mass check. Default 0 keeps them inactive for existing printers.
    def = this->add("machine_max_force_Y", coFloat);
    def->full_label = L("Maximum force of the Y axis");
    def->category   = L("Machine limits");
    def->readonly   = false;
    def->tooltip    = L("The allowed maximum output force of Y axis");
    def->sidetext   = L_CONTEXT("N", "Newton");	// Newtons, CIS languages need translation
    def->min        = 0;
    def->mode       = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0));

    def             = this->add("machine_bed_mass_Y", coFloat);
    def->full_label = L("Bed mass of the Y axis");
    def->category   = L("Machine limits");
    def->readonly   = false;
    def->tooltip    = L("The machine bed mass load of Y axis");
    def->sidetext   = L_CONTEXT("g", "gram");	// grams, CIS languages need translation
    def->min        = 0;
    def->mode       = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0));

    def             = this->add("machine_max_printed_mass", coFloat);
    def->full_label = L("The allowed max printed mass");
    def->category   = L("Machine limits");
    def->readonly   = false;
    def->tooltip    = L("The allowed max printed mass on a plate");
    def->sidetext   = L_CONTEXT("g", "gram");	// grams, CIS languages need translation
    def->min        = 0;
    def->mode       = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0));

    // M204 P... [mm/sec^2]
    def = this->add("machine_max_acceleration_extruding", coFloats);
    def->full_label = L("Maximum acceleration for extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for extruding (M204 P)");
    //                 "Marlin (legacy) firmware flavor will use this also "
    //                 "as travel acceleration (M204 T).");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->readonly = false;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });


    // M204 R... [mm/sec^2]
    def = this->add("machine_max_acceleration_retracting", coFloats);
    def->full_label = L("Maximum acceleration for retracting");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for retracting (M204 R)");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->readonly = false;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = this->add("machine_max_acceleration_travel", coFloats);
    def->full_label = L("Maximum acceleration for travel");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for travel (M204 T), it only applies to Marlin 2.");
    def->sidetext = L(u8"mm/s²");	// millimeters per second per second, CIS languages need translation
    def->min = 0;
    def->readonly = false;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    def = this->add("machine_prepare_compensation_time", coFloat);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(260));
}

} // namespace Slic3r
