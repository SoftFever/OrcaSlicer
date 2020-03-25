#ifndef slic3r_CustomGCode_hpp_
#define slic3r_CustomGCode_hpp_

#include <string>
#include <vector>

namespace Slic3r {

class DynamicPrintConfig;

// Additional Codes which can be set by user using DoubleSlider
static constexpr char ColorChangeCode[] = "M600";
static constexpr char PausePrintCode[]  = "M601";
static constexpr char ToolChangeCode[]  = "tool_change";

enum CustomGcodeType
{
    cgtColorChange,
    cgtPausePrint,
};

namespace CustomGCode {

struct Item
{
    bool operator<(const Item& rhs) const { return this->print_z < rhs.print_z; }
    bool operator==(const Item& rhs) const
    {
        return (rhs.print_z   == this->print_z    ) &&
               (rhs.gcode     == this->gcode      ) &&
               (rhs.extruder  == this->extruder   ) &&
               (rhs.color     == this->color      );
    }
    bool operator!=(const Item& rhs) const { return ! (*this == rhs); }
    
    double      print_z;
    std::string gcode;
    int         extruder;   // Informative value for ColorChangeCode and ToolChangeCode
                            // "gcode" == ColorChangeCode   => M600 will be applied for "extruder" extruder
                            // "gcode" == ToolChangeCode    => for whole print tool will be switched to "extruder" extruder
    std::string color;      // if gcode is equal to PausePrintCode, 
                            // this field is used for save a short message shown on Printer display 
};

enum Mode
{
    Undef,
    SingleExtruder,   // Single extruder printer preset is selected
    MultiAsSingle,    // Multiple extruder printer preset is selected, but 
                      // this mode works just for Single extruder print 
                      // (The same extruder is assigned to all ModelObjects and ModelVolumes).
    MultiExtruder     // Multiple extruder printer preset is selected
};

// string anlogue of custom_code_per_height mode
static constexpr char SingleExtruderMode[] = "SingleExtruder";
static constexpr char MultiAsSingleMode [] = "MultiAsSingle";
static constexpr char MultiExtruderMode [] = "MultiExtruder";

struct Info
{
    Mode mode = Undef;
    std::vector<Item> gcodes;

    bool operator==(const Info& rhs) const
    {
        return  (rhs.mode   == this->mode   ) &&
                (rhs.gcodes == this->gcodes );
    }
    bool operator!=(const Info& rhs) const { return !(*this == rhs); }
};

// If loaded configuration has a "colorprint_heights" option (if it was imported from older Slicer), 
// and if CustomGCode::Info.gcodes is empty (there is no color print data available in a new format
// then CustomGCode::Info.gcodes should be updated considering this option.
extern void update_custom_gcode_per_print_z_from_config(Info& info, DynamicPrintConfig* config);

// If information for custom Gcode per print Z was imported from older Slicer, mode will be undefined.
// So, we should set CustomGCode::Info.mode should be updated considering code values from items.
extern void check_mode_for_custom_gcode_per_print_z(Info& info);

// Return pairs of <print_z, 1-based extruder ID> sorted by increasing print_z from custom_gcode_per_print_z.
// print_z corresponds to the first layer printed with the new extruder.
std::vector<std::pair<double, unsigned int>> custom_tool_changes(const Info& custom_gcode_per_print_z, size_t num_extruders);

} // namespace CustomGCode

} // namespace Slic3r



#endif /* slic3r_CustomGCode_hpp_ */
