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

void PrintConfigDef::init_fff_process_strength()
{
    ConfigOptionDef* def;


    def = this->add("bottom_shell_layers", coInt);
    def->label = L("Bottom shell layers");
    def->category = L("Strength");
    def->sidetext = L("layers"); // ORCA add side text
    def->tooltip =  L("This is the number of solid layers of bottom shell, including the bottom "
                      "surface layer. When the thickness calculated by this value is thinner "
                      "than bottom shell thickness, the bottom shell layers will be increased.");
    def->full_label = L("Bottom shell layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("bottom_shell_thickness", coFloat);
    def->label = L("Bottom shell thickness");
    def->category = L("Strength");
    def->tooltip = L("The number of bottom solid layers is increased when slicing if the thickness calculated by bottom shell layers is thinner than this value. This can avoid having too thin a shell when layer height is small. 0 means that this setting is disabled and the thickness of the bottom shell is determined simply by the number of bottom shell layers.");
    def->full_label = L("Bottom shell thickness");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("gap_fill_target", coEnum);
    def->label = L("Apply gap fill");
    def->category = L("Strength");
    def->tooltip = L("Enables gap fill for the selected solid surfaces. The minimum gap length that will be filled can be controlled "
                     "from the filter out tiny gaps option below.\n\n"
                     "Options:\n"
                     "1. Everywhere: Applies gap fill to top, bottom and internal solid surfaces for maximum strength\n"
                     "2. Top and Bottom surfaces: Applies gap fill to top and bottom surfaces only, balancing print speed, "
                     "reducing potential over extrusion in the solid infill and making sure the top and bottom surfaces have "
                     "no pinhole gaps\n"
                     "3. Nowhere: Disables gap fill for all solid infill areas\n\n"
                     "Note that if using the classic perimeter generator, gap fill may also be generated between perimeters, "
                     "if a full width line cannot fit between them. That perimeter gap fill is not controlled by this setting.\n\n"
                     "If you would like all gap fill, including the classic perimeter generated one, removed, "
                     "set the filter out tiny gaps value to a large number, like 999999.\n\n"
                     "However this is not advised, as gap fill between perimeters is contributing to the model's strength. "
                     "For models where excessive gap fill is generated between perimeters, a better option would be to "
                     "switch to the arachne wall generator and use this option to control whether the cosmetic top and "
                     "bottom surface gap fill is generated.");
    def->enum_keys_map = &ConfigOptionEnum<GapFillTarget>::get_enum_values();
    def->enum_values.push_back("everywhere");
    def->enum_values.push_back("topbottom");
    def->enum_values.push_back("nowhere");
    def->enum_labels.push_back(L("Everywhere"));
    def->enum_labels.push_back(L("Top and bottom surfaces"));
    def->enum_labels.push_back(L("Nowhere"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<GapFillTarget>(gftNowhere));

    def = this->add("bridge_angle", coFloat);
    def->label = L("External bridge infill direction");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("External Bridging angle override.\n"
        "If left to zero, the bridging angle will be calculated automatically for each specific bridge.\n"
        "Otherwise the provided angle will be used according to:\n"
        " - The absolute coordinates\n"
        " - The absolute coordinates + Model rotation: If Align directions to model is enabled\n"
        " - The optimal automatic angle + this value: If 'Relative Bridge Angle' is enabled\n\n"
        "Use 180° for zero absolute angle.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));
    
    // ORCA: Internal bridge angle override
    def = this->add("internal_bridge_angle", coFloat);
    def->label = L("Internal bridge infill direction");
    def->category = L("Strength");
    def->tooltip = L("Internal Bridging angle override.\n"
        "If left to zero, the bridging angle will be calculated automatically for each specific bridge.\n"
        "Otherwise the provided angle will be used according to:\n"
        " - The absolute coordinates\n"
        " - The absolute coordinates + Model rotation: If Align directions to model is enabled\n"
        " - The optimal automatic angle + this value: If 'Relative Bridge Angle' is enabled\n\n"
        "Use 180° for zero absolute angle.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    // ORCA: Relative bridge angle
    def = this->add("relative_bridge_angle", coBool);
    def->label = L("Relative bridge angle");
    def->category = L("Strength");
    def->tooltip = L("When enabled, the bridge angle values are added to the automatically calculated bridge direction instead of overriding it.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ensure_vertical_shell_thickness", coEnum);
    def->label = L("Ensure vertical shell thickness");
    def->category = L("Strength");
    def->tooltip  = L(
        "Add solid infill near sloping surfaces to guarantee the vertical shell thickness (top+bottom solid layers)\nNone: No solid infill "
         "will be added anywhere. Caution: Use this option carefully if your model has sloped surfaces\nCritical Only: Avoid adding solid infill for walls\nModerate: Add solid infill for heavily "
         "sloping surfaces only\nAll: Add solid infill for all suitable sloping surfaces\nDefault value is All.");
    def->enum_keys_map = &ConfigOptionEnum<EnsureVerticalShellThickness>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("ensure_critical_only");
    def->enum_values.push_back("ensure_moderate");
    def->enum_values.push_back("ensure_all");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Critical Only"));
    def->enum_labels.push_back(L("Moderate"));
    def->enum_labels.push_back(L("All"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<EnsureVerticalShellThickness>(EnsureVerticalShellThickness::evstAll));
    
    auto def_top_fill_pattern = def = this->add("top_surface_pattern", coEnum);
    def->label = L("Top surface pattern");
    def->category = L("Strength");
    def->tooltip = L("This is the line pattern for top surface infill.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("monotonicline");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("spiralinset");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Monotonic line"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Spiral Inset"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonicLine));

    def = this->add("top_surface_density", coPercent);
    def->label = L("Top surface density");
    def->category = L("Strength");
    def->tooltip = L("Density of top surface layer. A value of 100% creates a fully solid, smooth top layer. "
                     "Reducing this value results in a textured top surface, according to the chosen top surface pattern. "
                     "A value of 0% will result in only the walls on the top layer being created. "
                     "Intended for aesthetic or functional purposes, not to fix issues such as over-extrusion.");
    def->sidetext = ("%");
    def->min = 0;
    def->max = 100;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("top_surface_expansion", coFloat);
    def->label = L("Top surface expansion");
    def->category = L("Strength");
    def->tooltip = L("Expands the top surfaces by this distance to connect distinct top surfaces and fill gaps.\n"
                     "Useful for cases where the top surface is interrupted by a raised feature, such as text on a plane. "
                     "Expanding it removes the holes beneath these features and creates a continuous path with a better finish for printing on top. "
                     "The expansion is applied to the original top surface, before any other processing such as bridging or overhang detection.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("top_surface_expansion_margin", coFloat);
    def->label = L("Top expansion wall margin");
    def->category = L("Strength");
    def->tooltip = L("Using “Top surface expansion” may cause a surface that did not previously touch the model's outer walls to now do so.\n"
                     "This can cause contraction marks (such as the hull line) on the outer walls.\n"
                     "By adding a small margin, this contraction will not occur directly on the walls, thereby preventing a visible mark.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("top_surface_expansion_direction", coEnum);
    def->label = L("Top expansion direction");
    def->category = L("Strength");
    def->tooltip = L("Direction in which the top surface expansion grows.\n"
                     " - Inward grows into the holes and gaps left by features rising from the middle of a top surface.\n"
                     " - Outward grows the outer edge of the surface, connecting surfaces separated by features that can divide a surface, such as a lattice pattern.\n"
                     " - Inward and Outward does both.");
    def->enum_keys_map = &ConfigOptionEnum<TopSurfaceExpansionDirection>::get_enum_values();
    def->enum_values.push_back("inward_and_outward");
    def->enum_values.push_back("inward");
    def->enum_values.push_back("outward");
    def->enum_labels.push_back(L("Inward and Outward"));
    def->enum_labels.push_back(L("Inward"));
    def->enum_labels.push_back(L("Outward"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<TopSurfaceExpansionDirection>(TopSurfaceExpansionDirection::InwardAndOutward));

    def = this->add("bottom_surface_pattern", coEnum);
    def->label = L("Bottom surface pattern");
    def->category = L("Strength");
    def->tooltip = L("This is the line pattern of bottom surface infill, not including bridge infill.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values = def_top_fill_pattern->enum_values;
    def->enum_labels = def_top_fill_pattern->enum_labels;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

    def           = this->add("bottom_surface_density", coPercent);
    def->label    = L("Bottom surface density");
    def->category = L("Strength");
    def->tooltip = L("Density of the bottom surface layer. "
                     "Intended for aesthetic or functional purposes, not to fix issues such as over-extrusion.\n"
                     "WARNING: Lowering this value may negatively affect bed adhesion.");
    def->sidetext = ("%");
    def->min      = 10;
    def->max      = 100;
    def->set_default_value(new ConfigOptionPercent(100));

    auto def_top_fill_order = def = this->add("top_surface_fill_order", coEnum);
    def->label = L("Top surface fill order");
    def->category = L("Strength");
    def->tooltip = L("Direction in which top surfaces are filled when using a center-based pattern "
                     "(Concentric, Spiral Inset, Archimedean Chords, Octagram Spiral).\n"
                     "Outward starts at the center of the surface, so any excess material is pushed "
                     "towards the edge where it is least visible. Inward starts at the edge and ends "
                     "with the tight curves at the center.\n"
                     "Default uses shortest-path ordering, which may run in either direction.");
    def->enum_keys_map = &ConfigOptionEnum<SurfaceFillOrder>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("outward");
    def->enum_values.push_back("inward");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Outward"));
    def->enum_labels.push_back(L("Inward"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SurfaceFillOrder>(SurfaceFillOrder::Default));

    def = this->add("bottom_surface_fill_order", coEnum);
    def->label = L("Bottom surface fill order");
    def->category = L("Strength");
    def->tooltip = L("Direction in which bottom surfaces are filled when using a center-based pattern "
                     "(Concentric, Spiral Inset, Archimedean Chords, Octagram Spiral).\n"
                     "Inward starts each surface with the wider outer curves, which improves first layer "
                     "adhesion on build plates where the tight curves at the center may not stick. "
                     "Outward starts at the center, pushing any excess material towards the edge.\n"
                     "Default uses shortest-path ordering, which may run in either direction.");
    def->enum_keys_map = &ConfigOptionEnum<SurfaceFillOrder>::get_enum_values();
    def->enum_values = def_top_fill_order->enum_values;
    def->enum_labels = def_top_fill_order->enum_labels;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SurfaceFillOrder>(SurfaceFillOrder::Default));

	def                = this->add("internal_solid_infill_pattern", coEnum);
    def->label         = L("Internal solid infill pattern");
    def->category      = L("Strength");
    def->tooltip       = L("Line pattern of internal solid infill. if the detect narrow internal solid infill be enabled, the concentric pattern will be used for the small area.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values   = def_top_fill_pattern->enum_values;
    def->enum_labels   = def_top_fill_pattern->enum_labels;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

    def = this->add("infill_direction", coFloat);
    def->label = L("Sparse infill direction");
    def->category = L("Strength");
    def->tooltip = L("This is the angle for sparse infill pattern, which controls the start or main direction of lines.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("solid_infill_direction", coFloat);
    def->label = L("Solid infill direction");
    def->category = L("Strength");
    def->tooltip = L("Angle for solid infill pattern, which controls the start or main direction of line.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("top_layer_direction", coFloat);
    def->label = L("Top layer direction");
    def->category = L("Strength");
    def->tooltip = L("Fixed angle for the top solid infill and ironing lines.\nSet to -1 to follow the default solid infill direction.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = -1;
    def->max = 360;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(-1));

    def = this->add("bottom_layer_direction", coFloat);
    def->label = L("Bottom layer direction");
    def->category = L("Strength");
    def->tooltip = L("Fixed angle for the bottom solid infill lines.\nSet to -1 to follow the default solid infill direction.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min = -1;
    def->max = 360;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(-1));

    def = this->add("sparse_infill_density", coPercent);
    def->label = L("Sparse infill density");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Density of internal sparse infill, 100% turns all sparse infill into solid infill and internal solid infill pattern will be used.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->set_default_value(new ConfigOptionPercent(20));

    def           = this->add("align_infill_direction_to_model", coBool);
    def->label    = L("Align directions to model");
    def->category = L("Strength");
    def->tooltip  = L("Aligns infill, bridge, ironing, and top/bottom surface directions to follow the model's orientation on the build plate.\n"
                      "When enabled, these directions rotate together with the model so the printed features keep their intended orientation "
                      "relative to the part, preserving optimal strength and surface characteristics regardless of how the model is placed.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("extra_solid_infills", coString);
    def->label    = L("Insert solid layers");
    def->category = L("Strength");
    def->tooltip  = L("Insert solid infill at specific layers. Use N to insert every Nth layer, N#K to insert K consecutive solid layers every N layers (K is optional, e.g. '5#' equals '5#1'), or a comma-separated list (e.g. 1,7,9) to insert at explicit layers. Layers are 1-based.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionString());


    // Infill multiline
    def             = this->add("fill_multiline", coInt);
    def->label      = L("Fill Multiline");
    def->category   = L("Strength");
    def->tooltip    = L("Using multiple lines for the infill pattern, if supported by infill pattern.");
    def->min = 1;
    def->max = 10; // Maximum number of lines for infill pattern
    def->set_default_value(new ConfigOptionInt(1));

    def             = this->add("tpms_adaptive", coEnum);
    def->label      = L("Adaptive density (experimental)");
    def->category   = L("Strength");
    def->tooltip    = L("Grades the Gyroid and TPMS infill inside the object: its cells grow from the surface of the "
                        "object towards its center. The sparse infill density is used at the surface and the interior "
                        "density at the center.\n"
                        "Distance warp, Smooth blend and Stepped shells follow the distance to the nearest surface, "
                        "including the top and bottom, with the interior density at the point farthest from it:\n"
                        " - Distance warp: one continuous pattern, stretched and sheared where the distance changes "
                        "across directions, as in plates and long parts.\n"
                        " - Smooth blend: the patterns of neighbouring densities blended into each other, with small "
                        "loops where they meet.\n"
                        " - Stepped shells: shells of the regular pattern at densities about 1.5 times apart, their "
                        "lines joined along the shell boundaries.\n"
                        " - Lobes: follows the 3D shape of the object, including its top and bottom. Every lobe, a part "
                        "joined to the rest by a narrower neck, is graded towards its own center.\n"
                        " - Normal Z, Y or X: follows the sections of the object normal to that axis, so the density "
                        "does not change along it.");
    def->enum_keys_map = &ConfigOptionEnum<TpmsAdaptiveMode>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("distance_warp");
    def->enum_values.push_back("smooth_blend");
    def->enum_values.push_back("stepped_shells");
    def->enum_values.push_back("lobes");
    def->enum_values.push_back("normal_z");
    def->enum_values.push_back("normal_y");
    def->enum_values.push_back("normal_x");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("Distance warp"));
    def->enum_labels.push_back(L("Smooth blend"));
    def->enum_labels.push_back(L("Stepped shells"));
    def->enum_labels.push_back(L("Lobes"));
    def->enum_labels.push_back(L("Normal Z"));
    def->enum_labels.push_back(L("Normal Y"));
    def->enum_labels.push_back(L("Normal X"));
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<TpmsAdaptiveMode>(TpmsAdaptiveMode::Disabled));

    def             = this->add("tpms_interior_density", coPercent);
    def->label      = L("Interior density");
    def->category   = L("Strength");
    def->tooltip    = L("Density of the adaptive infill at the center of the object.");
    def->sidetext   = "%";
    def->min        = 1;
    def->max        = 100;
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(5));

    def             = this->add("tpms_adaptive_gradient", coEnum);
    def->label      = L("Adaptive gradient");
    def->category   = L("Strength");
    def->tooltip    = L("How the density changes from the surface to the center of the object.\n"
                        "Linear: the density changes at a constant rate.\n"
                        "Quadratic: the density stays close to the sparse infill density near the surface and "
                        "changes faster towards the center.\n"
                        "Exponential: the density changes quickly just below the surface and levels off towards "
                        "the center.");
    def->enum_keys_map = &ConfigOptionEnum<TpmsAdaptiveGradient>::get_enum_values();
    def->enum_values.push_back("linear");
    def->enum_values.push_back("quadratic");
    def->enum_values.push_back("exponential");
    def->enum_labels.push_back(L("Linear"));
    def->enum_labels.push_back(L("Quadratic"));
    def->enum_labels.push_back(L("Exponential"));
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<TpmsAdaptiveGradient>(TpmsAdaptiveGradient::Linear));

    // Z-buckling bias optimization (experimental). Tightens the gyroid wave along the Z
    // (vertical) axis at low infill density to shorten the effective column length under
    // Z-axis compression. Filament use at the same `sparse_infill_density` setting is
    // preserved. No effect above ~30% density (formula clamps to no-op).
    def             = this->add("gyroid_optimized", coBool);
    def->label      = L("Z-buckling bias optimization (experimental)");
    def->category   = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip    = L("Tightens the gyroid wave along the Z (vertical) axis at low infill density "
                        "to shorten the effective vertical column length and improve Z-axis compression "
                        "buckling resistance. Filament use is preserved. No effect at ~30% sparse infill "
                        "density and above. Only applies when Sparse infill pattern is set to Gyroid.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("sparse_infill_pattern", coEnum);
    def->label = L("Sparse infill pattern");
    def->category = L("Strength");
    def->tooltip = L("This is the line pattern for internal sparse infill.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("zigzag");
    def->enum_values.push_back("crosszag");
    def->enum_values.push_back("lockedzag");
    def->enum_values.push_back("line");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("tri-hexagon");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("adaptivecubic");
    def->enum_values.push_back("quartercubic");
    def->enum_values.push_back("supportcubic");
    def->enum_values.push_back("lightning");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("3dhoneycomb");
    def->enum_values.push_back("lateral-honeycomb");
    def->enum_values.push_back("lateral-lattice");
    def->enum_values.push_back("crosshatch");
    def->enum_values.push_back("tpmsd");
    def->enum_values.push_back("tpmsfk");
    def->enum_values.push_back("gyroid");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("Zig Zag"));
    def->enum_labels.push_back(L("Cross Zag"));
    def->enum_labels.push_back(L("Locked Zag"));
    def->enum_labels.push_back(L("Line"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Triangles"));
    def->enum_labels.push_back(L("Tri-hexagon"));
    def->enum_labels.push_back(L("Cubic"));
    def->enum_labels.push_back(L("Adaptive Cubic"));
    def->enum_labels.push_back(L("Quarter Cubic"));
    def->enum_labels.push_back(L("Support Cubic"));
    def->enum_labels.push_back(L("Lightning"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("3D Honeycomb"));
    def->enum_labels.push_back(L("Lateral Honeycomb"));
    def->enum_labels.push_back(L("Lateral Lattice"));
    def->enum_labels.push_back(L("Cross Hatch"));
    def->enum_labels.push_back(L("TPMS-D"));
    def->enum_labels.push_back(L("TPMS-FK"));
    def->enum_labels.push_back(L("Gyroid"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipCrossHatch));

    def = this->add("sparse_infill_smooth_factor", coPercent);
    def->label = L("Sparse infill smooth factor");
    def->category = L("Strength");
    def->tooltip = L("Controls how strongly sparse infill corners are rounded. 0% keeps the original sharp path, "
                     "while 100% produces the largest possible curves between adjacent infill lines.");
    def->sidetext = "%";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(0));

    def = this->add("filter_out_gap_fill", coFloat);
    def->label = L("Filter out tiny gaps");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Don't print gap fill with a length is smaller than the threshold specified (in mm). This setting applies to top, "
                     "bottom and solid infill and, if using the classic perimeter generator, to wall gap fill.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    //BBS
    def = this->add("infill_combination", coBool);
    def->label = L("Infill combination");
    def->category = L("Strength");
    def->tooltip = L("Automatically combine sparse infill of several layers to print together in order to reduce time. Walls are still printed with original layer height.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("infill_shift_step", coFloat);
    def->label    = L("Infill shift step");
    def->category = L("Strength");
    def->tooltip  = L("This parameter adds a slight displacement to each layer of infill to create a cross texture.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    //Orca
    def           = this->add("sparse_infill_rotate_template", coString);
    def->label    = L("Sparse infill rotation template");
    def->category = L("Strength");
    def->tooltip  = L("Rotate the sparse infill direction per layer using a template of angles. "
                      "Enter comma-separated degrees (e.g., '0,30,60,90'). "
                      "Angles are applied in order by layer and repeat when the list ends. "
                      "Advanced syntax is supported: '+5' rotates +5° every layer; '+5#5' rotates +5° every 5 layers. See the Wiki for details. "
                      "When a template is set, the standard infill direction setting is ignored. "
                      "Note: some infill patterns (e.g., Gyroid) control rotation themselves; use with care.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    //Orca
    def           = this->add("solid_infill_rotate_template", coString);
    def->label    = L("Solid infill rotation template");
    def->category = L("Strength");
    def->tooltip  = L("This parameter adds a rotation of solid infill direction to each layer according to the specified template. "
                      "The template is a comma-separated list of angles in degrees, e.g. '0,90'. "
                      "The first angle is applied to the first layer, the second angle to the second layer, and so on. "
                      "If there are more layers than angles, the angles will be repeated. Note that not all solid infill patterns support rotation.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def           = this->add("skeleton_infill_density", coPercent);
    def->label    = L("Skeleton infill density");
    def->category = L("Strength");
    def->tooltip  = L("The remaining part of the model contour after removing a certain depth from the surface is called the skeleton. "
                      "This parameter is used to adjust the density of this section. "
                      "When two regions have the same sparse infill settings but different skeleton densities, their skeleton areas will develop overlapping sections. "
                      "Default is as same as infill density.");
    def->sidetext = "%";
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(25));

    def           = this->add("skin_infill_density", coPercent);
    def->label    = L("Skin infill density");
    def->category = L("Strength");
    def->tooltip  = L("The portion of the model's outer surface within a certain depth range is called the skin. "
                      "This parameter is used to adjust the density of this section. "
                      "When two regions have the same sparse infill settings but different skin densities, this area will not be split into two separate regions. "
                      "Default is as same as infill density.");
    def->sidetext = "%";
    def->min  = 0;
    def->max  = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(25));

    def           = this->add("skin_infill_depth", coFloat);
    def->label    = L("Skin infill depth");
    def->category = L("Strength");
    def->tooltip  = L("The parameter sets the depth of skin.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def           = this->add("infill_lock_depth", coFloat);
    def->label    = L("Infill lock depth");
    def->category = L("Strength");
    def->tooltip  = L("The parameter sets the overlapping depth between the interior and skin.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def           = this->add("skin_infill_line_width", coFloatOrPercent);
    def->label    = L("Skin line width");
    def->category = L("Strength");
    def->tooltip  = L("Adjust the line width of the selected skin paths.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->ratio_over = "nozzle_diameter";
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def           = this->add("skeleton_infill_line_width", coFloatOrPercent);
    def->label    = L("Skeleton line width");
    def->category = L("Strength");
    def->tooltip  = L("Adjust the line width of the selected skeleton paths.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->ratio_over = "nozzle_diameter";
    def->min      = 0;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def           = this->add("symmetric_infill_y_axis", coBool);
    def->label    = L("Symmetric infill Y axis");
    def->category = L("Strength");
    def->tooltip  = L("If the model has two parts that are symmetric about the Y axis,"
                      " and you want these parts to have symmetric textures, please click this option on one of the parts.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("infill_complete_top", coBool);
    def->label    = L("Fill pattern tops");
    def->category = L("Strength");
    def->tooltip  = L("Choose this option if you want to completely fill in the tops of the infill pattern");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    // Orca: max layer height for combined infill
    def = this->add("infill_combination_max_layer_height", coFloatOrPercent);
    def->label = L("Infill combination - Max layer height");
    def->category = L("Strength");
    def->tooltip = L("Maximum layer height for the combined sparse infill.\n\n"
                     "Set it to 0 or 100% to use the nozzle diameter (for maximum reduction in print time) or a value of ~80% to maximize sparse infill strength.\n\n"
                     "The number of layers over which infill is combined is derived by dividing this value with the layer height and rounded down to the nearest decimal.\n\n"
                     "Use either absolute mm values (eg. 0.32mm for a 0.4mm nozzle) or % values (eg 80%). This value must not be larger "
                     "than the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100., true));

    def           = this->add("lateral_lattice_angle_1", coFloat);
    def->label    = L("Lateral lattice angle 1");
    def->category = L("Strength");
    def->tooltip  = L("The angle of the first set of Lateral lattice elements in the Z direction. Zero is vertical.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = -75;
    def->max      = 75;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-45));

    def           = this->add("lateral_lattice_angle_2", coFloat);
    def->label    = L("Lateral lattice angle 2");
    def->category = L("Strength");
    def->tooltip  = L("The angle of the second set of Lateral lattice elements in the Z direction. Zero is vertical.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = -75;
    def->max      = 75;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def           = this->add("infill_overhang_angle", coFloat);
    def->label    = L("Infill overhang angle");
    def->category = L("Strength");
    def->tooltip  = L("The angle of the infill angled lines. 60° will result in a pure honeycomb.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 15;
    def->max      = 75;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def           = this->add("lightning_overhang_angle", coFloat);
    def->label    = L("Lightning overhang angle");
    def->category = L("Strength");
    def->tooltip  = L("Maximum overhang angle for Lightning infill support propagation.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 5;
    def->max      = 85;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(45));

    def           = this->add("lightning_prune_angle", coFloat);
    def->label    = L("Prune angle");
    def->category = L("Strength");
    def->tooltip  = L("Controls how aggressively short or unsupported Lightning branches are pruned.\n"
                      "This angle is converted internally to a per-layer distance.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 5;
    def->max      = 85;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(45));

    def           = this->add("lightning_straightening_angle", coFloat);
    def->label    = L("Straightening angle");
    def->category = L("Strength");
    def->tooltip  = L("Maximum straightening angle used to simplify Lightning branches.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->min      = 5;
    def->max      = 85;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionFloat(45));

    auto def_infill_anchor_min = def = this->add("infill_anchor", coFloatOrPercent);
    def->label = L("Sparse infill anchor length");
    def->category = L("Strength");
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "Orca Slicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max.\n"
                     "Set this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->sidetext = L("mm or %");
    def->ratio_over = "sparse_infill_line_width";
    def->max_literal = 1000;
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("1000");
    def->enum_labels.push_back(L("0 (no open anchors)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(400, true));

    def = this->add("infill_anchor_max", coFloatOrPercent);
    def->label = L("Maximum length of the infill anchor");
    def->category = L("Strength");
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "Orca Slicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter.\n"
                     "If set to 0, the old algorithm for infill connection will be used, it should create the same result as with 1000 & 0.");
    def->sidetext    = def_infill_anchor_min->sidetext;
    def->ratio_over  = def_infill_anchor_min->ratio_over;
    def->gui_type    = def_infill_anchor_min->gui_type;
    def->enum_values = def_infill_anchor_min->enum_values;
    def->max_literal = def_infill_anchor_min->max_literal;
    def->enum_labels.push_back(L("0 (Simple connect)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(20, false));

    def = this->add("infill_wall_overlap", coPercent);
    def->label = L("Infill/wall overlap");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("This allows the infill area to be enlarged slightly to overlap with walls for better bonding. The percentage value is relative to the inner wall line width. Set this value to ~10-15% to minimize potential over extrusion and accumulation of material resulting in rough top surfaces.");
    def->sidetext = "%";
    def->ratio_over = "inner_wall_line_width";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(15));
    
    def = this->add("top_bottom_infill_wall_overlap", coPercent);
    def->label = L("Top/Bottom solid infill/wall overlap");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Top solid infill area is enlarged slightly to overlap with wall for better bonding "
                     "and to minimize the appearance of pinholes where the top infill meets the walls. "
                     "A value of 25-30% is a good starting point, minimizing the appearance of pinholes. "
                     "The percentage value is relative to the inner wall line width.");
    def->sidetext = "%";
    def->ratio_over = "inner_wall_line_width";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("wall_loops", coInt);
    def->label = L("Wall loops");
    def->category = L("Strength");
    def->tooltip = L("This is the number of walls per layer.");
    def->min = 0;
    def->max = 1000;
    def->set_default_value(new ConfigOptionInt(2));
    
    def = this->add("alternate_extra_wall", coBool);
    def->label = L("Alternate extra wall");
    def->category = L("Strength");
    def->tooltip = L("This setting adds an extra wall to every other layer. This way the infill gets wedged vertically between the walls, resulting in stronger prints.\n\n"
                     "When this option is enabled, the ensure vertical shell thickness option needs to be disabled.\n\n"
                     "Using lightning infill together with this option is not recommended as there is limited infill to anchor the extra perimeters to.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("minimum_sparse_infill_area", coFloat);
    def->label = L("Minimum sparse infill threshold");
    def->category = L("Strength");
    def->tooltip = L("Sparse infill areas which are smaller than this threshold value are replaced by internal solid infill.");
    def->sidetext = L(u8"mm²");	// square millimeters, CIS languages need translation
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("detect_thin_wall", coBool);
    def->label = L("Detect thin walls");
    def->category = L("Strength");
    def->tooltip = L("This detects thin walls which can\u2019t contain two lines and uses a single line to print. It may not print as well because it\u2019s not a closed loop.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("top_shell_layers", coInt);
    def->label = L("Top shell layers");
    def->category = L("Strength");
    def->sidetext = L("layers"); // ORCA add side text
    def->tooltip = L("This is the number of solid layers of top shell, including the top surface layer. When the thickness calculated by this value is thinner than the top shell thickness, the top shell layers will be increased");
    def->full_label = L("Top solid layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(4));

    def = this->add("top_shell_thickness", coFloat);
    def->label = L("Top shell thickness");
    def->category = L("Strength");
    def->tooltip = L("The number of top solid layers is increased when slicing if the thickness calculated by top shell layers is thinner than this value. This can avoid having too thin a shell when layer height is small. 0 means that this setting is disabled and thickness of top shell is determined simply by the number of top shell layers.");
    def->full_label = L("Top shell thickness");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.6));

    def           = this->add("separated_infills", coBool);
    def->label    = L("Separated infills");
    def->category = L("Strength");
    def->tooltip  = L("Centers the internal infill of each part on itself, as if it were sliced on its own, instead of on the "
                       "whole assembly. Parts that touch or overlap are treated as one body and share a center; separate parts "
                       "(or distinct 3D objects) each get their own.\n"
                       "Useful when an assembly groups several objects that should each keep a consistent, self-centered infill.\n"
                       "Adaptive Cubic and Support Cubic always center each part on itself, and Lightning infill is generated for "
                       "the whole object and is unaffected.");
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def                = this->add("center_of_surface_pattern", coEnum);
    def->label         = L("Center surface pattern on");
    def->category      = L("Strength");
    def->tooltip       = L("Chooses where the centering point of centered top/bottom surface patterns (Archimedean Chords, "
                                 "Octagram Spiral) is placed.\n"
                                 " - Each Surface: centers the pattern on every individual surface region, so each island is symmetric on its own.\n"
                                 " - Each Model: centers the pattern on each connected body. Parts that touch or overlap share one center; "
                                 "parts detached from the rest each get their own.\n"
                                 " - Each Assembly: uses a single shared center for the whole object or assembly.");
    def->enum_keys_map = &ConfigOptionEnum<CenterOfSurfacePattern>::get_enum_values();
    def->enum_values.push_back("each_surface");
    def->enum_values.push_back("each_model");
    def->enum_values.push_back("each_assembly");
    def->enum_labels.push_back(L("Each Surface"));
    def->enum_labels.push_back(L("Each Model"));
    def->enum_labels.push_back(L("Each Assembly"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<CenterOfSurfacePattern>(CenterOfSurfacePattern::Each_Surface));

    def = this->add("detect_narrow_internal_solid_infill", coBool);
    def->label = L("Detect narrow internal solid infills");
    def->category = L("Strength");
    def->tooltip = L("This option will auto-detect narrow internal solid infill areas. "
                     "If enabled, the concentric pattern will be used for the area to speed up printing. "
                     "Otherwise, the rectilinear pattern will be used by default.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
}

} // namespace Slic3r
