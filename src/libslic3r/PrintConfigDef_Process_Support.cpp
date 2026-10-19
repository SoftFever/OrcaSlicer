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

void PrintConfigDef::init_fff_process_support()
{
    ConfigOptionDef* def;


    def = this->add("bridge_no_support", coBool);
    def->label = L("Don't support bridges");
    def->category = L("Support");
    def->tooltip = L("This disables supporting bridges, which decreases the amount of support required. Bridges can usually be printed directly without support over a reasonable distance.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));


    def = this->add("max_bridge_length", coFloat);
    def->label = L("Max bridge length");
    def->category = L("Support");
    def->tooltip = L("This is the maximum length of bridges that don\'t need support. Set it to 0 if you want all bridges to be supported, and set it to a very large value if you don\'t want any bridges to be supported.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10));
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("raft_contact_distance", coFloat);
    def->label = L("Raft contact Z distance");
    def->category = L("Support");
    def->tooltip = L("Z gap between raft and object. If Support Top Z Distance is 0, this value is ignored and the object is printed in direct contact with the raft (no gap).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def = this->add("raft_expansion", coFloat);
    def->label = L("Raft expansion");
    def->category = L("Support");
    def->tooltip = L("This expands all raft layers in XY plane.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.5));

    def = this->add("raft_first_layer_density", coPercent);
    def->label = L("First layer density");
    def->category = L("Support");
    def->tooltip = L("This is the density of the first raft or support layer.");
    def->sidetext = "%";
    def->min = 10;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(90));

    def = this->add("raft_first_layer_expansion", coFloat);
    def->label = L("First layer expansion");
    def->category = L("Support");
    def->tooltip = L("This expands the first raft or support layer to improve bed adhesion.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    //BBS: change from 3.0 to 2.0
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("raft_layers", coInt);
    def->label = L("Raft layers");
    def->category = L("Support");
    def->tooltip = L("Object will be raised by this number of support layers. "
                     "Use this function to avoid warping when printing ABS.");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(INITIAL_RAFT_LAYERS));
    
    def = this->add("enable_support", coBool);
    //BBS: remove material behind support
    def->label = L("Enable support");
    def->category = L("Support");
    def->tooltip = L("This enables support generation.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_type", coEnum);
    def->label = L("Type");
    def->category = L("Support");
    def->tooltip = L("Normal (auto) and Tree (auto) are used to generate support automatically. "
                     "If Normal (manual) or Tree (manual) is selected, only support enforcers are generated.");
    def->enum_keys_map = &ConfigOptionEnum<SupportType>::get_enum_values();
    def->enum_values.push_back("normal(auto)");
    def->enum_values.push_back("tree(auto)");
    def->enum_values.push_back("normal(manual)");
    def->enum_values.push_back("tree(manual)");
    def->enum_labels.push_back(L("Normal (auto)"));
    def->enum_labels.push_back(L("Tree (auto)"));
    def->enum_labels.push_back(L("Normal (manual)"));
    def->enum_labels.push_back(L("Tree (manual)"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SupportType>(stNormalAuto));

    def = this->add("support_object_xy_distance", coFloat);
    def->label = L("Support/object XY distance");
    def->category = L("Support");
    def->tooltip = L("This controls the XY separation between an object and its support.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    //Support with too small spacing may touch the object and difficult to remove.
    def->set_default_value(new ConfigOptionFloat(0.35));

    def = this->add("support_object_first_layer_gap", coFloat);
    def->label = L("Support/object first layer gap");
    def->category = L("Support");
    def->tooltip = L("XY separation between an object and its support at the first layer.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    //Support with too small spacing may touch the object and difficult to remove.
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_angle", coFloat);
    def->label = L("Pattern angle");
    def->category = L("Support");
    def->tooltip = L("Use this setting to rotate the support pattern on the horizontal plane.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 359;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_on_build_plate_only", coBool);
    def->label = L("On build plate only");
    def->category = L("Support");
    def->tooltip = L("This setting only generates supports that begin on the build plate.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    // BBS
    def           = this->add("support_critical_regions_only", coBool);
    def->label    = L("Support critical regions only");
    def->category = L("Support");
    def->tooltip  = L("Only create support for critical regions including sharp tail, cantilever, etc.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_remove_small_overhang", coBool);
    def->label = L("Ignore small overhangs");
    def->category = L("Support");
    def->tooltip = L("Ignore small overhangs that possibly don't require support.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    // BBS: change type to common float.
    // It may be rounded to mulitple layer height when independent_support_layer_height is false.
    def = this->add("support_top_z_distance", coFloat);
    //def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Top Z distance");
    def->min = 0;
    def->category = L("Support");
    def->tooltip = L("Z gap between the support\'s top and object.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
//    def->min = 0;
#if 0
    //def->enum_values.push_back("0");
    //def->enum_values.push_back("0.1");
    //def->enum_values.push_back("0.2");
    //def->enum_labels.push_back(L("0 (soluble)"));
    //def->enum_labels.push_back(L("0.1 (semi-detachable)"));
    //def->enum_labels.push_back(L("0.2 (detachable)"));
#endif
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_bottom_z_distance", coFloat);
    def->label = L("Bottom Z distance");
    def->category = L("Support");
    def->tooltip = L("Z gap between the object and the support bottom. If Support Top Z Distance is 0 and the bottom has interface layers, this value is ignored and the support is printed in direct contact with the object (no gap).");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("enforce_support_layers", coInt);
    //def->label = L("Enforce support for the first");
    def->category = L("Support");
    //def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
    //               "regardless of whether normal support material is enabled or not and regardless "
    //               "of any angle threshold. This is useful for getting more adhesion of objects "
    //               "having a very thin or poor footprint on the build plate.");
    def->sidetext = L("layers");
    //def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->max = 5000;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label    = L("Support/raft base");
    def->category = L("Support");
    def->tooltip = L("Filament to print support base and raft.\n\"Default\" means no specific filament for support and current filament is used.");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_interface_not_for_body",coBool);
    def->label    = L("Avoid interface filament for base");
    def->category = L("Support");
    def->tooltip = L("Avoid using support interface filament to print support base if possible.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_interface_loop_pattern", coBool);
    def->label = L("Loop pattern interface");
    def->category = L("Support");
    def->tooltip = L("This covers the top contact layer of the supports with loops. It is disabled by default.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_interface_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label    = L("Support/raft interface");
    def->category = L("Support");
    def->tooltip = L("Filament to print support interface.\n\"Default\" means no specific filament for support interface and current filament is used.");
    def->min = 0;
    // BBS
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(0));

    auto support_interface_top_layers = def = this->add("support_interface_top_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Top interface layers");
    def->category = L("Support");
    def->tooltip = L("This is the number of top interface layers.");
    def->sidetext = L("layers");
    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("3");
    def->enum_labels.push_back("0");
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_interface_bottom_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Bottom interface layers");
    def->category = L("Support");
    def->tooltip = L("Number of bottom interface layers.");
    def->sidetext = L("layers");
    def->min = -1;
    def->enum_values.push_back("-1");
    append(def->enum_values, support_interface_top_layers->enum_values);
    def->enum_labels.push_back(L("Same as top"));
    append(def->enum_labels, support_interface_top_layers->enum_labels);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_interface_spacing", coFloat);
    def->label = L("Top interface spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing of interface lines. Zero means solid interface.\n"
                     "Force using solid interface when support ironing is enabled.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    //BBS
    def = this->add("support_bottom_interface_spacing", coFloat);
    def->label = L("Bottom interface spacing");
    def->category = L("Support");
    def->tooltip = L("This is the spacing of bottom interface lines. 0 means solid interface.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("support_base_pattern", coEnum);
    def->label = L("Base pattern");
    def->category = L("Support");
    def->tooltip = L("Line pattern of support.\n\n"
                     "The Default option for Tree supports is Hollow, which means no base pattern. "
                     "For other support types, the Default option is the Rectilinear pattern.\n\n"
                     "NOTE: For Organic supports, the two walls are supported only with the Hollow/Default base pattern. "
                     "The Lightning base pattern is supported only by Tree Slim/Strong/Hybrid supports. "
                     "For the other support types, the Rectilinear will be used instead of Lightning.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("lightning");
    def->enum_values.push_back("hollow");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Rectilinear grid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("Lightning"));
    def->enum_labels.push_back(L("Hollow"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialPattern>(smpDefault));

    def = this->add("support_interface_pattern", coEnum);
    def->label = L("Interface pattern");
    def->category = L("Support");
    def->tooltip = L("This is the line pattern for support interfaces. The default pattern for non-soluble support interfaces is Rectilinear while the default pattern for soluble support interfaces is Concentric.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialInterfacePattern>::get_enum_values();
    def->enum_values.push_back("auto");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
	def->enum_values.push_back("spiralinset");
    def->enum_values.push_back("rectilinear_interlaced");
    def->enum_values.push_back("grid");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
	def->enum_labels.push_back(L("Spiral Inset"));
    def->enum_labels.push_back(L("Rectilinear Interlaced"));
    def->enum_labels.push_back(L("Grid"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialInterfacePattern>(smipAuto));

    def = this->add("support_base_pattern_spacing", coFloat);
    def->label = L("Base pattern spacing");
    def->category = L("Support");
    def->tooltip = L("This determines the spacing between support lines.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def = this->add("support_expansion", coFloat);
    def->label = L("Normal support expansion");
    def->category = L("Support");
    def->tooltip = L("Expand (+) or shrink (-) the horizontal span of normal support.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_style", coEnum);
    def->label = L("Style");
    def->category = L("Support");
    def->tooltip = L("Style and shape of the support. For normal support, projecting the supports into a regular grid "
                     "will create more stable supports (default), while snug support towers will save material and reduce "
                     "object scarring.\n"
                     "For tree support, slim and organic style will merge branches more aggressively and save "
                     "a lot of material (default organic), while hybrid style will create similar structure to normal support "
                     "under large flat overhangs.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialStyle>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("snug");
    def->enum_values.push_back("organic");
    def->enum_values.push_back("tree_slim");
    def->enum_values.push_back("tree_strong");
    def->enum_values.push_back("tree_hybrid");
    def->enum_labels.push_back(L("Default (Grid/Organic)"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Snug"));
    def->enum_labels.push_back(L("Organic"));
    def->enum_labels.push_back(L("Tree Slim"));
    def->enum_labels.push_back(L("Tree Strong"));
    def->enum_labels.push_back(L("Tree Hybrid"));

    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));

    def = this->add("independent_support_layer_height", coBool);
    def->label = L("Independent support layer height");
    def->category = L("Support");
    def->tooltip = L("Support layer uses layer height independent with object layer. This is to support customizing Z-gap and save print time. "
                     "This option will be invalid when the prime tower is enabled.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_threshold_angle", coInt);
    def->label = L("Threshold angle");
    def->category = L("Support");
    def->tooltip = L("Support will be generated for overhangs whose slope angle is below the threshold. The smaller this value is, the steeper the overhang that can be printed without support.\nNote: If set to 0, normal supports use the Threshold overlap instead, while tree supports fall back to a default value of 30.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 90;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(30));

    def = this->add("support_threshold_overlap", coFloatOrPercent);
    def->label = L("Threshold overlap");
    def->category = L("Support");
    def->tooltip = L("If threshold angle is zero, support will be generated for overhangs whose overlap is below the threshold. The smaller this value is, the steeper the overhang that can be printed without support.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max = 100;
    def->max_literal = 0.5;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloatOrPercent(50., true));

    def = this->add("tree_support_branch_angle", coFloat);
    def->label = L("Tree support branch angle");
    def->category = L("Support");
    def->tooltip = L("This setting determines the maximum overhang angle that the branches of tree support are allowed to make. "
                     "If the angle is increased, the branches can be printed more horizontally, allowing them to reach farther.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40.));

    def = this->add("tree_support_branch_angle_organic", coFloat);
    def->label = L("Tree support branch angle");
    def->category = L("Support");
    def->tooltip = L("This setting determines the maximum overhang angle that the branches of tree support are allowed to make. "
                     "If the angle is increased, the branches can be printed more horizontally, allowing them to reach farther.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40.));

    def = this->add("tree_support_angle_slow", coFloat);
    def->label = L("Preferred Branch Angle");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Preferred Branch Angle"
    def->tooltip = L("The preferred angle of the branches, when they do not have to avoid the model. "
                     "Use a lower angle to make them more vertical and more stable. Use a higher angle for branches to merge faster.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 10;
    def->max = 85;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(25));

    def           = this->add("tree_support_branch_distance", coFloat);
    def->label    = L("Tree support branch distance");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the distance between neighboring tree support nodes.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def           = this->add("tree_support_branch_distance_organic", coFloat);
    def->label    = L("Tree support branch distance");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the distance between neighboring tree support nodes.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("tree_support_top_rate", coPercent);
    def->label = L("Branch Density");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Branch Density"
    def->tooltip = L("Adjusts the density of the support structure used to generate the tips of the branches. "
                     "A higher value results in better overhangs but the supports are harder to remove, "
                     "thus it is recommended to enable top support interfaces instead of a high branch density value "
                     "if dense interfaces are needed.");
    def->sidetext = "%";
    def->min = 5;
    def->max_literal = 35;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(30));
    
    def = this->add("tree_support_auto_brim", coBool);
    def->label = L("Auto brim width");
    def->category = L("Quality");
    def->tooltip = L("Enabling this option means the width of the brim for tree support will be automatically calculated.");
    def->set_default_value(new ConfigOptionBool(1));
    
    def = this->add("tree_support_brim_width", coFloat);
    def->label = L("Tree support brim width");
    def->category = L("Quality");
    def->min      = 0.0;
    def->tooltip = L("Distance from tree branch to the outermost brim line.");
    def->set_default_value(new ConfigOptionFloat(3));

    def = this->add("tree_support_tip_diameter", coFloat);
    def->label = L("Tip Diameter");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Tip Diameter"
    def->tooltip = L("Branch tip diameter for organic supports.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0.1f;
    def->max = 100.f;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def           = this->add("tree_support_branch_diameter", coFloat);
    def->label    = L("Tree support branch diameter");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the initial diameter of support nodes.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("tree_support_branch_diameter_angle", coFloat);
    // TRN PrintSettings: #lmFIXME 
    def->label = L("Branch Diameter Angle");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Branch Diameter Angle"
    def->tooltip = L("The angle of the branches' diameter as they gradually become thicker towards the bottom. "
                     "An angle of 0 will cause the branches to have uniform thickness over their length. "
                     "A bit of an angle can increase stability of the organic support.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 15;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5));

    def           = this->add("tree_support_branch_diameter_organic", coFloat);
    def->label    = L("Tree support branch diameter");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the initial diameter of support nodes.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.));

    def = this->add("tree_support_wall_count", coInt);
    def->label = L("Support wall loops");
    def->category = L("Support");
    def->tooltip = L("This setting specifies the count of support walls in the range of [0,2]. 0 means auto.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));
    
    def = this->add("support_ironing", coBool);
    def->label = L("Ironing Support Interface");
    def->category = L("Support");
    def->tooltip = L("Ironing is using small flow to print on same height of support interface again to make it more smooth. "
                     "This setting controls whether support interface being ironed. When enabled, support interface will be extruded as solid too.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def                = this->add("support_ironing_pattern", coEnum);
    def->label         = L("Support Ironing Pattern");
    def->tooltip       = L("The pattern that will be used when ironing.");
    def->category      = L("Support");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    
    def = this->add("support_ironing_flow", coPercent);
    def->label = L("Support Ironing flow");
    def->category = L("Support");
    def->tooltip = L("The amount of material to extrude during ironing. Relative to flow of normal support interface layer height. "
                     "Too high value results in overextrusion on the surface.");
    def->sidetext = "%";
    def->ratio_over = "layer_height";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(10));

    def = this->add("support_ironing_spacing", coFloat);
    def->label = L("Support Ironing line spacing");
    def->category = L("Support");
    def->tooltip = L("This is the distance between the lines used for ironing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));
}

} // namespace Slic3r
