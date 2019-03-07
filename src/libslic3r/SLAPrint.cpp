#include "SLAPrint.hpp"
#include "SLA/SLASupportTree.hpp"
#include "SLA/SLABasePool.hpp"
#include "SLA/SLAAutoSupports.hpp"
#include "ClipperUtils.hpp"
#include "MTUtils.hpp"

#include <unordered_set>
#include <numeric>

#include <tbb/parallel_for.h>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

//#include <tbb/spin_mutex.h>//#include "tbb/mutex.h"

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

using SupportTreePtr = std::unique_ptr<sla::SLASupportTree>;

class SLAPrintObject::SupportData {
public:
    sla::EigenMesh3D emesh;              // index-triangle representation
    std::vector<sla::SupportPoint> support_points;     // all the support points (manual/auto)
    SupportTreePtr   support_tree_ptr;   // the supports
    SlicedSupports   support_slices;     // sliced supports
    std::vector<LevelID>    level_ids;

    inline SupportData(const TriangleMesh& trmesh): emesh(trmesh) {}
};

namespace {

// should add up to 100 (%)
const std::array<unsigned, slaposCount>     OBJ_STEP_LEVELS =
{
    10,     // slaposObjectSlice,
    30,     // slaposSupportPoints,
    25,     // slaposSupportTree,
    25,     // slaposBasePool,
    5,      // slaposSliceSupports,
    5       // slaposIndexSlices
};

const std::array<std::string, slaposCount> OBJ_STEP_LABELS =
{
    L("Slicing model"),                 // slaposObjectSlice,
    L("Generating support points"),      // slaposSupportPoints,
    L("Generating support tree"),       // slaposSupportTree,
    L("Generating pad"),                // slaposBasePool,
    L("Slicing supports"),              // slaposSliceSupports,
    L("Slicing supports")               // slaposIndexSlices,
};

// Should also add up to 100 (%)
const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS =
{
    80,     // slapsRasterize
    20,     // slapsValidate
};

const std::array<std::string, slapsCount> PRINT_STEP_LABELS =
{
    L("Rasterizing layers"),         // slapsRasterize
    L("Validating"),                 // slapsValidate
};

}

void SLAPrint::clear()
{
    tbb::mutex::scoped_lock lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
    for (SLAPrintObject *object : m_objects)
        delete object;
    m_objects.clear();
    m_model.clear_objects();
}

// Transformation without rotation around Z and without a shift by X and Y.
static Transform3d sla_trafo(const ModelObject &model_object)
{
    ModelInstance &model_instance = *model_object.instances.front();
    Vec3d          offset         = model_instance.get_offset();
    Vec3d          rotation       = model_instance.get_rotation();
    offset(0) = 0.;
    offset(1) = 0.;
    rotation(2) = 0.;
    return Geometry::assemble_transform(offset, rotation, model_instance.get_scaling_factor(), model_instance.get_mirror());
}

// List of instances, where the ModelInstance transformation is a composite of sla_trafo and the transformation defined by SLAPrintObject::Instance.
static std::vector<SLAPrintObject::Instance> sla_instances(const ModelObject &model_object)
{
    std::vector<SLAPrintObject::Instance> instances;
    for (ModelInstance *model_instance : model_object.instances)
        if (model_instance->is_printable()) {
            instances.emplace_back(SLAPrintObject::Instance(
                model_instance->id(),
                Point::new_scale(model_instance->get_offset(X), model_instance->get_offset(Y)),
                float(model_instance->get_rotation(Z))));
        }
    return instances;
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model, const DynamicPrintConfig &config_in)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Make a copy of the config, normalize it.
    DynamicPrintConfig config(config_in);
    config.normalize();
    // Collect changes to print config.
    t_config_option_keys print_diff    = m_print_config.diff(config);    
    t_config_option_keys printer_diff  = m_printer_config.diff(config);
    t_config_option_keys material_diff = m_material_config.diff(config);
    t_config_option_keys object_diff   = m_default_object_config.diff(config);
    t_config_option_keys placeholder_parser_diff = this->placeholder_parser().config_diff(config);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status.
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && printer_diff.empty() && material_diff.empty() && object_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
    tbb::mutex::scoped_lock lock(this->state_mutex());

    // The following call may stop the background processing.
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff));
    if (! printer_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(printer_diff));
    if (! material_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(material_diff));

    // Apply variables to placeholder parser. The placeholder parser is currently used
    // only to generate the output file name.
	if (! placeholder_parser_diff.empty()) {
        // update_apply_status(this->invalidate_step(slapsRasterize));
		PlaceholderParser &pp = this->placeholder_parser();
		pp.apply_config(config);
        // Set the profile aliases for the PrintBase::output_filename()
		pp.set("print_preset", config_in.option("sla_print_settings_id")->clone());
		pp.set("material_preset", config_in.option("sla_material_settings_id")->clone());
		pp.set("printer_preset", config_in.option("printer_settings_id")->clone());
    }

    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
    m_print_config.apply_only(config, print_diff, true);
    m_printer_config.apply_only(config, printer_diff, true);
    // Handle changes to material config.
    m_material_config.apply_only(config, material_diff, true);
    // Handle changes to object config defaults
    m_default_object_config.apply_only(config, object_diff, true);

    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ModelID id, Status status = Unknown) : id(id), status(status) {}
        ModelID                 id;
        Status                  status;
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
        for (SLAPrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
            update_apply_status(object->invalidate_all_steps());
            delete object;
        }
        m_objects.clear();
        m_model.assign_copy(model);
        for (const ModelObject *model_object : m_model.objects)
            model_object_status.emplace(model_object->id(), ModelObjectStatus::New);
    } else {
        if (model_object_list_equal(m_model, model)) {
            // The object list did not change.
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
        } else if (model_object_list_extended(m_model, model)) {
            // Add new objects. Their volumes and configs will be synchronized later.
            update_apply_status(this->invalidate_step(slapsRasterize));
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
            update_apply_status(this->invalidate_step(slapsRasterize));
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
                std::vector<SLAPrintObject*> print_objects_old = std::move(m_objects);
                m_objects.clear();
                m_objects.reserve(print_objects_old.size());
                for (SLAPrintObject *print_object : print_objects_old) {
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
        PrintObjectStatus(SLAPrintObject *print_object, Status status = Unknown) :
            id(print_object->model_object()->id()),
            print_object(print_object),
            trafo(print_object->trafo()),
            status(status) {}
        PrintObjectStatus(ModelID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ModelID          id;
        // Pointer to the old PrintObject
        SLAPrintObject  *print_object;
        // Trafo generated with model_object->world_matrix(true)
        Transform3d      trafo;
        Status           status;
        // Search by id.
        bool operator<(const PrintObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::multiset<PrintObjectStatus> print_object_status;
    for (SLAPrintObject *print_object : m_objects)
        print_object_status.emplace(PrintObjectStatus(print_object));

    // 3) Synchronize ModelObjects & PrintObjects.
    std::vector<SLAPrintObject*> print_objects_new;
    print_objects_new.reserve(std::max(m_objects.size(), m_model.objects.size()));
    bool new_objects = false;
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
        if (it_status->status == ModelObjectStatus::New)
            // PrintObject instances will be added in the next loop.
            continue;
        // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
        assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
        const ModelObject &model_object_new       = *model.objects[idx_model_object];
        auto               it_print_object_status = print_object_status.lower_bound(PrintObjectStatus(model_object.id()));
        if (it_print_object_status != print_object_status.end() && it_print_object_status->id != model_object.id())
            it_print_object_status = print_object_status.end();
        // Check whether a model part volume was added or removed, their transformations or order changed.
        bool model_parts_differ = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::MODEL_PART);
        bool sla_trafo_differs  = model_object.instances.empty() != model_object_new.instances.empty() ||
            (! model_object.instances.empty() && ! sla_trafo(model_object).isApprox(sla_trafo(model_object_new)));
        if (model_parts_differ || sla_trafo_differs) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            if (it_print_object_status != print_object_status.end()) {
                update_apply_status(it_print_object_status->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, do not change the parent.
            model_object.assign_copy(model_object_new);
        } else {
            // Synchronize Object's config.
            bool object_config_changed = model_object.config != model_object_new.config;
            if (object_config_changed)
                model_object.config = model_object_new.config;
            if (! object_diff.empty() || object_config_changed) {
                SLAPrintObjectConfig new_config = m_default_object_config;
                normalize_and_apply_config(new_config, model_object.config);
                if (it_print_object_status != print_object_status.end()) {
                    t_config_option_keys diff = it_print_object_status->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it_print_object_status->print_object->invalidate_state_by_config_options(diff));
                        it_print_object_status->print_object->config_apply_only(new_config, diff, true);
                    }
                }
            }
            /*if (model_object.sla_support_points != model_object_new.sla_support_points) {
                model_object.sla_support_points = model_object_new.sla_support_points;
                if (it_print_object_status != print_object_status.end())
                    update_apply_status(it_print_object_status->print_object->invalidate_step(slaposSupportPoints));
            }
            if (model_object.sla_points_status != model_object_new.sla_points_status) {
                // Change of this status should invalidate support points. The points themselves are not enough, there are none
                // in case that nothing was generated OR that points were autogenerated already and not copied to the front-end.
                // These cases can only be differentiated by checking the status change. However, changing from 'Generating' should NOT
                // invalidate - that would keep stopping the background processing without a reason.
                if (model_object.sla_points_status != sla::PointsStatus::Generating)
                    if (it_print_object_status != print_object_status.end())
                        update_apply_status(it_print_object_status->print_object->invalidate_step(slaposSupportPoints));
                model_object.sla_points_status = model_object_new.sla_points_status;
            }*/

            bool old_user_modified = model_object.sla_points_status == sla::PointsStatus::UserModified;
            bool new_user_modified = model_object_new.sla_points_status == sla::PointsStatus::UserModified;
            if ((old_user_modified && ! new_user_modified) || // switching to automatic supports from manual supports
                (! old_user_modified && new_user_modified) || // switching to manual supports from automatic supports
                (new_user_modified && model_object.sla_support_points != model_object_new.sla_support_points)) {
                if (it_print_object_status != print_object_status.end())
                    update_apply_status(it_print_object_status->print_object->invalidate_step(slaposSupportPoints));

                model_object.sla_points_status = model_object_new.sla_points_status;
                model_object.sla_support_points = model_object_new.sla_support_points;
            }

            // Copy the ModelObject name, input_file and instances. The instances will compared against PrintObject instances in the next step.
            model_object.name       = model_object_new.name;
            model_object.input_file = model_object_new.input_file;
            model_object.clear_instances();
            model_object.instances.reserve(model_object_new.instances.size());
            for (const ModelInstance *model_instance : model_object_new.instances) {
                model_object.instances.emplace_back(new ModelInstance(*model_instance));
                model_object.instances.back()->set_model_object(&model_object);
            }
        }

        std::vector<SLAPrintObject::Instance> new_instances = sla_instances(model_object);
        if (it_print_object_status != print_object_status.end() && it_print_object_status->status != PrintObjectStatus::Deleted) {
            // The SLAPrintObject is already there.
			if (new_instances.empty()) {
				const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Deleted;
			} else {
				if (new_instances != it_print_object_status->print_object->instances()) {
					// Instances changed.
					it_print_object_status->print_object->set_instances(new_instances);
					update_apply_status(this->invalidate_step(slapsRasterize));
				}
				print_objects_new.emplace_back(it_print_object_status->print_object);
				const_cast<PrintObjectStatus&>(*it_print_object_status).status = PrintObjectStatus::Reused;
			}
		} else if (! new_instances.empty()) {
            auto print_object = new SLAPrintObject(this, &model_object);

            // FIXME: this invalidates the transformed mesh in SLAPrintObject
            // which is expensive to calculate (especially the raw_mesh() call)
            print_object->set_trafo(sla_trafo(model_object));

            print_object->set_instances(new_instances);
            print_object->config_apply(config, true);
            print_objects_new.emplace_back(print_object);
            new_objects = true;
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
        if (new_objects)
            update_apply_status(false);
    }

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

    return static_cast<ApplyStatus>(apply_status);
}

// After calling the apply() function, set_task() may be called to limit the task to be processed by process().
void SLAPrint::set_task(const TaskParams &params)
{
	// Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->state_mutex());

	int n_object_steps = int(params.to_object_step) + 1;
	if (n_object_steps == 0)
		n_object_steps = (int)slaposCount;

	if (params.single_model_object.valid()) {
        // Find the print object to be processed with priority.
		SLAPrintObject *print_object = nullptr;
		size_t          idx_print_object = 0;
		for (; idx_print_object < m_objects.size(); ++ idx_print_object)
			if (m_objects[idx_print_object]->model_object()->id() == params.single_model_object) {
				print_object = m_objects[idx_print_object];
				break;
			}
		assert(print_object != nullptr);
        // Find out whether the priority print object is being currently processed.
        bool running = false;
		for (int istep = 0; istep < n_object_steps; ++ istep) {
			if (! print_object->m_stepmask[istep])
                // Step was skipped, cancel.
				break;
			if (print_object->is_step_started_unguarded(SLAPrintObjectStep(istep))) {
                // No step was skipped, and a wanted step is being processed. Don't cancel.
				running = true;
				break;
			}
		}
		if (! running)
			this->call_cancel_callback();

		// Now the background process is either stopped, or it is inside one of the print object steps to be calculated anyway.
		if (params.single_model_instance_only) {
			// Suppress all the steps of other instances.
			for (SLAPrintObject *po : m_objects)
				for (int istep = 0; istep < (int)slaposCount; ++ istep)
					po->m_stepmask[istep] = false;
		} else if (! running) {
			// Swap the print objects, so that the selected print_object is first in the row.
			// At this point the background processing must be stopped, so it is safe to shuffle print objects.
			if (idx_print_object != 0)
				std::swap(m_objects.front(), m_objects[idx_print_object]);
		}
        // and set the steps for the current object.
		for (int istep = 0; istep < n_object_steps; ++ istep)
			print_object->m_stepmask[istep] = true;
		for (int istep = n_object_steps; istep < (int)slaposCount; ++ istep)
			print_object->m_stepmask[istep] = false;
	} else {
        // Slicing all objects.
        bool running = false;
        for (SLAPrintObject *print_object : m_objects)
            for (int istep = 0; istep < n_object_steps; ++ istep) {
                if (! print_object->m_stepmask[istep]) {
                    // Step may have been skipped. Restart.
                    goto loop_end;
                }
                if (print_object->is_step_started_unguarded(SLAPrintObjectStep(istep))) {
                    // This step is running, and the state cannot be changed due to the this->state_mutex() being locked.
                    // It is safe to manipulate m_stepmask of other SLAPrintObjects and SLAPrint now.
                    running = true;
                    goto loop_end;
                }
            }
    loop_end:
        if (! running)
            this->call_cancel_callback();
        for (SLAPrintObject *po : m_objects) {
            for (int istep = 0; istep < n_object_steps; ++ istep)
                po->m_stepmask[istep] = true;
            for (int istep = n_object_steps; istep < (int)slaposCount; ++ istep)
                po->m_stepmask[istep] = false;
        }
    }

    if (params.to_object_step != -1 || params.to_print_step != -1) {
        // Limit the print steps.
		size_t istep = (params.to_object_step != -1) ? 0 : size_t(params.to_print_step) + 1;
		for (; istep < m_stepmask.size(); ++ istep)
			m_stepmask[istep] = false;
    }
}

// Clean up after process() finished, either with success, error or if canceled.
// The adjustments on the SLAPrint / SLAPrintObject data due to set_task() are to be reverted here.
void SLAPrint::finalize()
{
    for (SLAPrintObject *po : m_objects)
        for (int istep = 0; istep < (int)slaposCount; ++ istep)
			po->m_stepmask[istep] = true;
    for (int istep = 0; istep < (int)slapsCount; ++ istep)
        m_stepmask[istep] = true;
}

// Generate a recommended output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before the output is finalized).
std::string SLAPrint::output_filename() const
{ 
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    return this->PrintBase::output_filename(m_print_config.output_filename_format.value, "sl1", &config);
}

namespace {
// Compile the argument for support creation from the static print config.
sla::SupportConfig make_support_cfg(const SLAPrintObjectConfig& c) {
    sla::SupportConfig scfg;

    scfg.head_front_radius_mm = 0.5*c.support_head_front_diameter.getFloat();
    scfg.head_back_radius_mm = 0.5*c.support_pillar_diameter.getFloat();
    scfg.head_penetration_mm = c.support_head_penetration.getFloat();
    scfg.head_width_mm = c.support_head_width.getFloat();
    scfg.object_elevation_mm = c.support_object_elevation.getFloat();
    scfg.bridge_slope = c.support_critical_angle.getFloat() * PI / 180.0 ;
    scfg.max_bridge_length_mm = c.support_max_bridge_length.getFloat();
    switch(c.support_pillar_connection_mode.getInt()) {
    case slapcmZigZag:
        scfg.pillar_connection_mode = sla::PillarConnectionMode::zigzag; break;
    case slapcmCross:
        scfg.pillar_connection_mode = sla::PillarConnectionMode::cross; break;
    case slapcmDynamic:
        scfg.pillar_connection_mode = sla::PillarConnectionMode::dynamic; break;
    }
    scfg.ground_facing_only = c.support_buildplate_only.getBool();
    scfg.pillar_widening_factor = c.support_pillar_widening_factor.getFloat();
    scfg.base_radius_mm = 0.5*c.support_base_diameter.getFloat();
    scfg.base_height_mm = c.support_base_height.getFloat();

    return scfg;
}

void swapXY(ExPolygon& expoly) {
    for(auto& p : expoly.contour.points) std::swap(p(X), p(Y));
    for(auto& h : expoly.holes) for(auto& p : h.points) std::swap(p(X), p(Y));
}

}

std::vector<float> SLAPrint::calculate_heights(const BoundingBoxf3& bb3d,
                                               float elevation,
                                               float initial_layer_height,
                                               float layer_height) const
{
    std::vector<float> heights;
    float minZ = float(bb3d.min(Z)) - float(elevation);
    float maxZ = float(bb3d.max(Z));
    auto flh = float(layer_height);
    auto gnd = float(bb3d.min(Z));

    for(float h = minZ + initial_layer_height; h < maxZ; h += flh)
        if(h >= gnd) heights.emplace_back(h);

    return heights;
}

template<class...Args>
void report_status(SLAPrint& p, int st, const std::string& msg, Args&&...args) {
    BOOST_LOG_TRIVIAL(info) << st << "% " << msg;
    p.set_status(st, msg, std::forward<Args>(args)...);
}


void SLAPrint::process()
{
    using namespace sla;
    using ExPolygon = Slic3r::ExPolygon;

    // Assumption: at this point the print objects should be populated only with
    // the model objects we have to process and the instances are also filtered

    // shortcut to initial layer height
    double ilhd = m_material_config.initial_layer_height.getFloat();
    auto   ilh  = float(ilhd);
    const size_t objcount = m_objects.size();

    const unsigned min_objstatus = 0;   // where the per object operations start
    const unsigned max_objstatus = PRINT_STEP_LEVELS[slapsRasterize];  // where the per object operations end

    // the coefficient that multiplies the per object status values which
    // are set up for <0, 100>. They need to be scaled into the whole process
    const double ostepd = (max_objstatus - min_objstatus) / (objcount * 100.0);

    // The slicing will be performed on an imaginary 1D grid which starts from
    // the bottom of the bounding box created around the supported model. So
    // the first layer which is usually thicker will be part of the supports
    // not the model geometry. Exception is when the model is not in the air
    // (elevation is zero) and no pad creation was requested. In this case the
    // model geometry starts on the ground level and the initial layer is part
    // of it. In any case, the model and the supports have to be sliced in the
    // same imaginary grid (the height vector argument to TriangleMeshSlicer).

    // Slicing the model object. This method is oversimplified and needs to
    // be compared with the fff slicing algorithm for verification
    auto slice_model = [this, ilh](SLAPrintObject& po) {
        double lh = po.m_config.layer_height.getFloat();

        TriangleMesh mesh = po.transformed_mesh();
        TriangleMeshSlicer slicer(&mesh);

        // The 1D grid heights
        std::vector<float> heights = calculate_heights(mesh.bounding_box(),
                                                       float(po.get_elevation()),
                                                       ilh, float(lh));

        auto& layers = po.m_model_slices; layers.clear();
		slicer.slice(heights, float(po.config().slice_closing_radius.value), &layers, [this](){ throw_if_canceled(); });
    };

    // In this step we check the slices, identify island and cover them with
    // support points. Then we sprinkle the rest of the mesh.
    auto support_points = [this, ilh](SLAPrintObject& po) {
        const ModelObject& mo = *po.m_model_object;
        po.m_supportdata.reset(
                    new SLAPrintObject::SupportData(po.transformed_mesh()) );

        // If supports are disabled, we can skip the model scan.
        if(!po.m_config.supports_enable.getBool()) return;

        BOOST_LOG_TRIVIAL(debug) << "Support point count "
                                 << mo.sla_support_points.size();

        // Unless the user modified the points or we already did the calculation, we will do
        // the autoplacement. Otherwise we will just blindly copy the frontend data
        // into the backend cache.
        if (mo.sla_points_status != sla::PointsStatus::UserModified) {

            // calculate heights of slices (slices are calculated already)
            double lh = po.m_config.layer_height.getFloat();

            std::vector<float> heights =
                    calculate_heights(po.transformed_mesh().bounding_box(),
                                      float(po.get_elevation()),
                                      ilh, float(lh));

            this->throw_if_canceled();
            SLAAutoSupports::Config config;
            const SLAPrintObjectConfig& cfg = po.config();

            // the density config value is in percents:
            config.density_relative = float(cfg.support_points_density_relative / 100.f);
            config.minimal_distance = float(cfg.support_points_minimal_distance);

            // Construction of this object does the calculation.
            this->throw_if_canceled();
            SLAAutoSupports auto_supports(po.transformed_mesh(),
                                          po.m_supportdata->emesh,
                                          po.get_model_slices(),
                                          heights,
                                          config, 
                                          [this]() { throw_if_canceled(); });

            // Now let's extract the result.
            const std::vector<sla::SupportPoint>& points = auto_supports.output();
            this->throw_if_canceled();
            po.m_supportdata->support_points = points;

            BOOST_LOG_TRIVIAL(debug) << "Automatic support points: "
                                     << po.m_supportdata->support_points.size();

            // Using RELOAD_SLA_SUPPORT_POINTS to tell the Plater to pass the update status to GLGizmoSlaSupports
            report_status(*this, -1, L("Generating support points"), SlicingStatus::RELOAD_SLA_SUPPORT_POINTS);
        }
        else {
            // There are either some points on the front-end, or the user removed them on purpose. No calculation will be done.
            po.m_supportdata->support_points = po.transformed_support_points();
        }
    };

    // In this step we create the supports
    auto support_tree = [this, objcount, ostepd](SLAPrintObject& po) {
        if(!po.m_supportdata) return;

        if(!po.m_config.supports_enable.getBool()) {
            // Generate empty support tree. It can still host a pad
            po.m_supportdata->support_tree_ptr.reset(new SLASupportTree());
            return;
        }

        sla::SupportConfig scfg = make_support_cfg(po.m_config);
        sla::Controller ctl;

        // some magic to scale the status values coming from the support
        // tree creation into the whole print process
        auto stfirst = OBJ_STEP_LEVELS.begin();
        auto stthis = stfirst + slaposSupportTree;
        // we need to add up the status portions until this operation
        int init = std::accumulate(stfirst, stthis, 0);
        init = int(init * ostepd);     // scale the init portion

        // scaling for the sub operations
        double d = *stthis / (objcount * 100.0);

        ctl.statuscb = [this, init, d](unsigned st, const std::string& msg)
        {
            //FIXME this status line scaling does not seem to be correct.
            // How does it account for an increasing object index?
            report_status(*this, int(init + st*d), msg);
        };

        ctl.stopcondition = [this](){ return canceled(); };
        ctl.cancelfn = [this]() { throw_if_canceled(); };

        po.m_supportdata->support_tree_ptr.reset(
                    new SLASupportTree(po.m_supportdata->support_points,
                                       po.m_supportdata->emesh, scfg, ctl));

        throw_if_canceled();

        // Create the unified mesh
        auto rc = SlicingStatus::RELOAD_SCENE;

        // This is to prevent "Done." being displayed during merged_mesh()
        report_status(*this, -1, L("Visualizing supports"));
        po.m_supportdata->support_tree_ptr->merged_mesh();

        BOOST_LOG_TRIVIAL(debug) << "Processed support point count "
                                 << po.m_supportdata->support_points.size();

        // Check the mesh for later troubleshooting.
        if(po.support_mesh().empty())
            BOOST_LOG_TRIVIAL(warning) << "Support mesh is empty";

        report_status(*this, -1, L("Visualizing supports"), rc);

    };

    // This step generates the sla base pad
    auto base_pool = [this](SLAPrintObject& po) {
        // this step can only go after the support tree has been created
        // and before the supports had been sliced. (or the slicing has to be
        // repeated)

        if(!po.m_supportdata || !po.m_supportdata->support_tree_ptr) {
            BOOST_LOG_TRIVIAL(error) << "Uninitialized support data at "
                                     << "pad creation.";
            return;
        }

        if(po.m_config.pad_enable.getBool())
        {
            double wt = po.m_config.pad_wall_thickness.getFloat();
            double h =  po.m_config.pad_wall_height.getFloat();
            double md = po.m_config.pad_max_merge_distance.getFloat();
            // Radius is disabled for now...
            double er = 0; // po.m_config.pad_edge_radius.getFloat();
            double tilt = po.m_config.pad_wall_slope.getFloat()  * PI / 180.0;
            double lh = po.m_config.layer_height.getFloat();
            double elevation = po.m_config.support_object_elevation.getFloat();
            if(!po.m_config.supports_enable.getBool()) elevation = 0;
            sla::PoolConfig pcfg(wt, h, md, er, tilt);

            ExPolygons bp;
            double pad_h = sla::get_pad_fullheight(pcfg);
            auto&& trmesh = po.transformed_mesh();

            // This call can get pretty time consuming
            auto thrfn = [this](){ throw_if_canceled(); };

            if(elevation < pad_h) {
                // we have to count with the model geometry for the base plate
                sla::base_plate(trmesh, bp, float(pad_h), float(lh), thrfn);
            }

            pcfg.throw_on_cancel = thrfn;
            po.m_supportdata->support_tree_ptr->add_pad(bp, pcfg);
        } else {
            po.m_supportdata->support_tree_ptr->remove_pad();
        }

        po.throw_if_canceled();
        auto rc = SlicingStatus::RELOAD_SCENE;
        report_status(*this, -1, L("Visualizing supports"), rc);
    };

    // Slicing the support geometries similarly to the model slicing procedure.
    // If the pad had been added previously (see step "base_pool" than it will
    // be part of the slices)
    auto slice_supports = [ilh](SLAPrintObject& po) {
        auto& sd = po.m_supportdata;
        if(sd && sd->support_tree_ptr) {
            auto lh = float(po.m_config.layer_height.getFloat());
            sd->support_slices = sd->support_tree_ptr->slice(lh, ilh);
        }
    };

    // We have the layer polygon collection but we need to unite them into
    // an index where the key is the height level in discrete levels (clipper)
    auto index_slices = [ilhd](SLAPrintObject& po) {
        po.m_slice_index.clear();
        auto sih = LevelID(scale_(ilhd));

        // Establish the slice grid boundaries
        auto bb = po.transformed_mesh().bounding_box();
        double modelgnd = bb.min(Z);
        double elevation = po.get_elevation();
        double lh = po.m_config.layer_height.getFloat();
        double minZ = modelgnd - elevation;

        // scaled values:
        auto sminZ = LevelID(scale_(minZ));
        auto smaxZ = LevelID(scale_(bb.max(Z)));
        auto smodelgnd = LevelID(scale_(modelgnd));
        auto slh = LevelID(scale_(lh));

        // It is important that the next levels match the levels in
        // model_slice method. Only difference is that here it works with
        // scaled coordinates
        po.m_level_ids.clear();
        for(LevelID h = sminZ + sih; h < smaxZ; h += slh)
            if(h >= smodelgnd) po.m_level_ids.emplace_back(h);

        std::vector<ExPolygons>& oslices = po.m_model_slices;

        // If everything went well this code should not run at all, but
        // let's be robust...
        // assert(levelids.size() == oslices.size());
        if(po.m_level_ids.size() < oslices.size()) { // extend the levels until...

            BOOST_LOG_TRIVIAL(warning)
                    << "Height level mismatch at rasterization!\n";

            LevelID lastlvl = po.m_level_ids.back();
            while(po.m_level_ids.size() < oslices.size()) {
                lastlvl += slh;
                po.m_level_ids.emplace_back(lastlvl);
            }
        }

        for(size_t i = 0; i < oslices.size(); ++i) {
            LevelID h = po.m_level_ids[i];

            float fh = float(double(h) * SCALING_FACTOR);

            // now for the public slice index:
            SLAPrintObject::SliceRecord& sr = po.m_slice_index[fh];
            // There should be only one slice layer for each print object
            assert(sr.model_slices_idx == SLAPrintObject::SliceRecord::NONE);
            sr.model_slices_idx = i;
        }

        if(po.m_supportdata) { // deal with the support slices if present
            std::vector<ExPolygons>& sslices = po.m_supportdata->support_slices;
            po.m_supportdata->level_ids.clear();
            po.m_supportdata->level_ids.reserve(sslices.size());

            for(int i = 0; i < int(sslices.size()); ++i) {
                LevelID h = sminZ + sih + i * slh;
                po.m_supportdata->level_ids.emplace_back(h);

                float fh = float(double(h) * SCALING_FACTOR);

                SLAPrintObject::SliceRecord& sr = po.m_slice_index[fh];
                assert(sr.support_slices_idx == SLAPrintObject::SliceRecord::NONE);
                sr.support_slices_idx = SLAPrintObject::SliceRecord::Idx(i);
            }
        }
    };

    // Rasterizing the model objects, and their supports
    auto rasterize = [this, max_objstatus]() {
        if(canceled()) return;

        // clear the rasterizer input
        m_printer_input.clear();

        for(SLAPrintObject * o : m_objects) {
            auto& po = *o;
            std::vector<ExPolygons>& oslices = po.m_model_slices;

            // We need to adjust the min Z level of the slices to be zero
            LevelID smfirst =
                    po.m_supportdata && !po.m_supportdata->level_ids.empty() ?
                        po.m_supportdata->level_ids.front() : 0;
            LevelID mfirst = po.m_level_ids.empty()? 0 : po.m_level_ids.front();
            LevelID gndlvl = -(std::min(smfirst, mfirst));

            // now merge this object's support and object slices with the rest
            // of the print object slices

            for(size_t i = 0; i < oslices.size(); ++i) {
                auto& lyrs = m_printer_input[gndlvl + po.m_level_ids[i]];
                lyrs.emplace_back(oslices[i], po.m_instances);
            }

            if(!po.m_supportdata) continue;
            std::vector<ExPolygons>& sslices = po.m_supportdata->support_slices;
            for(size_t i = 0; i < sslices.size(); ++i) {
                LayerRefs& lyrs =
                       m_printer_input[gndlvl + po.m_supportdata->level_ids[i]];
                lyrs.emplace_back(sslices[i], po.m_instances);
            }
        }

        // collect all the keys
        std::vector<long long> keys; keys.reserve(m_printer_input.size());
        for(auto& e : m_printer_input) keys.emplace_back(e.first);

        // If the raster has vertical orientation, we will flip the coordinates
        bool flpXY = m_printer_config.display_orientation.getInt() ==
                SLADisplayOrientation::sladoPortrait;

        { // create a raster printer for the current print parameters
            // I don't know any better
            auto& ocfg = m_objects.front()->m_config;
            auto& matcfg = m_material_config;
            auto& printcfg = m_printer_config;

            double w = printcfg.display_width.getFloat();
            double h = printcfg.display_height.getFloat();
            auto pw = unsigned(printcfg.display_pixels_x.getInt());
            auto ph = unsigned(printcfg.display_pixels_y.getInt());
            double lh = ocfg.layer_height.getFloat();
            double exp_t = matcfg.exposure_time.getFloat();
            double iexp_t = matcfg.initial_exposure_time.getFloat();

            if(flpXY) { std::swap(w, h); std::swap(pw, ph); }

            m_printer.reset(new SLAPrinter(w, h, pw, ph, lh, exp_t, iexp_t,
                                           flpXY? SLAPrinter::RO_PORTRAIT :
                                                  SLAPrinter::RO_LANDSCAPE));
        }

        // Allocate space for all the layers
        SLAPrinter& printer = *m_printer;
        auto lvlcnt = unsigned(m_printer_input.size());
        printer.layers(lvlcnt);

        // slot is the portion of 100% that is realted to rasterization
        unsigned slot = PRINT_STEP_LEVELS[slapsRasterize];
        // ist: initial state; pst: previous state
        unsigned ist = max_objstatus, pst = ist;
        // coefficient to map the rasterization state (0-99) to the allocated
        // portion (slot) of the process state
        double sd = (100 - ist) / 100.0;
        SpinMutex slck;

        // procedure to process one height level. This will run in parallel
        auto lvlfn =
        [this, &slck, &keys, &printer, slot, sd, ist, &pst, flpXY]
            (unsigned level_id)
        {
            if(canceled()) return;

            LayerRefs& lrange = m_printer_input[keys[level_id]];

            // Switch to the appropriate layer in the printer
            printer.begin_layer(level_id);

            for(auto& lyrref : lrange) { // for all layers in the current level
                if(canceled()) break;
                const Layer& sl = lyrref.lref;   // get the layer reference
                const LayerCopies& copies = lyrref.copies;

                // Draw all the polygons in the slice to the actual layer.
                for(auto& cp : copies) {
                    for(ExPolygon slice : sl) {
                        // The order is important here:
                        // apply rotation before translation...
                        slice.rotate(double(cp.rotation));
                        slice.translate(cp.shift(X), cp.shift(Y));
                        if(flpXY) swapXY(slice);
                        printer.draw_polygon(slice, level_id);
                    }
                }
            }

            // Finish the layer for later saving it.
            printer.finish_layer(level_id);

            // Status indication guarded with the spinlock
            auto st = ist + unsigned(sd*level_id*slot/m_printer_input.size());
            { std::lock_guard<SpinMutex> lck(slck);
            if( st > pst) {
                report_status(*this, int(st), PRINT_STEP_LABELS[slapsRasterize]);
                pst = st;
            }
            }
        };

        // last minute escape
        if(canceled()) return;

        // Sequential version (for testing)
        // for(unsigned l = 0; l < lvlcnt; ++l) process_level(l);

        // Print all the layers in parallel
        tbb::parallel_for<unsigned, decltype(lvlfn)>(0, lvlcnt, lvlfn);

        // Fill statistics
        this->fill_statistics();
        // Set statistics values to the printer
        m_printer->set_statistics({(m_print_statistics.objects_used_material + m_print_statistics.support_used_material)/1000,
                                double(m_default_object_config.faded_layers.getInt()),
                                double(m_print_statistics.slow_layers_count),
                                double(m_print_statistics.fast_layers_count)
                                });
    };

    using slaposFn = std::function<void(SLAPrintObject&)>;
    using slapsFn  = std::function<void(void)>;

    std::array<slaposFn, slaposCount> pobj_program =
    {
        slice_model,
        support_points,
        support_tree,
        base_pool,
        slice_supports,
        index_slices
    };

    std::array<slapsFn, slapsCount> print_program =
    {
        rasterize,
        [](){}  // validate
    };

    unsigned st = min_objstatus;
    unsigned incr = 0;

    BOOST_LOG_TRIVIAL(info) << "Start slicing process.";

    // TODO: this loop could run in parallel but should not exhaust all the CPU
    // power available
    // Calculate the support structures first before slicing the supports, so that the preview will get displayed ASAP for all objects.
    std::vector<SLAPrintObjectStep> step_ranges = { slaposObjectSlice, slaposSliceSupports, slaposCount };
    for (size_t idx_range = 0; idx_range + 1 < step_ranges.size(); ++ idx_range) {
        for(SLAPrintObject * po : m_objects) {

            BOOST_LOG_TRIVIAL(info) << "Slicing object " << po->model_object()->name;

            for (int s = (int)step_ranges[idx_range]; s < (int)step_ranges[idx_range + 1]; ++s) {
                auto currentstep = (SLAPrintObjectStep)s;

                // Cancellation checking. Each step will check for cancellation
                // on its own and return earlier gracefully. Just after it returns
                // execution gets to this point and throws the canceled signal.
                throw_if_canceled();

                st += unsigned(incr * ostepd);

                if(po->m_stepmask[currentstep] && po->set_started(currentstep)) {
                    report_status(*this, int(st), OBJ_STEP_LABELS[currentstep]);
                    pobj_program[currentstep](*po);
                    throw_if_canceled();
                    po->set_done(currentstep);
                }

                incr = OBJ_STEP_LEVELS[currentstep];
            }
        }
    }

    std::array<SLAPrintStep, slapsCount> printsteps = {
        slapsRasterize, slapsValidate
    };

    // this would disable the rasterization step
    // m_stepmask[slapsRasterize] = false;

    double pstd = (100 - max_objstatus) / 100.0;
    st = max_objstatus;
    for(size_t s = 0; s < print_program.size(); ++s) {
        auto currentstep = printsteps[s];

        throw_if_canceled();

        if(m_stepmask[currentstep] && set_started(currentstep))
        {
            report_status(*this, int(st), PRINT_STEP_LABELS[currentstep]);
            print_program[currentstep]();
            throw_if_canceled();
            set_done(currentstep);
        }

        st += unsigned(PRINT_STEP_LEVELS[currentstep] * pstd);
    }

    // If everything vent well
    report_status(*this, 100, L("Slicing done"));
}

bool SLAPrint::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the final rasterization only,
    // or they are only notes not influencing the rasterization step.
    static std::unordered_set<std::string> steps_rasterize = {
        "exposure_time",
        "initial_exposure_time",
        "material_correction_printing",
        "material_correction_curing",
        "display_width",
        "display_height",
        "display_pixels_x",
        "display_pixels_y",
        "display_orientation",
        "printer_correction"
    };

    static std::unordered_set<std::string> steps_ignore = {
        "bed_shape",
        "max_print_height",
        "printer_technology",
        "output_filename_format",
        "fast_tilt_time", 
        "slow_tilt_time", 
        "area_fill"
    };

    std::vector<SLAPrintStep> steps;
    std::vector<SLAPrintObjectStep> osteps;
    bool invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_rasterize.find(opt_key) != steps_rasterize.end()) {
            // These options only affect the final rasterization, or they are just notes without influence on the output,
            // so there is nothing to invalidate.
            steps.emplace_back(slapsRasterize);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the output. Just ignore them.
        } else if (opt_key == "initial_layer_height") {
            steps.emplace_back(slapsRasterize);
            osteps.emplace_back(slaposObjectSlice);
        } else {
            // All values should be covered.
            assert(false);
        }
    }

    sort_remove_duplicates(steps);
    for (SLAPrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (SLAPrintObjectStep ostep : osteps)
        for (SLAPrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);
    return invalidated;
}

void SLAPrint::fill_statistics()
{
    const double init_layer_height  = m_material_config.initial_layer_height.getFloat();
    const double layer_height       = m_default_object_config.layer_height.getFloat();

    const double area_fill          = m_printer_config.area_fill.getFloat()*0.01;// 0.5 (50%);
    const double fast_tilt          = m_printer_config.fast_tilt_time.getFloat();// 5.0;
    const double slow_tilt          = m_printer_config.slow_tilt_time.getFloat();// 8.0;

    const double init_exp_time      = m_material_config.initial_exposure_time.getFloat();
    const double exp_time           = m_material_config.exposure_time.getFloat();

    const int    fade_layers_cnt    = m_default_object_config.faded_layers.getInt();// 10 // [3;20]

    const double width              = m_printer_config.display_width.getFloat() / SCALING_FACTOR;
    const double height             = m_printer_config.display_height.getFloat() / SCALING_FACTOR;
    const double display_area       = width*height;

    // get polygons for all instances in the object
    auto get_all_polygons = [](const ExPolygons& input_polygons, const std::vector<SLAPrintObject::Instance>& instances) {
        const size_t inst_cnt = instances.size();

        size_t polygon_cnt = 0;
        for (const ExPolygon& polygon : input_polygons)
			polygon_cnt += polygon.holes.size() + 1;

        Polygons polygons;
        polygons.reserve(polygon_cnt * inst_cnt);
        for (const ExPolygon& polygon : input_polygons) {
            for (size_t i = 0; i < inst_cnt; ++i)
            {
                ExPolygon tmp = polygon;
                tmp.rotate(Geometry::rad2deg(instances[i].rotation));
                tmp.translate(instances[i].shift.x(), instances[i].shift.y());
                polygons_append(polygons, to_polygons(std::move(tmp)));
            }
        }
        return polygons;
    };

    double supports_volume = 0.0;
    double models_volume = 0.0;

    double estim_time = 0.0;

    size_t slow_layers = 0;
    size_t fast_layers = 0;

    // find highest object
    // Which is a better bet? To compare by max_z or by number of layers in the index?
    double max_z = 0.;
	size_t max_layers_cnt = 0;
    size_t highest_obj_idx = 0;
	for (SLAPrintObject *&po : m_objects) {
        const SLAPrintObject::SliceIndex& slice_index = po->get_slice_index();
        if (! slice_index.empty()) {
            double z = (-- slice_index.end())->first;
            size_t cnt = slice_index.size();
            //if (z > max_z) {
            if (cnt > max_layers_cnt) {
                max_layers_cnt = cnt;
                max_z = z;
                highest_obj_idx = &po - &m_objects.front();
            }
        }
    }

    const SLAPrintObject * highest_obj = m_objects[highest_obj_idx];
    const SLAPrintObject::SliceIndex& highest_obj_slice_index = highest_obj->get_slice_index();

    const double delta_fade_time = (init_exp_time - exp_time) / (fade_layers_cnt + 1);
    double fade_layer_time = init_exp_time;

    int sliced_layer_cnt = 0;
    for (const auto& layer : highest_obj_slice_index)
    {
        const double l_height = (layer.first == highest_obj_slice_index.begin()->first) ? init_layer_height : layer_height;

        // Calculation of the consumed material 

        Polygons model_polygons;
        Polygons supports_polygons;

        for (SLAPrintObject * po : m_objects)
        {
            const SLAPrintObject::SliceRecord *record = nullptr;
            {
                const SLAPrintObject::SliceIndex& index = po->get_slice_index();
                auto key = layer.first;
				const SLAPrintObject::SliceIndex::const_iterator it_key = index.lower_bound(key - float(EPSILON));
                if (it_key == index.end() || it_key->first > key + EPSILON)
                    continue;
                record = &it_key->second;
            }

            if (record->model_slices_idx != SLAPrintObject::SliceRecord::NONE)
                append(model_polygons, get_all_polygons(po->get_model_slices()[record->model_slices_idx], po->instances()));
            
            if (record->support_slices_idx != SLAPrintObject::SliceRecord::NONE)
                append(supports_polygons, get_all_polygons(po->get_support_slices()[record->support_slices_idx], po->instances()));
        }
        
        model_polygons = union_(model_polygons);
        double layer_model_area = 0;
        for (const Polygon& polygon : model_polygons)
            layer_model_area += polygon.area();

        if (layer_model_area != 0)
            models_volume += layer_model_area * l_height;

        if (!supports_polygons.empty() && !model_polygons.empty())
            supports_polygons = diff(supports_polygons, model_polygons);
        double layer_support_area = 0;
        for (const Polygon& polygon : supports_polygons)
            layer_support_area += polygon.area();

        if (layer_support_area != 0)
            supports_volume += layer_support_area * l_height;

        // Calculation of the slow and fast layers to the future controlling those values on FW

        const bool is_fast_layer = (layer_model_area + layer_support_area) <= display_area*area_fill;
        const double tilt_time = is_fast_layer ? fast_tilt : slow_tilt;
        if (is_fast_layer)
            fast_layers++;
        else
            slow_layers++;


        // Calculation of the printing time

        if (sliced_layer_cnt < 3)
            estim_time += init_exp_time;
        else if (fade_layer_time > exp_time)
        {
            fade_layer_time -= delta_fade_time;
            estim_time += fade_layer_time;
        }
        else
            estim_time += exp_time;

        estim_time += tilt_time;

        sliced_layer_cnt++;
    }

    m_print_statistics.support_used_material = supports_volume * SCALING_FACTOR * SCALING_FACTOR; 
    m_print_statistics.objects_used_material = models_volume  * SCALING_FACTOR * SCALING_FACTOR;

    // Estimated printing time
    // A layers count o the highest object 
    if (max_layers_cnt == 0)
        m_print_statistics.estimated_print_time = "N/A";
    else
        m_print_statistics.estimated_print_time = get_time_dhms(float(estim_time));

    m_print_statistics.fast_layers_count = fast_layers;
    m_print_statistics.slow_layers_count = slow_layers;
}

// Returns true if an object step is done on all objects and there's at least one object.
bool SLAPrint::is_step_done(SLAPrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    tbb::mutex::scoped_lock lock(this->state_mutex());
    for (const SLAPrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

SLAPrintObject::SLAPrintObject(SLAPrint *print, ModelObject *model_object):
    Inherited(print, model_object),
    m_stepmask(slaposCount, true),
    m_transformed_rmesh( [this](TriangleMesh& obj){
            obj = m_model_object->raw_mesh(); obj.transform(m_trafo);
        })
{
}

SLAPrintObject::~SLAPrintObject() {}

// Called by SLAPrint::apply_config().
// This method only accepts SLAPrintObjectConfig option keys.
bool SLAPrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    std::vector<SLAPrintObjectStep> steps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
		if (   opt_key == "layer_height"
            || opt_key == "faded_layers"
            || opt_key == "slice_closing_radius") {
			steps.emplace_back(slaposObjectSlice);
        } else if (
               opt_key == "supports_enable"
            || opt_key == "support_points_density_relative"
            || opt_key == "support_points_minimal_distance") {
            steps.emplace_back(slaposSupportPoints);
		} else if (
               opt_key == "support_head_front_diameter"
            || opt_key == "support_head_penetration"
            || opt_key == "support_head_width"
            || opt_key == "support_pillar_diameter"
            || opt_key == "support_pillar_connection_mode"
            || opt_key == "support_buildplate_only"
            || opt_key == "support_base_diameter"
            || opt_key == "support_base_height"
            || opt_key == "support_critical_angle"
            || opt_key == "support_max_bridge_length"
            || opt_key == "support_object_elevation") {
            steps.emplace_back(slaposSupportTree);
        } else if (
               opt_key == "pad_enable"
            || opt_key == "pad_wall_thickness"
            || opt_key == "pad_wall_height"
            || opt_key == "pad_max_merge_distance"
            || opt_key == "pad_wall_slope"
            || opt_key == "pad_edge_radius") {
            steps.emplace_back(slaposBasePool);
        } else {
            // All keys should be covered.
            assert(false);
        }
    }

    sort_remove_duplicates(steps);
    for (SLAPrintObjectStep step : steps)
        invalidated |= this->invalidate_step(step);
    return invalidated;
}

bool SLAPrintObject::invalidate_step(SLAPrintObjectStep step)
{
    bool invalidated = Inherited::invalidate_step(step);
    // propagate to dependent steps
    if (step == slaposObjectSlice) {
        invalidated |= this->invalidate_all_steps();
    } else if (step == slaposSupportPoints) {
        invalidated |= this->invalidate_steps({ slaposSupportTree, slaposBasePool, slaposSliceSupports, slaposIndexSlices });
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposSupportTree) {
        invalidated |= this->invalidate_steps({ slaposBasePool, slaposSliceSupports, slaposIndexSlices });
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposBasePool) {
        invalidated |= this->invalidate_steps({slaposSliceSupports, slaposIndexSlices});
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if (step == slaposSliceSupports) {
        invalidated |= this->invalidate_step(slaposIndexSlices);
        invalidated |= m_print->invalidate_step(slapsRasterize);
    } else if(step == slaposIndexSlices) {
        invalidated |= m_print->invalidate_step(slapsRasterize);
    }
    return invalidated;
}

bool SLAPrintObject::invalidate_all_steps()
{
    return Inherited::invalidate_all_steps() | m_print->invalidate_all_steps();
}

double SLAPrintObject::get_elevation() const {
    bool se = m_config.supports_enable.getBool();
    double ret = se? m_config.support_object_elevation.getFloat() : 0;

    // if the pad is enabled, then half of the pad height is its base plate
    if(m_config.pad_enable.getBool()) {
        // Normally the elevation for the pad itself would be the thickness of
        // its walls but currently it is half of its thickness. Whatever it
        // will be in the future, we provide the config to the get_pad_elevation
        // method and we will have the correct value
        sla::PoolConfig pcfg;
        pcfg.min_wall_height_mm = m_config.pad_wall_height.getFloat();
        pcfg.min_wall_thickness_mm = m_config.pad_wall_thickness.getFloat();
        pcfg.edge_radius_mm = m_config.pad_edge_radius.getFloat();
        pcfg.max_merge_distance_mm = m_config.pad_max_merge_distance.getFloat();
        ret += sla::get_pad_elevation(pcfg);
    }

    return ret;
}

double SLAPrintObject::get_current_elevation() const
{
    bool se = m_config.supports_enable.getBool();
    bool has_supports = is_step_done(slaposSupportTree);
    bool has_pad = is_step_done(slaposBasePool);

    if(!has_supports && !has_pad)
        return 0;
    else if(has_supports && !has_pad)
        return se ? m_config.support_object_elevation.getFloat() : 0;

    return get_elevation();
}

namespace { // dummy empty static containers for return values in some methods
const std::vector<ExPolygons> EMPTY_SLICES;
const TriangleMesh EMPTY_MESH;
}

const std::vector<sla::SupportPoint>& SLAPrintObject::get_support_points() const
{
    return m_supportdata->support_points;
}

const std::vector<ExPolygons> &SLAPrintObject::get_support_slices() const
{
    // assert(is_step_done(slaposSliceSupports));
    if (!m_supportdata) return EMPTY_SLICES;
    return m_supportdata->support_slices;
}

const SLAPrintObject::SliceIndex &SLAPrintObject::get_slice_index() const
{
    // assert(is_step_done(slaposIndexSlices));
    return m_slice_index;
}

const std::vector<ExPolygons> &SLAPrintObject::get_model_slices() const
{
    // assert(is_step_done(slaposObjectSlice));
    return m_model_slices;
}

bool SLAPrintObject::has_mesh(SLAPrintObjectStep step) const
{
    switch (step) {
    case slaposSupportTree:
        return ! this->support_mesh().empty();
    case slaposBasePool:
        return ! this->pad_mesh().empty();
    default:
        return false;
    }
}

TriangleMesh SLAPrintObject::get_mesh(SLAPrintObjectStep step) const
{
    switch (step) {
    case slaposSupportTree:
        return this->support_mesh();
    case slaposBasePool:
        return this->pad_mesh();
    default:
        return TriangleMesh();
    }
}

const TriangleMesh& SLAPrintObject::support_mesh() const
{
    if(m_config.supports_enable.getBool() && m_supportdata &&
       m_supportdata->support_tree_ptr) {
        return m_supportdata->support_tree_ptr->merged_mesh();
    }

    return EMPTY_MESH;
}

const TriangleMesh& SLAPrintObject::pad_mesh() const
{
    if(m_config.pad_enable.getBool() && m_supportdata && m_supportdata->support_tree_ptr)
        return m_supportdata->support_tree_ptr->get_pad();

    return EMPTY_MESH;
}

const TriangleMesh &SLAPrintObject::transformed_mesh() const {
    // we need to transform the raw mesh...
    // currently all the instances share the same x and y rotation and scaling
    // so we have to extract those from e.g. the first instance and apply to the
    // raw mesh. This is also true for the support points.
    // BUT: when the support structure is spawned for each instance than it has
    // to omit the X, Y rotation and scaling as those have been already applied
    // or apply an inverse transformation on the support structure after it
    // has been created.

    return m_transformed_rmesh.get();
}

std::vector<sla::SupportPoint> SLAPrintObject::transformed_support_points() const
{
    assert(m_model_object != nullptr);
    std::vector<sla::SupportPoint>& spts = m_model_object->sla_support_points;

    // this could be cached as well
    std::vector<sla::SupportPoint> ret;
    ret.reserve(spts.size());

    for(sla::SupportPoint& sp : spts) {
        Vec3d transformed_pos = trafo() * Vec3d(sp.pos(0), sp.pos(1), sp.pos(2));
        ret.emplace_back(transformed_pos(0), transformed_pos(1), transformed_pos(2), sp.head_front_radius, sp.is_new_island);
    }

    return ret;
}

DynamicConfig SLAPrintStatistics::config() const
{
    DynamicConfig config;
    const std::string print_time = Slic3r::short_time(this->estimated_print_time);
    config.set_key_value("print_time", new ConfigOptionString(print_time));
    config.set_key_value("objects_used_material", new ConfigOptionFloat(this->objects_used_material));
    config.set_key_value("support_used_material", new ConfigOptionFloat(this->support_used_material));
    config.set_key_value("total_cost", new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_weight", new ConfigOptionFloat(this->total_weight));
    return config;
}

DynamicConfig SLAPrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : {
        "print_time", "total_cost", "total_weight",
        "objects_used_material", "support_used_material" })
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));
        return config;
}

std::string SLAPrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig cfg = this->config();
        PlaceholderParser pp;
        std::string new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path = (path.parent_path() / (new_stem + path.extension().string())).string();
    }
    catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

} // namespace Slic3r
