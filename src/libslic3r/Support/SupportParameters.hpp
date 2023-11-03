#pragma once
#include "../libslic3r.h"
#include "../Flow.hpp"
#include "../PrintConfig.hpp"
#include "../Slicing.hpp"
#include "../Fill/FillBase.hpp"
#include "../Print.hpp"
#include "../Layer.hpp"
#include "SupportLayer.hpp"

namespace Slic3r {
struct SupportParameters {
    SupportParameters(const PrintObject& object)
    {
        const PrintConfig& print_config = object.print()->config();
        const PrintObjectConfig& object_config = object.config();
        const SlicingParameters& slicing_params = object.slicing_parameters();

	    this->soluble_interface = slicing_params.soluble_interface;
	    this->soluble_interface_non_soluble_base =
	        // Zero z-gap between the overhangs and the support interface.
	        slicing_params.soluble_interface &&
	        // Interface extruder soluble.
	        object_config.support_interface_filament.value > 0 && print_config.filament_soluble.get_at(object_config.support_interface_filament.value - 1) &&
	        // Base extruder: Either "print with active extruder" not soluble.
	        (object_config.support_filament.value == 0 || ! print_config.filament_soluble.get_at(object_config.support_filament.value - 1));

	    {
	        this->num_top_interface_layers    = std::max(0, object_config.support_interface_top_layers.value);
	        this->num_bottom_interface_layers = object_config.support_interface_bottom_layers < 0 ? 
	            num_top_interface_layers : object_config.support_interface_bottom_layers;
	        this->has_top_contacts              = num_top_interface_layers    > 0;
	        this->has_bottom_contacts           = num_bottom_interface_layers > 0;
	        if (this->soluble_interface_non_soluble_base) {
	            // Try to support soluble dense interfaces with non-soluble dense interfaces.
	            this->num_top_base_interface_layers    = size_t(std::min(int(num_top_interface_layers) / 2, 2));
	            this->num_bottom_base_interface_layers = size_t(std::min(int(num_bottom_interface_layers) / 2, 2));
	        } else {
	            this->num_top_base_interface_layers    = 0;
	            this->num_bottom_base_interface_layers = 0;
	        }
	    }
        this->first_layer_flow = Slic3r::support_material_1st_layer_flow(&object, float(slicing_params.first_print_layer_height));
        this->support_material_flow = Slic3r::support_material_flow(&object, float(slicing_params.layer_height));
        this->support_material_interface_flow = Slic3r::support_material_interface_flow(&object, float(slicing_params.layer_height));

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
        for (size_t region_id = 0; region_id < object.num_printing_regions(); ++region_id) {
            const PrintRegion& region = object.printing_region(region_id);
            external_perimeter_width = std::max(external_perimeter_width, coordf_t(region.flow(object, frExternalPerimeter, slicing_params.layer_height).width()));
            bridge_flow_ratio += region.config().bridge_flow;
        }
        this->gap_xy = object_config.support_object_xy_distance.value;
        bridge_flow_ratio /= object.num_printing_regions();

        this->support_material_bottom_interface_flow = slicing_params.soluble_interface || !object_config.thick_bridges ?
            this->support_material_interface_flow.with_flow_ratio(bridge_flow_ratio) :
            Flow::bridging_flow(bridge_flow_ratio * this->support_material_interface_flow.nozzle_diameter(), this->support_material_interface_flow.nozzle_diameter());

        this->can_merge_support_regions = object_config.support_filament.value == object_config.support_interface_filament.value;
        if (!this->can_merge_support_regions && (object_config.support_filament.value == 0 || object_config.support_interface_filament.value == 0)) {
            // One of the support extruders is of "don't care" type.
            auto object_extruders = object.object_extruders();
            if (object_extruders.size() == 1 &&
                // object_extruders are 0-based but object_config.support_filament's are 1-based
                object_extruders[0] + 1 == std::max<unsigned int>(object_config.support_filament.value, object_config.support_interface_filament.value))
                // Object is printed with the same extruder as the support.
                this->can_merge_support_regions = true;
        }


        this->base_angle = Geometry::deg2rad(float(object_config.support_threshold_angle.value));
        this->interface_angle = Geometry::deg2rad(float(object_config.support_threshold_angle.value + 90.));
        this->interface_spacing = object_config.support_interface_spacing.value + this->support_material_interface_flow.spacing();
        this->interface_density = std::min(1., this->support_material_interface_flow.spacing() / this->interface_spacing);
        this->support_spacing = object_config.support_base_pattern_spacing.value + this->support_material_flow.spacing();
        this->support_density = std::min(1., this->support_material_flow.spacing() / this->support_spacing);
        if (object_config.support_interface_top_layers.value == 0) {
            // No interface layers allowed, print everything with the base support pattern.
            this->interface_spacing = this->support_spacing;
            this->interface_density = this->support_density;
        }

        SupportMaterialPattern  support_pattern = object_config.support_base_pattern;
        this->with_sheath = is_tree(object_config.support_type) && object_config.tree_support_wall_count > 0;
        this->base_fill_pattern =
            support_pattern == smpHoneycomb ? ipHoneycomb :
            this->support_density > 0.95 || this->with_sheath ? ipRectilinear : ipSupportBase;
        this->interface_fill_pattern = (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);
        if (object_config.support_interface_pattern == smipGrid)
            this->contact_fill_pattern = ipGrid;
        else if (object_config.support_interface_pattern == smipRectilinearInterlaced)
            this->contact_fill_pattern = ipRectilinear;
        else
            this->contact_fill_pattern =
            (object_config.support_interface_pattern == smipAuto && slicing_params.soluble_interface) ||
            object_config.support_interface_pattern == smipConcentric ?
            ipConcentric :
            (this->interface_density > 0.95 ? ipRectilinear : ipSupportBase);
    }
	// Both top / bottom contacts and interfaces are soluble.
    bool                    soluble_interface;
    // Support contact & interface are soluble, but support base is non-soluble.
    bool                    soluble_interface_non_soluble_base;

    // Is there at least a top contact layer extruded above support base?
    bool has_top_contacts;
    // Is there at least a bottom contact layer extruded below support base?
    bool has_bottom_contacts;

    // Number of top interface layers without counting the contact layer.
    size_t num_top_interface_layers;
    // Number of bottom interface layers without counting the contact layer.
    size_t num_bottom_interface_layers;
    // Number of top base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t num_top_base_interface_layers;
    // Number of bottom base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t num_bottom_base_interface_layers;

    bool                    has_contacts() const { return this->has_top_contacts || this->has_bottom_contacts; }
    bool                    has_interfaces() const { return this->num_top_interface_layers + this->num_bottom_interface_layers > 0; }
    bool                    has_base_interfaces() const { return this->num_top_base_interface_layers + this->num_bottom_base_interface_layers > 0; }
    size_t                  num_top_interface_layers_only() const { return this->num_top_interface_layers - this->num_top_base_interface_layers; }
    size_t                  num_bottom_interface_layers_only() const { return this->num_bottom_interface_layers - this->num_bottom_base_interface_layers; }
    Flow 		first_layer_flow;
    Flow 		support_material_flow;
    Flow 		support_material_interface_flow;
    Flow 		support_material_bottom_interface_flow;
    // Is merging of regions allowed? Could the interface & base support regions be printed with the same extruder?
    bool 		can_merge_support_regions;

    coordf_t 	support_layer_height_min;
    //	coordf_t	support_layer_height_max;

    coordf_t	gap_xy;

    float    				base_angle;
    float    				interface_angle;
    coordf_t 				interface_spacing;
    coordf_t				support_expansion;
    coordf_t 				interface_density;
    coordf_t 				support_spacing;
    coordf_t 				support_density;

    InfillPattern           base_fill_pattern;
    InfillPattern           interface_fill_pattern;
    InfillPattern 			contact_fill_pattern;
    bool                    with_sheath;
};
} // namespace Slic3r