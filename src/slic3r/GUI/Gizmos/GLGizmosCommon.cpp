#include "GLGizmosCommon.hpp"

#include <cassert>

#include "slic3r/GUI/GLCanvas3D.hpp"

namespace Slic3r {
namespace GUI {


CommonGizmosDataPool::CommonGizmosDataPool(GLCanvas3D* canvas)
    : m_canvas(canvas)
{
    using namespace CommonGizmosDataObjects;
    using c = CommonGizmosDataID;
    m_data[c::SelectionInfo].reset(       new SelectionInfo(this));
    //m_data[c::InstancesHider].reset(      new InstancesHider(this));
    //m_data[c::HollowedMesh].reset(        new HollowedMesh(this));
    //m_data[c::ClippingPlaneWrapper].reset(new ClippingPlaneWrapper(this));
    //m_data[c::SupportsClipper].reset(     new SupportsClipper(this));
    //m_data[c::MeshRaycaster].reset(       new Raycaster(this));
}

void CommonGizmosDataPool::update(CommonGizmosDataID required)
{
    assert(check_dependencies(required));
    for (auto& [id, data] : m_data)
        data->update(int(required) & int(CommonGizmosDataID(id)));
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




void CommonGizmosDataObjects::SelectionInfo::update(bool required)
{
    Selection selection = m_common->get_canvas()->get_selection();

}



} // namespace GUI
} // namespace Slic3r
