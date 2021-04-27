#ifndef slic3r_GUI_GLGizmosCommon_hpp_
#define slic3r_GUI_GLGizmosCommon_hpp_

#include <memory>
#include <map>

#include "slic3r/GUI/MeshUtils.hpp"
#include "libslic3r/SLA/Hollowing.hpp"

namespace Slic3r {

class ModelObject;


namespace GUI {

class GLCanvas3D;

enum class SLAGizmoEventType : unsigned char {
    LeftDown = 1,
    LeftUp,
    RightDown,
    RightUp,
    Dragging,
    Delete,
    SelectAll,
    ShiftUp,
    AltUp,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane,
    Moving
};



class CommonGizmosDataBase;
namespace CommonGizmosDataObjects {
    class SelectionInfo;
    class InstancesHider;
    class HollowedMesh;
    class Raycaster;
    class ObjectClipper;
    class SupportsClipper;
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
    Raycaster            = 1 << 3,
    ObjectClipper        = 1 << 4,
    SupportsClipper      = 1 << 5,

};


// Following class holds pointers to the common data objects and triggers
// their updating/releasing. There is just one object of this type (managed
// by GLGizmoManager, the gizmos keep a pointer to it.
class CommonGizmosDataPool {
public:
    CommonGizmosDataPool(GLCanvas3D* canvas);

    // Update all resources and release what is not used.
    // Accepts a bitmask of currently required resources.
    void update(CommonGizmosDataID required);

    // Getters for the data that need to be accessed from the gizmos directly.
    CommonGizmosDataObjects::SelectionInfo* selection_info() const;
    CommonGizmosDataObjects::InstancesHider* instances_hider() const;
    CommonGizmosDataObjects::HollowedMesh* hollowed_mesh() const;
    CommonGizmosDataObjects::Raycaster* raycaster() const;
    CommonGizmosDataObjects::ObjectClipper* object_clipper() const;
    CommonGizmosDataObjects::SupportsClipper* supports_clipper() const;


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
    virtual ~CommonGizmosDataBase() {}

    // Update the resource.
    void update() { on_update(); m_is_valid = true; }

    // Release any data that are stored internally.
    void release() { on_release(); m_is_valid = false; }

    // Returns whether the resource is currently maintained.
    bool is_valid() const { return m_is_valid; }

#ifndef NDEBUG
    // Return a bitmask of all resources that this one relies on.
    // The dependent resource must have higher ID than the one
    // it depends on.
    virtual CommonGizmosDataID get_dependencies() const { return CommonGizmosDataID::None; }
#endif // NDEBUG

protected:
    virtual void on_release() = 0;
    virtual void on_update() = 0;
    CommonGizmosDataPool* get_pool() const { return m_common; }


private:
    bool m_is_valid = false;
    CommonGizmosDataPool* m_common = nullptr;
};



// The specializations of the CommonGizmosDataBase class live in this
// namespace to avoid clashes in GUI namespace.
namespace CommonGizmosDataObjects
{

class SelectionInfo : public CommonGizmosDataBase
{
public:
    explicit SelectionInfo(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}

    ModelObject* model_object() const { return m_model_object; }
    int get_active_instance() const;
    float get_sla_shift() const { return m_z_shift; }

protected:
    void on_update() override;
    void on_release() override;

private:
    ModelObject* m_model_object = nullptr;
    // int m_active_inst = -1;
    float m_z_shift = 0.f;
};



class InstancesHider : public CommonGizmosDataBase
{
public:
    explicit InstancesHider(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    void show_supports(bool show);
    bool are_supports_shown() const { return m_show_supports; }

protected:
    void on_update() override;
    void on_release() override;

private:
    bool m_show_supports = false;
};



class HollowedMesh : public CommonGizmosDataBase
{
public:
    explicit HollowedMesh(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    const sla::DrainHoles &get_drainholes() const { return m_drainholes; }

    const TriangleMesh* get_hollowed_mesh() const;
    const TriangleMesh* get_hollowed_interior() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::unique_ptr<TriangleMesh> m_hollowed_mesh_transformed;
    std::unique_ptr<TriangleMesh> m_hollowed_interior_transformed;
    size_t m_old_hollowing_timestamp = 0;
    int m_print_object_idx = -1;
    int m_print_objects_count = 0;
    sla::DrainHoles m_drainholes;
};



class Raycaster : public CommonGizmosDataBase
{
public:
    explicit Raycaster(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    const MeshRaycaster* raycaster() const { assert(m_raycasters.size() == 1); return m_raycasters.front().get(); }
    std::vector<const MeshRaycaster*> raycasters() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<std::unique_ptr<MeshRaycaster>> m_raycasters;
    std::vector<const TriangleMesh*> m_old_meshes;
};



class ObjectClipper : public CommonGizmosDataBase
{
public:
    explicit ObjectClipper(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    void set_position(double pos, bool keep_normal);
    double get_position() const { return m_clp_ratio; }
    ClippingPlane* get_clipping_plane() const { return m_clp.get(); }
    void render_cut() const;


protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*> m_old_meshes;
    std::vector<std::unique_ptr<MeshClipper>> m_clippers;
    std::unique_ptr<ClippingPlane> m_clp;
    double m_clp_ratio = 0.;
    double m_active_inst_bb_radius = 0.;
};



class SupportsClipper : public CommonGizmosDataBase
{
public:
    explicit SupportsClipper(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override {
        return CommonGizmosDataID(
                    int(CommonGizmosDataID::SelectionInfo)
                  | int(CommonGizmosDataID::ObjectClipper)
               );
    }
#endif // NDEBUG

    void render_cut() const;


protected:
    void on_update() override;
    void on_release() override;

private:
    size_t m_old_timestamp = 0;
    int m_print_object_idx = -1;
    int m_print_objects_count = 0;
    std::unique_ptr<MeshClipper> m_clipper;
};

} // namespace CommonGizmosDataObjects






} // namespace GUI
} // namespace Slic3r


#endif // slic3r_GUI_GLGizmosCommon_hpp_
