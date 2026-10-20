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

void PrintConfigDef::init_fff_process_quality()
{
    ConfigOptionDef* def;


    // Maximum extruder temperature, bumped to 1500 to support printing of glass.

    def = this->add("reduce_crossing_wall", coBool);
    def->label = L("Avoid crossing walls");
    def->category = L("Quality");
    def->tooltip = L("This detours to avoid traveling across walls, which may cause blobs on the surface.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(INITIAL_REDUCE_CROSSING_WALL));

    def = this->add("max_travel_detour_distance", coFloatOrPercent);
    def->label = L("Avoid crossing walls - Max detour length");
    def->category = L("Quality");
    def->tooltip = L("Maximum detour distance for avoiding crossing wall: The printer won\'t detour if the detour distance is larger than this value. Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path. A value of 0 will disable this.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 1000;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("bridge_density", coPercent);
    def->label = L("External bridge density");
    def->category = L("Strength");
    def->tooltip = L("Controls the density (spacing) of external bridge lines.\n"
                     "Theoretically, 100% means a solid bridge, but due to the tendency of bridge extrusions to sag, 100% may not be sufficient.\n\n"
                     "- Higher than 100% density (Recommended Max 125%):\n"
                     "  - Pros: Produces smoother bridge surfaces, as overlapping lines provide additional support during printing.\n"
                     "  - Cons: Can cause overextrusion, which may reduce lower and upper surface quality and increase the risk of warping.\n\n"
                     "- Lower than 100% density (Min 10%):\n"
                     "  - Pros: Can create a string-like first layer. Faster and with better cooling because there is more space for air to circulate around the extruded bridge.\n"
                     "  - Cons: May lead to sagging and poorer surface finish.");
    def->sidetext = "%";
    def->min = 10;
    def->max = 125;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("internal_bridge_density", coPercent);
    def->label = L("Internal bridge density");
    def->category = L("Strength");
    def->tooltip = L("Controls the density (spacing) of internal bridge lines.\n"
                     "Internal bridges act as intermediate support between sparse infill and top solid infill and can strongly affect top surface quality.\n\n"
                     "- Higher than 100% density (Recommended Max 125%):\n"
                     "  - Pros: Improves internal bridge strength and support under top layers, reducing sagging and improving top-surface finish.\n"
                     "  - Cons: Increases material use and print time; excessive density may cause overextrusion and internal stresses.\n\n"
                     "- Lower than 100% density (Min 10%):\n"
                     "  - Pros: Can reduce pillowing and improve cooling (more airflow through the bridge), and may speed up printing.\n"
                     "  - Cons: May reduce internal support, increasing the risk of sagging and top surface defects.\n\n"
                     "This option works particularly well when combined with the second internal bridge over infill option to improve bridging further before solid infill is extruded.");
    def->sidetext = "%";
    def->min = 10;
    def->max = 125;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("bridge_flow", coFloat);
    def->label = L("Bridge flow ratio");
    def->category = L("Quality");
    def->tooltip = L("This value governs the thickness of the external (visible) bridge layer.\n"
                     "Values above 1.0: Increase the amount of material while maintaining line spacing. This can improve line contact and strength.\n"
                     "Values below 1.0: Reduce the amount of material while adjusting line spacing to maintain contact. This can improve sagging.\n\n"
                     "The actual bridge flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2.0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("bridge_line_width", coFloatOrPercent);
    def->label = L("Bridge");
    def->category = L("Quality");
    def->tooltip = L("Line width of the Bridge. If expressed as a %, it will be computed over the nozzle diameter.\n"
                     "Recommended to use with a higher Bridge density or Bridge flow ratio.\n\n"
                     "The maximum value is 100% or the nozzle diameter.\n"
                     "If set to 0, the line width will match the Internal solid infill width, or the nozzle diameter for thick bridges.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 100;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100., true));

    def = this->add("internal_bridge_flow", coFloat);
    def->label = L("Internal bridge flow ratio");
    def->category = L("Quality");
    def->tooltip = L("This value governs the thickness of the internal bridge layer. This is the first layer over sparse infill so increasing it may increase strength and upper layer quality.\n"
                     "Values above 1.0: Increase the amount of material while maintaining line spacing. This can improve line contact and strength.\n"
                     "Values below 1.0: Reduce the amount of material while adjusting line spacing to maintain contact. This can improve sagging.\n\n"
                     "The actual bridge flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2.0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("top_solid_infill_flow_ratio", coFloats);
    def->label = L("Top surface flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for top solid infill. "
                     "You can decrease it slightly to have smooth surface finish.\n\n"
                     "The actual top surface flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->nullable = true;
    def->set_default_value(new ConfigOptionFloatsNullable{1});

    def = this->add("bottom_solid_infill_flow_ratio", coFloat);
    def->label = L("Bottom surface flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for bottom solid infill.\n\n"
                     "The actual bottom solid infill flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("set_other_flow_ratios", coBool);
    def->label = L("Set other flow ratios");
    def->category = L("Advanced");
    def->tooltip = L("Change flow ratios for other extrusion path types.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("first_layer_flow_ratio", coFloat);
    def->label = L("First layer flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material on the first layer for the extrusion path roles listed in this section.\n\n"
                     "For the first layer, the actual flow ratio for each path role (does not affect brims and skirts) will be multiplied by this value.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("outer_wall_flow_ratio", coFloat);
    def->label = L("Outer wall flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for outer walls.\n\n"
                     "The actual outer wall flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("inner_wall_flow_ratio", coFloat);
    def->label = L("Inner wall flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for inner walls.\n\n"
                     "The actual inner wall flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("overhang_flow_ratio", coFloat);
    def->label = L("Overhang flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for overhangs.\n\n"
                     "The actual overhang flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("sparse_infill_flow_ratio", coFloat);
    def->label = L("Sparse infill flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for sparse infill.\n\n"
                     "The actual sparse infill flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("internal_solid_infill_flow_ratio", coFloat);
    def->label = L("Internal solid infill flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for internal solid infill.\n\n"
                     "The actual internal solid infill flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("gap_fill_flow_ratio", coFloat);
    def->label = L("Gap fill flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for filling the gaps.\n\n"
                     "The actual gap filling flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_flow_ratio", coFloat);
    def->label = L("Support flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for support.\n\n"
                     "The actual support flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_interface_flow_ratio", coFloat);
    def->label = L("Support interface flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for the support interface.\n\n"
                     "The actual support interface flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("precise_outer_wall",coBool);
    def->label = L("Precise wall");
    def->category = L("Quality");
    def->tooltip  = L("Improve shell precision by adjusting outer wall spacing. This also improves layer consistency. NOTE: This option "
                       "will be ignored for outer-inner or inner-outer-inner wall sequences.");
    def->set_default_value(new ConfigOptionBool{true});

    def = this->add("only_one_wall_top", coBool);
    def->label = L("Only one wall on top surfaces");
    def->category = L("Quality");
    def->tooltip = L("Use only one wall on flat top surfaces, to give more space to the top infill pattern.");
    def->set_default_value(new ConfigOptionBool(false));

    // the tooltip is copied from SuperStudio
    def = this->add("min_width_top_surface", coFloatOrPercent);
    def->label = L("One wall threshold");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("If a top surface has to be printed and it's partially covered by another layer, it won't be considered at a top layer where its width is below this value."
        " This can be useful to not let the 'one perimeter on top' trigger on surface that should be covered only by perimeters."
        " This value can be a mm or a % of the perimeter extrusion width."
        "\nWarning: If enabled, artifacts can be created if you have some thin features on the next layer, like letters. Set this setting to 0 to remove these artifacts.");
    def->sidetext = L("mm or %");
    def->ratio_over = "inner_wall_line_width";
    def->min = 0;
    def->max_literal = 15;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(300, true));

    def = this->add("only_one_wall_first_layer", coBool);
    def->label = L("Only one wall on first layer");
    def->category = L("Quality");
    def->tooltip = L("Use only one wall on first layer, to give more space to the bottom infill pattern.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("extra_perimeters_on_overhangs", coBool);
    def->label = L("Extra perimeters on overhangs");
    def->category = L("Quality");
    def->tooltip = L("Create additional perimeter paths over steep overhangs and areas where bridges cannot be anchored.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("overhang_reverse", coBool);
    def->label = L("Reverse on even");
    def->full_label = L("Overhang reversal");
    def->category = L("Quality");
    def->tooltip = L("Extrude perimeters that have a part over an overhang in the reverse direction on even layers. This alternating pattern can drastically improve steep overhangs.\n\nThis setting can also help reduce part warping due to the reduction of stresses in the part walls.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("overhang_reverse_internal_only", coBool);
    def->label = L("Reverse only internal perimeters");
    def->full_label = L("Reverse only internal perimeters");
    def->category = L("Quality");
    def->tooltip = L("Apply the reverse perimeters logic only on internal perimeters.\n\n"
                     "This setting greatly reduces part stresses as they are now distributed in alternating directions. "
                     "This should reduce part warping while also maintaining external wall quality. "
                     "This feature can be very useful for warp prone material, like ABS/ASA, and also for elastic filaments, like TPU and Silk PLA. "
                     "It can also help reduce warping on floating regions over supports.\n\nFor this setting to be the most effective, "
                     "it is recommended to set the Reverse Threshold to 0 so that all internal walls print in alternating directions on even layers irrespective of their overhang degree.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("counterbore_hole_bridging", coEnum);
    def->label = L("Bridge counterbore holes");
    def->category = L("Quality");
    def->tooltip  = L(
        "This option creates bridges for counterbore holes, allowing them to be printed without support. Available modes include:\n"
         "1. None: No bridge is created\n"
         "2. Partially Bridged: Only a part of the unsupported area will be bridged\n"
         "3. Sacrificial Layer: A full sacrificial bridge layer is created");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<CounterboreHoleBridgingOption>::get_enum_values();
    def->enum_values.emplace_back("none");
    def->enum_values.emplace_back("partiallybridge");
    def->enum_values.emplace_back("sacrificiallayer");
    def->enum_labels.emplace_back(L("None"));
    def->enum_labels.emplace_back(L("Partially bridged"));
    def->enum_labels.emplace_back(L("Sacrificial layer"));
    def->set_default_value(new ConfigOptionEnum<CounterboreHoleBridgingOption>(chbNone));

    def = this->add("overhang_reverse_threshold", coFloatOrPercent);
    def->label = L("Reverse threshold");
    def->full_label = L("Overhang reversal threshold");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Number of mm the overhang need to be for the reversal to be considered useful. Can be a % of the perimeter width."
                     "\nValue 0 enables reversal on every even layers regardless."
                     "\nWhen Detect overhang wall is not enabled, this option is ignored and reversal happens on every even layers regardless.");
    def->sidetext = L("mm or %");
    def->ratio_over = "line_width";
    def->min = 0;
    def->max_literal = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("thick_bridges", coBool);
    def->label = L("Thick external bridges");
    def->category = L("Quality");
    def->tooltip = L("If enabled, bridge extrusion uses a line height equal to the nozzle diameter.\n"
                     "This increases bridge strength and reliability, allowing longer spans, but may worsen appearance.\n"
                     "If disabled, bridges may look better but are generally reliable only for shorter spans.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("thick_internal_bridges", coBool);
    def->label = L("Thick internal bridges");
    def->category = L("Quality");
    def->tooltip  = L("If enabled, internal bridge extrusion uses a line height equal to the nozzle diameter.\n"
                       "This increases internal bridge strength and reliability when printed over sparse infill, but may worsen appearance.\n"
                       "If disabled, internal bridges may look better but can be less reliable over sparse infill.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
    
    def = this->add("enable_extra_bridge_layer", coEnum);
    def->label = L("Extra bridge layers (beta)");
    def->category = L("Quality");
    def->tooltip = L("This option enables the generation of an extra bridge layer over internal and/or external bridges.\n\n"
                     "Extra bridge layers help improve bridge appearance and reliability, as the solid infill is better supported. "
                     "This is especially useful in fast printers, where the bridge and solid infill speeds vary greatly. "
                     "The extra bridge layer results in reduced pillowing on top surfaces, as well as reduced separation of the external bridge layer from its surrounding perimeters.\n\n"
                     "It is generally recommended to set this to at least 'External bridge only', unless specific issues with the sliced model are found.\n\n"
                     "Options:\n"
                     "1. Disabled - does not generate second bridge layers. This is the default and is set for compatibility purposes\n"
                     "2. External bridge only - generates second bridge layers for external-facing bridges only. Please note that small bridges that are shorter "
                     "or narrower than the set number of perimeters will be skipped as they would not benefit from a second bridge layer. If generated, "
                     "the second bridge layer will be extruded parallel to the first bridge layer to reinforce the bridge strength\n"
                     "3. Internal bridge only - generates second bridge layers for internal bridges over sparse infill only. "
                     "Please note that the internal bridges count towards the top shell layer count of your model. "
                     "The second internal bridge layer will be extruded as close to perpendicular to the first as possible. "
                     "If multiple regions in the same island, with varying bridge angles are present, the last region of that island will be selected as the angle reference\n"
                     "4. Apply to all - generates second bridge layers for both internal and external-facing bridges\n");

    def->enum_keys_map = &ConfigOptionEnum<EnableExtraBridgeLayer>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("external_bridge_only");
    def->enum_values.push_back("internal_bridge_only");
    def->enum_values.push_back("apply_to_all");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("External bridge only"));
    def->enum_labels.push_back(L("Internal bridge only"));
    def->enum_labels.push_back(L("Apply to all"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<EnableExtraBridgeLayer>(eblDisabled));

    def = this->add("dont_filter_internal_bridges", coEnum);
    def->label = L("Filter out small internal bridges");
    def->category = L("Quality");
    def->tooltip = L("This option can help reduce pillowing on top surfaces in heavily slanted or curved models.\n"
                     "By default, small internal bridges are filtered out and the internal solid infill is printed "
                     "directly over the sparse infill. This works well in most cases, speeding up printing without "
                     "too much compromise on top surface quality.\n"
                     "However, in heavily slanted or curved models, especially where too low a sparse infill density "
                     "is used, this may result in curling of the unsupported solid infill, causing pillowing.\n"
                     "Enabling limited filtering or no filtering will print internal bridge layer over slightly "
                     "unsupported internal solid infill. The options below control the sensitivity of the filtering, "
                     "i.e. they control where internal bridges are created:\n"
                     "1. Filter - enables this option. This is the default behavior and works well in most cases\n"
                     "2. Limited filtering - creates internal bridges on heavily slanted surfaces while avoiding "
                     "unnecessary bridges. This works well for most difficult models\n"
                     "3. No filtering - creates internal bridges on every potential internal overhang. This option is "
                     "useful for heavily slanted top surface models; however, in most cases, it creates too many "
                     "unnecessary bridges.");
    def->enum_keys_map = &ConfigOptionEnum<InternalBridgeFilter>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("limited");
    def->enum_values.push_back("nofilter");
    def->enum_labels.push_back(L("Filter"));
    def->enum_labels.push_back(L("Limited filtering"));
    def->enum_labels.push_back(L("No filtering"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InternalBridgeFilter>(ibfDisabled));
    
    def = this->add("outer_wall_line_width", coFloatOrPercent);
    def->label = L("Outer wall");
    def->category = L("Quality");
    def->tooltip = L("Line width of outer wall. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("wall_sequence", coEnum);
    def->label = L("Walls printing order");
    def->category = L("Quality");
    def->tooltip = L("Print sequence of the internal (inner) and external (outer) walls.\n\n"
                     "Use Inner/Outer for best overhangs. This is because the overhanging walls can adhere to a neighbouring perimeter while printing. "
                     "However, this option results in slightly reduced surface quality as the external perimeter is deformed by being squashed to the internal perimeter.\n\n"
                     "Use Inner/Outer/Inner for the best external surface finish and dimensional accuracy as the external wall is printed undisturbed from an internal perimeter. "
                     "However, overhang performance will reduce as there is no internal perimeter to print the external wall against. "
                     "This option requires a minimum of 3 walls to be effective as it prints the internal walls from the 3rd perimeter onwards first, "
                     "then the external perimeter and, finally, the first internal perimeter. "
                     "This option is recommended over the Outer/Inner option in most cases.\n\n"
                     "Use Outer/Inner for the same external wall quality and dimensional accuracy benefits of Inner/Outer/Inner option. "
                     "However, the Z seams will appear less consistent as the first extrusion of a new layer starts on a visible surface.");
    def->enum_keys_map = &ConfigOptionEnum<WallSequence>::get_enum_values();
    def->enum_values.push_back("inner wall/outer wall");
    def->enum_values.push_back("outer wall/inner wall");
    def->enum_values.push_back("inner-outer-inner wall");
    def->enum_labels.push_back(L("Inner/Outer"));
    def->enum_labels.push_back(L("Outer/Inner"));
    def->enum_labels.push_back(L("Inner/Outer/Inner"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WallSequence>(WallSequence::InnerOuter));

    def = this->add("is_infill_first",coBool);
    def->label    = L("Print infill first");
    def->tooltip  = L("Order of wall/infill. When the tickbox is unchecked the walls are printed first, which works best in most cases.\n\nPrinting infill first may help with extreme overhangs as the walls have the neighbouring infill to adhere to. However, the infill will slightly push out the printed walls where it is attached to them, resulting in a worse external surface finish. It can also cause the infill to shine through the external surfaces of the part.");
    def->category = L("Quality");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool{false});

    def = this->add("wall_direction", coEnum);
    def->label = L("Wall loop direction");
    def->category = L("Quality");
    def->tooltip = L("The direction which the contour wall loops are extruded when looking down from the top.\nHoles are printed in the opposite direction to the contour to maintain alignment with layers whose contour polygons are incomplete and change direction, also partially forming the contour of a hole.\n\nThis option will be disabled if spiral vase mode is enabled.");
    def->enum_keys_map = &ConfigOptionEnum<WallDirection>::get_enum_values();
    def->enum_values.push_back("ccw");
    def->enum_values.push_back("cw");
    def->enum_labels.push_back(L("Counter clockwise"));
    def->enum_labels.push_back(L("Clockwise"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WallDirection>(WallDirection::CounterClockwise));

    def = this->add("print_flow_ratio", coFloat);
    def->label = L("Flow ratio");
    def->category = L("Quality");
    def->tooltip = L("The material may have volumetric change after switching between molten and crystalline states. "
                     "This setting changes all extrusion flow of this filament in G-code proportionally. "
                     "The recommended value range is between 0.95 and 1.05. "
                     "You may be able to tune this value to get a nice flat surface if there is slight overflow or underflow."
                     "\n\nThe final object flow ratio is this value multiplied by the filament flow ratio.");
    def->mode = comAdvanced;
    def->max = 2.f;
    def->min = 0.01f;
    def->set_default_value(new ConfigOptionFloat(1.f));

    def = this->add("line_width", coFloatOrPercent);
    def->label = L("Default");
    def->category = L("Quality");
    def->tooltip = L("Default line width if other line widths are set to 0. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("initial_layer_line_width", coFloatOrPercent);
    def->label = L("First layer");
    def->category = L("Quality");
    def->tooltip = L("Line width of the first layer. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("initial_layer_print_height", coFloat);
    def->label = L("First layer height");
    def->category = L("Quality");
    def->tooltip = L("Height of the first layer. Making the first layer height thicker can improve build plate adhesion.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    // BBS
    def          = this->add("precise_z_height", coBool);
    def->label   = L("Precise Z height");
    def->category = L("Quality");
    def->tooltip = L("Enable this to get precise Z height of object after slicing. "
                     "It will get the precise object height by fine-tuning the layer heights of the last few layers. "
                     "Note that this is an experimental parameter.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    // BBS
    def = this->add("enable_arc_fitting", coBool);
    def->label = L("Arc fitting");
    def->tooltip = L("Enable this to get a G-code file which has G2 and G3 moves. "
                     "The fitting tolerance is same as the resolution.\n\n"
                     "Note: For Klipper machines, this option is recommended to be disabled. Klipper does not benefit from "
                     "arc commands as these are split again into line segments by the firmware. This results in a reduction "
                     "in surface quality as line segments are converted to arcs by the slicer and then back to line segments "
                     "by the firmware.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));
    // BBS

    def = this->add("sparse_infill_line_width", coFloatOrPercent);
    def->label = L("Sparse infill");
    def->category = L("Quality");
    def->tooltip = L("Line width of internal sparse infill. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("ironing_type", coEnum);
    def->label = L("Ironing type");
    def->category = L("Quality");
    def->tooltip = L("Ironing uses a small flow to print at the same height of a surface to make flat surfaces smoother. This setting controls which layers are being ironed.");
    def->enum_keys_map = &ConfigOptionEnum<IroningType>::get_enum_values();
    def->enum_values.push_back("no ironing");
    def->enum_values.push_back("top");
    def->enum_values.push_back("topmost");
    def->enum_values.push_back("solid");
    def->enum_labels.push_back(L("No ironing"));
    def->enum_labels.push_back(L("All top surfaces"));
    def->enum_labels.push_back(L("Topmost surface only"));
    def->enum_labels.push_back(L("All solid layers"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<IroningType>(IroningType::NoIroning));

    def                = this->add("ironing_pattern", coEnum);
    def->label         = L("Ironing Pattern");
    def->tooltip       = L("The pattern that will be used when ironing.");
    def->category      = L("Quality");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    
    def = this->add("ironing_flow", coPercent);
    def->label = L("Ironing flow");
    def->category = L("Quality");
    def->tooltip = L("This is the amount of material to be extruded during ironing. It is relative to the flow of normal layer height. Too high a value will result in overextrusion on the surface.");
    def->sidetext = "%";
    def->ratio_over = "layer_height";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(10));

    def = this->add("ironing_spacing", coFloat);
    def->label = L("Ironing line spacing");
    def->category = L("Quality");
    def->tooltip = L("This is the distance between the lines used for ironing.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def           = this->add("ironing_inset", coFloat);
    def->label    = L("Ironing inset");
    def->category = L("Quality");
    def->tooltip  = L("The distance to keep from the edges. A value of 0 sets this to half of the nozzle diameter.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("ironing_angle", coFloat);
    def->label    = L("Ironing angle offset");
    def->category = L("Quality");
    def->tooltip  = L("The angle of ironing lines offset from the top surface.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 0;
    def->max      = 359;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def           = this->add("ironing_angle_fixed", coBool);
    def->label    = L("Fixed ironing angle");
    def->category = L("Quality");
    def->tooltip  = L("Use a fixed absolute angle for ironing.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("ironing_expansion", coFloat);
    def->label    = L("Ironing expansion");
    def->category = L("Quality");
    def->tooltip  = L("Expand or contract the ironing area.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = -100;
    def->max      = 100;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("zaa_enabled", coBool);
    def->label    = L("Z contouring enabled");
    def->category = L("Quality");
    def->tooltip  = L("Enable Z-layer contouring (aka Z-layer anti-aliasing).");
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("zaa_minimize_perimeter_height", coFloat);
    def->label    = L("Minimize wall height angle");
    def->category = L("Quality");
    def->tooltip  = L("Reduce the height of top-surface perimeters to match the model edge height.\n"
                      "Affects perimeters with a slope greater than this angle (degrees).\n"
                      "A reasonable value is 35. Set to 0 to disable.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 0;
    def->max      = 90;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(35));

    def = this->add("zaa_dont_alternate_fill_direction", coBool);
    def->label    = L("Don't alternate fill direction");
    def->category = L("Quality");
    def->tooltip  = L("Disable alternating fill direction when using Z contouring.");
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("zaa_min_z", coFloat);
    def->label    = L("Minimum Z height");
    def->category = L("Quality");
    def->tooltip  = L("Minimum Z-layer height.\n"
                      "Also controls the slicing plane.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->max      = 100;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.05));

    def = this->add("small_area_infill_flow_compensation", coBool);
    def->label = L("Small area flow compensation (beta)");
    def->category = L("Quality");
    def->tooltip = L("Enable flow compensation for small infill areas.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("small_area_infill_flow_compensation_model", coStrings);
    def->label = L("Flow Compensation Model");
    def->tooltip = L(
        "Flow Compensation Model, used to adjust the flow for small infill "
        "areas. The model is expressed as a comma separated pair of values for "
        "extrusion length and flow correction factor. Each pair is on a "
        "separate line, followed by a semicolon, in the following format: \"1.234, 5.678;\"");
    def->mode = comAdvanced;
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 15;
    def->set_default_value(new ConfigOptionStrings{"0,0", "\n0.2,0.4444", "\n0.4,0.6145", "\n0.6,0.7059", "\n0.8,0.7619", "\n1.5,0.8571", "\n2,0.8889", "\n3,0.9231", "\n5,0.9520", "\n10,1"});

    def = this->add("make_overhang_printable", coBool);
    def->label = L("Make overhangs printable");
    def->category = L("Quality");
    def->tooltip = L("Modify the geometry to print overhangs without support material.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("make_overhang_printable_angle", coFloat);
    def->label = L("Make overhangs printable - Maximum angle");
    def->category = L("Quality");
    def->tooltip = L("Maximum angle of overhangs to allow after making more steep overhangs printable."
                     "90° will not change the model at all and allow any overhang, while 0 will "
                     "replace all overhangs with conical material.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->mode = comAdvanced;
    def->min = 0.;
    def->max = 90.;
    def->set_default_value(new ConfigOptionFloat(55.));

    def = this->add("make_overhang_printable_hole_size", coFloat);
    def->label = L("Make overhangs printable - Hole area");
    def->category = L("Quality");
    def->tooltip = L("Maximum area of a hole in the base of the model before it's filled by conical material. "
                     "A value of 0 will fill all the holes in the model base.");
    def->sidetext = L(u8"mm²");	// square millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->min = 0.;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("detect_overhang_wall", coBool);
    def->label = L("Detect overhang walls");
    def->category = L("Quality");
    def->tooltip = L("This detects the overhang percentage relative to line width and uses a different speed to print. For 100%% overhang, bridging speed is used.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("unsupported_wall_last", coBool);
    def->label = L("Print unsupported walls last");
    def->category = L("Quality");
    def->tooltip = L("Wall loops that lie entirely in mid air are printed once something can hold them:\n"
                     "they are extruded after the other walls of their island, innermost first, whatever the wall order is.\n"
                     "A loop that only the bridges of this layer can anchor waits until those bridges are printed, while a loop running "
                     "alongside a supported wall keeps its place before the infill, which needs it as an anchor.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("inner_wall_line_width", coFloatOrPercent);
    def->label = L("Inner wall");
    def->category = L("Quality");
    def->tooltip = L("Line width of inner wall. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("resolution", coFloat);
    def->label = L("Resolution");
    def->tooltip = L("The G-code path is generated after simplifying the contour of models to avoid too many points and G-code lines. Smaller values mean higher resolution and more time required to slice.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.01));

    def = this->add("seam_position", coEnum);
    def->label = L("Seam position");
    def->category = L("Quality");
    def->tooltip = L("This is the starting position for each part of the outer wall.");
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("nearest");
    def->enum_values.push_back("aligned");
    def->enum_values.push_back("aligned_back");
    def->enum_values.push_back("back");
    def->enum_values.push_back("random");
    def->enum_labels.push_back(L("Nearest"));
    def->enum_labels.push_back(L("Aligned"));
    def->enum_labels.push_back(L("Aligned back"));
    def->enum_labels.push_back(L("Back"));
    def->enum_labels.push_back(L("Random"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SeamPosition>(spAligned));

    def = this->add("staggered_inner_seams", coBool);
    def->label = L("Staggered inner seams");
    def->category = L("Quality");
    def->tooltip = L("This option causes the inner seams to be shifted backwards based on their depth, forming a zigzag pattern.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("seam_gap", coFloatOrPercent);
    def->label = L("Seam gap");
    def->category = L("Quality");
    def->tooltip = L("In order to reduce the visibility of the seam in a closed loop extrusion, the loop is interrupted and shortened by a specified amount.\n"
                     "This amount can be specified in millimeters or as a percentage of the current extruder diameter. The default value for this parameter is 10%.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(10,true));

    def = this->add("seam_slope_type", coEnum);
    def->label = L("Scarf joint seam (beta)");
    def->category = L("Quality");
    def->tooltip = L("Use scarf joint to minimize seam visibility and increase seam strength.");
    def->enum_keys_map = &ConfigOptionEnum<SeamScarfType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("all");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Contour"));
    def->enum_labels.push_back(L("Contour and hole"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));

    def = this->add("seam_slope_conditional", coBool);
    def->label = L("Conditional scarf joint");
    def->category = L("Quality");
    def->tooltip = L("Apply scarf joints only to smooth perimeters where traditional seams do not conceal the seams at sharp corners effectively.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("scarf_angle_threshold", coInt);
    def->label = L("Conditional angle threshold");
    def->category = L("Quality");
    def->tooltip = L(
        "This option sets the threshold angle for applying a conditional scarf joint seam.\nIf the maximum angle within the perimeter loop "
        "exceeds this value (indicating the absence of sharp corners), a scarf joint seam will be used. The default value is 155°.");
    def->mode = comAdvanced;
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 180;
    def->set_default_value(new ConfigOptionInt(155));

    def = this->add("scarf_overhang_threshold", coPercent);
    def->label = L("Conditional overhang threshold");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip  = L("This option determines the overhang threshold for the application of scarf joint seams. If the unsupported portion "
                       "of the perimeter is less than this threshold, scarf joint seams will be applied. The default threshold is set at 40% "
                       "of the external wall's width. Due to performance considerations, the degree of overhang is estimated.");
    def->sidetext = "%";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(40));

    def = this->add("scarf_joint_speed", coFloatOrPercent);
    def->label = L("Scarf joint speed");
    def->category = L("Quality");
    def->tooltip  = L(
        "This option sets the printing speed for scarf joints. It is recommended to print scarf joints at a slow speed (less than 100 "
         "mm/s). It's also advisable to enable 'Extrusion rate smoothing' if the set speed varies significantly from the speed of the "
         "outer or inner walls. If the speed specified here is higher than the speed of the outer or inner walls, the printer will default "
         "to the slower of the two speeds. When specified as a percentage (e.g., 80%), the speed is calculated based on the respective "
         "outer or inner wall speed. The default value is set to 100%.");
    def->sidetext = L("mm/s or %");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("scarf_joint_flow_ratio", coFloat);
    def->label = L("Scarf joint flow ratio");
    def->category = L("Quality");
    def->tooltip = L("This factor affects the amount of material for scarf joints.");
    def->mode = comExpert;
    def->min = 0;
    def->max = 2;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("seam_slope_start_height", coFloatOrPercent);
    def->label = L("Scarf start height");
    def->category = L("Quality");
    def->tooltip = L("Start height of the scarf.\n"
                     "This amount can be specified in millimeters or as a percentage of the current layer height. The default value for this parameter is 0.");
    def->sidetext = L("mm or %");
    def->ratio_over = "layer_height";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("seam_slope_entire_loop", coBool);
    def->label = L("Scarf around entire wall");
    def->category = L("Quality");
    def->tooltip = L("The scarf extends to the entire length of the wall.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("seam_slope_min_length", coFloat);
    def->label = L("Scarf length");
    def->category = L("Quality");
    def->tooltip = L("Length of the scarf. Setting this parameter to zero effectively disables the scarf.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("seam_slope_steps", coInt);
    def->label = L("Scarf steps");
    def->category = L("Quality");
    def->tooltip = L("Minimum number of segments of each scarf.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("seam_slope_inner_walls", coBool);
    def->label = L("Scarf joint for inner walls");
    def->category = L("Quality");
    def->tooltip = L("Use scarf joint for inner walls as well.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("role_based_wipe_speed", coBool);
    def->label = L("Role based wipe speed");
    def->category = L("Speed");
    def->tooltip = L("The wipe speed is determined by the speed of the current extrusion role. "
                     "e.g. if a wipe action is executed immediately following an outer wall extrusion, the speed of the outer wall extrusion will be utilized for the wipe action.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
    
    def = this->add("wipe_on_loops", coBool);
    def->label = L("Wipe on loops");
    def->category = L("Quality");
    def->tooltip = L("To minimize the visibility of the seam in a closed loop extrusion, a small inward movement is executed before the extruder leaves the loop.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_inward", coBool);
    def->label = L("Wipe inward");
    def->category = L("Quality");
    def->tooltip = L("Applies only to external walls, including hole boundaries. Moves the hot nozzle toward printed "
                     "inner walls during wiping to reduce reheating of freshly printed plastic and seam marks.\n\n"
                     "Especially useful at layer heights below 0.1 mm, where wipe marks are more visible.\n\n"
                     "Uses the regular wipe if no adjacent inner wall is already printed (single-wall areas or "
                     "Outer/Inner wall order), or if no supported inward path can be found, for example at tight "
                     "corners or seam gaps.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_inward_distance", coFloatOrPercent);
    def->label = L("Wipe inward distance");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("The distance the wipe path is shifted away from the external perimeter, specified in millimeters "
                     "or as a percentage of the actual outer-wall extrusion width.\n\n"
                     "For example, 50% shifts the path by half of the outer-wall width. The effective offset is limited "
                     "by both the actual outer-wall width and the available spacing to the adjacent wall, so values "
                     "above 100% or an equivalent absolute distance have no additional effect. "
                     "Set to 0 to disable the offset.");
    def->sidetext = L("mm or %");
    def->ratio_over = "outer_wall_line_width";
    def->min = 0;
    def->max = 100;
    def->max_literal = 2; // Orca: G-code generation also clamps literal values to the actual outer-wall width.
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("wipe_before_external_loop", coBool);
    def->label = L("Wipe before external loop");
    def->category = L("Quality");
    def->tooltip = L("To minimize visibility of potential overextrusion at the start of an external perimeter when printing with "
                     "Outer/Inner or Inner/Outer/Inner wall print order, the de-retraction is performed slightly on the inside from the "
                     "start of the external perimeter. That way any potential over extrusion is hidden from the outside surface.\n\n"
                     "This is useful when printing with Outer/Inner or Inner/Outer/Inner wall print order as in these modes it is more likely "
                     "an external perimeter is printed immediately after a de-retraction move.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_speed", coFloatOrPercent);
    def->label = L("Wipe speed");
    def->category = L("Speed");
    def->tooltip = L("The wipe speed is determined by the speed setting specified in this configuration. "
                     "If the value is expressed as a percentage (e.g. 80%), it will be calculated based on the travel speed setting above. "
                     "The default value for this parameter is 80%.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "travel_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(80,true));

    def = this->add("internal_solid_infill_line_width", coFloatOrPercent);
    def->label = L("Internal solid infill");
    def->category = L("Quality");
    def->tooltip = L("Line width of internal solid infill. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("slice_closing_radius", coFloat);
    def->label = L("Slice gap closing radius");
    def->category = L("Quality");
    def->tooltip = L("Cracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. "
        "The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.049));

    def = this->add("support_line_width", coFloatOrPercent);
    def->label = L("Support");
    def->category = L("Quality");
    def->tooltip = L("Line width of support. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("top_surface_line_width", coFloatOrPercent);
    def->label = L("Top surface");
    def->category = L("Quality");
    def->tooltip = L("Line width for top surfaces. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("enable_mixed_color_sublayer", coBool);
    def->label = L("Mixed color sublayer");
    def->tooltip = L("Enable mixed color sublayer splitting. When enabled, layers containing mixed color "
                     "filaments will be split into sub-layers to achieve color mixing effects.");
    def->category = L("Quality");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("xy_hole_compensation", coFloat);
    def->label = L("X-Y hole compensation");
    def->category = L("Quality");
    def->tooltip = L("Holes in objects will expand or contract in the XY plane by the set value. Positive values make holes bigger and negative values make holes smaller. This function is used to adjust sizes slightly when objects have assembly issues.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("xy_contour_compensation", coFloat);
    def->label = L("X-Y contour compensation");
    def->category = L("Quality");
    def->tooltip = L("Contours of objects will expand or contract in the XY plane by the set value. Positive values make contours bigger and negative values make contours smaller. This function is used to adjust sizes slightly when objects have assembly issues.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("hole_to_polyhole", coBool);
    def->label = L("Convert holes to polyholes");
    def->category = L("Quality");
    def->tooltip = L("Search for almost-circular holes that span more than one layer and convert the geometry to polyholes."
                     " Use the nozzle size and the (biggest) diameter to compute the polyhole."
                     "\nSee http://hydraraptor.blogspot.com/2011/02/polyholes.html");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("hole_to_polyhole_threshold", coFloatOrPercent);
    def->label = L("Polyhole detection margin");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Maximum defection of a point to the estimated radius of the circle."
                     "\nAs cylinders are often exported as triangles of varying size, points may not be on the circle circumference."
                     " This setting allows you some leeway to broaden the detection."
                     "\nIn mm or in % of the radius.");
    def->sidetext = L("mm or %");
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0.01, false));

    def = this->add("hole_to_polyhole_twisted", coBool);
    def->label = L("Polyhole twist");
    def->category = L("Quality");
    def->tooltip = L("Rotate the polyhole every layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("hole_to_polyhole_max_edges", coInt);
    def->label = L("Maximum Polyhole edge count");
    def->category = L("Quality");
    def->tooltip = L("Maximum number of polyhole edges"
            "\nThis setting limits the amount of edges a polyhole can have");
    def->mode = comExpert;
    def->min = 3;
    def->set_default_value(new ConfigOptionInt(50));

    def = this->add("wall_generator", coEnum);
    def->label = L("Wall generator");
    def->category = L("Quality");
    def->tooltip = L("The classic wall generator produces walls with constant extrusion width and for very thin areas, gap-fill is used. The Arachne engine produces walls with variable extrusion width.");
    def->enum_keys_map = &ConfigOptionEnum<PerimeterGeneratorType>::get_enum_values();
    def->enum_values.push_back("classic");
    def->enum_values.push_back("arachne");
    def->enum_labels.push_back(L("Classic"));
    def->enum_labels.push_back(L("Arachne"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));

    def = this->add("wall_transition_length", coPercent);
    def->label = L("Wall transition length");
    def->category = L("Quality");
    def->tooltip = L("When transitioning between different numbers of walls as the part becomes thinner, a certain amount of space is allotted to split or join the wall segments. It\'s expressed as a percentage over nozzle diameter.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("wall_transition_filter_deviation", coPercent);
    def->label = L("Wall transitioning filter margin");
    def->category = L("Quality");
    def->tooltip = L("Prevent transitioning back and forth between one extra wall and one less. This "
        "margin extends the range of extrusion widths which follow to [Minimum wall width "
        "- margin, 2 * Minimum wall width + margin]. Increasing this margin "
        "reduces the number of transitions, which reduces the number of extrusion "
        "starts/stops and travel time. However, large extrusion width variation can lead to "
        "under- or overextrusion problems. "
        "It's expressed as a percentage over nozzle diameter.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("wall_transition_angle", coFloat);
    def->label = L("Wall transitioning threshold angle");
    def->category = L("Quality");
    def->tooltip = L("When to create transitions between even and odd numbers of walls. A wedge shape with"
        " an angle greater than this setting will not have transitions and no walls will be "
        "printed in the center to fill the remaining space. Reducing this setting reduces "
        "the number and length of these center walls, but may leave gaps or overextrude.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->mode = comAdvanced;
    def->min = 1.;
    def->max = 59.;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("wall_distribution_count", coInt);
    def->label = L("Wall distribution count");
    def->category = L("Quality");
    def->tooltip = L("The number of walls, counted from the center, over which the variation needs to be "
        "spread. Lower values mean that the outer walls don't change in width.");
    def->mode = comAdvanced;
    def->min = 1;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("min_feature_size", coPercent);
    def->label = L("Minimum feature size");
    def->category = L("Quality");
    def->tooltip = L("Minimum thickness of thin features. Model features that are thinner than this value will not be printed, "
                     "while features thicker than than this value will be widened to the minimum wall width. "
                     "It's expressed as a percentage over nozzle diameter.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("min_length_factor", coFloat);
    def->label = L("Minimum wall length");
    def->category = L("Quality");
    def->tooltip = L("Adjust this value to prevent short, unclosed walls from being printed, which could increase print time. "
    "The value is a multiple of the wall width. Higher values remove more and longer walls.\n\n"
    "NOTE: Bottom and top surfaces will not be affected by this value to prevent visual gaps on the outside of the model. "
    "Adjust 'One wall threshold' in the 'Walls and surfaces' settings below to adjust the sensitivity of what is considered a top-surface. "
    "'One wall threshold' is only visible if this setting is set above the default value of 0.5, or if single-wall top surfaces is enabled.");
    def->mode = comAdvanced;
    def->min = 0.0;
    def->max = 25.0;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("wall_maximum_resolution", coFloat);
    def->label = L("Maximum wall resolution");
    def->category = L("Quality");
    def->tooltip  = L("This value determines the smallest wall line segment length in mm. "
        "The smaller you set this value, the more accurate and precise the walls will be.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comExpert;
    def->min = 0.005;
    def->max = 0.5f;
    def->set_default_value(new ConfigOptionFloat(0.5f));

    def = this->add("wall_maximum_deviation", coFloat);
    def->label = L("Maximum wall deviation");
    def->category = L("Quality");
    def->tooltip = L("The maximum deviation allowed when reducing the resolution for the 'Maximum wall resolution' setting. If you increase this, "
        "the print will be less accurate, but the G-code will be smaller. 'Maximum wall deviation' limits 'Maximum wall resolution', "
        "so if the two conflict, 'Maximum wall deviation' takes precedence.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comExpert;
    def->min = 0.005f;
    def->max = 0.05f;
    def->set_default_value(new ConfigOptionFloat(0.025f));

    def = this->add("initial_layer_min_bead_width", coPercent);
    def->label = L("First layer minimum wall width");
    def->category = L("Quality");
    def->tooltip = L("The minimum wall width that should be used for the first layer is recommended to be set "
                     "to the same size as the nozzle. This adjustment is expected to enhance adhesion.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(85));

    def = this->add("min_bead_width", coPercent);
    def->label = L("Minimum wall width");
    def->category = L("Quality");
    def->tooltip = L("Width of the wall that will replace thin features (according to the Minimum feature size) "
        "of the model. If the Minimum wall width is thinner than the thickness of the feature,"
        " the wall will become as thick as the feature itself. "
        "It's expressed as a percentage over nozzle diameter.");
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(85));
}

} // namespace Slic3r
