#include "GLGizmosCommon.hpp"

#include <cassert>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"

#include "libslic3r/PresetBundle.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

using namespace CommonGizmosDataObjects;

CommonGizmosDataPool::CommonGizmosDataPool(GLCanvas3D* canvas)
    : m_canvas(canvas)
{
    using c = CommonGizmosDataID;
    m_data[c::SelectionInfo].reset(   new SelectionInfo(this));
    m_data[c::InstancesHider].reset(  new InstancesHider(this));
    m_data[c::HollowedMesh].reset(    new HollowedMesh(this));
    m_data[c::Raycaster].reset(       new Raycaster(this));
    m_data[c::ObjectClipper].reset(   new ObjectClipper(this));
    m_data[c::SupportsClipper].reset( new SupportsClipper(this));

}

void CommonGizmosDataPool::update(CommonGizmosDataID required)
{
    assert(check_dependencies(required));
    for (auto& [id, data] : m_data) {
        if (int(required) & int(CommonGizmosDataID(id)))
            data->update();
        else
            if (data->is_valid())
                data->release();

    }
}


SelectionInfo* CommonGizmosDataPool::selection_info() const
{
    SelectionInfo* sel_info = dynamic_cast<SelectionInfo*>(m_data.at(CommonGizmosDataID::SelectionInfo).get());
    assert(sel_info);
    return sel_info->is_valid() ? sel_info : nullptr;
}


InstancesHider* CommonGizmosDataPool::instances_hider() const
{
    InstancesHider* inst_hider = dynamic_cast<InstancesHider*>(m_data.at(CommonGizmosDataID::InstancesHider).get());
    assert(inst_hider);
    return inst_hider->is_valid() ? inst_hider : nullptr;
}

HollowedMesh* CommonGizmosDataPool::hollowed_mesh() const
{
    HollowedMesh* hol_mesh = dynamic_cast<HollowedMesh*>(m_data.at(CommonGizmosDataID::HollowedMesh).get());
    assert(hol_mesh);
    return hol_mesh->is_valid() ? hol_mesh : nullptr;
}

Raycaster* CommonGizmosDataPool::raycaster() const
{
    Raycaster* rc = dynamic_cast<Raycaster*>(m_data.at(CommonGizmosDataID::Raycaster).get());
    assert(rc);
    return rc->is_valid() ? rc : nullptr;
}

ObjectClipper* CommonGizmosDataPool::object_clipper() const
{
    ObjectClipper* oc = dynamic_cast<ObjectClipper*>(m_data.at(CommonGizmosDataID::ObjectClipper).get());
    // ObjectClipper is used from outside the gizmos to report current clipping plane.
    // This function can be called when oc is nullptr.
    return (oc && oc->is_valid()) ? oc : nullptr;
}

SupportsClipper* CommonGizmosDataPool::supports_clipper() const
{
    SupportsClipper* sc = dynamic_cast<SupportsClipper*>(m_data.at(CommonGizmosDataID::SupportsClipper).get());
    assert(sc);
    return sc->is_valid() ? sc : nullptr;
}

#ifndef NDEBUG
// Check the required resources one by one and return true if all
// dependencies are met.
bool CommonGizmosDataPool::check_dependencies(CommonGizmosDataID required) const
{
    // This should iterate over currently required data. Each of them should
    // be asked about its dependencies and it must check that all dependencies
    // are also in required and before the current one.
    for (auto& [id, data] : m_data) {
        // in case we don't use this, the deps are irrelevant
        if (! (int(required) & int(CommonGizmosDataID(id))))
            continue;


        CommonGizmosDataID deps = data->get_dependencies();
        assert(int(deps) == (int(deps) & int(required)));
    }


    return true;
}
#endif // NDEBUG




void SelectionInfo::on_update()
{
    const Selection& selection = get_pool()->get_canvas()->get_selection();
    if (selection.is_single_full_instance()) {
        m_model_object = selection.get_model()->objects[selection.get_object_idx()];
        m_z_shift = selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z();
    }
    else
        m_model_object = nullptr;
}

void SelectionInfo::on_release()
{
    m_model_object = nullptr;
}

int SelectionInfo::get_active_instance() const
{
    const Selection& selection = get_pool()->get_canvas()->get_selection();
    return selection.get_instance_idx();
}





void InstancesHider::on_update()
{
    const ModelObject* mo = get_pool()->selection_info()->model_object();
    int active_inst = get_pool()->selection_info()->get_active_instance();
    GLCanvas3D* canvas = get_pool()->get_canvas();

    if (mo && active_inst != -1) {
        canvas->toggle_model_objects_visibility(false);
        canvas->toggle_model_objects_visibility(true, mo, active_inst);
        canvas->toggle_sla_auxiliaries_visibility(m_show_supports, mo, active_inst);
    }
    else
        canvas->toggle_model_objects_visibility(true);
}

void InstancesHider::on_release()
{
    get_pool()->get_canvas()->toggle_model_objects_visibility(true);
}

void InstancesHider::show_supports(bool show) {
    if (m_show_supports != show) {
        m_show_supports = show;
        on_update();
    }
}



void HollowedMesh::on_update()
{
    const ModelObject* mo = get_pool()->selection_info()->model_object();
    bool is_sla = wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA;
    if (! mo || ! is_sla)
        return;

    const GLCanvas3D* canvas = get_pool()->get_canvas();
    const PrintObjects& print_objects = canvas->sla_print()->objects();
    const SLAPrintObject* print_object = m_print_object_idx != -1
            ? print_objects[m_print_object_idx]
            : nullptr;

    // Find the respective SLAPrintObject.
    if (m_print_object_idx < 0 || m_print_objects_count != int(print_objects.size())) {
        m_print_objects_count = print_objects.size();
        m_print_object_idx = -1;
        for (const SLAPrintObject* po : print_objects) {
            ++m_print_object_idx;
            if (po->model_object()->id() == mo->id()) {
                print_object = po;
                break;
            }
        }
    }

    // If there is a valid SLAPrintObject, check state of Hollowing step.
    if (print_object) {
        if (print_object->is_step_done(slaposDrillHoles) && print_object->has_mesh(slaposDrillHoles)) {
            size_t timestamp = print_object->step_state_with_timestamp(slaposDrillHoles).timestamp;
            if (timestamp > m_old_hollowing_timestamp) {
                const TriangleMesh& backend_mesh = print_object->get_mesh_to_slice();
                if (! backend_mesh.empty()) {
                    m_hollowed_mesh_transformed.reset(new TriangleMesh(backend_mesh));
                    Transform3d trafo_inv = canvas->sla_print()->sla_trafo(*mo).inverse();
                    m_hollowed_mesh_transformed->transform(trafo_inv);
                    m_drainholes = print_object->model_object()->sla_drain_holes;
                    m_old_hollowing_timestamp = timestamp;

                    const indexed_triangle_set &interior = print_object->hollowed_interior_mesh();
                    if (!interior.empty()) {
                        m_hollowed_interior_transformed = std::make_unique<TriangleMesh>(interior);
                        m_hollowed_interior_transformed->repaired = false;
                        m_hollowed_interior_transformed->repair(true);
                        m_hollowed_interior_transformed->transform(trafo_inv);
                    }
                }
                else {
                    m_hollowed_mesh_transformed.reset(nullptr);
                }
            }
        }
        else
            m_hollowed_mesh_transformed.reset(nullptr);
    }
}


void HollowedMesh::on_release()
{
    m_hollowed_mesh_transformed.reset();
    m_old_hollowing_timestamp = 0;
    m_print_object_idx = -1;
}


const TriangleMesh* HollowedMesh::get_hollowed_mesh() const
{
    return m_hollowed_mesh_transformed.get();
}

const TriangleMesh* HollowedMesh::get_hollowed_interior() const
{
    return m_hollowed_interior_transformed.get();
}




void Raycaster::on_update()
{
    wxBusyCursor wait;
    const ModelObject* mo = get_pool()->selection_info()->model_object();

    if (! mo)
        return;

    std::vector<const TriangleMesh*> meshes;
    const std::vector<ModelVolume*>& mvs = mo->volumes;
    if (mvs.size() == 1) {
        assert(mvs.front()->is_model_part());
        const HollowedMesh* hollowed_mesh_tracker = get_pool()->hollowed_mesh();
        if (hollowed_mesh_tracker && hollowed_mesh_tracker->get_hollowed_mesh())
            meshes.push_back(hollowed_mesh_tracker->get_hollowed_mesh());
    }
    if (meshes.empty()) {
        for (const ModelVolume* mv : mvs) {
            if (mv->is_model_part())
                meshes.push_back(&mv->mesh());
        }
    }

    if (meshes != m_old_meshes) {
        m_raycasters.clear();
        for (const TriangleMesh* mesh : meshes)
            m_raycasters.emplace_back(new MeshRaycaster(*mesh));
        m_old_meshes = meshes;
    }
}

void Raycaster::on_release()
{
    m_raycasters.clear();
    m_old_meshes.clear();
}

std::vector<const MeshRaycaster*> Raycaster::raycasters() const
{
    std::vector<const MeshRaycaster*> mrcs;
    for (const auto& raycaster_unique_ptr : m_raycasters)
        mrcs.push_back(raycaster_unique_ptr.get());
    return mrcs;
}





void ObjectClipper::on_update()
{
    const ModelObject* mo = get_pool()->selection_info()->model_object();
    if (! mo)
        return;

    // which mesh should be cut?
    std::vector<const TriangleMesh*> meshes;
    bool has_hollowed = get_pool()->hollowed_mesh() && get_pool()->hollowed_mesh()->get_hollowed_mesh();
    if (has_hollowed)
        meshes.push_back(get_pool()->hollowed_mesh()->get_hollowed_mesh());

    if (meshes.empty())
        for (const ModelVolume* mv : mo->volumes)
            meshes.push_back(&mv->mesh());

    if (meshes != m_old_meshes) {
        m_clippers.clear();
        for (const TriangleMesh* mesh : meshes) {
            m_clippers.emplace_back(new MeshClipper);
            m_clippers.back()->set_mesh(*mesh);
        }
        m_old_meshes = meshes;

        if (has_hollowed)
            m_clippers.front()->set_negative_mesh(*get_pool()->hollowed_mesh()->get_hollowed_interior());

        m_active_inst_bb_radius =
            mo->instance_bounding_box(get_pool()->selection_info()->get_active_instance()).radius();
        //if (has_hollowed && m_clp_ratio != 0.)
        //    m_clp_ratio = 0.25;
    }
}


void ObjectClipper::on_release()
{
    m_clippers.clear();
    m_old_meshes.clear();
    m_clp.reset();
    m_clp_ratio = 0.;

}

void ObjectClipper::render_cut() const
{
    if (m_clp_ratio == 0.)
        return;
    const SelectionInfo* sel_info = get_pool()->selection_info();
    const ModelObject* mo = sel_info->model_object();
    Geometry::Transformation inst_trafo = mo->instances[sel_info->get_active_instance()]->get_transformation();

    size_t clipper_id = 0;
    for (const ModelVolume* mv : mo->volumes) {
        Geometry::Transformation vol_trafo  = mv->get_transformation();
        Geometry::Transformation trafo = inst_trafo * vol_trafo;
        trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., sel_info->get_sla_shift()));

        auto& clipper = m_clippers[clipper_id];
        clipper->set_plane(*m_clp);
        clipper->set_transformation(trafo);

        ::glPushMatrix();
        ::glColor3f(1.0f, 0.37f, 0.0f);
        clipper->render_cut();
        ::glPopMatrix();

        ++clipper_id;
    }
}


void ObjectClipper::set_position(double pos, bool keep_normal)
{
    const ModelObject* mo = get_pool()->selection_info()->model_object();
    int active_inst = get_pool()->selection_info()->get_active_instance();
    double z_shift = get_pool()->selection_info()->get_sla_shift();

    Vec3d normal = (keep_normal && m_clp) ? m_clp->get_normal() : -wxGetApp().plater()->get_camera().get_dir_forward();
    const Vec3d& center = mo->instances[active_inst]->get_offset() + Vec3d(0., 0., z_shift);
    float dist = normal.dot(center);

    if (pos < 0.)
        pos = m_clp_ratio;

    m_clp_ratio = pos;
    m_clp.reset(new ClippingPlane(normal, (dist - (-m_active_inst_bb_radius) - m_clp_ratio * 2*m_active_inst_bb_radius)));
    get_pool()->get_canvas()->set_as_dirty();
}



void SupportsClipper::on_update()
{
    const ModelObject* mo = get_pool()->selection_info()->model_object();
    bool is_sla = wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA;
    if (! mo || ! is_sla)
        return;

    const GLCanvas3D* canvas = get_pool()->get_canvas();
    const PrintObjects& print_objects = canvas->sla_print()->objects();
    const SLAPrintObject* print_object = m_print_object_idx != -1
            ? print_objects[m_print_object_idx]
            : nullptr;

    // Find the respective SLAPrintObject.
    if (m_print_object_idx < 0 || m_print_objects_count != int(print_objects.size())) {
        m_print_objects_count = print_objects.size();
        m_print_object_idx = -1;
        for (const SLAPrintObject* po : print_objects) {
            ++m_print_object_idx;
            if (po->model_object()->id() == mo->id()) {
                print_object = po;
                break;
            }
        }
    }

    if (print_object
     && print_object->is_step_done(slaposSupportTree)
     && ! print_object->support_mesh().empty())
    {
        // If the supports are already calculated, save the timestamp of the respective step
        // so we can later tell they were recalculated.
        size_t timestamp = print_object->step_state_with_timestamp(slaposSupportTree).timestamp;
        if (! m_clipper || timestamp != m_old_timestamp) {
            // The timestamp has changed.
            m_clipper.reset(new MeshClipper);
            // The mesh should already have the shared vertices calculated.
            m_clipper->set_mesh(print_object->support_mesh());
            m_old_timestamp = timestamp;
        }
    }
    else
        // The supports are not valid. We better dump the cached data.
        m_clipper.reset();
}


void SupportsClipper::on_release()
{
    m_clipper.reset();
    m_old_timestamp = 0;
    m_print_object_idx = -1;
}

void SupportsClipper::render_cut() const
{
    const CommonGizmosDataObjects::ObjectClipper* ocl = get_pool()->object_clipper();
    if (ocl->get_position() == 0.
     || ! get_pool()->instances_hider()->are_supports_shown()
     || ! m_clipper)
        return;

    const SelectionInfo* sel_info = get_pool()->selection_info();
    const ModelObject* mo = sel_info->model_object();
    Geometry::Transformation inst_trafo = mo->instances[sel_info->get_active_instance()]->get_transformation();
    //Geometry::Transformation vol_trafo  = mo->volumes.front()->get_transformation();
    Geometry::Transformation trafo = inst_trafo;// * vol_trafo;
    trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., sel_info->get_sla_shift()));


    // Get transformation of supports
    Geometry::Transformation supports_trafo = trafo;
    supports_trafo.set_scaling_factor(Vec3d::Ones());
    supports_trafo.set_offset(Vec3d(trafo.get_offset()(0), trafo.get_offset()(1), sel_info->get_sla_shift()));
    supports_trafo.set_rotation(Vec3d(0., 0., trafo.get_rotation()(2)));
    // I don't know why, but following seems to be correct.
    supports_trafo.set_mirror(Vec3d(trafo.get_mirror()(0) * trafo.get_mirror()(1) * trafo.get_mirror()(2),
                                    1,
                                    1.));

    m_clipper->set_plane(*ocl->get_clipping_plane());
    m_clipper->set_transformation(supports_trafo);

    ::glPushMatrix();
    ::glColor3f(1.0f, 0.f, 0.37f);
    m_clipper->render_cut();
    ::glPopMatrix();
}


} // namespace GUI
} // namespace Slic3r
