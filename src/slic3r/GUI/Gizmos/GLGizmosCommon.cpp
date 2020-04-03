#include "GLGizmosCommon.hpp"

#include <cassert>

#include "slic3r/GUI/GLCanvas3D.hpp"

namespace Slic3r {
namespace GUI {

using namespace CommonGizmosDataObjects;

CommonGizmosDataPool::CommonGizmosDataPool(GLCanvas3D* canvas)
    : m_canvas(canvas)
{
    using c = CommonGizmosDataID;
    m_data[c::SelectionInfo].reset(       new SelectionInfo(this));
    m_data[c::InstancesHider].reset(      new InstancesHider(this));
    //m_data[c::HollowedMesh].reset(        new HollowedMesh(this));
    //m_data[c::ClippingPlaneWrapper].reset(new ClippingPlaneWrapper(this));
    //m_data[c::SupportsClipper].reset(     new SupportsClipper(this));
    //m_data[c::MeshRaycaster].reset(       new Raycaster(this));
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


SelectionInfo* CommonGizmosDataPool::selection_info()
{
    SelectionInfo* sel_info = dynamic_cast<SelectionInfo*>(m_data[CommonGizmosDataID::SelectionInfo].get());
    assert(sel_info->is_valid());
    return sel_info;
}

#ifndef NDEBUG
bool CommonGizmosDataPool::check_dependencies(CommonGizmosDataID required) const
{
    // This should iterate over currently required data. Each of them should
    // be asked about its dependencies and it must check that all dependencies
    // are also in required and before the current one.
    return true;
}
#endif // NDEBUG




void SelectionInfo::on_update()
{
    const Selection& selection = m_common->get_canvas()->get_selection();
    if (selection.is_single_full_instance())
        m_model_object = selection.get_model()->objects[selection.get_object_idx()];
    else
        m_model_object = nullptr;
}

void SelectionInfo::on_release()
{
    m_model_object = nullptr;
}

int SelectionInfo::get_active_instance()
{
    const Selection& selection = m_common->get_canvas()->get_selection();
    return selection.get_instance_idx();
}





void InstancesHider::on_update()
{
    const ModelObject* mo = m_common->selection_info()->model_object();
    int active_inst = m_common->selection_info()->get_active_instance();
    GLCanvas3D* canvas = m_common->get_canvas();

    if (mo && active_inst != -1) {
        canvas->toggle_model_objects_visibility(false);
        canvas->toggle_model_objects_visibility(true, mo, active_inst);
    }
    else
        canvas->toggle_model_objects_visibility(true);
}

void InstancesHider::on_release()
{
    m_common->get_canvas()->toggle_model_objects_visibility(true);
}



} // namespace GUI
} // namespace Slic3r
