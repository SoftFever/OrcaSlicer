#include "clipper/clipper_z.hpp"

#include "Exception.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Flow.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "ShortestPath.hpp"
#include "SupportMaterial.hpp"
#include "GCode.hpp"
#include "GCode/WipeTower.hpp"
#include "Utils.hpp"

//#include "PrintExport.hpp"

#include <float.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

void Print::clear() 
{
	tbb::mutex::scoped_lock lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
	for (PrintObject *object : m_objects)
		delete object;
	m_objects.clear();
    for (PrintRegion *region : m_regions)
        delete region;
    m_regions.clear();
    m_model.clear_objects();
}

PrintRegion* Print::add_region()
{
    m_regions.emplace_back(new PrintRegion(this));
    return m_regions.back();
}

PrintRegion* Print::add_region(const PrintRegionConfig &config)
{
    m_regions.emplace_back(new PrintRegion(this, config));
    return m_regions.back();
}

// Called by Print::apply().
// This method only accepts PrintConfig option keys.
bool Print::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the G-code generator only,
    // or they are only notes not influencing the generated G-code.
    static std::unordered_set<std::string> steps_gcode = {
        "avoid_crossing_perimeters",
        "bed_shape",
        "bed_temperature",
        "before_layer_gcode",
        "between_objects_gcode",
        "bridge_acceleration",
        "bridge_fan_speed",
        "colorprint_heights",
        "cooling",
        "default_acceleration",
        "deretract_speed",
        "disable_fan_first_layers",
        "duplicate_distance",
        "end_gcode",
        "end_filament_gcode",
        "extrusion_axis",
        "extruder_clearance_height",
        "extruder_clearance_radius",
        "extruder_colour",
        "extruder_offset",
        "extrusion_multiplier",
        "fan_always_on",
        "fan_below_layer_time",
        "filament_colour",
        "filament_diameter",
        "filament_density",
        "filament_notes",
        "filament_cost",
        "first_layer_acceleration",
        "first_layer_bed_temperature",
        "first_layer_speed",
        "gcode_comments",
        "gcode_label_objects",
        "infill_acceleration",
        "layer_gcode",
        "min_fan_speed",
        "max_fan_speed",
        "max_print_height",
        "min_print_speed",
        "max_print_speed",
        "max_volumetric_speed",
#ifdef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive",
        "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "notes",
        "only_retract_when_crossing_perimeters",
        "output_filename_format",
        "perimeter_acceleration",
        "post_process",
        "printer_notes",
        "retract_before_travel",
        "retract_before_wipe",
        "retract_layer_change",
        "retract_length",
        "retract_length_toolchange",
        "retract_lift",
        "retract_lift_above",
        "retract_lift_below",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
        "retract_speed",
        "single_extruder_multi_material_priming",
        "slowdown_below_layer_time",
        "standby_temperature_delta",
        "start_gcode",
        "start_filament_gcode",
        "toolchange_gcode",
        "threads",
        "travel_speed",
        "use_firmware_retraction",
        "use_relative_e_distances",
        "use_volumetric_e",
        "variable_layer_height",
        "wipe"
    };

    static std::unordered_set<std::string> steps_ignore;

    std::vector<PrintStep> steps;
    std::vector<PrintObjectStep> osteps;
    bool invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_gcode.find(opt_key) != steps_gcode.end()) {
            // These options only affect G-code export or they are just notes without influence on the generated G-code,
            // so there is nothing to invalidate.
            steps.emplace_back(psGCodeExport);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the G-code whatsoever. Just ignore them.
        } else if (
               opt_key == "skirts"
            || opt_key == "skirt_height"
            || opt_key == "draft_shield"
            || opt_key == "skirt_distance"
            || opt_key == "min_skirt_length"
            || opt_key == "ooze_prevention"
            || opt_key == "wipe_tower_x"
            || opt_key == "wipe_tower_y"
            || opt_key == "wipe_tower_rotation_angle") {
            steps.emplace_back(psSkirt);
        } else if (opt_key == "brim_width") {
            steps.emplace_back(psBrim);
            steps.emplace_back(psSkirt);
        } else if (
               opt_key == "nozzle_diameter"
            || opt_key == "resolution"
            // Spiral Vase forces different kind of slicing than the normal model:
            // In Spiral Vase mode, holes are closed and only the largest area contour is kept at each layer.
            // Therefore toggling the Spiral Vase on / off requires complete reslicing.
            || opt_key == "spiral_vase") {
            osteps.emplace_back(posSlice);
        } else if (
               opt_key == "complete_objects"
            || opt_key == "filament_type"
            || opt_key == "filament_soluble"
            || opt_key == "first_layer_temperature"
            || opt_key == "filament_loading_speed"
            || opt_key == "filament_loading_speed_start"
            || opt_key == "filament_unloading_speed"
            || opt_key == "filament_unloading_speed_start"
            || opt_key == "filament_toolchange_delay"
            || opt_key == "filament_cooling_moves"
            || opt_key == "filament_minimal_purge_on_wipe_tower"
            || opt_key == "filament_cooling_initial_speed"
            || opt_key == "filament_cooling_final_speed"
            || opt_key == "filament_ramming_parameters"
            || opt_key == "filament_max_volumetric_speed"
            || opt_key == "gcode_flavor"
            || opt_key == "high_current_on_filament_swap"
            || opt_key == "infill_first"
            || opt_key == "single_extruder_multi_material"
            || opt_key == "temperature"
            || opt_key == "wipe_tower"
            || opt_key == "wipe_tower_width"
            || opt_key == "wipe_tower_bridging"
            || opt_key == "wipe_tower_no_sparse_layers"
            || opt_key == "wiping_volumes_matrix"
            || opt_key == "parking_pos_retraction"
            || opt_key == "cooling_tube_retraction"
            || opt_key == "cooling_tube_length"
            || opt_key == "extra_loading_move"
            || opt_key == "z_offset") {
            steps.emplace_back(psWipeTower);
            steps.emplace_back(psSkirt);
        } else if (
               opt_key == "first_layer_extrusion_width" 
            || opt_key == "min_layer_height"
            || opt_key == "max_layer_height") {
            osteps.emplace_back(posPerimeters);
            osteps.emplace_back(posInfill);
            osteps.emplace_back(posSupportMaterial);
            steps.emplace_back(psSkirt);
            steps.emplace_back(psBrim);
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            //FIXME invalidate all steps of all objects as well?
            invalidated |= this->invalidate_all_steps();
            // Continue with the other opt_keys to possibly invalidate any object specific steps.
        }
    }

    sort_remove_duplicates(steps);
    for (PrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (PrintObjectStep ostep : osteps)
        for (PrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);
    return invalidated;
}

bool Print::invalidate_step(PrintStep step)
{
	bool invalidated = Inherited::invalidate_step(step);
    // Propagate to dependent steps.
    if (step == psSkirt)
		invalidated |= Inherited::invalidate_step(psBrim);
    if (step != psGCodeExport)
        invalidated |= Inherited::invalidate_step(psGCodeExport);
    return invalidated;
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::is_step_done(PrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    tbb::mutex::scoped_lock lock(this->state_mutex());
    for (const PrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(m_regions.size() * 3);
    std::vector<unsigned char> region_used(m_regions.size(), false);
    for (const PrintObject *object : m_objects)
		for (const std::vector<std::pair<t_layer_height_range, int>> &volumes_per_region : object->region_volumes)
        	if (! volumes_per_region.empty())
        		region_used[&volumes_per_region - &object->region_volumes.front()] = true;
    for (size_t idx_region = 0; idx_region < m_regions.size(); ++ idx_region)
    	if (region_used[idx_region])
        	m_regions[idx_region]->collect_object_printing_extruders(extruders);
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::support_material_extruders() const
{
    std::vector<unsigned int> extruders;
    bool support_uses_current_extruder = false;
    auto num_extruders = (unsigned int)m_config.nozzle_diameter.size();

    for (PrintObject *object : m_objects) {
        if (object->has_support_material()) {
        	assert(object->config().support_material_extruder >= 0);
            if (object->config().support_material_extruder == 0)
                support_uses_current_extruder = true;
            else {
            	unsigned int i = (unsigned int)object->config().support_material_extruder - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        	assert(object->config().support_material_interface_extruder >= 0);
            if (object->config().support_material_interface_extruder == 0)
                support_uses_current_extruder = true;
            else {
            	unsigned int i = (unsigned int)object->config().support_material_interface_extruder - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        }
    }

    if (support_uses_current_extruder)
        // Add all object extruders to the support extruders as it is not know which one will be used to print supports.
        append(extruders, this->object_extruders());
    
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::extruders() const
{
    std::vector<unsigned int> extruders = this->object_extruders();
    append(extruders, this->support_material_extruders());
    sort_remove_duplicates(extruders);
    return extruders;
}

unsigned int Print::num_object_instances() const
{
	unsigned int instances = 0;
    for (const PrintObject *print_object : m_objects)
        instances += (unsigned int)print_object->instances().size();
    return instances;
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max = 0.;
    for (unsigned int extruder_id : this->extruders())
        nozzle_diameter_max = std::max(nozzle_diameter_max, m_config.nozzle_diameter.get_at(extruder_id));
    return nozzle_diameter_max;
}

// Add or remove support modifier ModelVolumes from model_object_dst to match the ModelVolumes of model_object_new
// in the exact order and with the same IDs.
// It is expected, that the model_object_dst already contains the non-support volumes of model_object_new in the correct order.
void Print::model_volume_list_update_supports(ModelObject &model_object_dst, const ModelObject &model_object_new)
{
	typedef std::pair<const ModelVolume*, bool> ModelVolumeWithStatus;
	std::vector<ModelVolumeWithStatus> old_volumes;
    old_volumes.reserve(model_object_dst.volumes.size());
	for (const ModelVolume *model_volume : model_object_dst.volumes)
		old_volumes.emplace_back(ModelVolumeWithStatus(model_volume, false));
	auto model_volume_lower = [](const ModelVolumeWithStatus &mv1, const ModelVolumeWithStatus &mv2){ return mv1.first->id() <  mv2.first->id(); };
	auto model_volume_equal = [](const ModelVolumeWithStatus &mv1, const ModelVolumeWithStatus &mv2){ return mv1.first->id() == mv2.first->id(); };
    std::sort(old_volumes.begin(), old_volumes.end(), model_volume_lower);
    model_object_dst.volumes.clear();
    model_object_dst.volumes.reserve(model_object_new.volumes.size());
    for (const ModelVolume *model_volume_src : model_object_new.volumes) {
		ModelVolumeWithStatus key(model_volume_src, false);
		auto it = std::lower_bound(old_volumes.begin(), old_volumes.end(), key, model_volume_lower);
		if (it != old_volumes.end() && model_volume_equal(*it, key)) {
            // The volume was found in the old list. Just copy it.
            assert(! it->second); // not consumed yet
            it->second = true;
            ModelVolume *model_volume_dst = const_cast<ModelVolume*>(it->first);
			// For support modifiers, the type may have been switched from blocker to enforcer and vice versa.
			assert((model_volume_dst->is_support_modifier() && model_volume_src->is_support_modifier()) || model_volume_dst->type() == model_volume_src->type());
            model_object_dst.volumes.emplace_back(model_volume_dst);
			if (model_volume_dst->is_support_modifier()) {
				// For support modifiers, the type may have been switched from blocker to enforcer and vice versa.
				model_volume_dst->set_type(model_volume_src->type());
				model_volume_dst->set_transformation(model_volume_src->get_transformation());
			}
            assert(model_volume_dst->get_matrix().isApprox(model_volume_src->get_matrix()));
        } else {
            // The volume was not found in the old list. Create a new copy.
            assert(model_volume_src->is_support_modifier());
            model_object_dst.volumes.emplace_back(new ModelVolume(*model_volume_src));
            model_object_dst.volumes.back()->set_model_object(&model_object_dst);
        }
    }
    // Release the non-consumed old volumes (those were deleted from the new list).
	for (ModelVolumeWithStatus &mv_with_status : old_volumes)
        if (! mv_with_status.second)
            delete mv_with_status.first;
}

static inline void model_volume_list_copy_configs(ModelObject &model_object_dst, const ModelObject &model_object_src, const ModelVolumeType type)
{
    size_t i_src, i_dst;
    for (i_src = 0, i_dst = 0; i_src < model_object_src.volumes.size() && i_dst < model_object_dst.volumes.size();) {
        const ModelVolume &mv_src = *model_object_src.volumes[i_src];
        ModelVolume       &mv_dst = *model_object_dst.volumes[i_dst];
        if (mv_src.type() != type) {
            ++ i_src;
            continue;
        }
        if (mv_dst.type() != type) {
            ++ i_dst;
            continue;
        }
        assert(mv_src.id() == mv_dst.id());
        // Copy the ModelVolume data.
        mv_dst.name   = mv_src.name;
		static_cast<DynamicPrintConfig&>(mv_dst.config) = static_cast<const DynamicPrintConfig&>(mv_src.config);
        mv_dst.m_supported_facets = mv_src.m_supported_facets;
        mv_dst.m_seam_facets = mv_src.m_seam_facets;
        //FIXME what to do with the materials?
        // mv_dst.m_material_id = mv_src.m_material_id;
        ++ i_src;
        ++ i_dst;
    }
}

static inline void layer_height_ranges_copy_configs(t_layer_config_ranges &lr_dst, const t_layer_config_ranges &lr_src)
{
    assert(lr_dst.size() == lr_src.size());
    auto it_src = lr_src.cbegin();
    for (auto &kvp_dst : lr_dst) {
        const auto &kvp_src = *it_src ++;
        assert(std::abs(kvp_dst.first.first  - kvp_src.first.first ) <= EPSILON);
        assert(std::abs(kvp_dst.first.second - kvp_src.first.second) <= EPSILON);
        // Layer heights are allowed do differ in case the layer height table is being overriden by the smooth profile.
        // assert(std::abs(kvp_dst.second.option("layer_height")->getFloat() - kvp_src.second.option("layer_height")->getFloat()) <= EPSILON);
        kvp_dst.second = kvp_src.second;
    }
}

static inline bool transform3d_lower(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv) {
        if (*lv < *rv)
            return true;
        else if (*lv > *rv)
            return false;
    }
    return false;
}

static inline bool transform3d_equal(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv)
        if (*lv != *rv)
            return false;
    return true;
}

struct PrintObjectTrafoAndInstances
{
    Transform3d    	trafo;
    PrintInstances	instances;
    bool operator<(const PrintObjectTrafoAndInstances &rhs) const { return transform3d_lower(this->trafo, rhs.trafo); }
};

// Generate a list of trafos and XY offsets for instances of a ModelObject
static std::vector<PrintObjectTrafoAndInstances> print_objects_from_model_object(const ModelObject &model_object)
{
    std::set<PrintObjectTrafoAndInstances> trafos;
    PrintObjectTrafoAndInstances           trafo;
    for (ModelInstance *model_instance : model_object.instances)
        if (model_instance->is_printable()) {
            trafo.trafo = model_instance->get_matrix();
            auto shift = Point::new_scale(trafo.trafo.data()[12], trafo.trafo.data()[13]);
            // Reset the XY axes of the transformation.
            trafo.trafo.data()[12] = 0;
            trafo.trafo.data()[13] = 0;
            // Search or insert a trafo.
            auto it = trafos.emplace(trafo).first;
            const_cast<PrintObjectTrafoAndInstances&>(*it).instances.emplace_back(PrintInstance{ nullptr, model_instance, shift });
        }
    return std::vector<PrintObjectTrafoAndInstances>(trafos.begin(), trafos.end());
}

// Compare just the layer ranges and their layer heights, not the associated configs.
// Ignore the layer heights if check_layer_heights is false.
static bool layer_height_ranges_equal(const t_layer_config_ranges &lr1, const t_layer_config_ranges &lr2, bool check_layer_height)
{
    if (lr1.size() != lr2.size())
        return false;
    auto it2 = lr2.begin();
    for (const auto &kvp1 : lr1) {
        const auto &kvp2 = *it2 ++;
        if (std::abs(kvp1.first.first  - kvp2.first.first ) > EPSILON ||
            std::abs(kvp1.first.second - kvp2.first.second) > EPSILON ||
            (check_layer_height && std::abs(kvp1.second.option("layer_height")->getFloat() - kvp2.second.option("layer_height")->getFloat()) > EPSILON))
            return false;
    }
    return true;
}

// Returns true if va == vb when all CustomGCode items that are not ToolChangeCode are ignored.
static bool custom_per_printz_gcodes_tool_changes_differ(const std::vector<CustomGCode::Item> &va, const std::vector<CustomGCode::Item> &vb)
{
	auto it_a = va.begin();
	auto it_b = vb.begin();
	while (it_a != va.end() || it_b != vb.end()) {
		if (it_a != va.end() && it_a->type != CustomGCode::ToolChange) {
			// Skip any CustomGCode items, which are not tool changes.
			++ it_a;
			continue;
		}
		if (it_b != vb.end() && it_b->type != CustomGCode::ToolChange) {
			// Skip any CustomGCode items, which are not tool changes.
			++ it_b;
			continue;
		}
		if (it_a == va.end() || it_b == vb.end())
			// va or vb contains more Tool Changes than the other.
			return true;
		assert(it_a->type == CustomGCode::ToolChange);
		assert(it_b->type == CustomGCode::ToolChange);
		if (*it_a != *it_b)
			// The two Tool Changes differ.
			return true;
		++ it_a;
		++ it_b;
	}
	// There is no change in custom Tool Changes.
	return false;
}

// Collect diffs of configuration values at various containers,
// resolve the filament rectract overrides of extruder retract values.
void Print::config_diffs(
	const DynamicPrintConfig &new_full_config, 
	t_config_option_keys &print_diff, t_config_option_keys &object_diff, t_config_option_keys &region_diff, 
	t_config_option_keys &full_config_diff, 
	DynamicPrintConfig &filament_overrides) const
{
    // Collect changes to print config, account for overrides of extruder retract values by filament presets.
    {
	    const std::vector<std::string> &extruder_retract_keys = print_config_def.extruder_retract_keys();
	    const std::string               filament_prefix       = "filament_";
	    for (const t_config_option_key &opt_key : m_config.keys()) {
	        const ConfigOption *opt_old = m_config.option(opt_key);
	        assert(opt_old != nullptr);
	        const ConfigOption *opt_new = new_full_config.option(opt_key);
			// assert(opt_new != nullptr);
			if (opt_new == nullptr)
				//FIXME This may happen when executing some test cases.
				continue;
	        const ConfigOption *opt_new_filament = std::binary_search(extruder_retract_keys.begin(), extruder_retract_keys.end(), opt_key) ? new_full_config.option(filament_prefix + opt_key) : nullptr;
	        if (opt_new_filament != nullptr && ! opt_new_filament->is_nil()) {
	        	// An extruder retract override is available at some of the filament presets.
	        	if (*opt_old != *opt_new || opt_new->overriden_by(opt_new_filament)) {
	        		auto opt_copy = opt_new->clone();
	        		opt_copy->apply_override(opt_new_filament);
	        		if (*opt_old == *opt_copy)
	        			delete opt_copy;
	        		else {
	        			filament_overrides.set_key_value(opt_key, opt_copy);
	        			print_diff.emplace_back(opt_key);
	        		}
	        	}
	        } else if (*opt_new != *opt_old)
	            print_diff.emplace_back(opt_key);
	    }
	}
	// Collect changes to object and region configs.
    object_diff = m_default_object_config.diff(new_full_config);
    region_diff = m_default_region_config.diff(new_full_config);
    // Prepare for storing of the full print config into new_full_config to be exported into the G-code and to be used by the PlaceholderParser.
    for (const t_config_option_key &opt_key : new_full_config.keys()) {
        const ConfigOption *opt_old = m_full_print_config.option(opt_key);
        const ConfigOption *opt_new = new_full_config.option(opt_key);
        if (opt_old == nullptr || *opt_new != *opt_old)
            full_config_diff.emplace_back(opt_key);
    }
}

Print::ApplyStatus Print::apply(const Model &model, DynamicPrintConfig new_full_config)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Normalize the config.
	new_full_config.option("print_settings_id",    true);
	new_full_config.option("filament_settings_id", true);
	new_full_config.option("printer_settings_id",  true);
    new_full_config.normalize();

    // Find modified keys of the various configs. Resolve overrides extruder retract values by filament profiles.
	t_config_option_keys print_diff, object_diff, region_diff, full_config_diff;
	DynamicPrintConfig filament_overrides;
	this->config_diffs(new_full_config, print_diff, object_diff, region_diff, full_config_diff, filament_overrides);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && object_diff.empty() && region_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->state_mutex());

    // The following call may stop the background processing.
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff));

    // Apply variables to placeholder parser. The placeholder parser is used by G-code export,
    // which should be stopped if print_diff is not empty.
    size_t num_extruders = m_config.nozzle_diameter.size();
    bool   num_extruders_changed = false;
    if (! full_config_diff.empty()) {
        update_apply_status(this->invalidate_step(psGCodeExport));
        // Set the profile aliases for the PrintBase::output_filename()
		m_placeholder_parser.set("print_preset",    new_full_config.option("print_settings_id")->clone());
		m_placeholder_parser.set("filament_preset", new_full_config.option("filament_settings_id")->clone());
		m_placeholder_parser.set("printer_preset",  new_full_config.option("printer_settings_id")->clone());
		// We want the filament overrides to be applied over their respective extruder parameters by the PlaceholderParser.
		// see "Placeholders do not respect filament overrides." GH issue #3649
		m_placeholder_parser.apply_config(filament_overrides);
	    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
	    m_config.apply_only(new_full_config, print_diff, true);
	    //FIXME use move semantics once ConfigBase supports it.
	    m_config.apply(filament_overrides);
	    // Handle changes to object config defaults
	    m_default_object_config.apply_only(new_full_config, object_diff, true);
	    // Handle changes to regions config defaults
	    m_default_region_config.apply_only(new_full_config, region_diff, true);
        m_full_print_config = std::move(new_full_config);
        if (num_extruders != m_config.nozzle_diameter.size()) {
        	num_extruders = m_config.nozzle_diameter.size();
        	num_extruders_changed = true;
        }
    }
    
    class LayerRanges
    {
    public:
        LayerRanges() {}
        // Convert input config ranges into continuous non-overlapping sorted vector of intervals and their configs.
        void assign(const t_layer_config_ranges &in) {
            m_ranges.clear();
            m_ranges.reserve(in.size());
            // Input ranges are sorted lexicographically. First range trims the other ranges.
            coordf_t last_z = 0;
            for (const std::pair<const t_layer_height_range, DynamicPrintConfig> &range : in)
				if (range.first.second > last_z) {
                    coordf_t min_z = std::max(range.first.first, 0.);
                    if (min_z > last_z + EPSILON) {
                        m_ranges.emplace_back(t_layer_height_range(last_z, min_z), nullptr);
                        last_z = min_z;
                    }
                    if (range.first.second > last_z + EPSILON) {
						const DynamicPrintConfig* cfg = &range.second;
                        m_ranges.emplace_back(t_layer_height_range(last_z, range.first.second), cfg);
                        last_z = range.first.second;
                    }
                }
            if (m_ranges.empty())
                m_ranges.emplace_back(t_layer_height_range(0, DBL_MAX), nullptr);
            else if (m_ranges.back().second == nullptr)
                m_ranges.back().first.second = DBL_MAX;
            else
                m_ranges.emplace_back(t_layer_height_range(m_ranges.back().first.second, DBL_MAX), nullptr);
        }

        const DynamicPrintConfig* config(const t_layer_height_range &range) const {
            auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), std::make_pair< t_layer_height_range, const DynamicPrintConfig*>(t_layer_height_range(range.first - EPSILON, range.second - EPSILON), nullptr));
            // #ys_FIXME_COLOR
            // assert(it != m_ranges.end());
            // assert(it == m_ranges.end() || std::abs(it->first.first  - range.first ) < EPSILON);
            // assert(it == m_ranges.end() || std::abs(it->first.second - range.second) < EPSILON);
            if (it == m_ranges.end() ||
                std::abs(it->first.first - range.first) > EPSILON ||
                std::abs(it->first.second - range.second) > EPSILON )
                return nullptr; // desired range doesn't found
            return (it == m_ranges.end()) ? nullptr : it->second;
        }
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>>::const_iterator begin() const { return m_ranges.cbegin(); }
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>>::const_iterator end() const { return m_ranges.cend(); }
    private:
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>> m_ranges;
    };
    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ObjectID id, Status status = Unknown) : id(id), status(status) {}
		ObjectID     id;
        Status       status;
        LayerRanges  layer_ranges;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id()) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        for (PrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
			update_apply_status(object->invalidate_all_steps());
			delete object;
        }
        m_objects.clear();
        for (PrintRegion *region : m_regions)
            delete region;
        m_regions.clear();
        m_model.assign_copy(model);
		for (const ModelObject *model_object : m_model.objects)
			model_object_status.emplace(model_object->id(), ModelObjectStatus::New);
    } else {
        if (m_model.custom_gcode_per_print_z != model.custom_gcode_per_print_z) {
            update_apply_status(num_extruders_changed || 
            	// Tool change G-codes are applied as color changes for a single extruder printer, no need to invalidate tool ordering.
            	//FIXME The tool ordering may be invalidated unnecessarily if the custom_gcode_per_print_z.mode is not applicable
            	// to the active print / model state, and then it is reset, so it is being applicable, but empty, thus the effect is the same.
            	(num_extruders > 1 && custom_per_printz_gcodes_tool_changes_differ(m_model.custom_gcode_per_print_z.gcodes, model.custom_gcode_per_print_z.gcodes)) ?
            	// The Tool Ordering and the Wipe Tower are no more valid.
            	this->invalidate_steps({ psWipeTower, psGCodeExport }) :
            	// There is no change in Tool Changes stored in custom_gcode_per_print_z, therefore there is no need to update Tool Ordering.
            	this->invalidate_step(psGCodeExport));
            m_model.custom_gcode_per_print_z = model.custom_gcode_per_print_z;
        }
        if (model_object_list_equal(m_model, model)) {
            // The object list did not change.
			for (const ModelObject *model_object : m_model.objects)
				model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
        } else if (model_object_list_extended(m_model, model)) {
            // Add new objects. Their volumes and configs will be synchronized later.
            update_apply_status(this->invalidate_step(psGCodeExport));
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
            for (size_t i = m_model.objects.size(); i < model.objects.size(); ++ i) {
                model_object_status.emplace(model.objects[i]->id(), ModelObjectStatus::New);
                m_model.objects.emplace_back(ModelObject::new_copy(*model.objects[i]));
				m_model.objects.back()->set_model(&m_model);
            }
        } else {
            // Reorder the objects, add new objects.
            // First stop background processing before shuffling or deleting the PrintObjects in the object list.
            this->call_cancel_callback();
            update_apply_status(this->invalidate_step(psGCodeExport));
            // Second create a new list of objects.
            std::vector<ModelObject*> model_objects_old(std::move(m_model.objects));
            m_model.objects.clear();
            m_model.objects.reserve(model.objects.size());
            auto by_id_lower = [](const ModelObject *lhs, const ModelObject *rhs){ return lhs->id() < rhs->id(); };
            std::sort(model_objects_old.begin(), model_objects_old.end(), by_id_lower);
            for (const ModelObject *mobj : model.objects) {
                auto it = std::lower_bound(model_objects_old.begin(), model_objects_old.end(), mobj, by_id_lower);
                if (it == model_objects_old.end() || (*it)->id() != mobj->id()) {
                    // New ModelObject added.
					m_model.objects.emplace_back(ModelObject::new_copy(*mobj));
					m_model.objects.back()->set_model(&m_model);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::New);
                } else {
                    // Existing ModelObject re-added (possibly moved in the list).
                    m_model.objects.emplace_back(*it);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::Moved);
                }
            }
            bool deleted_any = false;
			for (ModelObject *&model_object : model_objects_old) {
                if (model_object_status.find(ModelObjectStatus(model_object->id())) == model_object_status.end()) {
                    model_object_status.emplace(model_object->id(), ModelObjectStatus::Deleted);
                    deleted_any = true;
                } else
                    // Do not delete this ModelObject instance.
                    model_object = nullptr;
            }
            if (deleted_any) {
                // Delete PrintObjects of the deleted ModelObjects.
                std::vector<PrintObject*> print_objects_old = std::move(m_objects);
                m_objects.clear();
                m_objects.reserve(print_objects_old.size());
                for (PrintObject *print_object : print_objects_old) {
                    auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                    assert(it_status != model_object_status.end());
                    if (it_status->status == ModelObjectStatus::Deleted) {
                        update_apply_status(print_object->invalidate_all_steps());
                        delete print_object;
                    } else
                        m_objects.emplace_back(print_object);
                }
                for (ModelObject *model_object : model_objects_old)
                    delete model_object;
            }
        }
    }

    // 2) Map print objects including their transformation matrices.
    struct PrintObjectStatus {
        enum Status {
            Unknown,
            Deleted,
            Reused,
            New
        };
        PrintObjectStatus(PrintObject *print_object, Status status = Unknown) : 
            id(print_object->model_object()->id()),
            print_object(print_object),
            trafo(print_object->trafo()),
            status(status) {}
        PrintObjectStatus(ObjectID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ObjectID          id;
        // Pointer to the old PrintObject
        PrintObject     *print_object;
        // Trafo generated with model_object->world_matrix(true) 
        Transform3d      trafo;
        Status           status;
        // Search by id.
        bool operator<(const PrintObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::multiset<PrintObjectStatus> print_object_status;
    for (PrintObject *print_object : m_objects)
        print_object_status.emplace(PrintObjectStatus(print_object));

    // 3) Synchronize ModelObjects & PrintObjects.
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
		const ModelObject& model_object_new = *model.objects[idx_model_object];
		const_cast<ModelObjectStatus&>(*it_status).layer_ranges.assign(model_object_new.layer_config_ranges);
        if (it_status->status == ModelObjectStatus::New)
            // PrintObject instances will be added in the next loop.
            continue;
        // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
        assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
        // Check whether a model part volume was added or removed, their transformations or order changed.
        // Only volume IDs, volume types, transformation matrices and their order are checked, configuration and other parameters are NOT checked.
        bool model_parts_differ         = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::MODEL_PART);
        bool modifiers_differ           = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::PARAMETER_MODIFIER);
        bool support_blockers_differ    = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_BLOCKER);
        bool support_enforcers_differ   = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_ENFORCER);
        if (model_parts_differ || modifiers_differ || 
            model_object.origin_translation         != model_object_new.origin_translation   ||
            model_object.layer_height_profile       != model_object_new.layer_height_profile ||
            ! layer_height_ranges_equal(model_object.layer_config_ranges, model_object_new.layer_config_ranges, model_object_new.layer_height_profile.empty())) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it) {
                update_apply_status(it->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, do not change the parent.
            model_object.assign_copy(model_object_new);
        } else if (support_blockers_differ || support_enforcers_differ || model_custom_supports_data_changed(model_object, model_object_new)) {
            // First stop background processing before shuffling or deleting the ModelVolumes in the ModelObject's list.
            this->call_cancel_callback();
            update_apply_status(false);
            // Invalidate just the supports step.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it)
                update_apply_status(it->print_object->invalidate_step(posSupportMaterial));
            if (support_enforcers_differ || support_blockers_differ) {
                // Copy just the support volumes.
                model_volume_list_update_supports(model_object, model_object_new);
            }
        }
        if (model_custom_seam_data_changed(model_object, model_object_new)) {
            update_apply_status(this->invalidate_step(psGCodeExport));
        }
        if (! model_parts_differ && ! modifiers_differ) {
            // Synchronize Object's config.
            bool object_config_changed = model_object.config != model_object_new.config;
			if (object_config_changed)
				static_cast<DynamicPrintConfig&>(model_object.config) = static_cast<const DynamicPrintConfig&>(model_object_new.config);
            if (! object_diff.empty() || object_config_changed || num_extruders_changed) {
                PrintObjectConfig new_config = PrintObject::object_config_from_model_object(m_default_object_config, model_object, num_extruders);
                auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
                for (auto it = range.first; it != range.second; ++ it) {
                    t_config_option_keys diff = it->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it->print_object->invalidate_state_by_config_options(diff));
                        it->print_object->config_apply_only(new_config, diff, true);
                    }
                }
            }
            // Synchronize (just copy) the remaining data of ModelVolumes (name, config, custom supports data).
            //FIXME What to do with m_material_id?
			model_volume_list_copy_configs(model_object /* dst */, model_object_new /* src */, ModelVolumeType::MODEL_PART);
			model_volume_list_copy_configs(model_object /* dst */, model_object_new /* src */, ModelVolumeType::PARAMETER_MODIFIER);
            layer_height_ranges_copy_configs(model_object.layer_config_ranges /* dst */, model_object_new.layer_config_ranges /* src */);
            // Copy the ModelObject name, input_file and instances. The instances will be compared against PrintObject instances in the next step.
            model_object.name       = model_object_new.name;
            model_object.input_file = model_object_new.input_file;
            // Only refresh ModelInstances if there is any change.
            if (model_object.instances.size() != model_object_new.instances.size() || 
            	! std::equal(model_object.instances.begin(), model_object.instances.end(), model_object_new.instances.begin(), [](auto l, auto r){ return l->id() == r->id(); })) {
            	// G-code generator accesses model_object.instances to generate sequential print ordering matching the Plater object list.
            	update_apply_status(this->invalidate_step(psGCodeExport));
	            model_object.clear_instances();
	            model_object.instances.reserve(model_object_new.instances.size());
	            for (const ModelInstance *model_instance : model_object_new.instances) {
	                model_object.instances.emplace_back(new ModelInstance(*model_instance));
	                model_object.instances.back()->set_model_object(&model_object);
	            }
	        } else if (! std::equal(model_object.instances.begin(), model_object.instances.end(), model_object_new.instances.begin(), 
	        		[](auto l, auto r){ return l->print_volume_state == r->print_volume_state && l->printable == r->printable && 
	        						           l->get_transformation().get_matrix().isApprox(r->get_transformation().get_matrix()); })) {
	        	// If some of the instances changed, the bounding box of the updated ModelObject is likely no more valid.
	        	// This is safe as the ModelObject's bounding box is only accessed from this function, which is called from the main thread only.
	 			model_object.invalidate_bounding_box();
	        	// Synchronize the content of instances.
	        	auto new_instance = model_object_new.instances.begin();
				for (auto old_instance = model_object.instances.begin(); old_instance != model_object.instances.end(); ++ old_instance, ++ new_instance) {
					(*old_instance)->set_transformation((*new_instance)->get_transformation());
                    (*old_instance)->print_volume_state = (*new_instance)->print_volume_state;
                    (*old_instance)->printable 		    = (*new_instance)->printable;
  				}
	        }
        }
    }

    // 4) Generate PrintObjects from ModelObjects and their instances.
    {
        std::vector<PrintObject*> print_objects_new;
        print_objects_new.reserve(std::max(m_objects.size(), m_model.objects.size()));
        bool new_objects = false;
        // Walk over all new model objects and check, whether there are matching PrintObjects.
        for (ModelObject *model_object : m_model.objects) {
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object->id()));
            std::vector<const PrintObjectStatus*> old;
            if (range.first != range.second) {
                old.reserve(print_object_status.count(PrintObjectStatus(model_object->id())));
                for (auto it = range.first; it != range.second; ++ it)
                    if (it->status != PrintObjectStatus::Deleted)
                        old.emplace_back(&(*it));
            }
            // Generate a list of trafos and XY offsets for instances of a ModelObject
            PrintObjectConfig config = PrintObject::object_config_from_model_object(m_default_object_config, *model_object, num_extruders);
            std::vector<PrintObjectTrafoAndInstances> new_print_instances = print_objects_from_model_object(*model_object);
            if (old.empty()) {
                // Simple case, just generate new instances.
                for (PrintObjectTrafoAndInstances &print_instances : new_print_instances) {
                    PrintObject *print_object = new PrintObject(this, model_object, print_instances.trafo, std::move(print_instances.instances));
                    print_object->config_apply(config);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                }
                continue;
            }
            // Complex case, try to merge the two lists.
            // Sort the old lexicographically by their trafos.
            std::sort(old.begin(), old.end(), [](const PrintObjectStatus *lhs, const PrintObjectStatus *rhs){ return transform3d_lower(lhs->trafo, rhs->trafo); });
            // Merge the old / new lists.
            auto it_old = old.begin();
            for (PrintObjectTrafoAndInstances &new_instances : new_print_instances) {
				for (; it_old != old.end() && transform3d_lower((*it_old)->trafo, new_instances.trafo); ++ it_old);
				if (it_old == old.end() || ! transform3d_equal((*it_old)->trafo, new_instances.trafo)) {
                    // This is a new instance (or a set of instances with the same trafo). Just add it.
                    PrintObject *print_object = new PrintObject(this, model_object, new_instances.trafo, std::move(new_instances.instances));
                    print_object->config_apply(config);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                    if (it_old != old.end())
                        const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Deleted;
                } else {
                    // The PrintObject already exists and the copies differ.
					PrintBase::ApplyStatus status = (*it_old)->print_object->set_instances(std::move(new_instances.instances));
                    if (status != PrintBase::APPLY_STATUS_UNCHANGED)
						update_apply_status(status == PrintBase::APPLY_STATUS_INVALIDATED);
					print_objects_new.emplace_back((*it_old)->print_object);
					const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Reused;
				}
            }
        }
        if (m_objects != print_objects_new) {
            this->call_cancel_callback();
			update_apply_status(this->invalidate_all_steps());
            m_objects = print_objects_new;
            // Delete the PrintObjects marked as Unknown or Deleted.
            bool deleted_objects = false;
            for (auto &pos : print_object_status)
                if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                    update_apply_status(pos.print_object->invalidate_all_steps());
                    delete pos.print_object;
					deleted_objects = true;
                }
			if (new_objects || deleted_objects)
				update_apply_status(this->invalidate_steps({ psSkirt, psBrim, psWipeTower, psGCodeExport }));
			if (new_objects)
	            update_apply_status(false);
        }
        print_object_status.clear();
    }

    // 5) Synchronize configs of ModelVolumes, synchronize AMF / 3MF materials (and their configs), refresh PrintRegions.
    // Update reference counts of regions from the remaining PrintObjects and their volumes.
    // Regions with zero references could and should be reused.
    for (PrintRegion *region : m_regions)
        region->m_refcnt = 0;
    for (PrintObject *print_object : m_objects) {
        int idx_region = 0;
        for (const auto &volumes : print_object->region_volumes) {
            if (! volumes.empty())
				++ m_regions[idx_region]->m_refcnt;
            ++ idx_region;
        }
    }

    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    for (size_t region_id = 0; region_id < m_regions.size(); ++ region_id) {
        PrintRegion       &region = *m_regions[region_id];
        PrintRegionConfig  this_region_config;
        bool               this_region_config_set = false;
        for (PrintObject *print_object : m_objects) {
            const LayerRanges *layer_ranges;
            {
                auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                assert(it_status != model_object_status.end());
                assert(it_status->status != ModelObjectStatus::Deleted);
                layer_ranges = &it_status->layer_ranges;
            }
            if (region_id < print_object->region_volumes.size()) {
                for (const std::pair<t_layer_height_range, int> &volume_and_range : print_object->region_volumes[region_id]) {
                    const ModelVolume        &volume             = *print_object->model_object()->volumes[volume_and_range.second];
                    const DynamicPrintConfig *layer_range_config = layer_ranges->config(volume_and_range.first);
                    if (this_region_config_set) {
                        // If the new config for this volume differs from the other
                        // volume configs currently associated to this region, it means
                        // the region subdivision does not make sense anymore.
                        if (! this_region_config.equals(PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders)))
                            // Regions were split. Reset this print_object.
                            goto print_object_end;
                    } else {
                        this_region_config = PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders);
						for (size_t i = 0; i < region_id; ++ i) {
							const PrintRegion &region_other = *m_regions[i];
							if (region_other.m_refcnt != 0 && region_other.config().equals(this_region_config))
								// Regions were merged. Reset this print_object.
								goto print_object_end;
						}
                        this_region_config_set = true;
                    }
                }
            }
            continue;
        print_object_end:
            update_apply_status(print_object->invalidate_all_steps());
            // Decrease the references to regions from this volume.
            int ireg = 0;
            for (const std::vector<std::pair<t_layer_height_range, int>> &volumes : print_object->region_volumes) {
                if (! volumes.empty())
                    -- m_regions[ireg]->m_refcnt;
                ++ ireg;
            }
            print_object->region_volumes.clear();
        }
        if (this_region_config_set) {
            t_config_option_keys diff = region.config().diff(this_region_config);
            if (! diff.empty()) {
                region.config_apply_only(this_region_config, diff, false);
                for (PrintObject *print_object : m_objects)
                    if (region_id < print_object->region_volumes.size() && ! print_object->region_volumes[region_id].empty())
                        update_apply_status(print_object->invalidate_state_by_config_options(diff));
            }
        }
    }

    // Possibly add new regions for the newly added or resetted PrintObjects.
    for (size_t idx_print_object = 0; idx_print_object < m_objects.size(); ++ idx_print_object) {
        PrintObject        &print_object0 = *m_objects[idx_print_object];
        const ModelObject  &model_object  = *print_object0.model_object();
        const LayerRanges *layer_ranges;
        {
            auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
            assert(it_status != model_object_status.end());
            assert(it_status->status != ModelObjectStatus::Deleted);
            layer_ranges = &it_status->layer_ranges;
        }
        std::vector<int>   regions_in_object;
        regions_in_object.reserve(64);
        for (size_t i = idx_print_object; i < m_objects.size() && m_objects[i]->model_object() == &model_object; ++ i) {
            PrintObject &print_object = *m_objects[i];
			bool         fresh = print_object.region_volumes.empty();
            unsigned int volume_id = 0;
            unsigned int idx_region_in_object = 0;
            for (const ModelVolume *volume : model_object.volumes) {
                if (! volume->is_model_part() && ! volume->is_modifier()) {
					++ volume_id;
					continue;
				}
                // Filter the layer ranges, so they do not overlap and they contain at least a single layer.
                // Now insert a volume with a layer range to its own region.
                for (auto it_range = layer_ranges->begin(); it_range != layer_ranges->end(); ++ it_range) {
                    int region_id = -1;
                    if (&print_object == &print_object0) {
                        // Get the config applied to this volume.
                        PrintRegionConfig config = PrintObject::region_config_from_model_volume(m_default_region_config, it_range->second, *volume, num_extruders);
                        // Find an existing print region with the same config.
    					int idx_empty_slot = -1;
    					for (int i = 0; i < (int)m_regions.size(); ++ i) {
    						if (m_regions[i]->m_refcnt == 0) {
                                if (idx_empty_slot == -1)
                                    idx_empty_slot = i;
                            } else if (config.equals(m_regions[i]->config())) {
                                region_id = i;
                                break;
                            }
    					}
                        // If no region exists with the same config, create a new one.
    					if (region_id == -1) {
    						if (idx_empty_slot == -1) {
    							region_id = (int)m_regions.size();
    							this->add_region(config);
    						} else {
    							region_id = idx_empty_slot;
                                m_regions[region_id]->set_config(std::move(config));
    						}
                        }
                        regions_in_object.emplace_back(region_id);
                    } else
                        region_id = regions_in_object[idx_region_in_object ++];
                    // Assign volume to a region.
    				if (fresh) {
    					if ((size_t)region_id >= print_object.region_volumes.size() || print_object.region_volumes[region_id].empty())
    						++ m_regions[region_id]->m_refcnt;
    					print_object.add_region_volume(region_id, volume_id, it_range->first);
    				}
                }
				++ volume_id;
			}
        }
    }

    // Update SlicingParameters for each object where the SlicingParameters is not valid.
    // If it is not valid, then it is ensured that PrintObject.m_slicing_params is not in use
    // (posSlicing and posSupportMaterial was invalidated).
    for (PrintObject *object : m_objects)
        object->update_slicing_parameters();

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

	return static_cast<ApplyStatus>(apply_status);
}

bool Print::has_infinite_skirt() const
{
    return (m_config.draft_shield && m_config.skirts > 0) || (m_config.ooze_prevention && this->extruders().size() > 1);
}

bool Print::has_skirt() const
{
    return (m_config.skirt_height > 0 && m_config.skirts > 0) || this->has_infinite_skirt();
}

static inline bool sequential_print_horizontal_clearance_valid(const Print &print)
{
	Polygons convex_hulls_other;
	std::map<ObjectID, Polygon> map_model_object_to_convex_hull;
	for (const PrintObject *print_object : print.objects()) {
	    assert(! print_object->model_object()->instances.empty());
	    assert(! print_object->instances().empty());
	    ObjectID model_object_id = print_object->model_object()->id();
	    auto it_convex_hull = map_model_object_to_convex_hull.find(model_object_id);
        // Get convex hull of all printable volumes assigned to this print object.
        ModelInstance *model_instance0 = print_object->model_object()->instances.front();
	    if (it_convex_hull == map_model_object_to_convex_hull.end()) {
	        // Calculate the convex hull of a printable object. 
	        // Grow convex hull with the clearance margin.
	        // FIXME: Arrangement has different parameters for offsetting (jtMiter, limit 2)
	        // which causes that the warning will be showed after arrangement with the
	        // appropriate object distance. Even if I set this to jtMiter the warning still shows up.
	        it_convex_hull = map_model_object_to_convex_hull.emplace_hint(it_convex_hull, model_object_id, 
                offset(print_object->model_object()->convex_hull_2d(
	                        Geometry::assemble_transform(Vec3d::Zero(), model_instance0->get_rotation(), model_instance0->get_scaling_factor(), model_instance0->get_mirror())),
                	// Shrink the extruder_clearance_radius a tiny bit, so that if the object arrangement algorithm placed the objects
	                // exactly by satisfying the extruder_clearance_radius, this test will not trigger collision.
	                float(scale_(0.5 * print.config().extruder_clearance_radius.value - EPSILON)),
	                jtRound, float(scale_(0.1))).front());
	    }
	    // Make a copy, so it may be rotated for instances.
	    Polygon convex_hull0 = it_convex_hull->second;
		double z_diff = Geometry::rotation_diff_z(model_instance0->get_rotation(), print_object->instances().front().model_instance->get_rotation());
		if (std::abs(z_diff) > EPSILON)
			convex_hull0.rotate(z_diff);
	    // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
	    for (const PrintInstance &instance : print_object->instances()) {
	        Polygon convex_hull = convex_hull0;
	        // instance.shift is a position of a centered object, while model object may not be centered.
	        // Conver the shift from the PrintObject's coordinates into ModelObject's coordinates by removing the centering offset.
	        convex_hull.translate(instance.shift - print_object->center_offset());
	        if (! intersection(convex_hulls_other, convex_hull).empty())
	            return false;
	        polygons_append(convex_hulls_other, convex_hull);
	    }
	}
	return true;
}

static inline bool sequential_print_vertical_clearance_valid(const Print &print)
{
	std::vector<const PrintInstance*> print_instances_ordered = sort_object_instances_by_model_order(print);
	// Ignore the last instance printed.
	print_instances_ordered.pop_back();
	// Find the other highest instance.
	auto it = std::max_element(print_instances_ordered.begin(), print_instances_ordered.end(), [](auto l, auto r) {
		return l->print_object->height() < r->print_object->height();
	});
    return it == print_instances_ordered.end() || (*it)->print_object->height() <= scale_(print.config().extruder_clearance_height.value);
}

// Precondition: Print::validate() requires the Print::apply() to be called its invocation.
std::string Print::validate() const
{
    if (m_objects.empty())
        return L("All objects are outside of the print volume.");

    if (extruders().empty())
        return L("The supplied settings will cause an empty print.");

    if (m_config.complete_objects) {
    	if (! sequential_print_horizontal_clearance_valid(*this))
            return L("Some objects are too close; your extruder will collide with them.");
        if (! sequential_print_vertical_clearance_valid(*this))
	        return L("Some objects are too tall and cannot be printed without extruder collisions.");
    }

    if (m_config.spiral_vase) {
        size_t total_copies_count = 0;
        for (const PrintObject *object : m_objects)
            total_copies_count += object->instances().size();
        // #4043
        if (total_copies_count > 1 && ! m_config.complete_objects.value)
            return L("The Spiral Vase option can only be used when printing a single object.");
        assert(m_objects.size() == 1);
        size_t num_regions = 0;
        for (const std::vector<std::pair<t_layer_height_range, int>> &volumes_per_region : m_objects.front()->region_volumes)
        	if (! volumes_per_region.empty())
        		++ num_regions;
        if (num_regions > 1)
            return L("The Spiral Vase option can only be used when printing single material objects.");
    }

    if (this->has_wipe_tower() && ! m_objects.empty()) {
        // Make sure all extruders use same diameter filament and have the same nozzle diameter
        // EPSILON comparison is used for nozzles and 10 % tolerance is used for filaments
        double first_nozzle_diam = m_config.nozzle_diameter.get_at(extruders().front());
        double first_filament_diam = m_config.filament_diameter.get_at(extruders().front());
        for (const auto& extruder_idx : extruders()) {
            double nozzle_diam = m_config.nozzle_diameter.get_at(extruder_idx);
            double filament_diam = m_config.filament_diameter.get_at(extruder_idx);
            if (nozzle_diam - EPSILON > first_nozzle_diam || nozzle_diam + EPSILON < first_nozzle_diam
             || std::abs((filament_diam-first_filament_diam)/first_filament_diam) > 0.1)
                 return L("The wipe tower is only supported if all extruders have the same nozzle diameter "
                          "and use filaments of the same diameter.");
        }

        if (m_config.gcode_flavor != gcfRepRap && m_config.gcode_flavor != gcfRepetier && m_config.gcode_flavor != gcfMarlin)
            return L("The Wipe Tower is currently only supported for the Marlin, RepRap/Sprinter and Repetier G-code flavors.");
        if (! m_config.use_relative_e_distances)
            return L("The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).");
        if (m_config.ooze_prevention)
            return L("Ooze prevention is currently not supported with the wipe tower enabled.");
        if (m_config.use_volumetric_e)
            return L("The Wipe Tower currently does not support volumetric E (use_volumetric_e=0).");
        if (m_config.complete_objects && extruders().size() > 1)
            return L("The Wipe Tower is currently not supported for multimaterial sequential prints.");
        
        if (m_objects.size() > 1) {
            bool                                has_custom_layering = false;
            std::vector<std::vector<coordf_t>>  layer_height_profiles;
            for (const PrintObject *object : m_objects) {
                has_custom_layering = ! object->model_object()->layer_config_ranges.empty() || ! object->model_object()->layer_height_profile.empty();
                if (has_custom_layering) {
                    layer_height_profiles.assign(m_objects.size(), std::vector<coordf_t>());
                    break;
                }
            }
            const SlicingParameters &slicing_params0 = m_objects.front()->slicing_parameters();
            size_t            tallest_object_idx = 0;
            if (has_custom_layering)
                PrintObject::update_layer_height_profile(*m_objects.front()->model_object(), slicing_params0, layer_height_profiles.front());
            for (size_t i = 1; i < m_objects.size(); ++ i) {
                const PrintObject       *object         = m_objects[i];
                const SlicingParameters &slicing_params = object->slicing_parameters();
                if (std::abs(slicing_params.first_print_layer_height - slicing_params0.first_print_layer_height) > EPSILON ||
                    std::abs(slicing_params.layer_height             - slicing_params0.layer_height            ) > EPSILON)
                    return L("The Wipe Tower is only supported for multiple objects if they have equal layer heights");
                if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                    return L("The Wipe Tower is only supported for multiple objects if they are printed over an equal number of raft layers");
                if (object->config().support_material_contact_distance != m_objects.front()->config().support_material_contact_distance)
                    return L("The Wipe Tower is only supported for multiple objects if they are printed with the same support_material_contact_distance");
                if (! equal_layering(slicing_params, slicing_params0))
                    return L("The Wipe Tower is only supported for multiple objects if they are sliced equally.");
                if (has_custom_layering) {
                    PrintObject::update_layer_height_profile(*object->model_object(), slicing_params, layer_height_profiles[i]);
                    if (*(layer_height_profiles[i].end()-2) > *(layer_height_profiles[tallest_object_idx].end()-2))
                        tallest_object_idx = i;
                }
            }

            if (has_custom_layering) {
                const std::vector<coordf_t> &layer_height_profile_tallest = layer_height_profiles[tallest_object_idx];
                for (size_t idx_object = 0; idx_object < m_objects.size(); ++ idx_object) {
                    if (idx_object == tallest_object_idx)
                        continue;
                    const std::vector<coordf_t> &layer_height_profile = layer_height_profiles[idx_object];

                    // The comparison of the profiles is not just about element-wise equality, some layers may not be
                    // explicitely included. Always remember z and height of last reference layer that in the vector
                    // and compare to that. In case some layers are in the vectors multiple times, only the last entry is
                    // taken into account and compared.
                    size_t i = 0; // index into tested profile
                    size_t j = 0; // index into reference profile
                    coordf_t ref_z = -1.;
                    coordf_t next_ref_z = layer_height_profile_tallest[0];
                    coordf_t ref_height = -1.;
                    while (i < layer_height_profile.size()) {
                        coordf_t this_z = layer_height_profile[i];
                        // find the last entry with this z
                        while (i+2 < layer_height_profile.size() && layer_height_profile[i+2] == this_z)
                            i += 2;

                        coordf_t this_height = layer_height_profile[i+1];
                        if (ref_height < -1. || next_ref_z < this_z + EPSILON) {
                            ref_z = next_ref_z;
                            do { // one layer can be in the vector several times
                                ref_height = layer_height_profile_tallest[j+1];
                                if (j+2 >= layer_height_profile_tallest.size())
                                    break;
                                j += 2;
                                next_ref_z = layer_height_profile_tallest[j];
                            } while (ref_z == next_ref_z);
                        }
                        if (std::abs(this_height - ref_height) > EPSILON)
                            return L("The Wipe tower is only supported if all objects have the same variable layer height");
                        i += 2;
                    }
                }
            }
        }
    }
    
	{
		std::vector<unsigned int> extruders = this->extruders();

		// Find the smallest used nozzle diameter and the number of unique nozzle diameters.
		double min_nozzle_diameter = std::numeric_limits<double>::max();
		double max_nozzle_diameter = 0;
		for (unsigned int extruder_id : extruders) {
			double dmr = m_config.nozzle_diameter.get_at(extruder_id);
			min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
			max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
		}

#if 0
        // We currently allow one to assign extruders with a higher index than the number
        // of physical extruders the machine is equipped with, as the Printer::apply() clamps them.
        unsigned int total_extruders_count = m_config.nozzle_diameter.size();
        for (const auto& extruder_idx : extruders)
            if ( extruder_idx >= total_extruders_count )
                return L("One or more object were assigned an extruder that the printer does not have.");
#endif

		auto validate_extrusion_width = [min_nozzle_diameter, max_nozzle_diameter](const ConfigBase &config, const char *opt_key, double layer_height, std::string &err_msg) -> bool {
        	double extrusion_width_min = config.get_abs_value(opt_key, min_nozzle_diameter);
        	double extrusion_width_max = config.get_abs_value(opt_key, max_nozzle_diameter);
        	if (extrusion_width_min == 0) {
        		// Default "auto-generated" extrusion width is always valid.
        	} else if (extrusion_width_min <= layer_height) {
        		err_msg = (boost::format(L("%1%=%2% mm is too low to be printable at a layer height %3% mm")) % opt_key % extrusion_width_min % layer_height).str();
				return false;
			} else if (extrusion_width_max >= max_nozzle_diameter * 3.) {
				err_msg = (boost::format(L("Excessive %1%=%2% mm to be printable with a nozzle diameter %3% mm")) % opt_key % extrusion_width_max % max_nozzle_diameter).str();
				return false;
			}
			return true;
		};
        for (PrintObject *object : m_objects) {
            if (object->config().raft_layers > 0 || object->config().support_material.value) {
				if ((object->config().support_material_extruder == 0 || object->config().support_material_interface_extruder == 0) && max_nozzle_diameter - min_nozzle_diameter > EPSILON) {
                    // The object has some form of support and either support_material_extruder or support_material_interface_extruder
                    // will be printed with the current tool without a forced tool change. Play safe, assert that all object nozzles
                    // are of the same diameter.
                    return L("Printing with multiple extruders of differing nozzle diameters. "
                           "If support is to be printed with the current extruder (support_material_extruder == 0 or support_material_interface_extruder == 0), "
                           "all nozzles have to be of the same diameter.");
                }
                if (this->has_wipe_tower()) {
    				if (object->config().support_material_contact_distance == 0) {
    					// Soluble interface
    					if (object->config().support_material_contact_distance == 0 && ! object->config().support_material_synchronize_layers)
    						return L("For the Wipe Tower to work with the soluble supports, the support layers need to be synchronized with the object layers.");
    				} else {
    					// Non-soluble interface
    					if (object->config().support_material_extruder != 0 || object->config().support_material_interface_extruder != 0)
    						return L("The Wipe Tower currently supports the non-soluble supports only if they are printed with the current extruder without triggering a tool change. "
    							     "(both support_material_extruder and support_material_interface_extruder need to be set to 0).");
    				}
                }
            }
            
            // validate first_layer_height
            double first_layer_height = object->config().get_abs_value("first_layer_height");
            double first_layer_min_nozzle_diameter;
            if (object->config().raft_layers > 0) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder = object->config().raft_layers == 1
                    ? object->config().support_material_interface_extruder-1
                    : object->config().support_material_extruder-1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ? 
                    min_nozzle_diameter : 
                    m_config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (first_layer_height > first_layer_min_nozzle_diameter)
                return L("First layer height can't be greater than nozzle diameter");
            
            // validate layer_height
            double layer_height = object->config().layer_height.value;
            if (layer_height > min_nozzle_diameter)
                return L("Layer height can't be greater than nozzle diameter");

            // Validate extrusion widths.
            std::string err_msg;
            if (! validate_extrusion_width(object->config(), "extrusion_width", layer_height, err_msg))
            	return err_msg;
            if ((object->config().support_material || object->config().raft_layers > 0) && ! validate_extrusion_width(object->config(), "support_material_extrusion_width", layer_height, err_msg))
            	return err_msg;
            for (const char *opt_key : { "perimeter_extrusion_width", "external_perimeter_extrusion_width", "infill_extrusion_width", "solid_infill_extrusion_width", "top_infill_extrusion_width" })
				for (size_t i = 0; i < object->region_volumes.size(); ++ i)
            		if (! object->region_volumes[i].empty() && ! validate_extrusion_width(this->get_region(i)->config(), opt_key, layer_height, err_msg))
		            	return err_msg;
        }
    }

    return std::string();
}

#if 0
// the bounding box of objects placed in copies position
// (without taking skirt/brim/support material into account)
BoundingBox Print::bounding_box() const
{
    BoundingBox bb;
    for (const PrintObject *object : m_objects)
        for (const PrintInstance &instance : object->instances()) {
        	BoundingBox bb2(object->bounding_box());
        	bb.merge(bb2.min + instance.shift);
        	bb.merge(bb2.max + instance.shift);
        }
    return bb;
}

// the total bounding box of extrusions, including skirt/brim/support material
// this methods needs to be called even when no steps were processed, so it should
// only use configuration values
BoundingBox Print::total_bounding_box() const
{
    // get objects bounding box
    BoundingBox bb = this->bounding_box();
    
    // we need to offset the objects bounding box by at least half the perimeters extrusion width
    Flow perimeter_flow = m_objects.front()->get_layer(0)->get_region(0)->flow(frPerimeter);
    double extra = perimeter_flow.width/2;
    
    // consider support material
    if (this->has_support_material()) {
        extra = std::max(extra, SUPPORT_MATERIAL_MARGIN);
    }
    
    // consider brim and skirt
    if (m_config.brim_width.value > 0) {
        Flow brim_flow = this->brim_flow();
        extra = std::max(extra, m_config.brim_width.value + brim_flow.width/2);
    }
    if (this->has_skirt()) {
        int skirts = m_config.skirts.value;
        if (skirts == 0 && this->has_infinite_skirt()) skirts = 1;
        Flow skirt_flow = this->skirt_flow();
        extra = std::max(
            extra,
            m_config.brim_width.value
                + m_config.skirt_distance.value
                + skirts * skirt_flow.spacing()
                + skirt_flow.width/2
        );
    }
    
    if (extra > 0)
        bb.offset(scale_(extra));
    
    return bb;
}
#endif

double Print::skirt_first_layer_height() const
{
    if (m_objects.empty()) 
        throw Slic3r::InvalidArgument("skirt_first_layer_height() can't be called without PrintObjects");
    return m_objects.front()->config().get_abs_value("first_layer_height");
}

Flow Print::brim_flow() const
{
    ConfigOptionFloatOrPercent width = m_config.first_layer_extrusion_width;
    if (width.value == 0) 
        width = m_regions.front()->config().perimeter_extrusion_width;
    if (width.value == 0) 
        width = m_objects.front()->config().extrusion_width;
    
    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
        (float)m_config.nozzle_diameter.get_at(m_regions.front()->config().perimeter_extruder-1),
		(float)this->skirt_first_layer_height(),
        0
    );
}

Flow Print::skirt_flow() const
{
    ConfigOptionFloatOrPercent width = m_config.first_layer_extrusion_width;
    if (width.value == 0) 
        width = m_regions.front()->config().perimeter_extrusion_width;
    if (width.value == 0)
        width = m_objects.front()->config().extrusion_width;
    
    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
		(float)m_config.nozzle_diameter.get_at(m_objects.front()->config().support_material_extruder-1),
		(float)this->skirt_first_layer_height(),
        0
    );
}

bool Print::has_support_material() const
{
    for (const PrintObject *object : m_objects)
        if (object->has_support_material()) 
            return true;
    return false;
}

/*  This method assigns extruders to the volumes having a material
    but not having extruders set in the volume config. */
void Print::auto_assign_extruders(ModelObject* model_object) const
{
    // only assign extruders if object has more than one volume
    if (model_object->volumes.size() < 2)
        return;
    
//    size_t extruders = m_config.nozzle_diameter.values.size();
    for (size_t volume_id = 0; volume_id < model_object->volumes.size(); ++ volume_id) {
        ModelVolume *volume = model_object->volumes[volume_id];
        //FIXME Vojtech: This assigns an extruder ID even to a modifier volume, if it has a material assigned.
        if ((volume->is_model_part() || volume->is_modifier()) && ! volume->material_id().empty() && ! volume->config.has("extruder"))
            volume->config.opt<ConfigOptionInt>("extruder", true)->value = int(volume_id + 1);
    }
}

// Slicing process, running at a background thread.
void Print::process()
{
    BOOST_LOG_TRIVIAL(info) << "Starting the slicing process." << log_memory_info();
    for (PrintObject *obj : m_objects)
        obj->make_perimeters();
    this->set_status(70, L("Infilling layers"));
    for (PrintObject *obj : m_objects)
        obj->infill();
    for (PrintObject *obj : m_objects)
        obj->ironing();
    for (PrintObject *obj : m_objects)
        obj->generate_support_material();
    if (this->set_started(psWipeTower)) {
        m_wipe_tower_data.clear();
        m_tool_ordering.clear();
        if (this->has_wipe_tower()) {
            //this->set_status(95, L("Generating wipe tower"));
            this->_make_wipe_tower();
        } else if (! this->config().complete_objects.value) {
        	// Initialize the tool ordering, so it could be used by the G-code preview slider for planning tool changes and filament switches.
        	m_tool_ordering = ToolOrdering(*this, -1, false);
            if (m_tool_ordering.empty() || m_tool_ordering.last_extruder() == unsigned(-1))
                throw Slic3r::RuntimeError("The print is empty. The model is not printable with current print settings.");
        }
        this->set_done(psWipeTower);
    }
    if (this->set_started(psSkirt)) {
        m_skirt.clear();
        m_skirt_convex_hull.clear();
        m_first_layer_convex_hull.points.clear();
        if (this->has_skirt()) {
            this->set_status(88, L("Generating skirt"));
            this->_make_skirt();
        }
        this->set_done(psSkirt);
    }
	if (this->set_started(psBrim)) {
        m_brim.clear();
        m_first_layer_convex_hull.points.clear();
        if (m_config.brim_width > 0) {
            this->set_status(88, L("Generating brim"));
            this->_make_brim();
        }
        // Brim depends on skirt (brim lines are trimmed by the skirt lines), therefore if
        // the skirt gets invalidated, brim gets invalidated as well and the following line is called.
        this->finalize_first_layer_convex_hull();
        this->set_done(psBrim);
    }
    BOOST_LOG_TRIVIAL(info) << "Slicing process finished." << log_memory_info();
}

// G-code export process, running at a background thread.
// The export_gcode may die for various reasons (fails to process output_filename_format,
// write error into the G-code, cannot execute post-processing scripts).
// It is up to the caller to show an error message.
#if ENABLE_GCODE_VIEWER
std::string Print::export_gcode(const std::string& path_template, GCodeProcessor::Result* result, ThumbnailsGeneratorCallback thumbnail_cb)
#else
std::string Print::export_gcode(const std::string& path_template, GCodePreviewData* preview_data, ThumbnailsGeneratorCallback thumbnail_cb)
#endif // ENABLE_GCODE_VIEWER
{
    // output everything to a G-code file
    // The following call may die if the output_filename_format template substitution fails.
    std::string path = this->output_filepath(path_template);
    std::string message;
#if ENABLE_GCODE_VIEWER
    if (!path.empty() && result == nullptr) {
#else
    if (! path.empty() && preview_data == nullptr) {
#endif // ENABLE_GCODE_VIEWER
        // Only show the path if preview_data is not set -> running from command line.
        message = L("Exporting G-code");
        message += " to ";
        message += path;
    } else
        message = L("Generating G-code");
    this->set_status(90, message);

    // The following line may die for multiple reasons.
    GCode gcode;
#if ENABLE_GCODE_VIEWER
    gcode.do_export(this, path.c_str(), result, thumbnail_cb);
#else
    gcode.do_export(this, path.c_str(), preview_data, thumbnail_cb);
#endif // ENABLE_GCODE_VIEWER
    return path.c_str();
}

void Print::_make_skirt()
{
    // First off we need to decide how tall the skirt must be.
    // The skirt_height option from config is expressed in layers, but our
    // object might have different layer heights, so we need to find the print_z
    // of the highest layer involved.
    // Note that unless has_infinite_skirt() == true
    // the actual skirt might not reach this $skirt_height_z value since the print
    // order of objects on each layer is not guaranteed and will not generally
    // include the thickest object first. It is just guaranteed that a skirt is
    // prepended to the first 'n' layers (with 'n' = skirt_height).
    // $skirt_height_z in this case is the highest possible skirt height for safety.
    coordf_t skirt_height_z = 0.;
    for (const PrintObject *object : m_objects) {
        size_t skirt_layers = this->has_infinite_skirt() ?
            object->layer_count() : 
            std::min(size_t(m_config.skirt_height.value), object->layer_count());
        skirt_height_z = std::max(skirt_height_z, object->m_layers[skirt_layers-1]->print_z);
    }
    
    // Collect points from all layers contained in skirt height.
    Points points;
    for (const PrintObject *object : m_objects) {
        Points object_points;
        // Get object layers up to skirt_height_z.
        for (const Layer *layer : object->m_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->lslices)
                // Collect the outer contour points only, ignore holes for the calculation of the convex hull.
                append(object_points, expoly.contour.points);
        }
        // Get support layers up to skirt_height_z.
        for (const SupportLayer *layer : object->support_layers()) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExtrusionEntity *extrusion_entity : layer->support_fills.entities)
                append(object_points, extrusion_entity->as_polyline().points);
        }
        // Repeat points for each object copy.
        for (const PrintInstance &instance : object->instances()) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt += instance.shift;
            append(points, copy_points);
        }
    }

    // Include the wipe tower.
    append(points, this->first_layer_wipe_tower_corners());

    if (points.size() < 3)
        // At least three points required for a convex hull.
        return;
    
    this->throw_if_canceled();
    Polygon convex_hull = Slic3r::Geometry::convex_hull(points);
    
    // Skirt may be printed on several layers, having distinct layer heights,
    // but loops must be aligned so can't vary width/spacing
    // TODO: use each extruder's own flow
    double first_layer_height = this->skirt_first_layer_height();
    Flow   flow = this->skirt_flow();
    float  spacing = flow.spacing();
    double mm3_per_mm = flow.mm3_per_mm();
    
    std::vector<size_t> extruders;
    std::vector<double> extruders_e_per_mm;
    {
        auto set_extruders = this->extruders();
        extruders.reserve(set_extruders.size());
        extruders_e_per_mm.reserve(set_extruders.size());
        for (auto &extruder_id : set_extruders) {
            extruders.push_back(extruder_id);
            extruders_e_per_mm.push_back(Extruder((unsigned int)extruder_id, &m_config).e_per_mm(mm3_per_mm));
        }
    }

    // Number of skirt loops per skirt layer.
    size_t n_skirts = m_config.skirts.value;
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    // Initial offset of the brim inner edge from the object (possible with a support & raft).
    // The skirt will touch the brim if the brim is extruded.
    auto   distance = float(scale_(m_config.skirt_distance.value) - spacing/2.);
    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (size_t i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        this->throw_if_canceled();
        // Offset the skirt outside.
        distance += float(scale_(spacing));
        // Generate the skirt centerline.
        Polygon loop;
        {
            Polygons loops = offset(convex_hull, distance, ClipperLib::jtRound, float(scale_(0.1)));
            Geometry::simplify_polygons(loops, scale_(0.05), &loops);
			if (loops.empty())
				break;
			loop = loops.front();
        }
        // Extrude the skirt loop.
        ExtrusionLoop eloop(elrSkirt);
        eloop.paths.emplace_back(ExtrusionPath(
            ExtrusionPath(
                erSkirt,
                (float)mm3_per_mm,         // this will be overridden at G-code export time
                flow.width,
				(float)first_layer_height  // this will be overridden at G-code export time
            )));
        eloop.paths.back().polyline = loop.split_at_first_point();
        m_skirt.append(eloop);
        if (m_config.min_skirt_length.value > 0) {
            // The skirt length is limited. Sum the total amount of filament length extruded, in mm.
            extruded_length[extruder_idx] += unscale<double>(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < m_config.min_skirt_length.value) {
                // Not extruded enough yet with the current extruder. Add another loop.
                if (i == 1)
                    ++ i;
            } else {
                assert(extruded_length[extruder_idx] >= m_config.min_skirt_length.value);
                // Enough extruded with the current extruder. Extrude with the next one,
                // until the prescribed number of skirt loops is extruded.
                if (extruder_idx + 1 < extruders.size())
                    ++ extruder_idx;
            }
        } else {
            // The skirt lenght is not limited, extrude the skirt with the 1st extruder only.
        }
    }
    // Brims were generated inside out, reverse to print the outmost contour first.
    m_skirt.reverse();

    // Remember the outer edge of the last skirt line extruded as m_skirt_convex_hull.
    for (Polygon &poly : offset(convex_hull, distance + 0.5f * float(scale_(spacing)), ClipperLib::jtRound, float(scale_(0.1))))
        append(m_skirt_convex_hull, std::move(poly.points));
}

void Print::_make_brim()
{
    // Brim is only printed on first layer and uses perimeter extruder.
    Polygons    islands = this->first_layer_islands();
    Polygons    loops;
    Flow        flow = this->brim_flow();
    size_t      num_loops = size_t(floor(m_config.brim_width.value / flow.spacing()));
    for (size_t i = 0; i < num_loops; ++ i) {
        this->throw_if_canceled();
        islands = offset(islands, float(flow.scaled_spacing()), jtSquare);
        for (Polygon &poly : islands) {
            // poly.simplify(SCALED_RESOLUTION);
            poly.points.push_back(poly.points.front());
            Points p = MultiPoint::_douglas_peucker(poly.points, SCALED_RESOLUTION);
            p.pop_back();
            poly.points = std::move(p);
        }
        if (i + 1 == num_loops) {
            // Remember the outer edge of the last brim line extruded as m_first_layer_convex_hull.
            for (Polygon &poly : islands)
                append(m_first_layer_convex_hull.points, poly.points);
        }
        polygons_append(loops, offset(islands, -0.5f * float(flow.scaled_spacing())));
    }
    loops = union_pt_chained(loops, false);
    // The function above produces ordering well suited for concentric infill (from outside to inside).
    // For Brim, the ordering should be reversed (from inside to outside).
    std::reverse(loops.begin(), loops.end());

    // If there is a possibility that brim intersects skirt, go through loops and split those extrusions
    // The result is either the original Polygon or a list of Polylines
    if (! m_skirt.empty() && m_config.skirt_distance.value < m_config.brim_width)
    {
        // Find the bounding polygons of the skirt
        const Polygons skirt_inners = offset(dynamic_cast<ExtrusionLoop*>(m_skirt.entities.back())->polygon(),
                                              -float(scale_(this->skirt_flow().spacing()))/2.f,
                                              ClipperLib::jtRound,
                                              float(scale_(0.1)));
        const Polygons skirt_outers = offset(dynamic_cast<ExtrusionLoop*>(m_skirt.entities.front())->polygon(),
                                              float(scale_(this->skirt_flow().spacing()))/2.f,
                                              ClipperLib::jtRound,
                                              float(scale_(0.1)));

        // First calculate the trimming region.
		ClipperLib_Z::Paths trimming;
		{
		    ClipperLib_Z::Paths input_subject;
		    ClipperLib_Z::Paths input_clip;
		    for (const Polygon &poly : skirt_outers) {
		    	input_subject.emplace_back();
		    	ClipperLib_Z::Path &out = input_subject.back();
		    	out.reserve(poly.points.size());
			    for (const Point &pt : poly.points)
					out.emplace_back(pt.x(), pt.y(), 0);
		    }
		    for (const Polygon &poly : skirt_inners) {
		    	input_clip.emplace_back();
		    	ClipperLib_Z::Path &out = input_clip.back();
		    	out.reserve(poly.points.size());
			    for (const Point &pt : poly.points)
					out.emplace_back(pt.x(), pt.y(), 0);
		    }
		    // init Clipper
		    ClipperLib_Z::Clipper clipper;	    
		    // add polygons
		    clipper.AddPaths(input_subject, ClipperLib_Z::ptSubject, true);
		    clipper.AddPaths(input_clip,    ClipperLib_Z::ptClip,    true);
		    // perform operation
		    clipper.Execute(ClipperLib_Z::ctDifference, trimming, ClipperLib_Z::pftEvenOdd, ClipperLib_Z::pftEvenOdd);
		}

		// Second, trim the extrusion loops with the trimming regions.
		ClipperLib_Z::Paths loops_trimmed;
		{
			// Produce a closed polyline (repeat the first point at the end).
			ClipperLib_Z::Paths input_clip;
			for (const Polygon &loop : loops) {
				input_clip.emplace_back();
				ClipperLib_Z::Path& out = input_clip.back();
				out.reserve(loop.points.size());
				int64_t loop_idx = &loop - &loops.front();
				for (const Point& pt : loop.points)
					// The Z coordinate carries index of the source loop.
					out.emplace_back(pt.x(), pt.y(), loop_idx + 1);
				out.emplace_back(out.front());
			}
			// init Clipper
			ClipperLib_Z::Clipper clipper;
			clipper.ZFillFunction([](const ClipperLib_Z::IntPoint& e1bot, const ClipperLib_Z::IntPoint& e1top, const ClipperLib_Z::IntPoint& e2bot, const ClipperLib_Z::IntPoint& e2top, ClipperLib_Z::IntPoint& pt) {
				// Assign a valid input loop identifier. Such an identifier is strictly positive, the next line is safe even in case one side of a segment
				// hat the Z coordinate not set to the contour coordinate.
				pt.Z = std::max(std::max(e1bot.Z, e1top.Z), std::max(e2bot.Z, e2top.Z));
			});
			// add polygons
			clipper.AddPaths(input_clip, ClipperLib_Z::ptSubject, false);
			clipper.AddPaths(trimming,   ClipperLib_Z::ptClip,    true);
			// perform operation
			ClipperLib_Z::PolyTree loops_trimmed_tree;
			clipper.Execute(ClipperLib_Z::ctDifference, loops_trimmed_tree, ClipperLib_Z::pftEvenOdd, ClipperLib_Z::pftEvenOdd);
			ClipperLib_Z::PolyTreeToPaths(loops_trimmed_tree, loops_trimmed);
		}

		// Third, produce the extrusions, sorted by the source loop indices.
		{
			std::vector<std::pair<const ClipperLib_Z::Path*, size_t>> loops_trimmed_order;
			loops_trimmed_order.reserve(loops_trimmed.size());
			for (const ClipperLib_Z::Path &path : loops_trimmed) {
				size_t input_idx = 0;
				for (const ClipperLib_Z::IntPoint &pt : path)
					if (pt.Z > 0) {
						input_idx = (size_t)pt.Z;
						break;
					}
				assert(input_idx != 0);
				loops_trimmed_order.emplace_back(&path, input_idx);
			}
			std::stable_sort(loops_trimmed_order.begin(), loops_trimmed_order.end(),
				[](const std::pair<const ClipperLib_Z::Path*, size_t> &l, const std::pair<const ClipperLib_Z::Path*, size_t> &r) {
					return l.second < r.second;
				});

			Point last_pt(0, 0);
			for (size_t i = 0; i < loops_trimmed_order.size();) {
				// Find all pieces that the initial loop was split into.
				size_t j = i + 1;
                for (; j < loops_trimmed_order.size() && loops_trimmed_order[i].second == loops_trimmed_order[j].second; ++ j) ;
                const ClipperLib_Z::Path &first_path = *loops_trimmed_order[i].first;
				if (i + 1 == j && first_path.size() > 3 && first_path.front().X == first_path.back().X && first_path.front().Y == first_path.back().Y) {
					auto *loop = new ExtrusionLoop();
					m_brim.entities.emplace_back(loop);
					loop->paths.emplace_back(erSkirt, float(flow.mm3_per_mm()), float(flow.width), float(this->skirt_first_layer_height()));
		            Points &points = loop->paths.front().polyline.points;
		            points.reserve(first_path.size());
		            for (const ClipperLib_Z::IntPoint &pt : first_path)
		            	points.emplace_back(coord_t(pt.X), coord_t(pt.Y));
		            i = j;
				} else {
			    	//FIXME The path chaining here may not be optimal.
			    	ExtrusionEntityCollection this_loop_trimmed;
					this_loop_trimmed.entities.reserve(j - i);
			    	for (; i < j; ++ i) {
			            this_loop_trimmed.entities.emplace_back(new ExtrusionPath(erSkirt, float(flow.mm3_per_mm()), float(flow.width), float(this->skirt_first_layer_height())));
						const ClipperLib_Z::Path &path = *loops_trimmed_order[i].first;
			            Points &points = static_cast<ExtrusionPath*>(this_loop_trimmed.entities.back())->polyline.points;
			            points.reserve(path.size());
			            for (const ClipperLib_Z::IntPoint &pt : path)
			            	points.emplace_back(coord_t(pt.X), coord_t(pt.Y));
		           	}
		           	chain_and_reorder_extrusion_entities(this_loop_trimmed.entities, &last_pt);
		           	m_brim.entities.reserve(m_brim.entities.size() + this_loop_trimmed.entities.size());
		           	append(m_brim.entities, std::move(this_loop_trimmed.entities));
		           	this_loop_trimmed.entities.clear();
		        }
		        last_pt = m_brim.last_point();
			}
		}
    } else {
    	extrusion_entities_append_loops(m_brim.entities, std::move(loops), erSkirt, float(flow.mm3_per_mm()), float(flow.width), float(this->skirt_first_layer_height()));
    }
}

Polygons Print::first_layer_islands() const
{
    Polygons islands;
    for (PrintObject *object : m_objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers().empty())
            object->support_layers().front()->support_fills.polygons_covered_by_spacing(object_islands, float(SCALED_EPSILON));
        islands.reserve(islands.size() + object_islands.size() * object->instances().size());
        for (const PrintInstance &instance : object->instances())
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(instance.shift);
            }
    }
    return islands;
}

std::vector<Point> Print::first_layer_wipe_tower_corners() const
{
    std::vector<Point> corners;
    if (has_wipe_tower() && ! m_wipe_tower_data.tool_changes.empty()) {
        double width = m_config.wipe_tower_width + 2*m_wipe_tower_data.brim_width;
        double depth = m_wipe_tower_data.depth + 2*m_wipe_tower_data.brim_width;
        Vec2d pt0(-m_wipe_tower_data.brim_width, -m_wipe_tower_data.brim_width);
        for (Vec2d pt : {
                pt0,
                Vec2d(pt0.x()+width, pt0.y()      ),
                Vec2d(pt0.x()+width, pt0.y()+depth),
                Vec2d(pt0.x(),       pt0.y()+depth)
            }) {
            pt = Eigen::Rotation2Dd(Geometry::deg2rad(m_config.wipe_tower_rotation_angle.value)) * pt;
            pt += Vec2d(m_config.wipe_tower_x.value, m_config.wipe_tower_y.value);
            corners.emplace_back(Point(scale_(pt.x()), scale_(pt.y())));
        }
    }
    return corners;
}

void Print::finalize_first_layer_convex_hull()
{
    append(m_first_layer_convex_hull.points, m_skirt_convex_hull);
    if (m_first_layer_convex_hull.empty()) {
        // Neither skirt nor brim was extruded. Collect points of printed objects from 1st layer.
        for (Polygon &poly : this->first_layer_islands())
            append(m_first_layer_convex_hull.points, std::move(poly.points));
    }
    append(m_first_layer_convex_hull.points, this->first_layer_wipe_tower_corners());
    m_first_layer_convex_hull = Geometry::convex_hull(m_first_layer_convex_hull.points);
}

// Wipe tower support.
bool Print::has_wipe_tower() const
{
    return 
        ! m_config.spiral_vase.value &&
        m_config.wipe_tower.value && 
        m_config.nozzle_diameter.values.size() > 1;
}

const WipeTowerData& Print::wipe_tower_data(size_t extruders_cnt, double first_layer_height, double nozzle_diameter) const
{
    // If the wipe tower wasn't created yet, make sure the depth and brim_width members are set to default.
    if (! is_step_done(psWipeTower) && extruders_cnt !=0) {

        float width = float(m_config.wipe_tower_width);
        float brim_spacing = float(nozzle_diameter * 1.25f - first_layer_height * (1. - M_PI_4));

        const_cast<Print*>(this)->m_wipe_tower_data.depth = (900.f/width) * float(extruders_cnt - 1);
        const_cast<Print*>(this)->m_wipe_tower_data.brim_width = 4.5f * brim_spacing;
    }

    return m_wipe_tower_data;
}

void Print::_make_wipe_tower()
{
    m_wipe_tower_data.clear();
    if (! this->has_wipe_tower())
        return;

    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> wiping_matrix(cast<float>(m_config.wiping_volumes_matrix.values));
    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    const unsigned int number_of_extruders = (unsigned int)(sqrt(wiping_matrix.size())+EPSILON);
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        wipe_volumes.push_back(std::vector<float>(wiping_matrix.begin()+i*number_of_extruders, wiping_matrix.begin()+(i+1)*number_of_extruders));

    // Let the ToolOrdering class know there will be initial priming extrusions at the start of the print.
    m_wipe_tower_data.tool_ordering = ToolOrdering(*this, (unsigned int)-1, true);

    if (! m_wipe_tower_data.tool_ordering.has_wipe_tower())
        // Don't generate any wipe tower.
        return;

    // Check whether there are any layers in m_tool_ordering, which are marked with has_wipe_tower,
    // they print neither object, nor support. These layers are above the raft and below the object, and they
    // shall be added to the support layers to be printed.
    // see https://github.com/prusa3d/PrusaSlicer/issues/607
    {
        size_t idx_begin = size_t(-1);
        size_t idx_end   = m_wipe_tower_data.tool_ordering.layer_tools().size();
        // Find the first wipe tower layer, which does not have a counterpart in an object or a support layer.
        for (size_t i = 0; i < idx_end; ++ i) {
            const LayerTools &lt = m_wipe_tower_data.tool_ordering.layer_tools()[i];
            if (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support) {
                idx_begin = i;
                break;
            }
        }
        if (idx_begin != size_t(-1)) {
            // Find the position in m_objects.first()->support_layers to insert these new support layers.
            double wipe_tower_new_layer_print_z_first = m_wipe_tower_data.tool_ordering.layer_tools()[idx_begin].print_z;
            SupportLayerPtrs::const_iterator it_layer = m_objects.front()->support_layers().begin();
            SupportLayerPtrs::const_iterator it_end   = m_objects.front()->support_layers().end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++ it_layer);
            // Find the stopper of the sequence of wipe tower layers, which do not have a counterpart in an object or a support layer.
            for (size_t i = idx_begin; i < idx_end; ++ i) {
                LayerTools &lt = const_cast<LayerTools&>(m_wipe_tower_data.tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                // Insert the new support layer.
                double height    = lt.print_z - (i == 0 ? 0. : m_wipe_tower_data.tool_ordering.layer_tools()[i-1].print_z);
                //FIXME the support layer ID is set to -1, as Vojtech hopes it is not being used anyway.
                it_layer = m_objects.front()->insert_support_layer(it_layer, -1, height, lt.print_z, lt.print_z - 0.5 * height);
                ++ it_layer;
            }
        }
    }
    this->throw_if_canceled();

    // Initialize the wipe tower.
    WipeTower wipe_tower(m_config, wipe_volumes, m_wipe_tower_data.tool_ordering.first_extruder());

    //wipe_tower.set_retract();
    //wipe_tower.set_zhop();

    // Set the extruder & material properties at the wipe tower object.
    for (size_t i = 0; i < number_of_extruders; ++ i)

        wipe_tower.set_extruder(
            i, m_config);

    m_wipe_tower_data.priming = Slic3r::make_unique<std::vector<WipeTower::ToolChangeResult>>(
        wipe_tower.prime((float)this->skirt_first_layer_height(), m_wipe_tower_data.tool_ordering.all_extruders(), false));

    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        unsigned int current_extruder_id = m_wipe_tower_data.tool_ordering.all_extruders().back();
        for (auto &layer_tools : m_wipe_tower_data.tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            bool first_layer = &layer_tools == &m_wipe_tower_data.tool_ordering.front();
            wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id, false);
            for (const auto extruder_id : layer_tools.extruders) {
                if ((first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back()) || extruder_id != current_extruder_id) {
                    float volume_to_wipe = wipe_volumes[current_extruder_id][extruder_id];             // total volume to wipe after this toolchange
                    // Not all of that can be used for infill purging:
                    volume_to_wipe -= (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // try to assign some infills/objects for the wiping:
                    volume_to_wipe = layer_tools.wiping_extrusions().mark_wiping_extrusions(*this, current_extruder_id, extruder_id, volume_to_wipe);

                    // add back the minimal amount toforce on the wipe tower:
                    volume_to_wipe += (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // request a toolchange at the wipe tower with at least volume_to_wipe purging amount
                    wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, extruder_id,
                                               first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back(), volume_to_wipe);
                    current_extruder_id = extruder_id;
                }
            }
            layer_tools.wiping_extrusions().ensure_perimeters_infills_order(*this);
            if (&layer_tools == &m_wipe_tower_data.tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }

    // Generate the wipe tower layers.
    m_wipe_tower_data.tool_changes.reserve(m_wipe_tower_data.tool_ordering.layer_tools().size());
    wipe_tower.generate(m_wipe_tower_data.tool_changes);
    m_wipe_tower_data.depth = wipe_tower.get_depth();
    m_wipe_tower_data.brim_width = wipe_tower.get_brim_width();

    // Unload the current filament over the purge tower.
    coordf_t layer_height = m_objects.front()->config().layer_height.value;
    if (m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions > 0) {
        // The wipe tower goes up to the last layer of the print.
        if (wipe_tower.layer_finished()) {
            // The wipe tower is printed to the top of the print and it has no space left for the final extruder purge.
            // Lift Z to the next layer.
            wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z + layer_height), float(layer_height), 0, false, true);
        } else {
            // There is yet enough space at this layer of the wipe tower for the final purge.
        }
    } else {
        // The wipe tower does not reach the last print layer, perform the pruge at the last print layer.
        assert(m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions == 0);
        wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z), float(layer_height), 0, false, true);
    }
    m_wipe_tower_data.final_purge = Slic3r::make_unique<WipeTower::ToolChangeResult>(
		wipe_tower.tool_change((unsigned int)-1, false));

    m_wipe_tower_data.used_filament = wipe_tower.get_used_filament();
    m_wipe_tower_data.number_of_toolchanges = wipe_tower.get_number_of_toolchanges();
}

// Generate a recommended G-code output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before G-code is finalized).
std::string Print::output_filename(const std::string &filename_base) const 
{ 
    // Set the placeholders for the data know first after the G-code export is finished.
    // These values will be just propagated into the output file name.
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    config.set_key_value("num_extruders", new ConfigOptionInt((int)m_config.nozzle_diameter.size()));
    return this->PrintBase::output_filename(m_config.output_filename_format.value, ".gcode", filename_base, &config);
}

DynamicConfig PrintStatistics::config() const
{
    DynamicConfig config;
    std::string normal_print_time = short_time(this->estimated_normal_print_time);
    std::string silent_print_time = short_time(this->estimated_silent_print_time);
    config.set_key_value("print_time", new ConfigOptionString(normal_print_time));
    config.set_key_value("normal_print_time", new ConfigOptionString(normal_print_time));
    config.set_key_value("silent_print_time", new ConfigOptionString(silent_print_time));
    config.set_key_value("used_filament",             new ConfigOptionFloat(this->total_used_filament / 1000.));
    config.set_key_value("extruded_volume",           new ConfigOptionFloat(this->total_extruded_volume));
    config.set_key_value("total_cost",                new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_toolchanges",         new ConfigOptionInt(this->total_toolchanges));
    config.set_key_value("total_weight",              new ConfigOptionFloat(this->total_weight));
    config.set_key_value("total_wipe_tower_cost",     new ConfigOptionFloat(this->total_wipe_tower_cost));
    config.set_key_value("total_wipe_tower_filament", new ConfigOptionFloat(this->total_wipe_tower_filament));
    return config;
}

DynamicConfig PrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : { 
        "print_time", "normal_print_time", "silent_print_time", 
        "used_filament", "extruded_volume", "total_cost", "total_weight", 
        "total_toolchanges", "total_wipe_tower_cost", "total_wipe_tower_filament"})
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));
    return config;
}

std::string PrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig cfg = this->config();
        PlaceholderParser pp;
        std::string new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path = (path.parent_path() / (new_stem + path.extension().string())).string();
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

} // namespace Slic3r
