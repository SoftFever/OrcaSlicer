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
    // BBS still keep object pointer when selection is volume
    //if (selection.is_single_full_instance()) {
    if (!selection.is_empty()) {
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
    double z_min;
    if (canvas->get_canvas_type() == GLCanvas3D::CanvasAssembleView)
        z_min = std::numeric_limits<double>::max();
    else
        z_min = -SINKING_Z_THRESHOLD;

    if (mo && active_inst != -1) {
        canvas->toggle_model_objects_visibility(false);
        canvas->toggle_model_objects_visibility(true, mo, active_inst);
        canvas->toggle_sla_auxiliaries_visibility(m_show_supports, mo, active_inst);
        canvas->set_use_clipping_planes(true);
        // Some objects may be sinking, do not show whatever is below the bed.
        canvas->set_clipping_plane(0, ClippingPlane(Vec3d::UnitZ(), z_min));
        canvas->set_clipping_plane(1, ClippingPlane(-Vec3d::UnitZ(), std::numeric_limits<double>::max()));


        std::vector<const TriangleMesh*> meshes;
        for (const ModelVolume* mv : mo->volumes)
            meshes.push_back(&mv->mesh());

        if (meshes != m_old_meshes) {
            m_clippers.clear();
            for (const TriangleMesh* mesh : meshes) {
                m_clippers.emplace_back(new MeshClipper);
                m_clippers.back()->set_plane(ClippingPlane(-Vec3d::UnitZ(), z_min));
                m_clippers.back()->set_mesh(*mesh);
            }
            m_old_meshes = meshes;
        }
    }
    else
        canvas->toggle_model_objects_visibility(true);
}

void InstancesHider::on_release()
{
    get_pool()->get_canvas()->toggle_model_objects_visibility(true);
    get_pool()->get_canvas()->set_use_clipping_planes(false);
    m_old_meshes.clear();
    m_clippers.clear();
}

void InstancesHider::show_supports(bool show) {
    if (m_show_supports != show) {
        m_show_supports = show;
        on_update();
    }
}

void InstancesHider::render_cut() const
{
    const SelectionInfo* sel_info = get_pool()->selection_info();
    const ModelObject* mo = sel_info->model_object();
    Geometry::Transformation inst_trafo = mo->instances[sel_info->get_active_instance()]->get_transformation();

    size_t clipper_id = 0;
    for (const ModelVolume* mv : mo->volumes) {
        Geometry::Transformation vol_trafo  = mv->get_transformation();
        Geometry::Transformation trafo = inst_trafo * vol_trafo;
        trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., sel_info->get_sla_shift()));

        auto& clipper = m_clippers[clipper_id];
        clipper->set_transformation(trafo);
        const ObjectClipper* obj_clipper = get_pool()->object_clipper();
        if (obj_clipper->is_valid() && obj_clipper->get_clipping_plane()
         && obj_clipper->get_position() != 0.) {
            ClippingPlane clp = *get_pool()->object_clipper()->get_clipping_plane();
            clp.set_normal(-clp.get_normal());
            clipper->set_limiting_plane(clp);
        } else
            clipper->set_limiting_plane(ClippingPlane::ClipsNothing());

        glsafe(::glPushMatrix());
        if (mv->is_model_part())
            glsafe(::glColor3f(0.8f, 0.3f, 0.0f));
        else {
            const std::array<float, 4>& c = color_from_model_volume(*mv);
            glsafe(::glColor4f(c[0], c[1], c[2], c[3]));
        }
        glsafe(::glPushAttrib(GL_DEPTH_TEST));
        glsafe(::glDisable(GL_DEPTH_TEST));
        clipper->render_cut(mv->is_model_part() ? ColorRGBA{0.8f, 0.3f, 0.0f, 1.0f} : color_from_model_volume(*mv));
        glsafe(::glPopAttrib());
        glsafe(::glPopMatrix());

        ++clipper_id;
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

                    indexed_triangle_set interior = print_object->hollowed_interior_mesh();
                    its_flip_triangles(interior);
                    m_hollowed_interior_transformed = std::make_unique<TriangleMesh>(std::move(interior));
                    m_hollowed_interior_transformed->transform(trafo_inv);
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
    const ModelObject *mo = get_pool()->selection_info()->model_object();
    if (!mo) return;

    // which mesh should be cut?
    std::vector<const TriangleMesh *>     meshes;
    std::vector<Geometry::Transformation> trafos;
    bool                                  force_clipper_regeneration = false;

    std::unique_ptr<MeshClipper> mc;
    Geometry::Transformation     mc_tr;

    if (!mc && meshes.empty()) {
        for (const ModelVolume *mv : mo->volumes) {
            meshes.emplace_back(&mv->mesh());
            trafos.emplace_back(mv->get_transformation());
        }
    }

    if (mc || force_clipper_regeneration || meshes != m_old_meshes) {
        m_clippers.clear();
        for (size_t i = 0; i < meshes.size(); ++i) {
            m_clippers.emplace_back(new MeshClipper, trafos[i]);
            auto tri_mesh = new TriangleMesh(meshes[i]->its);
            m_clippers.back().first->set_mesh(*tri_mesh);
        }
        m_old_meshes = std::move(meshes);

        if (mc) { m_clippers.emplace_back(std::move(mc), mc_tr); }

        m_active_inst_bb_radius = mo->instance_bounding_box(get_pool()->selection_info()->get_active_instance()).radius();
    }
}


void ObjectClipper::on_release()
{
    m_clippers.clear();
    m_old_meshes.clear();
    m_clp.reset();
    m_clp_ratio = 0.;

}



void CommonGizmosDataObjects::ObjectClipper::render_cut(const std::vector<size_t> *ignore_idxs) const
{
    if (m_clp_ratio == 0.) return;
    const SelectionInfo *          sel_info          = get_pool()->selection_info();
    //consider normal view  or assemble view
    const ModelObject *      mo       = sel_info->model_object();
    Geometry::Transformation inst_trafo;
    bool                     is_assem_cnv             = get_pool()->get_canvas()->get_canvas_type() == GLCanvas3D::CanvasAssembleView;
    inst_trafo                                        = is_assem_cnv ? mo->instances[sel_info->get_active_instance()]->get_assemble_transformation() :
                                                                       mo->instances[sel_info->get_active_instance()]->get_transformation();
    auto                           offset_to_assembly = mo->instances[0]->get_offset_to_assembly();

    auto                           debug             = sel_info->get_sla_shift();
    std::vector<size_t>            ignore_idxs_local = ignore_idxs ? *ignore_idxs : std::vector<size_t>();

    for (auto &clipper : m_clippers) {
        auto                     vol_trafo = clipper.second;
        Geometry::Transformation trafo     = inst_trafo * vol_trafo;
        if (is_assem_cnv) {
            trafo.set_offset(trafo.get_offset() + offset_to_assembly * (GLVolume::explosion_ratio - 1.0) + vol_trafo.get_offset() * (GLVolume::explosion_ratio - 1.0));
        } else {
            trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., sel_info->get_sla_shift()));
        }
        clipper.first->set_plane(*m_clp);
        clipper.first->set_transformation(trafo);
        clipper.first->set_limiting_plane(ClippingPlane(Vec3d::UnitZ(), -SINKING_Z_THRESHOLD));
        clipper.first->render_cut({1.0f, 0.37f, 0.0f, 1.0f}, &ignore_idxs_local);
        clipper.first->render_contour({1.f, 1.f, 1.f, 1.f}, &ignore_idxs_local);

        // Now update the ignore idxs. Find the first element belonging to the next clipper,
        // and remove everything before it and decrement everything by current number of contours.
        const int num_of_contours = clipper.first->get_number_of_contours();
        ignore_idxs_local.erase(ignore_idxs_local.begin(),std::find_if(ignore_idxs_local.begin(), ignore_idxs_local.end(), [num_of_contours](size_t idx) {
            return idx >= size_t(num_of_contours);
            }));
        for (size_t &idx : ignore_idxs_local) idx -= num_of_contours;
    }
}

void ObjectClipper::set_range_and_pos(const Vec3d &cpl_normal, double cpl_offset, double pos)
{
    m_clp.reset(new ClippingPlane(cpl_normal, cpl_offset));
    m_clp_ratio = pos;
    get_pool()->get_canvas()->set_as_dirty();
}

void CommonGizmosDataObjects::ObjectClipper::set_behaviour(bool hide_clipped, bool fill_cut, double contour_width)
{
    m_hide_clipped = hide_clipped;
    for (auto &clipper : m_clippers)
        clipper.first->set_behaviour(fill_cut, contour_width);
}

void ObjectClipper::set_position(double pos, bool keep_normal, bool vertical_normal)
{
    const ModelObject *mo          = get_pool()->selection_info()->model_object();
    int                active_inst = get_pool()->selection_info()->get_active_instance();
    double             z_shift     = get_pool()->selection_info()->get_sla_shift();
    if (active_inst < 0) {
        return;
    }
    Vec3d normal;
    if(vertical_normal) {
        normal = {0, 0, 1};
    }else {
        normal = (keep_normal && m_clp) ? m_clp->get_normal() : -wxGetApp().plater()->get_camera().get_dir_forward();
    }
    Vec3d center;
    if (get_pool()->get_canvas()->get_canvas_type() == GLCanvas3D::CanvasAssembleView) {
        const SelectionInfo *sel_info           = get_pool()->selection_info();
        auto                 trafo              = mo->instances[sel_info->get_active_instance()]->get_assemble_transformation();
        auto                 offset_to_assembly = mo->instances[0]->get_offset_to_assembly();
        center                                  = trafo.get_offset() + offset_to_assembly * (GLVolume::explosion_ratio - 1.0);
    } else {
        center = mo->instances[active_inst]->get_offset() + Vec3d(0., 0., z_shift);
    }
    float        dist   = normal.dot(center);

    if (pos < 0.)
        pos = m_clp_ratio;

    m_clp_ratio = pos;
    m_clp.reset(new ClippingPlane(normal, (dist - (-m_active_inst_bb_radius) - m_clp_ratio * 2 * m_active_inst_bb_radius)));
    get_pool()->get_canvas()->set_as_dirty();
}

void ObjectClipper::set_position_to_init_layer()
{
    m_clp.reset(new ClippingPlane({0, 0, 1}, 0.1));
    get_pool()->get_canvas()->set_as_dirty();
}

int CommonGizmosDataObjects::ObjectClipper::get_number_of_contours() const
{
    int sum = 0;
    for (const auto &[clipper, trafo] : m_clippers)
        sum += clipper->get_number_of_contours();
    return sum;
}

std::vector<Vec3d> CommonGizmosDataObjects::ObjectClipper::point_per_contour() const
{
    std::vector<Vec3d> pts;

    for (const auto &clipper : m_clippers) {
        const std::vector<Vec3d> pts_clipper = clipper.first->point_per_contour();
        pts.insert(pts.end(), pts_clipper.begin(), pts_clipper.end());
    }
    return pts;
}

int ObjectClipper::is_projection_inside_cut(const Vec3d &point) const
{
    if (m_clp_ratio == 0.)
        return -1;
    int idx_offset = 0;
    for (const auto &[clipper, trafo] : m_clippers) {
        if (int idx = clipper->is_projection_inside_cut(point); idx != -1)
            return idx_offset + idx;
        idx_offset += clipper->get_number_of_contours();
    }
    return -1;
}

bool ObjectClipper::has_valid_contour() const
{
    return m_clp_ratio != 0. && std::any_of(m_clippers.begin(), m_clippers.end(), [](const auto &cl) {
        return cl.first->has_valid_contour();
    });
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

    glsafe(::glPushMatrix());
    m_clipper->render_cut({1.0f, 0.f, 0.37f, 1.0f});
    glsafe(::glPopMatrix());
}



using namespace AssembleViewDataObjects;
AssembleViewDataPool::AssembleViewDataPool(GLCanvas3D* canvas)
    : m_canvas(canvas)
{
    using c = AssembleViewDataID;
    m_data[c::ModelObjectsInfo].reset(new ModelObjectsInfo(this));
    m_data[c::ModelObjectsClipper].reset(new ModelObjectsClipper(this));
}

void AssembleViewDataPool::update(AssembleViewDataID required)
{
    assert(check_dependencies(required));
    for (auto& [id, data] : m_data) {
        if (int(required) & int(AssembleViewDataID(id)))
            data->update();
        else
            if (data->is_valid())
                data->release();
    }
}


ModelObjectsInfo* AssembleViewDataPool::model_objects_info() const
{
    ModelObjectsInfo* sel_info = dynamic_cast<ModelObjectsInfo*>(m_data.at(AssembleViewDataID::ModelObjectsInfo).get());
    assert(sel_info);
    return sel_info->is_valid() ? sel_info : nullptr;
}


ModelObjectsClipper* AssembleViewDataPool::model_objects_clipper() const
{
    ModelObjectsClipper* oc = dynamic_cast<ModelObjectsClipper*>(m_data.at(AssembleViewDataID::ModelObjectsClipper).get());
    // ObjectClipper is used from outside the gizmos to report current clipping plane.
    // This function can be called when oc is nullptr.
    return (oc && oc->is_valid()) ? oc : nullptr;
}

#ifndef NDEBUG
// Check the required resources one by one and return true if all
// dependencies are met.
bool AssembleViewDataPool::check_dependencies(AssembleViewDataID required) const
{
    // This should iterate over currently required data. Each of them should
    // be asked about its dependencies and it must check that all dependencies
    // are also in required and before the current one.
    for (auto& [id, data] : m_data) {
        // in case we don't use this, the deps are irrelevant
        if (!(int(required) & int(AssembleViewDataID(id))))
            continue;


        AssembleViewDataID deps = data->get_dependencies();
        assert(int(deps) == (int(deps) & int(required)));
    }


return true;
}
#endif // NDEBUG




void ModelObjectsInfo::on_update()
{
    if (!get_pool()->get_canvas()->get_model()->objects.empty()) {
        m_model_objects = get_pool()->get_canvas()->get_model()->objects;
    }
    else {
        m_model_objects.clear();
    }
}

void ModelObjectsInfo::on_release()
{
    m_model_objects.clear();
}

//int ModelObjectsInfo::get_active_instance() const
//{
//    const Selection& selection = get_pool()->get_canvas()->get_selection();
//    return selection.get_instance_idx();
//}


void ModelObjectsClipper::on_update()
{
    const ModelObjectPtrs model_objects = get_pool()->model_objects_info()->model_objects();
    if (model_objects.empty())
        return;

    // which mesh should be cut?
    std::vector<const TriangleMesh*> meshes;

    if (meshes.empty())
        for (auto mo : model_objects) {
            for (const ModelVolume* mv : mo->volumes)
                meshes.push_back(&mv->mesh());
        }

    if (meshes != m_old_meshes) {
        m_clippers.clear();
        for (const TriangleMesh* mesh : meshes) {
            m_clippers.emplace_back(new MeshClipper);
            m_clippers.back()->set_mesh(*mesh);
        }
        m_old_meshes = meshes;

        m_active_inst_bb_radius = get_pool()->get_canvas()->volumes_bounding_box().radius();
    }
}


void ModelObjectsClipper::on_release()
{
    m_clippers.clear();
    m_old_meshes.clear();
    m_clp.reset();
    m_clp_ratio = 0.;

}

void ModelObjectsClipper::render_cut() const
{
    if (m_clp_ratio == 0.)
        return;
    const ModelObjectPtrs model_objects = get_pool()->model_objects_info()->model_objects();

    size_t clipper_id = 0;
    for (const ModelObject* mo : model_objects) {
        Geometry::Transformation assemble_objects_trafo = mo->instances[0]->get_assemble_transformation();
        auto offset_to_assembly = mo->instances[0]->get_offset_to_assembly();
        for (const ModelVolume* mv : mo->volumes) {
            Geometry::Transformation vol_trafo = mv->get_transformation();
            Geometry::Transformation trafo = assemble_objects_trafo * vol_trafo;
            trafo.set_offset(trafo.get_offset() + vol_trafo.get_offset() * (GLVolume::explosion_ratio - 1.0) + offset_to_assembly * (GLVolume::explosion_ratio - 1.0));

            auto& clipper = m_clippers[clipper_id];
            clipper->set_plane(*m_clp);
            clipper->set_transformation(trafo);
            glsafe(::glPushMatrix());
            // BBS
            clipper->render_cut({0.25f, 0.25f, 0.25f, 1.0f});
            glsafe(::glPopMatrix());

            ++clipper_id;
        }
    }
}


void ModelObjectsClipper::set_position(double pos, bool keep_normal)
{
    Vec3d normal = (keep_normal && m_clp) ? m_clp->get_normal() : -wxGetApp().plater()->get_camera().get_dir_forward();
    const Vec3d& center = get_pool()->get_canvas()->volumes_bounding_box().center();
    float dist = normal.dot(center);

    if (pos < 0.)
        pos = m_clp_ratio;

    m_clp_ratio = pos;
    m_clp.reset(new ClippingPlane(normal, (dist - (-m_active_inst_bb_radius * GLVolume::explosion_ratio) - m_clp_ratio * 2 * m_active_inst_bb_radius * GLVolume::explosion_ratio)));
    get_pool()->get_canvas()->set_as_dirty();

}


} // namespace GUI
} // namespace Slic3r
