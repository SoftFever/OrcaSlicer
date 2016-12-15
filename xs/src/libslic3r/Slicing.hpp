// Based on implementation by @platsch

#ifndef slic3r_Slicing_hpp_
#define slic3r_Slicing_hpp_

#include <set>
#include <vector>

#include "libslic3r.h"
namespace Slic3r
{

class PrintConfig;
class PrintObjectConfig;
class ModelVolume;
typedef std::vector<ModelVolume*> ModelVolumePtrs;

// Parameters to guide object slicing and support generation.
// The slicing parameters account for a raft and whether the 1st object layer is printed with a normal or a bridging flow
// (using a normal flow over a soluble support, using a bridging flow over a non-soluble support).
struct SlicingParameters
{
	SlicingParameters() { memset(this, 0, sizeof(SlicingParameters)); }

    static SlicingParameters create_from_config(
        const PrintConfig       &print_config, 
        const PrintObjectConfig &object_config,
        coordf_t                 object_height,
        const std::set<size_t>  &object_extruders);

    // Has any raft layers?
    bool        has_raft() const { return raft_layers() > 0; }
    size_t      raft_layers() const { return base_raft_layers + interface_raft_layers; }

    // Is the 1st object layer height fixed, or could it be varied?
    bool        first_object_layer_height_fixed()  const { return ! has_raft() || first_object_layer_bridging; }

    // Height of the object to be printed. This value does not contain the raft height.
    coordf_t    object_print_z_height() const { return object_print_z_max - object_print_z_min; }

    // Number of raft layers.
    size_t      base_raft_layers;
    // Number of interface layers including the contact layer.
    size_t      interface_raft_layers;

    // Layer heights of the raft (base, interface and a contact layer).
    coordf_t    base_raft_layer_height;
    coordf_t    interface_raft_layer_height;
    coordf_t    contact_raft_layer_height;
    bool        contact_raft_layer_height_bridging;

	// The regular layer height, applied for all but the first layer, if not overridden by layer ranges
	// or by the variable layer thickness table.
    coordf_t    layer_height;

    // Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
    // or a bridging flow thickness if printed over a non-soluble raft,
    // or a normal layer height if printed over a soluble raft.
    coordf_t    first_object_layer_height;

    // If the object is printed over a non-soluble raft, the first layer may be printed with a briding flow.
    bool 		first_object_layer_bridging;

    // Minimum / maximum layer height, to be used for the automatic adaptive layer height algorithm,
    // or by an interactive layer height editor.
    coordf_t    min_layer_height;
    coordf_t    max_layer_height;

    // Bottom and top of the printed object.
    // If printed without a raft, object_print_z_min = 0 and object_print_z_max = object height.
    // Otherwise object_print_z_min is equal to the raft height.
    coordf_t 	object_print_z_min;
    coordf_t 	object_print_z_max;
};

typedef std::pair<coordf_t,coordf_t> t_layer_height_range;
typedef std::map<t_layer_height_range,coordf_t> t_layer_height_ranges;

extern std::vector<coordf_t> layer_height_profile_from_ranges(
    const SlicingParameters     &slicing_params,
    const t_layer_height_ranges &layer_height_ranges);

extern std::vector<coordf_t> layer_height_profile_adaptive(
    const SlicingParameters     &slicing_params,
    const t_layer_height_ranges &layer_height_ranges,
    const ModelVolumePtrs       &volumes);

extern void adjust_layer_height_profile(
    const SlicingParameters     &slicing_params,
    std::vector<coordf_t>       &layer_height_profile,
    coordf_t                     z,
    coordf_t                     layer_thickness_delta, 
    coordf_t                     band_width,
    int                          action);

// Produce object layers as pairs of low / high layer boundaries, stored into a linear vector.
// The object layers are based at z=0, ignoring the raft layers.
extern std::vector<coordf_t> generate_object_layers(
    const SlicingParameters     &slicing_params,
    const std::vector<coordf_t> &layer_height_profile);

// Produce a 1D texture packed into a 2D texture describing in the RGBA format
// the planned object layers.
// Returns number of cells used by the texture of the 0th LOD level.
extern int generate_layer_height_texture(
    const SlicingParameters     &slicing_params,
    const std::vector<coordf_t> &layers,
    void *data, int rows, int cols, bool level_of_detail_2nd_level);

}; // namespace Slic3r

#endif /* slic3r_Slicing_hpp_ */
