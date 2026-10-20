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

void PrintConfigDef::init_fff_process_others()
{
    ConfigOptionDef* def;


    def = this->add("brim_width", coFloat);
    def->label = L("Brim width");
    def->category = L("Support");
    def->tooltip = L("This is the distance from the model to the outermost brim line.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("leading_brim_length", coFloat);
    def->label = L("Leading brim length");
    def->category = L("Support");
    def->tooltip = L("Belt printers only. Extends the brim AHEAD of the object along the belt, on "
                     "every downhill-facing edge of its contact area - both the object's first "
                     "contact with the belt and any island that lands later. This apron is laid "
                     "onto the belt before the object reaches it, so the leading edge has "
                     "something already stuck down to hold on to.\n\n"
                     "Measured on the belt surface, and added on top of Brim width: the brim "
                     "reaches Brim-object gap + Leading brim length + Brim width ahead of the "
                     "object. Set Brim-object gap to 0, or the apron will not touch the object it "
                     "is meant to anchor.\n\n"
                     "On a tilted belt each layer lays one strip of the brim, so the thickness of "
                     "the resulting brim sheet is set by flow rather than by layer height. Use "
                     "Brim flow ratio to tune it.\n\n"
                     "Set to 0 to disable.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("extra_brim_width", coFloat);
    def->label = L("Extra brim width");
    def->category = L("Support");
    def->tooltip = L("Belt printers only. Widens the brim SIDEWAYS, across the belt, without "
                     "extending it further ahead of or behind the object. Use it when a part needs "
                     "more grip along its length than Brim width alone gives.\n\n"
                     "Measured on the belt surface, and added on top of Brim width: the brim "
                     "reaches Brim-object gap + Brim width + Extra brim width to either side of "
                     "the object. To extend the brim ahead of the object instead, use Leading brim "
                     "length.\n\n"
                     "Set to 0 to disable.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("brim_type", coEnum);
    def->label = L("Brim type");
    def->category = L("Support");
    def->tooltip = L("This controls the generation of the brim at outer and/or inner side of models. "
                     "Auto means the brim width is analyzed and calculated automatically.");
    def->enum_keys_map = &ConfigOptionEnum<BrimType>::get_enum_values();
    def->enum_values.emplace_back("auto_brim");
    def->enum_values.emplace_back("brim_ears");
    def->enum_values.emplace_back("painted");
    def->enum_values.emplace_back("outer_only");
    def->enum_values.emplace_back("inner_only");
    def->enum_values.emplace_back("outer_and_inner");
    def->enum_values.emplace_back("no_brim");
    def->enum_values.emplace_back("leading_edge_only");
    def->enum_labels.emplace_back(L("Auto"));
    def->enum_labels.emplace_back(L("Mouse ear"));
    def->enum_labels.emplace_back(L("Painted"));
    def->enum_labels.emplace_back(L("Outer brim only"));
    def->enum_labels.emplace_back(L("Inner brim only"));
    def->enum_labels.emplace_back(L("Outer and inner brim"));
    def->enum_labels.emplace_back(L("No-brim"));
    def->enum_labels.emplace_back(L("Leading edge only"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<BrimType>(btAutoBrim));

    def = this->add("brim_object_gap", coFloat);
    def->label = L("Brim-object gap");
    def->category = L("Support");
    def->tooltip = L("This creates a gap between the innermost brim line and the object and can make the brim easier to remove.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("brim_flow_ratio", coFloat);
    def->label = L("Brim flow ratio");
    def->category = L("Support");
    def->tooltip = L("This factor affects the amount of material for brims.\n\n"
                     "The actual brim flow used is calculated by multiplying this value by the filament flow ratio, and if set, the object's flow ratio.\n\n"
                     "Note: The resulting value will not be affected by the first-layer flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("brim_use_efc_outline", coBool);
    def->label = L("Brim follows compensated outline");
    def->category = L("Support");
    def->tooltip = L("When enabled, the brim is aligned with the first-layer perimeter geometry after Elephant Foot Compensation is applied.\nThis option is intended for cases where Elephant Foot Compensation significantly alters the first-layer footprint.\n\nIf your current setup already works well, enabling it may be unnecessary and can cause the brim to fuse with upper layers.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("combine_brims", coBool);
    def->label = L("Combine brims");
    def->category = L("Support");
    def->tooltip  = L("Combine multiple brims into one when they are close to each other. This can improve brim adhesion.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("brim_ears_max_angle", coFloat);
    def->label = L("Brim ear max angle");
    def->category = L("Support");
    def->tooltip = L("Maximum angle to let a brim ear appear.\n"
                     "If set to 0, no brim will be created.\n"
                     "If set to ~180, brim will be created on everything but straight sections.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(125));

    def = this->add("brim_ears_detection_length", coFloat);
    def->label = L("Brim ear detection radius");
    def->category = L("Support");
    def->tooltip = L("The geometry will be decimated before detecting sharp angles. "
                     "This parameter indicates the minimum length of the deviation for the decimation.\n"
                     "0 to deactivate.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("brim_ears_outer_only", coBool);
    def->label = L("Brim ears outer only");
    def->category = L("Support");
    def->tooltip = L("Generate mouse ears only on the outer contour of the model, excluding holes and enclosed sections.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("print_sequence", coEnum);
    def->label = L("Print sequence");
    def->tooltip = L("This determines the print sequence, allowing you to print layer-by-layer or object-by-object.");
    def->enum_keys_map = &ConfigOptionEnum<PrintSequence>::get_enum_values();
    def->enum_values.push_back("by layer");
    def->enum_values.push_back("by object");
    def->enum_labels.push_back(L("By layer"));
    def->enum_labels.push_back(L("By object"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));

    def = this->add("print_order", coEnum);
    def->label = L("Intra-layer order");
    def->tooltip = L("Order in which object instances are visited within a single layer, which controls how much "
                     "travel is spent moving between them.\n\n"
                     "Default: nearest-neighbor chaining, refined with 2-opt and crossing removal. A good general "
                     "choice.\n"
                     "As object list: instances are printed in the same order as the object list, without any path "
                     "optimization. Use it when you need a predictable, manually controlled order.\n"
                     "Best of all (shortest path): every strategy is evaluated and the shortest one is used. The "
                     "object instance order is decided once for the whole print, while the ordering of individual "
                     "islands is decided per layer, so different layers may end up using different strategies. "
                     "Slightly slower to slice.\n"
                     "Snake: serpentine row-by-row traversal, refined with 2-opt. Well suited to regular grids of "
                     "many small parts.\n\n"
                     "With multiple filaments or tools in the same layer, minimizing tool changes takes priority: "
                     "objects are grouped by filament first and this setting only orders the instances within each "
                     "filament group, so the overall sequence may not look like the shortest path across the plate.");
    def->enum_keys_map = &ConfigOptionEnum<PrintOrder>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("as_obj_list");
    def->enum_values.push_back("best_of");
    def->enum_values.push_back("snake");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("As object list"));
    def->enum_labels.push_back(L("Best of all (shortest path)"));
    def->enum_labels.push_back(L("Snake"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<PrintOrder>(PrintOrder::Default));

    def = this->add("fuzzy_skin", coEnum);
    def->label = L("Fuzzy skin");
    def->category = L("Others");
    def->tooltip = L("This setting makes the toolhead randomly jitter while printing walls so that the surface has a rough textured look. This setting controls the fuzzy position.");
    def->enum_keys_map = &ConfigOptionEnum<FuzzySkinType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("hole");
    def->enum_values.push_back("all");
    def->enum_values.push_back("allwalls");
    def->enum_values.push_back("disabled_fuzzy");
    def->enum_labels.push_back(L("Painted only"));
    def->enum_labels.push_back(L("Contour"));
    def->enum_labels.push_back(L("Hole"));
    def->enum_labels.push_back(L("Contour and hole"));
    def->enum_labels.push_back(L("All walls"));
    def->enum_labels.push_back(L("Disabled"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<FuzzySkinType>(FuzzySkinType::Disabled_fuzzy));

    def = this->add("fuzzy_skin_thickness", coFloat);
    def->label = L("Fuzzy skin thickness");
    def->category = L("Others");
    def->tooltip = L("The width of jittering: it\u2019s recommended to keep this lower than the outer wall line width.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 2;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("fuzzy_skin_point_distance", coFloat);
    def->label = L("Fuzzy skin point distance");
    def->category = L("Others");
    def->tooltip = L("The average distance between the random points introduced on each line segment.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0.01f; // point distance cannot be 0! Otherwise we get infinite loop + OOM due to infinite line division.
    def->max = 5.f;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.3f));

    def = this->add("fuzzy_skin_first_layer", coBool);
    def->label = L("Apply fuzzy skin to first layer");
    def->category = L("Others");
    def->tooltip = L("Whether to apply fuzzy skin on the first layer.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("fuzzy_skin_mode", coEnum);
    def->label = L("Fuzzy skin generator mode");
    def->category = L("Others");
    def->tooltip = L("Fuzzy skin generation mode. Works only with Arachne!\n"
                     "Displacement: Сlassic mode when the pattern is formed by shifting the nozzle sideways from the original path.\n"
                     "Extrusion: The mode when the pattern formed by the amount of extruded plastic. "
                     "This is the fast and straight algorithm without unnecessary nozzle shake that gives a smooth pattern. "
                     "But it is more useful for forming loose walls in the entire they array.\n"
                     "Combined: Joint mode [Displacement] + [Extrusion]. The appearance of the walls is similar to [Displacement] Mode, but it leaves no pores between the perimeters.\n\n"
                     "Attention! The [Extrusion] and [Combined] modes works only the fuzzy_skin_thickness parameter not more than the thickness of printed loop. "
                     "At the same time, the width of the extrusion for a particular layer should also not be below a certain level. "
                     "It is usually equal 15-25%% of a layer height. Therefore, the maximum fuzzy skin thickness with a perimeter width of 0.4 mm and a layer height of 0.2 mm will be 0.4-(0.2*0.25)=±0.35mm! "
                     "If you enter a higher parameter than this, the error Flow::spacing() will displayed, and the model will not be sliced. You can choose this number until this error is repeated." );
    def->enum_keys_map = &ConfigOptionEnum<FuzzySkinMode>::get_enum_values();
    def->enum_values.push_back("displacement");
    def->enum_values.push_back("extrusion");
    def->enum_values.push_back("combined");
    def->enum_labels.push_back(L("Displacement"));
    def->enum_labels.push_back(L("Extrusion"));
    def->enum_labels.push_back(L("Combined"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<FuzzySkinMode>(FuzzySkinMode::Displacement));

    def = this->add("fuzzy_skin_noise_type", coEnum);
    def->label = L("Fuzzy skin noise type");
    def->category = L("Others");
    def->tooltip = L("Noise type to use for fuzzy skin generation:\n"
                     "Classic: Classic uniform random noise.\n"
                     "Perlin: Perlin noise, which gives a more consistent texture.\n"
                     "Billow: Similar to perlin noise, but clumpier.\n"
                     "Ridged Multifractal: Ridged noise with sharp, jagged features. Creates marble-like textures.\n"
                     "Voronoi: Divides the surface into voronoi cells, and displaces each one by a random amount. Creates a patchwork texture.\n"
                     "Ripple: Uniform ripple pattern that ripples left and right of the original path. Repeating pattern, woven appearance.");
    def->enum_keys_map = &ConfigOptionEnum<NoiseType>::get_enum_values();
    def->enum_values.push_back("classic");
    def->enum_values.push_back("perlin");
    def->enum_values.push_back("billow");
    def->enum_values.push_back("ridgedmulti");
    def->enum_values.push_back("voronoi");
    def->enum_values.push_back("ripple");
    def->enum_labels.push_back(L("Classic"));
    def->enum_labels.push_back(L("Perlin"));
    def->enum_labels.push_back(L("Billow"));
    def->enum_labels.push_back(L("Ridged Multifractal"));
    def->enum_labels.push_back(L("Voronoi"));
    def->enum_labels.push_back(L("Ripple"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<NoiseType>(NoiseType::Classic));

    def = this->add("fuzzy_skin_scale", coFloat);
    def->label = L("Fuzzy skin feature size");
    def->category = L("Others");
    def->tooltip = L("The base size of the coherent noise features, in mm. Higher values will result in larger features.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0.1f;
    def->max = 500;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("fuzzy_skin_octaves", coInt);
    def->label = L("Fuzzy Skin Noise Octaves");
    def->category = L("Others");
    def->tooltip = L("The number of octaves of coherent noise to use. Higher values increase the detail of the noise, but also increase computation time.");
    def->min = 1;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(4));

    def = this->add("fuzzy_skin_persistence", coFloat);
    def->label = L("Fuzzy skin noise persistence");
    def->category = L("Others");
    def->tooltip = L("The decay rate for higher octaves of the coherent noise. Lower values will result in smoother noise.");
    def->min = 0.01f;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("fuzzy_skin_ripples_per_layer", coInt);
    def->label = L("Number of ripples per layer");
    def->category = L("Others");
    def->tooltip  = L("Controls how many full cycles of ripples will be added per layer.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(15));

    def = this->add("fuzzy_skin_ripple_offset", coPercent);
    def->label = L("Ripple offset");
    def->category = L("Others");
    def->tooltip = L("Shifts the ripple phase forward along the print path by the specified percentage of a wavelength each layer period.\n"
                     "- 0% keeps every layer identical.\n"
                     "- 50% shifts the pattern by half a wavelength, effectively inverting the phase.\n"
                     "- 100% shifts the pattern by a full wavelength, returning to the original phase.\n\n"
                     "The shift is applied once every number of layers set by Layers between ripple offset, so layers within the same group are printed identically.");
    def->min = 0;
    def->max = 100;
    def->sidetext = "%";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));

    def = this->add("fuzzy_skin_layers_between_ripple_offset", coInt);
    def->label = L("Layers between ripple offset");
    def->category = L("Others");
    def->tooltip = L("Specifies how many consecutive layers share the same ripple phase before the offset is applied.\n"
                     "For example:\n"
                     "- 1 = Layer 1 is printed with the base ripple pattern, then layer 2 is shifted by the configured offset, then layer 3 returns to the base pattern, and so on.\n"
                     "- 3 = Layers 1 to 3 are printed with the base ripple pattern, then layers 4 to 6 are shifted by the configured offset, then layers 7 to 9 return to the base pattern, etc.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));
    def = this->add("gcode_add_line_number", coBool);
    def->label = L("Add line number");
    def->tooltip = L("Enable this to add line number(Nx) at the beginning of each G-code line.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("gcode_label_objects", coBool);
    def->label = L("Label objects");
    def->tooltip = L("Enable this to add comments into the G-code labeling print moves with what object they belong to,"
                   " which is useful for the Octoprint CancelObject plug-in. This setting is NOT compatible with "
                   "Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(1));

    def = this->add("exclude_object", coBool);
    def->label = L("Exclude objects");
    def->tooltip = L("Enable this option to add EXCLUDE OBJECT command in G-code.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcode_comments", coBool);
    def->label = L("Verbose G-code");
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
                   "If you print from SD card, the additional weight of the file could make your firmware "
                   "slow down.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    // BBS
    def          = this->add("enable_wrapping_detection", coBool);
    def->label   = L("Enable clumping detection");
    def->tooltip = L("Enable clumping detection");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("notes", coString);
    def->label = L("Configuration notes");
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("reduce_infill_retraction", coBool);
    def->label = L("Reduce infill retraction");
    def->tooltip = L("Don\'t retract when the travel is entirely within an infill area. That means the oozing can\'t been seen. This can reduce times of retraction for complex model and save printing time, but make slicing and G-code generating slower. Note that Z-hop is also not performed in areas where retraction is skipped.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("filename_format", coString);
    def->label = L("Filename format");
    def->tooltip = L("Users can decide project file names when exporting.");
    def->full_width = true;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("{input_filename_base}_{filament_type[initial_tool]}_{print_time}.gcode"));
    
    def = this->add("post_process", coStrings);
    def->label = L("Post-processing Scripts");
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Orca Slicer config settings by reading environment variables.");
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("process_change_extrusion_role_gcode", coString);
    def->label = L("Change extrusion role G-code (process)");
    def->tooltip = L("This G-code is inserted when the extrusion role is changed. It runs after the machine and filament extrusion role G-code.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString());

    def = this->add("slicing_pipeline_plugin", coStrings);
    def->label = L("Slicing Pipeline Plugin");
    def->tooltip = L("Python plugins invoked at each slicing pipeline step to read and modify intermediate slicing data, "
                   "including a final G-code post-processing step. Research/experimental.");
    def->gui_type = ConfigOptionDef::GUIType::plugin_picker;
    def->plugin_type = "slicing-pipeline";
    def->full_width = true;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("skirt_distance", coFloat);
    def->label = L("Skirt distance");
    def->tooltip = L("This is the distance from the skirt to the brim or the object.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2));

    def = this->add("skirt_start_angle", coFloat);
    def->label = L("Skirt start point");
    def->category = L("Support");
    def->tooltip = L("Angle from the object center to skirt start point. Zero is the most right position, counter clockwise is positive angle.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = -180;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-135));

    def = this->add("skirt_height", coInt);
    def->label = L("Skirt height");
    def->tooltip = L("Number of skirt layers: usually only one");
    def->sidetext = L("layers");
    def->mode = comSimple;
    def->max = 10000;
    def->set_default_value(new ConfigOptionInt(1));
    
    def = this->add("single_loop_draft_shield", coBool);
    def->label = L("Single loop after first layer");
    def->tooltip = L("Limits the skirt/draft shield loops to one wall after the first layer. This is useful, on occasion, to conserve filament but may cause the draft shield/skirt to warp / crack.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("draft_shield", coEnum);
    def->label = L("Draft shield");
    def->tooltip = L("A draft shield is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft. "
                     "It is usually needed only with open frame printers, i.e. without an enclosure.\n\n"
                     "Enabled = skirt is as tall as the highest printed object. Otherwise 'Skirt height' is used.");
    def->enum_keys_map = &ConfigOptionEnum<DraftShield>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("enabled");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("Enabled"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<DraftShield>(dsDisabled));

    def = this->add("skirt_type", coEnum);
    def->label = L("Skirt type");
    def->full_label = L("Skirt type");
    def->tooltip = L("Combined - single skirt for all objects, Per object - individual object skirt.");
    def->enum_keys_map = &ConfigOptionEnum<SkirtType>::get_enum_values();
    def->enum_values.push_back("combined");
    def->enum_values.push_back("perobject");
    def->enum_labels.push_back(L("Combined"));
    def->enum_labels.push_back(L("Per object"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SkirtType>(stCombined));
    
    def = this->add("skirt_loops", coInt);
    def->label = L("Skirt loops");
    def->full_label = L("Skirt loops");
    def->tooltip = L("This is the number of loops for the skirt. 0 means the skirt is disabled.");
    def->min = 0;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("skirt_speed", coFloat);
    def->label = L("Skirt speed");
    def->full_label = L("Skirt speed");
    def->tooltip = L("Speed of skirt, in mm/s. Zero means use default layer extrusion speed.");
    def->min = 0;
    def->sidetext = L("mm/s");	// millimeters per second, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.0));

    def = this->add("min_skirt_length", coFloat);
    def->label = L("Skirt minimum extrusion length");
    def->full_label = L("Skirt minimum extrusion length");
    def->tooltip = L("Minimum filament extrusion length in mm when printing the skirt. Zero means this feature is disabled.\n\n"
                     "Using a non-zero value is useful if the printer is set up to print without a prime line.\n"
                     "Final number of loops is not taking into account while arranging or validating objects distance. Increase loop number in such case.");
    def->min = 0;
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("spiral_mode", coBool);
    def->label = L("Spiral vase");
    def->tooltip = L("This enables spiraling, which smooths out the Z moves of the outer contour and turns a solid model into a single walled print with solid bottom layers. The final generated model has no seam.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("spiral_mode_smooth", coBool);
    def->label = L("Smooth Spiral");
    def->tooltip = L("Smooth Spiral smooths out X and Y moves as well, "
                     "resulting in no visible seam at all, even in the XY directions on walls that are not vertical.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("spiral_mode_max_xy_smoothing", coFloatOrPercent);
    def->label = L("Max XY Smoothing");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Maximum distance to move points in XY to try to achieve a smooth spiral. "
                     "If expressed as a %, it will be computed over nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("spiral_starting_flow_ratio", coFloat);
    def->label = L("Spiral starting flow ratio");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Sets the starting flow ratio while transitioning from the last bottom layer to the spiral. "
                    "Normally the spiral transition scales the flow ratio from 0% to 100% during the first loop "
                    "which can in some cases lead to under extrusion at the start of the spiral.");
    def->min = 0;
    def->max = 1;
    def->set_default_value(new ConfigOptionFloat(0));
    def->mode = comAdvanced;

    def = this->add("spiral_finishing_flow_ratio", coFloat);
    def->label = L("Spiral finishing flow ratio");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Sets the finishing flow ratio while ending the spiral. "
                    "Normally the spiral transition scales the flow ratio from 100% to 0% during the last loop "
                    "which can in some cases lead to under extrusion at the end of the spiral.");
    def->min = 0;
    def->max = 1;
    def->set_default_value(new ConfigOptionFloat(0));
    def->mode = comAdvanced;

    def = this->add("timelapse_type", coEnum);
    def->label = L("Timelapse");
    def->tooltip = L("If smooth or traditional mode is selected, a timelapse video will be generated for each print. After each layer is printed, a snapshot is taken with the chamber camera. All of these snapshots are composed into a timelapse video when printing completes. If smooth mode is selected, the toolhead will move to the excess chute after each layer is printed and then take a snapshot. Since the melt filament may leak from the nozzle during the process of taking a snapshot, a prime tower is required for smooth mode to wipe the nozzle.");
    def->enum_keys_map = &ConfigOptionEnum<TimelapseType>::get_enum_values();
    def->enum_values.emplace_back("0");
    def->enum_values.emplace_back("1");
    def->enum_labels.emplace_back(L("Traditional"));
    def->enum_labels.emplace_back(L("Smooth"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<TimelapseType>(tlTraditional));

    def = this->add("slicing_mode", coEnum);
    def->label = L("Slicing Mode");
    def->category = L("Others");
    def->tooltip = L("Use \"Even-odd\" for 3DLabPrint airplane models. Use \"Close holes\" to close all holes in the model.");
    def->enum_keys_map = &ConfigOptionEnum<SlicingMode>::get_enum_values();
    def->enum_values.push_back("regular");
    def->enum_values.push_back("even_odd");
    def->enum_values.push_back("close_holes");
    def->enum_labels.push_back(L("Regular"));
    def->enum_labels.push_back(L("Even-odd"));
    def->enum_labels.push_back(L("Close holes"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SlicingMode>(SlicingMode::Regular));
}

} // namespace Slic3r
