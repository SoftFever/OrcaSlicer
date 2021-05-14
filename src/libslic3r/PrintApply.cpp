#include "Model.hpp"
#include "Print.hpp"

namespace Slic3r {

// Add or remove support modifier ModelVolumes from model_object_dst to match the ModelVolumes of model_object_new
// in the exact order and with the same IDs.
// It is expected, that the model_object_dst already contains the non-support volumes of model_object_new in the correct order.
// Friend to ModelVolume to allow copying.
// static is not accepted by gcc if declared as a friend of ModelObject.
/* static */ void model_volume_list_update_supports(ModelObject &model_object_dst, const ModelObject &model_object_new)
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
		mv_dst.config.assign_config(mv_src.config);
        assert(mv_dst.supported_facets.id() == mv_src.supported_facets.id());
        mv_dst.supported_facets.assign(mv_src.supported_facets);
        assert(mv_dst.seam_facets.id() == mv_src.seam_facets.id());
        mv_dst.seam_facets.assign(mv_src.seam_facets);
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

// Collect changes to print config, account for overrides of extruder retract values by filament presets.
static t_config_option_keys print_config_diffs(
    const PrintConfig        &current_config,
    const DynamicPrintConfig &new_full_config,
    DynamicPrintConfig       &filament_overrides)
{
    const std::vector<std::string> &extruder_retract_keys = print_config_def.extruder_retract_keys();
    const std::string               filament_prefix       = "filament_";
    t_config_option_keys            print_diff;
    for (const t_config_option_key &opt_key : current_config.keys()) {
        const ConfigOption *opt_old = current_config.option(opt_key);
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

    return print_diff;
 }

// Prepare for storing of the full print config into new_full_config to be exported into the G-code and to be used by the PlaceholderParser.
static t_config_option_keys full_print_config_diffs(const DynamicPrintConfig &current_full_config, const DynamicPrintConfig &new_full_config)
{
    t_config_option_keys full_config_diff;
    for (const t_config_option_key &opt_key : new_full_config.keys()) {
        const ConfigOption *opt_old = current_full_config.option(opt_key);
        const ConfigOption *opt_new = new_full_config.option(opt_key);
        if (opt_old == nullptr || *opt_new != *opt_old)
            full_config_diff.emplace_back(opt_key);
    }
    return full_config_diff;
}

Print::ApplyStatus Print::apply(const Model &model, DynamicPrintConfig new_full_config)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Normalize the config.
	new_full_config.option("print_settings_id",            true);
	new_full_config.option("filament_settings_id",         true);
	new_full_config.option("printer_settings_id",          true);
    new_full_config.option("physical_printer_settings_id", true);
    new_full_config.normalize_fdm();

    // Find modified keys of the various configs. Resolve overrides extruder retract values by filament profiles.
    DynamicPrintConfig   filament_overrides;
    t_config_option_keys print_diff       = print_config_diffs(m_config, new_full_config, filament_overrides);
    t_config_option_keys full_config_diff = full_print_config_diffs(m_full_print_config, new_full_config);
    // Collect changes to object and region configs.
    t_config_option_keys object_diff      = m_default_object_config.diff(new_full_config);
    t_config_option_keys region_diff      = m_default_region_config.diff(new_full_config);

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
        update_apply_status(this->invalidate_state_by_config_options(new_full_config, print_diff));

    // Apply variables to placeholder parser. The placeholder parser is used by G-code export,
    // which should be stopped if print_diff is not empty.
    size_t num_extruders = m_config.nozzle_diameter.size();
    bool   num_extruders_changed = false;
    if (! full_config_diff.empty()) {
        update_apply_status(this->invalidate_step(psGCodeExport));
        // Set the profile aliases for the PrintBase::output_filename()
		m_placeholder_parser.set("print_preset",              new_full_config.option("print_settings_id")->clone());
		m_placeholder_parser.set("filament_preset",           new_full_config.option("filament_settings_id")->clone());
		m_placeholder_parser.set("printer_preset",            new_full_config.option("printer_settings_id")->clone());
        m_placeholder_parser.set("physical_printer_preset",   new_full_config.option("physical_printer_settings_id")->clone());
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
            for (const std::pair<const t_layer_height_range, ModelConfig> &range : in)
				if (range.first.second > last_z) {
                    coordf_t min_z = std::max(range.first.first, 0.);
                    if (min_z > last_z + EPSILON) {
                        m_ranges.emplace_back(t_layer_height_range(last_z, min_z), nullptr);
                        last_z = min_z;
                    }
                    if (range.first.second > last_z + EPSILON) {
						const DynamicPrintConfig *cfg = &range.second.get();
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
    bool print_regions_reshuffled = false;
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
        print_regions_reshuffled = true;
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
                PrintObjectPtrs print_objects_old = std::move(m_objects);
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
                print_regions_reshuffled = true;
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
        bool supports_differ            = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_BLOCKER) ||
                                          model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_ENFORCER);
        if (model_parts_differ || modifiers_differ || 
            model_object.origin_translation != model_object_new.origin_translation   ||
            ! model_object.layer_height_profile.timestamp_matches(model_object_new.layer_height_profile) ||
            ! layer_height_ranges_equal(model_object.layer_config_ranges, model_object_new.layer_config_ranges, model_object_new.layer_height_profile.empty())) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it) {
                update_apply_status(it->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, do not change the parent.
            model_object.assign_copy(model_object_new);
        } else if (supports_differ || model_custom_supports_data_changed(model_object, model_object_new)) {
            // First stop background processing before shuffling or deleting the ModelVolumes in the ModelObject's list.
            if (supports_differ) {
                this->call_cancel_callback();
                update_apply_status(false);
            }
            // Invalidate just the supports step.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it)
                update_apply_status(it->print_object->invalidate_step(posSupportMaterial));
            if (supports_differ) {
                // Copy just the support volumes.
                model_volume_list_update_supports(model_object, model_object_new);
            }
        } else if (model_custom_seam_data_changed(model_object, model_object_new)) {
            update_apply_status(this->invalidate_step(psGCodeExport));
        }
        if (! model_parts_differ && ! modifiers_differ) {
            // Synchronize Object's config.
            bool object_config_changed = ! model_object.config.timestamp_matches(model_object_new.config);
			if (object_config_changed)
				model_object.config.assign_config(model_object_new.config);
            if (! object_diff.empty() || object_config_changed || num_extruders_changed) {
                PrintObjectConfig new_config = PrintObject::object_config_from_model_object(m_default_object_config, model_object, num_extruders);
                auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
                for (auto it = range.first; it != range.second; ++ it) {
                    t_config_option_keys diff = it->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it->print_object->invalidate_state_by_config_options(it->print_object->config(), new_config, diff));
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
        PrintObjectPtrs print_objects_new;
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
            // Producing the config for PrintObject on demand, caching it at print_object_last.
            const PrintObject *print_object_last = nullptr;
            auto print_object_apply_config = [this, &print_object_last, model_object, num_extruders](PrintObject* print_object) {
                print_object->config_apply(print_object_last ?
                    print_object_last->config() :
                    PrintObject::object_config_from_model_object(m_default_object_config, *model_object, num_extruders));
                print_object_last = print_object;
            };
            std::vector<PrintObjectTrafoAndInstances> new_print_instances = print_objects_from_model_object(*model_object);
            if (old.empty()) {
                // Simple case, just generate new instances.
                for (PrintObjectTrafoAndInstances &print_instances : new_print_instances) {
                    PrintObject *print_object = new PrintObject(this, model_object, print_instances.trafo, std::move(print_instances.instances));
                    print_object_apply_config(print_object);
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
                    print_object_apply_config(print_object);
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
            print_regions_reshuffled = true;
        }
        print_object_status.clear();
    }

    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    for (PrintObject *print_object : m_objects) {
        const LayerRanges *layer_ranges;
        {
            auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
            assert(it_status != model_object_status.end());
            assert(it_status->status != ModelObjectStatus::Deleted);
            layer_ranges = &it_status->layer_ranges;
        }
        bool some_object_region_modified = false;
        bool regions_merged              = false;
        for (size_t region_id = 0; region_id < print_object->m_region_volumes.size(); ++ region_id) {
            PrintRegion       &region = *print_object->m_all_regions[region_id];
            PrintRegionConfig  region_config;
            bool               region_config_set = false;
            for (const PrintRegionVolumes::VolumeWithZRange &volume_w_zrange : print_object->m_region_volumes[region_id].volumes) {
                const ModelVolume        &volume             = *print_object->model_object()->volumes[volume_w_zrange.volume_idx];
                const DynamicPrintConfig *layer_range_config = layer_ranges->config(volume_w_zrange.layer_height_range);
                PrintRegionConfig         this_region_config = PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders);
                if (region_config_set) {
                    if (this_region_config != region_config) {
                        regions_merged = true;
                        break;
                    }
                } else {
                    region_config = std::move(this_region_config);
                    region_config_set = true;
                }
            }
            if (regions_merged)
                break;
            size_t region_config_hash = region_config.hash();
            bool   modified           = region.config_hash() != region_config_hash || region.config() != region_config;
            some_object_region_modified |= modified;
            if (some_object_region_modified)
                // Verify whether this region was not merged with some other region.
    			for (size_t i = 0; i < region_id; ++ i) {
    				const PrintRegion &region_other = *print_object->m_all_regions[i];
    				if (region_other.config_hash() == region_config_hash && region_other.config() == region_config) {
    					// Regions were merged. Reset this print_object.
                        regions_merged = true;
                        break;
                    }
    			}
            if (modified) {
                // Stop the background process before assigning new configuration to the regions.
                t_config_option_keys diff = region.config().diff(region_config);
                update_apply_status(print_object->invalidate_state_by_config_options(region.config(), region_config, diff));
                region.config_apply_only(region_config, diff, false);
            }
        }
        if (regions_merged) {
            // Two regions of a single object were either split or merged. This invalidates the whole slicing.
            update_apply_status(print_object->invalidate_all_steps());
            print_object->m_region_volumes.clear();
        }
    }

    // Possibly add new regions for the newly added or resetted PrintObjects.
    for (size_t idx_print_object = 0; idx_print_object < m_objects.size();) {
        PrintObject        &print_object0 = *m_objects[idx_print_object];
        const ModelObject  &model_object  = *print_object0.model_object();
        const LayerRanges *layer_ranges;
        {
            auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
            assert(it_status != model_object_status.end());
            assert(it_status->status != ModelObjectStatus::Deleted);
            layer_ranges = &it_status->layer_ranges;
        }
        if (print_object0.m_region_volumes.empty()) {
            // Fresh or completely invalidated print_object. Assign regions.
            unsigned int volume_id = 0;
            for (const ModelVolume *volume : model_object.volumes) {
                if (! volume->is_model_part() && ! volume->is_modifier()) {
                    ++ volume_id;
                    continue;
                }
                // Filter the layer ranges, so they do not overlap and they contain at least a single layer.
                // Now insert a volume with a layer range to its own region.
                for (auto it_range = layer_ranges->begin(); it_range != layer_ranges->end(); ++ it_range) {
                    int region_id = -1;
                    // Get the config applied to this volume.
                    PrintRegionConfig config = PrintObject::region_config_from_model_volume(m_default_region_config, it_range->second, *volume, num_extruders);
                    size_t            hash   = config.hash();
                    for (size_t i = 0; i < print_object0.m_all_regions.size(); ++ i)
                        if (hash == print_object0.m_all_regions[i]->config_hash() && config == *print_object0.m_all_regions[i]) {
                            region_id = int(i);
                            break;
                        }
                    // If no region exists with the same config, create a new one.
                    if (region_id == -1) {
                        region_id = int(print_object0.m_all_regions.size());
                        print_object0.m_all_regions.emplace_back(std::make_unique<PrintRegion>(std::move(config), hash));
                    }
                    print_object0.add_region_volume(region_id, volume_id, it_range->first);
                }
                ++ volume_id;
            }
            print_regions_reshuffled = true;
        }
        for (++ idx_print_object; idx_print_object < m_objects.size() && m_objects[idx_print_object]->model_object() == &model_object; ++ idx_print_object) {
            PrintObject &print_object = *m_objects[idx_print_object];
            if (print_object.m_region_volumes.empty()) {
                // Copy region volumes and regions from print_object0.
                print_object.m_region_volumes = print_object0.m_region_volumes;
                print_object.m_all_regions.reserve(print_object0.m_all_regions.size());
                for (const std::unique_ptr<Slic3r::PrintRegion> &region : print_object0.m_all_regions)
                    print_object.m_all_regions.emplace_back(std::make_unique<PrintRegion>(*region));
                print_regions_reshuffled = true;
            }
        }
    }

    if (print_regions_reshuffled) {
        // Update Print::m_print_regions from objects.
        struct cmp { bool operator() (const PrintRegion *l, const PrintRegion *r) const { return l->config_hash() == r->config_hash() && l->config() == r->config(); } };
        std::set<const PrintRegion*, cmp> region_set;
        m_print_regions.clear();
        for (PrintObject *print_object : m_objects)
            for (std::unique_ptr<Slic3r::PrintRegion> &print_region : print_object->m_all_regions)
                if (auto it = region_set.find(print_region.get()); it == region_set.end()) {
                    int print_region_id = int(m_print_regions.size());
                    m_print_regions.emplace_back(print_region.get());
                    print_region->m_print_region_id = print_region_id;
                } else {
                    print_region->m_print_region_id = (*it)->print_region_id();
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

} // namespace Slic3r
