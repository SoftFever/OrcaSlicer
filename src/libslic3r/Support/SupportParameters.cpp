#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Slicing.hpp"
#include "SupportParameters.hpp"

namespace Slic3r::FFFSupport {

SupportParameters::SupportParameters(const PrintObject &object)
{
    const PrintConfig       &print_config   = object.print()->config();
    const PrintObjectConfig &object_config  = object.config();
    const SlicingParameters &slicing_params = object.slicing_parameters();

    this->soluble_interface = slicing_params.soluble_interface;
    this->soluble_interface_non_soluble_base =
        // Zero z-gap between the overhangs and the support interface.
        slicing_params.soluble_interface &&
        // Interface extruder soluble.
        object_config.support_interface_filament.value > 0 && print_config.filament_soluble.get_at(object_config.support_interface_filament.value - 1) &&
        // Base extruder: Either "print with active extruder" not soluble.
        (object_config.support_filament.value == 0 || ! print_config.filament_soluble.get_at(object_config.support_filament.value - 1));

    {
        int num_top_interface_layers    = std::max(0, object_config.support_interface_top_layers.value);
        int num_bottom_interface_layers = object_config.support_interface_bottom_layers < 0 ? 
            num_top_interface_layers : object_config.support_interface_bottom_layers;
        this->has_top_contacts              = num_top_interface_layers    > 0;
        this->has_bottom_contacts           = num_bottom_interface_layers > 0;
        this->num_top_interface_layers      = this->has_top_contacts ? size_t(num_top_interface_layers - 1) : 0;
        this->num_bottom_interface_layers   = this->has_bottom_contacts ? size_t(num_bottom_interface_layers - 1) : 0;
        if (this->soluble_interface_non_soluble_base) {
            // Try to support soluble dense interfaces with non-soluble dense interfaces.
            this->num_top_base_interface_layers    = size_t(std::min(num_top_interface_layers / 2, 2));
            this->num_bottom_base_interface_layers = size_t(std::min(num_bottom_interface_layers / 2, 2));
        } else {
            this->num_top_base_interface_layers    = 0;
            this->num_bottom_base_interface_layers = 0;
        }
    }

    this->first_layer_flow                   = Slic3r::support_material_1st_layer_flow(&object, float(slicing_params.first_print_layer_height));
    this->support_material_flow              = Slic3r::support_material_flow(&object, float(slicing_params.layer_height));
    this->support_material_interface_flow    = Slic3r::support_material_interface_flow(&object, float(slicing_params.layer_height));
    this->raft_interface_flow                = support_material_interface_flow;

    // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
    this->support_layer_height_min = scaled<coord_t>(0.01);
    for (auto lh : print_config.min_layer_height.values)
        this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, lh));
    for (auto layer : object.layers())
        this->support_layer_height_min = std::min(this->support_layer_height_min, std::max(0.01, layer->height));

    if (object_config.support_interface_top_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        this->support_material_interface_flow = this->support_material_flow;
    }

    // Evaluate the XY gap between the object outer perimeters and the support structures.
    // Evaluate the XY gap between the object outer perimeters and the support structures.
    coordf_t external_perimeter_width = 0.;
    coordf_t bridge_flow_ratio = 0;
    for (size_t region_id = 0; region_id < object.num_printing_regions(); ++ region_id) {
        const PrintRegion &region = object.printing_region(region_id);
        external_perimeter_width = std::max(external_perimeter_width, coordf_t(region.flow(object, frExternalPerimeter, slicing_params.layer_height).width()));
        bridge_flow_ratio += region.config().bridge_flow;
    }
    this->gap_xy = object_config.support_object_xy_distance;//.get_abs_value(external_perimeter_width);
    bridge_flow_ratio /= object.num_printing_regions();

    this->support_material_bottom_interface_flow = slicing_params.soluble_interface || ! object_config.thick_bridges ?
        this->support_material_interface_flow.with_flow_ratio(bridge_flow_ratio) :
        Flow::bridging_flow(bridge_flow_ratio * this->support_material_interface_flow.nozzle_diameter(), this->support_material_interface_flow.nozzle_diameter());

    this->can_merge_support_regions = object_config.support_filament.value == object_config.support_interface_filament.value;
    if (!this->can_merge_support_regions && (object_config.support_filament.value == 0 || object_config.support_interface_filament.value == 0)) {
        // One of the support extruders is of "don't care" type.
        auto object_extruders = object.object_extruders();
        if (object_extruders.size() == 1 &&
            *object_extruders.begin() == std::max<unsigned int>(object_config.support_filament.value, object_config.support_interface_filament.value))
            // Object is printed with the same extruder as the support.
            this->can_merge_support_regions = true;
    }

    double interface_spacing = object_config.support_interface_spacing.value + this->support_material_interface_flow.spacing();
    this->interface_density  = std::min(1., this->support_material_interface_flow.spacing() / interface_spacing);
    double raft_interface_spacing = object_config.support_interface_spacing.value + this->raft_interface_flow.spacing();
    this->raft_interface_density = std::min(1., this->raft_interface_flow.spacing() / raft_interface_spacing);
    double support_spacing   = object_config.support_base_pattern_spacing.value + this->support_material_flow.spacing();
    this->support_density    = std::min(1., this->support_material_flow.spacing() / support_spacing);
    if (object_config.support_interface_top_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        this->interface_density = this->support_density;
    }

    SupportMaterialPattern  support_pattern = object_config.support_base_pattern;
    this->with_sheath            = false;//object_config.support_material_with_sheath;
    this->base_fill_pattern      = 
        support_pattern == smpHoneycomb ? ipHoneycomb :
        this->support_density > 0.95 || this->with_sheath ? ipRectilinear : ipSupportBase;
    this->interface_fill_pattern = (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);
    this->raft_interface_fill_pattern = this->raft_interface_density > 0.95 ? ipRectilinear : ipSupportBase;
    this->contact_fill_pattern   =
        (object_config.support_interface_pattern == smipAuto && slicing_params.soluble_interface) ||
        object_config.support_interface_pattern == smipConcentric ?
        ipConcentric :
        (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);

    this->base_angle            = Geometry::deg2rad(float(object_config.support_angle.value));
    this->interface_angle       = Geometry::deg2rad(float(object_config.support_angle.value + 90.));
    this->raft_angle_1st_layer  = 0.f;
    this->raft_angle_base       = 0.f;
    this->raft_angle_interface  = 0.f;
    if (slicing_params.base_raft_layers > 1) {
        assert(slicing_params.raft_layers() >= 4);
        // There are all raft layer types (1st layer, base, interface & contact layers) available.
        this->raft_angle_1st_layer  = this->interface_angle;
        this->raft_angle_base       = this->base_angle;
        this->raft_angle_interface  = this->interface_angle;
        if ((slicing_params.interface_raft_layers & 1) == 0)
            // Allign the 1st raft interface layer so that the object 1st layer is hatched perpendicularly to the raft contact interface.
            this->raft_angle_interface += float(0.5 * M_PI);
    } else if (slicing_params.base_raft_layers == 1 || slicing_params.interface_raft_layers > 1) {
        assert(slicing_params.raft_layers() == 2 || slicing_params.raft_layers() == 3);
        // 1st layer, interface & contact layers available.
        this->raft_angle_1st_layer  = this->base_angle;
        this->raft_angle_interface  = this->interface_angle + 0.5 * M_PI;
    } else if (slicing_params.interface_raft_layers == 1) {
        // Only the contact raft layer is non-empty, which will be printed as the 1st layer.
        assert(slicing_params.base_raft_layers == 0);
        assert(slicing_params.interface_raft_layers == 1);
        assert(slicing_params.raft_layers() == 1);
        this->raft_angle_1st_layer = float(0.5 * M_PI);
        this->raft_angle_interface = this->raft_angle_1st_layer;
    } else {
        // No raft.
        assert(slicing_params.base_raft_layers == 0);
        assert(slicing_params.interface_raft_layers == 0);
        assert(slicing_params.raft_layers() == 0);
    }

    this->tree_branch_diameter_double_wall_area_scaled = 0.25 * sqr(scaled<double>(object_config.tree_support_branch_diameter_double_wall.value)) * M_PI;
}

} // namespace Slic3r
