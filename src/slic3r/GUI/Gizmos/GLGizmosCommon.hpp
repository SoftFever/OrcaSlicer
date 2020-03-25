#ifndef slic3r_GUI_GLGizmosCommon_hpp_
#define slic3r_GUI_GLGizmosCommon_hpp_

#include <memory>
#include <map>

namespace Slic3r {

class ModelObject;


namespace GUI {

class GLCanvas3D;

class CommonGizmosDataBase;
namespace CommonGizmosDataObjects {
    class SelectionInfo;
}

// Some of the gizmos use the same data that need to be updated ocassionally.
// It is also desirable that the data are not recalculated when the gizmos
// are just switched, but on the other hand, they should be released when
// they are not in use by any gizmo anymore.

// Enumeration of various data types that the data pool can contain.
// Each gizmo can tell which of the data it wants to use through
// on_get_requirements() method.
enum class CommonGizmosDataID {
    None                 = 0,
    SelectionInfo        = 1 << 0,
    InstancesHider       = 1 << 1,
    HollowedMesh         = 1 << 2,
    ClippingPlaneWrapper = 1 << 3,
    SupportsClipper      = 1 << 4,
    MeshRaycaster        = 1 << 5,
};


// Following class holds pointers to the common data objects and triggers
// their updating/releasing. There is just one object of this type (managed
// by GLGizmoManager, the gizmos keep a pointer to it.
class CommonGizmosDataPool {
public:
    CommonGizmosDataPool(GLCanvas3D* canvas);

    // Update all resources and release what is not used.
    // Accepts a bitmask of currently required resources.
    void update(CommonGizmosDataID required = CommonGizmosDataID::None);

    // Getters for the data that need to be accessed from the gizmos directly.
    CommonGizmosDataObjects::SelectionInfo selection_info();

    GLCanvas3D* get_canvas() const { return m_canvas; }

private:
    std::map<CommonGizmosDataID, std::unique_ptr<CommonGizmosDataBase>> m_data;
    GLCanvas3D* m_canvas;

#ifndef NDEBUG
    bool check_dependencies(CommonGizmosDataID required) const;
#endif
};





// Base class for a wrapper object managing a single resource.
// Each of the enum values above (safe None) will have an object of this kind.
class CommonGizmosDataBase {
public:
    // Pass a backpointer to the pool, so the individual
    // objects can communicate with one another.
    explicit CommonGizmosDataBase(CommonGizmosDataPool* cgdp)
        : m_common{cgdp} {}

    // Update the resource. If it is not needed (based on argument value)
    // any persistent data will be released.
    virtual void update(bool required) = 0;

    // Returns whether the resource is currently maintained.
    bool is_valid() const { return m_is_valid; }

protected:
    CommonGizmosDataPool* m_common = nullptr;

private:
    bool m_is_valid = false;
};



// The specializations of the CommonGizmosDataBase class live in this
// namespace to avoid clashes in GUI namespace.
namespace CommonGizmosDataObjects
{

class SelectionInfo : public CommonGizmosDataBase
{
public:
    explicit SelectionInfo(CommonGizmosDataPool* cgdp) :
        CommonGizmosDataBase(cgdp) {}
    void update(bool required) override;

    ModelObject* model_object();
    int get_active_instance();

private:
    ModelObject* m_model_object = nullptr;
    int m_active_inst = -1;
};


class InstancesHider : public CommonGizmosDataBase
{
public:
    explicit InstancesHider(CommonGizmosDataPool* cgdp) :
        CommonGizmosDataBase(cgdp) {}
    void update(bool required) override;
};







class HollowedMesh : public CommonGizmosDataBase
{
public:
    explicit HollowedMesh(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
    void update(bool required) override;
};







class ClippingPlaneWrapper : public CommonGizmosDataBase
{
public:
    explicit ClippingPlaneWrapper(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
    void update(bool required) override;
};







class SupportsClipper : public CommonGizmosDataBase
{
public:
    explicit SupportsClipper(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
    void update(bool required) override;
};







class Raycaster : public CommonGizmosDataBase
{
public:
    explicit Raycaster(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
    void update(bool required) override;
};


} // namespace CommonGizmosDataObjects






} // namespace GUI
} // namespace Slic3r


#endif // slic3r_GUI_GLGizmosCommon_hpp_
