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

void PrintConfigDef::init_fff_filament_filament()
{
    ConfigOptionDef* def;

    const int max_temp = 1500;


    // BBS
    def = this->add("supertack_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("This is the bed temperature for layers except for the first one. A value of 0 means the filament does not support printing on the Cool Plate SuperTack.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{35});

    def = this->add("cool_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("This is the bed temperature for layers except for the first one. A value of 0 means the filament does not support printing on the Cool Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("textured_cool_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("This is the bed temperature for layers except for the first one. A value of 0 means the filament does not support printing on the Textured Cool Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 40 });

    def = this->add("eng_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("This is the bed temperature for layers except for the first one. A value of 0 means the filament does not support printing on the Engineering Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("hot_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("This is the bed temperature for layers except for the first one. A value of 0 means the filament does not support printing on the High Temp Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("textured_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("This is the bed temperature for layers except for the first one. A value of 0 means the filament does not support printing on the Textured PEI Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{45});

    def = this->add("supertack_plate_temp_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("This is the bed temperature of the first layer. A value of 0 means the filament does not support printing on the Cool Plate SuperTack.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("cool_plate_temp_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("This is the bed temperature of the first layer. A value of 0 means the filament does not support printing on the Cool Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("textured_cool_plate_temp_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("This is the bed temperature of the first layer. A value of 0 means the filament does not support printing on the Textured Cool Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 40 });

    def = this->add("eng_plate_temp_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("This is the bed temperature of the first layer. A value of 0 means the filament does not support printing on the Engineering Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("hot_plate_temp_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("This is the bed temperature of the first layer. A value of 0 means the filament does not support printing on the High Temp Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("textured_plate_temp_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("This is the bed temperature of the first layer. A value of 0 means the filament does not support printing on the Textured PEI Plate.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{45});

    def = this->add("filament_flow_ratio", coFloats);
    def->label = L("Flow ratio");
    def->tooltip = L("The material may have volumetric change after switching between molten and crystalline states. "
                     "This setting changes all extrusion flow of this filament in G-code proportionally. "
                     "The recommended value range is between 0.95 and 1.05. "
                     "You may be able to tune this value to get a nice flat surface if there is slight overflow or underflow.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable { 1. });

    def = this->add("enable_pressure_advance", coBools);
    def->label = L("Enable pressure advance");
    def->tooltip = L("Enable pressure advance, auto calibration result will be overwritten once enabled.");
    def->set_default_value(new ConfigOptionBools{ false });

    def = this->add("pressure_advance", coFloats);
    def->label = L("Pressure advance");
    def->tooltip = L("Pressure advance (Klipper) AKA Linear advance factor (Marlin).");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.02 });
    
    // Orca: Adaptive pressure advance option and calibration values
    def = this->add("adaptive_pressure_advance", coBools);
    def->label = L("Enable adaptive pressure advance (beta)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("With increasing print speeds (and hence increasing volumetric flow through the nozzle) and increasing accelerations, "
                     "it has been observed that the effective PA value typically decreases. "
                     "This means that a single PA value is not always 100% optimal for all features and a compromise value is usually used "
                     "that does not cause too much bulging on features with lower flow speed and accelerations while also not causing gaps on faster features.\n\n"
                     "This feature aims to address this limitation by modeling the response of your printer's extrusion system depending "
                     "on the volumetric flow speed and acceleration it is printing at. Internally, it generates a fitted model that can extrapolate the needed pressure "
                     "advance for any given volumetric flow speed and acceleration, which is then emitted to the printer depending on the current print conditions.\n\n"
                     "When enabled, the pressure advance value above is overridden. However, a reasonable default value above is "
                     "strongly recommended to act as a fallback and for when tool changing.\n\n");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools{ false });

    // Orca: Adaptive pressure advance option and calibration values
    def = this->add("adaptive_pressure_advance_model", coStrings);
    def->label = L("Adaptive pressure advance measurements (beta)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Add sets of pressure advance (PA) values, the volumetric flow speeds and accelerations they were measured at, separated by a comma. "
                     "One set of values per line. For example\n"
                     "0.04,3.96,3000\n0.033,3.96,10000\n0.029,7.91,3000\n0.026,7.91,10000\n\n"
                     "How to calibrate:\n"
                     "1. Run the pressure advance test for at least 3 speeds per acceleration value. It is recommended that the test is run "
                     "for at least the speed of the external perimeters, the speed of the internal perimeters and the fastest feature "
                     "print speed in your profile (usually its the sparse or solid infill). Then run them for the same speeds for the slowest and fastest print accelerations, "
                     "and no faster than the recommended maximum acceleration as given by the Klipper input shaper\n"
                     "2. Take note of the optimal PA value for each volumetric flow speed and acceleration. You can find the flow number by selecting "
                     "flow from the color scheme drop down and move the horizontal slider over the PA pattern lines. The number should be visible "
                     "at the bottom of the page. The ideal PA value should be decreasing the higher the volumetric flow is. "
                     "If it is not, confirm that your extruder is functioning correctly. The slower and with less acceleration you print, "
                     "the larger the range of acceptable PA values. If no difference is visible, use the PA value from the faster test\n"
                     "3. Enter the triplets of PA values, Flow and Accelerations in the text box here and save your filament profile.");
    def->mode = comAdvanced;
    //def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 15;
    def->set_default_value(new ConfigOptionStrings{"0,0,0\n0,0,0"});
    
    // xgettext:no-c-format, no-boost-format
    def = this->add("adaptive_pressure_advance_overhangs", coBools);
    def->label = L("Enable adaptive pressure advance within features (beta)");
    def->tooltip = L("Enable adaptive PA whenever there are flow changes in a feature, such as line width changes in a corner or overhangs.\n\n" 
					"Not compatible with Prusa printers as they pause to process PA changes, which causes delays and defects.\n\n"
					"This is an experimental option, as if the PA profile is not set accurately, it will cause uniformity issues.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools{ false });

    def = this->add("adaptive_pressure_advance_bridges", coFloats);
    def->label = L("Static pressure advance for bridges");
    def->tooltip = L("Static pressure advance value for bridges. Set to 0 to apply the same pressure advance as \n"
					"equivalent walls (using adaptive settings if enabled).\n\n"
                    "A lower PA value when printing bridges helps reduce the appearance of slight under-extrusion immediately after bridges. "
                    "This is caused by the pressure drop in the nozzle when printing in the air and a lower PA helps counteract this.");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.0 });

    def           = this->add("default_filament_colour", coStrings);
    def->label    = L("Default color");
    def->tooltip  = L("Default filament color.\n"
                      "Right click to reset value to system default.");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{""});

    def = this->add("filament_colour", coStrings);
    def->label = L("Color");
    def->tooltip = L("Only used as a visual help on UI.");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{ "#F2754E" });

    //bbs
    def          = this->add("required_nozzle_HRC", coInts);
    def->label   = L("Required nozzle HRC");
    def->tooltip = L("Minimum HRC of nozzle required to print the filament. A value of 0 means no checking of the nozzle\'s HRC.");
    def->min     = 0;
    def->max     = 500;
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = L("Max volumetric speed");
    def->tooltip = L("This setting is the volume of filament that can be melted and extruded per second. Printing speed is limited by max volumetric speed, in case of too high and unreasonable speed setting. This value cannot be zero.");
    def->sidetext = L(u8"mm³/s");	// cubic millimeters per second, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("filament_diameter", coFloats);
    def->label = L("Diameter");
    def->tooltip = L("Filament diameter is used to calculate extrusion variables in G-code, so it is important that this is accurate and precise.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 1.75 });

    /*
        Large format printers with print volumes in the order of 1m^3 generally use pellets for printing.
        The overall tech is very similar to FDM printing. 
        It is FDM printing, but instead of filaments, it uses pellets.

        The difference here is that where filaments have a filament_diameter that is used to calculate 
        the volume of filament ingested, pellets have a particular flow_coefficient that is empirically 
        devised for that particular pellet.

        pellet_flow_coefficient is basically a measure of the packing density of a particular pellet.
        Shape, material and density of an individual pellet will determine the packing density and
        the only thing that matters for 3d printing is how much of that pellet material is extruded by 
        one turn of whatever feeding mehcanism/gear your printer uses. You can emperically derive that
        for your own pellets for a particular printer model.

        We are translating the pellet_flow_coefficient into filament_diameter so that everything works just like it 
        does already with very minor adjustments.

        filament_diameter = sqrt( (4 * pellet_flow_coefficient) / PI )

        sqrt just makes the relationship between flow_coefficient and volume linear.

        higher packing density -> more material extruded by single turn -> higher pellet_flow_coefficient -> treated as if a filament of larger diameter is being used
        All other calculations remain the same for slicing.
    */

    def = this->add("pellet_flow_coefficient", coFloats);
    def->label = L("Pellet flow coefficient");
    def->tooltip = L("Pellet flow coefficient is empirically derived and allows for volume calculation for pellet printers.\n\nInternally it is converted to filament_diameter. All other volume calculations remain the same.\n\nfilament_diameter = sqrt( (4 * pellet_flow_coefficient) / PI )");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats{ 0.4157 });

    def = this->add("filament_adaptive_volumetric_speed", coBools);
    def->label = L("Adaptive volumetric speed");
    def->tooltip = L("When enabled, the extrusion flow is limited by the smaller of "
        "the fitted value (calculated from line width and layer height) and the user-defined maximum flow."
        " When disabled, only the user-defined maximum flow is applied.\n\n"
        "Note: Experimental and incomplete feature imported from BBS. Functional for some profiles that already have the variable saved.");
    def->mode = comDevelop;
    def->nullable = true;
    def->set_default_value(new ConfigOptionBoolsNullable {false});

    def = this->add("filament_shrink", coPercents);
    def->label = L("Shrinkage (XY)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm). "
        "The part will be scaled in XY to compensate. For multi-material prints, ensure filament shrinkage matches across all used filaments\n"
        "Be sure to allow enough space between objects, as this compensation is done after the checks.");
    def->sidetext = "%";
    def->ratio_over = "";
    def->min = 50;
    def->max = 150;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents{ 100 });
    
    def = this->add("filament_shrinkage_compensation_z", coPercents);
    def->label = L("Shrinkage (Z)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm)."
        " The part will be scaled in Z to compensate.");
    def->sidetext = "%";
    def->ratio_over = "";
    def->min = 50;
    def->max = 150;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents{ 100 });

    def           = this->add("filament_adhesiveness_category", coInts);
    def->label    = L("Adhesiveness Category");
    def->tooltip  = L("Filament category.");
    def->min      = 0;
    def->mode     = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});
    
    def = this->add("filament_density", coFloats);
    def->label = L("Density");
    def->tooltip = L("Filament density, for statistical purposes only.");
    def->sidetext = L(u8"g/cm³");	// grams per cubic centimeter, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_type", coStrings);
    def->label = L("Type");
    def->tooltip = L("Filament material type");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->gui_flags = "show_value";

    // Populate the enum values using the shared material type database
    for (const auto& filament : MaterialType::all()) {
        def->enum_values.push_back(filament.name);
    }

    def->mode = comSimple;
    def->set_default_value(new ConfigOptionStrings { "PLA" });

    def = this->add("filament_soluble", coBools);
    def->label = L("Soluble material");
    def->tooltip = L("Soluble material is commonly used to print supports and support interfaces.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def           = this->add("filament_change_length", coFloats);
    def->label    = L("Filament ramming length");
    def->tooltip  = L("When changing the extruder, it is recommended to extrude a certain length of filament from the original extruder. This helps minimize nozzle oozing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{10});

    def = this->add("filament_is_support", coBools);
    def->label = L("Support material");
    def->tooltip = L("Support material is commonly used to print supports and support interfaces.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    // BBS
    def = this->add("temperature_vitrification", coInts);
    def->label = L("Softening temperature");
    def->tooltip = L("The material softens at this temperature, so when the bed temperature is equal to or greater than this, it\'s highly recommended to open the front door and/or remove the upper glass to avoid clogs.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{ 100 });

    def = this->add("filament_cost", coFloats);
    def->label = L("Price");
    def->tooltip = L("Filament price, for statistical purposes only.");
    def->sidetext = L("money/kg");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_vendor", coStrings);
    def->label = L("Vendor");
    def->tooltip = L("Vendor of filament. For show only.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{L("(Undefined)")});
    def->cli = ConfigOptionDef::nocli;

    def = this->add("nozzle_temperature_initial_layer", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer nozzle temperature");
    def->tooltip = L("Nozzle temperature for printing the first layer with this filament");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("activate_chamber_temp_control",coBools);
    def->label = L("Activate temperature control");
    def->tooltip = L("Enable this option for automated chamber temperature control. "
                     "This option activates the emitting of an M191 command before the \"machine_start_gcode\"\n which sets the chamber temperature and waits until it is reached. "
                     "In addition, it emits an M141 command at the end of the print to turn off the chamber heater, if present.\n\n"
                     "This option relies on the firmware supporting the M191 and M141 commands either via macros or natively and is usually used when an active chamber heater is installed.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{false});

    def = this->add("chamber_temperature", coInts);
    def->label = L("Chamber temperature");
    def->tooltip = L("For high-temperature materials like ABS, ASA, PC, and PA, a higher chamber temperature can help "
                     "suppress or reduce warping and potentially lead to higher interlayer bonding strength. "
                     "However, at the same time, a higher chamber temperature will reduce the efficiency of air "
                     "filtration for ABS and ASA.\n\n"
                     "For PLA, PETG, TPU, PVA, and other low-temperature materials, this option should be disabled "
                     "(set to 0) as the chamber temperature should be low to avoid extruder clogging caused by "
                     "material softening at the heat break.\n\n"
                     "If enabled, this parameter also sets a G-code variable named chamber_temperature, which can be "
                     "used to pass the desired chamber temperature to your print start macro, or a heat soak macro "
                     "like this: PRINT_START (other variables) CHAMBER_TEMP=[chamber_temperature]. "
                     "This may be useful if your printer does not support M141/M191 commands, or if you desire "
                     "to handle heat soaking in the print start macro if no active chamber heater is installed."
                    );
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Chamber temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("chamber_minimal_temperature", coInts);
    def->label = L("Minimal");
    def->tooltip = L("This is the chamber temperature at which printing should start, while the chamber continues heating "
                     "toward the \"Target\" chamber temperature. For example, set the Target to 60 and the Minimal to 50 to "
                     "begin printing once the chamber reaches 50℃, without waiting for the full 60℃.\n\n"
                     "It sets a G-code variable named chamber_minimal_temperature, which can be passed to your print start macro "
                     "or a heat soak macro, like this: PRINT_START (other variables) CHAMBER_MIN_TEMP=[chamber_minimal_temperature].\n\n"
                     "Unlike the \"Target\" chamber temperature, this option does not emit any M141/M191 commands; it only exposes "
                     "the value to your custom G-code. It should not exceed the \"Target\" chamber temperature.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Chamber minimal temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("nozzle_temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Nozzle temperature after the first layer");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("nozzle_temperature_range_low", coInts);
    def->label = L("Min");
    //def->tooltip = L("");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 190 });

    def = this->add("nozzle_temperature_range_high", coInts);
    def->label = L("Max");
    //def->tooltip = L("");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 240 });

    def = this->add("idle_temperature", coInts);
    def->label = L("Idle temperature");
    def->tooltip = L("Nozzle temperature when the tool is currently not used in multi-tool setups. "
                     "This is only used when 'Ooze prevention' is active in Print Settings. Set to 0 to disable.");
    def->sidetext = L(u8"\u2103" /* °C */);	// degrees Celsius, CIS languages need translation
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts{0});
}

} // namespace Slic3r
