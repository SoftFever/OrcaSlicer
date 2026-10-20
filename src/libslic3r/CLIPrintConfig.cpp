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

CLIActionsConfigDef::CLIActionsConfigDef()
{
    ConfigOptionDef* def;

    // Actions:
    /*def = this->add("export_obj", coBool);
    def->label = L("Export OBJ");
    def->tooltip = L("Export the models as OBJ.");
    def->set_default_value(new ConfigOptionBool(false));*/

/*
    def = this->add("export_svg", coBool);
    def->label = L("Export SVG");
    def->tooltip = L("Slice the model and export solid slices as SVG.");
    def->set_default_value(new ConfigOptionBool(false));
*/

    /*def = this->add("export_sla", coBool);
    def->label = L("Export SLA");
    def->tooltip = L("Slice the model and export SLA printing layers as PNG.");
    def->cli = "export-sla|sla";
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("export_3mf", coString);
    def->label = L("Export 3MF");
    def->tooltip = L("This exports the project as a 3MF file.");
    def->cli_params = "filename.3mf";
    def->set_default_value(new ConfigOptionString("output.3mf"));

    def = this->add("export_slicedata", coString);
    def->label = L("Export slicing data");
    def->tooltip = L("Export slicing data to a folder");
    def->cli_params = "slicing_data_directory";
    def->set_default_value(new ConfigOptionString("cached_data"));

    def = this->add("load_slicedata", coStrings);
    def->label = L("Load slicing data");
    def->tooltip = L("Load cached slicing data from directory.");
    def->cli_params = "slicing_data_directory";
    def->set_default_value(new ConfigOptionString("cached_data"));

    /*def = this->add("export_amf", coBool);
    def->label = L("Export AMF");
    def->tooltip = L("Export the models as AMF.");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("export_stl", coBool);
    def->label = L("Export STL");
    def->tooltip = L("Export the objects as single STL.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_stls", coString);
    def->label = L("Export multiple STLs");
    def->tooltip = L("Export the objects as multiple STLs to directory.");
    def->set_default_value(new ConfigOptionString("stl_path"));

    /*def = this->add("export_gcode", coBool);
    def->label = L("Export G-code");
    def->tooltip = L("Slice the model and export toolpaths as G-code.");
    def->cli = "export-gcode|gcode|g";
    def->set_default_value(new ConfigOptionBool(false));*/

    /*def = this->add("gcodeviewer", coBool);
    // BBS: remove _L()
    def->label = L("G-code viewer");
    def->tooltip = L("Visualize an already sliced and saved G-code.");
    def->cli = "gcodeviewer";
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("slice", coInt);
    def->label = L("Slice");
    def->tooltip = L("Slice the plates: 0-all plates, i-plate i, others-invalid");
    def->cli = "slice";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("help", coBool);
    def->label = L("Help");
    def->tooltip = L("This shows command help.");
    def->cli = "help|h";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("uptodate", coBool);
    def->label = L("UpToDate");
    def->tooltip = L("Update the config values of 3MF to latest.");
    def->cli = "uptodate";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("load_defaultfila", coBool);
    def->label = L("Load default filaments");
    def->tooltip = L("Load first filament as default for those not loaded.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("min_save", coBool);
    def->label = L("Minimum save");
    def->tooltip = L("Export 3MF with minimum size.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("mtcpp", coInt);
    def->label = L("mtcpp");
    def->tooltip = L("max triangle count per plate for slicing");
    def->cli = "mtcpp";
    def->cli_params = "count";
    def->set_default_value(new ConfigOptionInt(1000000));

    def = this->add("mstpp", coInt);
    def->label = L("mstpp");
    def->tooltip = L("max slicing time per plate in seconds");
    def->cli = "mstpp";
    def->cli_params = "time";
    def->set_default_value(new ConfigOptionInt(300));

    // must define new params here, otherwise comamnd param check will fail
    def = this->add("no_check", coBool);
    def->label = L("No check");
    def->tooltip = L("Do not run any validity checks, such as G-code path conflicts check.");
    def->set_default_value(new ConfigOptionBool(false));

    // --strict turns the non-critical slicing warnings the CLI otherwise only logs into a
    // failed run, and records strict_mode in result.json so consumers can tell the modes apart.
    def = this->add("strict", coBool);
    def->label = L("Strict mode");
    def->tooltip = L("Exit non-zero when slicing raises a non-critical warning that is "
                     "otherwise only logged, such as a model that needs support while "
                     "support is disabled. Use this in CI or scripted pipelines that should "
                     "never ship a subtly broken slice. Each such warning is also listed "
                     "with a stable class in the `warnings` array of result.json, which is "
                     "written on Linux only. Cannot be combined with --no-check, which skips "
                     "the support check.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("normative_check", coBool);
    def->label = L("Normative check");
    def->tooltip = L("Check the normative items.");
    def->set_default_value(new ConfigOptionBool(true));

    /*def = this->add("help_fff", coBool);
    def->label = L("Help (FFF options)");
    def->tooltip = L("Show the full list of print/G-code configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_sla", coBool);
    def->label = L("Help (SLA options)");
    def->tooltip = L("Show the full list of SLA print configuration options.");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("info", coBool);
    def->label = L("Output Model Info");
    def->tooltip = L("This outputs the model\u2019s information.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("inspect_mesh", coBool);
    def->label = L("Inspect mesh (JSON to stdout)");
    def->tooltip = L("Print a JSON summary of each loaded object to stdout, then exit: its bounding boxes and the "
                     "convex hull faces it can be laid on, with their normals, areas and centers. These are the faces "
                     "the --ground-* options choose from. Machine-readable alternative to --info.");
    def->set_default_value(new ConfigOptionBool(false));

    // --inspect-paint \u2014 dump the per-facet enforcer/blocker/extruder/fuzzy
    // paint state stored on the loaded model (supports, seam, MMU color,
    // fuzzy-skin) as JSON. Read-only; lets CI / scripted / AI tooling
    // reason about existing paint on a .3mf without loading the GUI.
    def = this->add("inspect_paint", coBool);
    def->label = L("Inspect paint (JSON to stdout)");
    def->tooltip = L("Print a structured JSON summary of every painted layer "
                     "(supports, seam, MMU color, fuzzy-skin) already stored on "
                     "the loaded model \u2014 per-state facet count, surface area, "
                     "and mesh-local bounding box \u2014 then exit. Machine-readable "
                     "alternative to opening the paint gizmos in the GUI.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_settings", coString);
    def->label = L("Export Settings");
    def->tooltip = L("This exports settings to a file. Use - to write them to stdout.");
    def->cli_params = "settings.json";
    def->set_default_value(new ConfigOptionString("output.json"));

    def = this->add("pipe", coString);
    def->label = L("Send progress to pipe");
    def->tooltip = L("Send progress to pipe.");
    def->cli_params = "pipename";
    def->set_default_value(new ConfigOptionString());
}

//BBS: remove unused command currently

CLITransformConfigDef::CLITransformConfigDef()
{
    ConfigOptionDef* def;

    // Transform options:
    /*def = this->add("align_xy", coPoint);
    def->label = L("Align XY");
    def->tooltip = L("Align the model to the given point.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("cut", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Z.");
    def->set_default_value(new ConfigOptionFloat(0));*/

/*
    def = this->add("cut_grid", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model in the XY plane into tiles of the specified max size.");
    def->set_default_value(new ConfigOptionPoint());

    def = this->add("cut_x", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given X.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("cut_y", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Y.");
    def->set_default_value(new ConfigOptionFloat(0));
*/

    /*def = this->add("center", coPoint);
    def->label = L("Center");
    def->tooltip = L("Center the print around the given center.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));*/

    def = this->add("arrange", coInt);
    def->label = L("Arrange Options");
    def->tooltip = L("Arrange options: 0-disable, 1-enable, others-auto");
    def->cli_params = "option";
    //def->cli = "arrange|a";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("repetitions", coInt);
    def->label = L("Repetition count");
    def->tooltip = L("Repetition count of the whole model.");
    def->cli_params = "count";
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("ensure_on_bed", coBool);
    def->label = L("Ensure on bed");
    def->tooltip = L("Lift the object above the bed when it is partially below. Disabled by default.");
    def->set_default_value(new ConfigOptionBool(false));

    /*def = this->add("copy", coInt);
    def->label = L("Copy");
    def->tooltip =L("Duplicate copies of model.");
    def->min = 1;
    def->set_default_value(new ConfigOptionInt(1));*/

    /*def = this->add("duplicate_grid", coPoint);
    def->label = L("Duplicate by grid");
    def->tooltip = L("Multiply copies by creating a grid.");*/

    def = this->add("assemble", coBool);
    def->label = L("Assemble");
    def->tooltip = L("Arrange the supplied models in a plate and merge them in a single model in order to perform actions once.");
    //def->cli = "merge|m";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("convert_unit", coBool);
    def->label = L("Convert Unit");
    def->tooltip = L("Convert the units of model.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("orient", coInt);
    def->label = L("Orient Options");
    def->tooltip = L("Orient options: 0-disable, 1-enable, others-auto");
    //def->cli = "orient|o";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("repair", coBool);
    def->label = L("Repair");
    def->tooltip = L("Repair the meshes of the models as Fix Model does: close holes and cracks, orient them outward and "
                     "remove self-intersections.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("rotate", coFloat);
    def->label = L("Rotate");
    def->tooltip = L("Rotation angle around the Z axis in degrees.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_x", coFloat);
    def->label = L("Rotate around X");
    def->tooltip = L("Rotation angle around the X axis in degrees.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_y", coFloat);
    def->label = L("Rotate around Y");
    def->tooltip = L("Rotation angle around the Y axis in degrees.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->set_default_value(new ConfigOptionFloat(0));

    // The --ground-* options choose from the faces the "Lay on Face" gizmo offers. Like the other
    // transforms they run in command-line order, so they see the rotations given before them.
    def = this->add("ground_largest_face", coBool);
    def->label = L("Ground largest face");
    def->tooltip = L("Lay each object on the largest face of its convex hull and drop it onto the bed. Of equally large "
                     "faces, the one already facing down is kept. Objects without a face large enough to rest on are left "
                     "as they are. Transforms run in command-line order, so rotations given before this option are respected. "
                     "--orient 1 runs after all transforms and replaces the orientation.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ground_face_normal", coString);
    def->label = L("Ground face by normal");
    def->tooltip = L("Lay each object on the convex hull face whose outward normal is closest to the direction NX,NY,NZ "
                     "and drop it onto the bed. The direction is in object coordinates, which include the rotations given "
                     "before this option and match the plate axes unless the input file rotates the object. For example, "
                     "1,0,0 stands the object on its +X side. --orient 1 runs after all transforms and replaces the orientation.");
    def->cli_params = "NX,NY,NZ";
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("ground_face_point", coString);
    def->label = L("Ground face at point");
    def->tooltip = L("Lay each object on the convex hull face that contains the point X,Y,Z and drop it onto the bed. "
                     "The point is in object coordinates, which include the rotations given before this option; "
                     "--inspect-mesh reports face centers in them. Objects without such a face are left as they are, and "
                     "the run fails if no object has one. --orient 1 runs after all transforms and replaces the orientation.");
    def->cli_params = "X,Y,Z";
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("scale", coFloat);
    def->label = L("Scale");
    def->tooltip = L("Scale the model by a float factor.");
    def->cli_params = "factor";
    def->set_default_value(new ConfigOptionFloat(1.f));

    /*def = this->add("split", coBool);
    def->label = L("Split");
    def->tooltip = L("Detect unconnected parts in the given models and split them into separate objects.");

    def = this->add("scale_to_fit", coPoint3);
    def->label = L("Scale to Fit");
    def->tooltip = L("Scale to fit the given volume.");
    def->set_default_value(new ConfigOptionPoint3(Vec3d(0,0,0)));*/
}

CLIMiscConfigDef::CLIMiscConfigDef()
{
    ConfigOptionDef* def;

    /*def = this->add("ignore_nonexistent_config", coBool);
    def->label = L("Ignore non-existent config files");
    def->tooltip = L("Do not fail if a file supplied to --load does not exist.");

    def = this->add("config_compatibility", coEnum);
    def->label = L("Forward-compatibility rule when loading configurations from config files and project files (3MF, AMF).");
    def->tooltip = L("This version of OrcaSlicer may not understand configurations produced by the newest OrcaSlicer versions. "
                     "For example, newer OrcaSlicer may extend the list of supported firmware flavors. One may decide to "
                     "bail out or to substitute an unknown value with a default silently or verbosely.");
    def->enum_keys_map = &ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>::get_enum_values();
    def->enum_values.push_back("disable");
    def->enum_values.push_back("enable");
    def->enum_values.push_back("enable_silent");
    def->enum_labels.push_back(L("Bail out on unknown configuration values"));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by verbosely substituting them with defaults."));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by silently substituting them with defaults."));
    def->set_default_value(new ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>(ForwardCompatibilitySubstitutionRule::Enable));*/

    /*def = this->add("load", coStrings);
    def->label = L("Load config file");
    def->tooltip = L("Load configuration from the specified file. It can be used more than once to load options from multiple files.");*/

    def = this->add("load_settings", coStrings);
    def->label = L("Load General Settings");
    def->tooltip = L("Load process/machine settings from the specified file.");
    def->cli_params = "\"setting1.json;setting2.json\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("load_filaments", coStrings);
    def->label = L("Load Filament Settings");
    def->tooltip = L("Load filament settings from the specified file list.");
    def->cli_params = "\"filament1.json;filament2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("skip_objects", coInts);
    def->label = L("Skip Objects");
    def->tooltip = L("Skip some objects in this print.");
    def->cli_params = "\"3,5,10,77\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("clone_objects", coInts);
    def->label = L("Clone Objects");
    def->tooltip = L("Clone objects in the load list.");
    def->cli_params = "\"1,3,1,10\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("uptodate_settings", coStrings);
    def->label = L("Load uptodate process/machine settings when using uptodate");
    def->tooltip = L("load up-to-date process/machine settings from the specified file when using up-to-date");
    def->cli_params = "\"setting1.json;setting2.json\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("uptodate_filaments", coStrings);
    def->label = L("Load uptodate filament settings when using uptodate");
    def->tooltip = L("Load uptodate filament settings from the specified file when using uptodate.");
    def->cli_params = "\"filament1.json;filament2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("downward_check", coBool);
    def->label = L("Downward machines check");
    def->tooltip = L("If enabled, check whether current machine downward compatible with the machines in the list.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("downward_settings", coStrings);
    def->label = L("Downward machines settings");
    def->tooltip = L("The machine settings list needs to do downward checking.");
    def->cli_params = "\"machine1.json;machine2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("load_assemble_list", coString);
    def->label = L("Load assemble list");
    def->tooltip = L("Load assemble object list from config file.");
    def->cli_params = "assemble_list.json";
    def->set_default_value(new ConfigOptionString());

    /*def = this->add("output", coString);
    def->label = L("Output File");
    def->tooltip = L("The file where the output will be written (if not specified, it will be based on the input file).");
    def->cli = "output|o";

    def = this->add("single_instance", coBool);
    def->label = L("Single instance mode");
    def->tooltip = L("If enabled, the command line arguments are sent to an existing instance of GUI OrcaSlicer, "
                     "or an existing OrcaSlicer window is activated. "
                     "Overrides the \"single_instance\" configuration value from application preferences.");*/

/*
    def = this->add("autosave", coString);
    def->label = L("Autosave");
    def->tooltip = L("Automatically export current configuration to the specified file.");
*/

    def = this->add("datadir", coString);
    def->label = L("Data directory");
    def->tooltip = L("Load and store settings at the given directory. This is useful for maintaining different profiles or including configurations from a network storage.");


    def = this->add("outputdir", coString);
    def->label = L("Output directory");
    def->tooltip = L("This is the output directory for exported files.");
    def->cli_params = "dir";
    def->set_default_value(new ConfigOptionString());

    def = this->add("export_statistics", coBool);
    def->label = L("Export statistics");
    def->tooltip = L("Write the statistics of each sliced plate as JSON, as plate_N.statistics.json next to its G-code in the output "
                     "directory and inside the 3MF of --export-3mf: print time, filament and feature totals, and the same per layer "
                     "and per object.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("debug", coInt);
    def->label = L("Debug level");
    def->tooltip = L("Sets debug logging level. 0:fatal, 1:error, 2:warning, 3:info, 4:debug, 5:trace\n");
    def->min = 0;
    def->cli_params = "level";
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("logfile", coString);
    def->label = L("Log file");
    def->tooltip = L("Redirects debug logging to file.\n");
    def->cli_params = "file";
    def->set_default_value(new ConfigOptionString());

    def = this->add("enable_timelapse", coBool);
    def->label = L("Enable timelapse for print");
    def->tooltip = L("If enabled, this slicing will be considered using timelapse.");
    def->set_default_value(new ConfigOptionBool(false));

#if (defined(_MSC_VER) || defined(__MINGW32__)) && defined(SLIC3R_GUI)
    /*def = this->add("sw_renderer", coBool);
    def->label = L("Render with a software renderer");
    def->tooltip = L("Render with a software renderer. The bundled MESA software renderer is loaded instead of the default OpenGL driver.");
    def->min = 0;*/
#endif /* _MSC_VER */

    def = this->add("load_custom_gcodes", coString);
    def->label = L("Load custom G-code");
    def->tooltip = L("Load custom G-code from json.");
    def->cli_params = "custom_gcode_toolchange.json";
    def->set_default_value(new ConfigOptionString());

    def = this->add("load_filament_ids", coInts);
    def->label = L("Load filament IDs");
    def->tooltip = L("Load filament IDs for each object.");
    def->cli_params = "\"1,2,3,1\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("allow_multicolor_oneplate", coBool);
    def->label = L("Allow multiple colors on one plate");
    def->tooltip = L("If enabled, Arrange will allow multiple colors on one plate.");
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("allow_rotations", coBool);
    def->label = L("Allow rotation when arranging");
    def->tooltip = L("If enabled, Arrange will allow rotation when placing objects.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("align_to_y_axis", coBool);
    def->label = L("Align to Y axis when arranging");
    def->tooltip = L("If enabled, Arrange will turn each object so its long side runs along the Y axis before placing it. "
                     "When not given, it is on for i3 printers and off for the others, as in the GUI.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("avoid_extrusion_cali_region", coBool);
    def->label = L("Avoid extrusion calibrate region when arranging");
    def->tooltip = L("If enabled, Arrange will avoid extrusion calibrate region when placing objects.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("skip_modified_gcodes", coBool);
    def->label = L("Skip modified G-code in 3MF");
    def->tooltip = L("Skip the modified G-code in 3MF from printer or filament presets.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("makerlab_name", coString);
    def->label = L("MakerLab name");
    def->tooltip = L("MakerLab name to generate this 3MF.");
    def->cli_params = "name";
    def->set_default_value(new ConfigOptionString());

    def = this->add("makerlab_version", coString);
    def->label = L("MakerLab version");
    def->tooltip = L("MakerLab version to generate this 3MF.");
    def->cli_params = "version";
    def->set_default_value(new ConfigOptionString());

    def = this->add("metadata_name", coStrings);
    def->label = L("Metadata name list");
    def->tooltip = L("Metadata name list added into 3MF.");
    def->cli_params = "\"name1;name2;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("metadata_value", coStrings);
    def->label = L("Metadata value list");
    def->tooltip = L("Metadata value list added into 3MF.");
    def->cli_params = "\"value1;value2;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("allow_newer_file", coBool);
    def->label = L("Allow 3MF with newer version to be sliced");
    def->tooltip = L("Allow 3MF with newer version to be sliced.");
    def->set_default_value(new  ConfigOptionBool(false));

    def = this->add("allow_mix_temp", coBool);
    // internal use only, don't need translation
    def->label = "Allow filaments with high/low temperature to be printed together";
    def->tooltip = "Allow filaments with high/low temperature to be printed together.";
    def->set_default_value(new  ConfigOptionBool(false));
}

const CLIActionsConfigDef    cli_actions_config_def;
const CLITransformConfigDef  cli_transform_config_def;
const CLIMiscConfigDef       cli_misc_config_def;

DynamicPrintAndCLIConfig::PrintAndCLIConfigDef DynamicPrintAndCLIConfig::s_def;

void DynamicPrintAndCLIConfig::handle_legacy(t_config_option_key &opt_key, std::string &value) const
{
    if (cli_actions_config_def  .options.find(opt_key) == cli_actions_config_def  .options.end() &&
        cli_transform_config_def.options.find(opt_key) == cli_transform_config_def.options.end() &&
        cli_misc_config_def     .options.find(opt_key) == cli_misc_config_def     .options.end()) {
        PrintConfigDef::handle_legacy(opt_key, value);
    }
}

// SlicingStatesConfigDefs


} // namespace Slic3r