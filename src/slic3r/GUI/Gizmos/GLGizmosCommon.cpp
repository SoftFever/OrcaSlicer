#include "GLGizmosCommon.hpp"

#include <cassert>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

using namespace CommonGizmosDataObjects;

CommonGizmosDataPool::CommonGizmosDataPool(GLCanvas3D* canvas)
    : m_canvas(canvas)
{
    using c = CommonGizmosDataID;
    m_data[c::SelectionInfo].reset(       new SelectionInfo(this));
    m_data[c::InstancesHider].reset(      new InstancesHider(this));
    m_data[c::HollowedMesh].reset(        new HollowedMesh(this));
    m_data[c::Raycaster].reset(           new Raycaster(this));
    m_data[c::ObjectClipper].reset(new ObjectClipper(this));
    //m_data[c::SupportsClipper].reset(     new SupportsClipper(this));

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
    assert(oc);
    return oc->is_valid() ? oc : nullptr;
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
    }
    else
        canvas->toggle_model_objects_visibility(true);
}

void InstancesHider::on_release()
{
    get_pool()->get_canvas()->toggle_model_objects_visibility(true);
}



void HollowedMesh::on_update()
{
    const ModelObject* mo = get_pool()->selection_info()->model_object();
    if (! mo)
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
                const TriangleMesh& backend_mesh = print_object->get_mesh_to_print();
                m_hollowed_mesh_transformed.reset(new TriangleMesh(backend_mesh));
                Transform3d trafo_inv = canvas->sla_print()->sla_trafo(*mo).inverse();
                m_hollowed_mesh_transformed->transform(trafo_inv);
                m_old_hollowing_timestamp = timestamp;
            }
        }
        else {
            m_hollowed_mesh_transformed.reset(nullptr);
            m_old_hollowing_timestamp = 0;
        }
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





void Raycaster::on_update()
{
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
    const TriangleMesh* mesh = &mo->volumes.front()->mesh();
    bool has_hollowed = get_pool()->hollowed_mesh() && get_pool()->hollowed_mesh()->get_hollowed_mesh();
    if (has_hollowed)
        mesh = get_pool()->hollowed_mesh()->get_hollowed_mesh();

    if (mesh != m_old_mesh) {
        m_clipper.reset(new MeshClipper);
        m_clipper->set_mesh(*mesh);
        m_old_mesh = mesh;
        m_active_inst_bb_radius =
            mo->instance_bounding_box(get_pool()->selection_info()->get_active_instance()).radius();
        //if (has_hollowed && m_clp_ratio != 0.)
        //    m_clp_ratio = 0.25;
    }
}


void ObjectClipper::on_release()
{
    m_clipper.reset();
    m_old_mesh = nullptr;
    m_clp.reset();

}

void ObjectClipper::render_cut() const
{
    if (m_clp_ratio == 0.)
        return;
    const SelectionInfo* sel_info = get_pool()->selection_info();
    const ModelObject* mo = sel_info->model_object();
    Geometry::Transformation inst_trafo = mo->instances[sel_info->get_active_instance()]->get_transformation();
    Geometry::Transformation vol_trafo  = mo->volumes.front()->get_transformation();
    Geometry::Transformation trafo = inst_trafo * vol_trafo;
    trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., sel_info->get_sla_shift()));

    m_clipper->set_plane(*m_clp);
    m_clipper->set_transformation(trafo);

    if (! m_clipper->get_triangles().empty()) {
        ::glPushMatrix();
        ::glColor3f(1.0f, 0.37f, 0.0f);
        ::glBegin(GL_TRIANGLES);
        for (const Vec3f& point : m_clipper->get_triangles())
            ::glVertex3f(point(0), point(1), point(2));
        ::glEnd();
        ::glPopMatrix();
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



} // namespace GUI
} // namespace Slic3r
