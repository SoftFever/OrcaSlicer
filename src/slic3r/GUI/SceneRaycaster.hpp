#ifndef slic3r_SceneRaycaster_hpp_
#define slic3r_SceneRaycaster_hpp_

#include "MeshUtils.hpp"
#include "GLModel.hpp"

#include <vector>
#include <string>
#include <optional>

namespace Slic3r {
namespace GUI {

struct Camera;

using PickingId = int;

class SceneRaycasterItem
{
    PickingId m_id{ -1 };
    bool m_active{ true };
    const MeshRaycaster* m_raycaster;
    Transform3d m_trafo;

public:
    SceneRaycasterItem(PickingId id, const MeshRaycaster& raycaster, const Transform3d& trafo)
        : m_id(id), m_raycaster(&raycaster), m_trafo(trafo)
    {}

    PickingId get_id() const { return m_id; }
    bool is_active() const { return m_active; }
    void set_active(bool active) { m_active = active; }
    const MeshRaycaster* get_raycaster() const { return m_raycaster; }
    const Transform3d& get_transform() const { return m_trafo; }
    void set_transform(const Transform3d& trafo) { m_trafo = trafo; }
};

class SceneRaycaster
{
public:
    enum class EType
    {
        None,
        Bed,
        Volume,
        Gizmo
    };

    enum class EPickingIdBase
    {
        Bed    = 0,
        Volume = 1000,
        Gizmo  = 1000000
    };

    struct HitResult
    {
        EType type{ EType::None };
        PickingId raycaster_id{ -1 };
        Vec3f position{ Vec3f::Zero() };
        Vec3f normal{ Vec3f::Zero() };

        bool is_valid() const { return raycaster_id != -1; }
    };

private:
    std::vector<SceneRaycasterItem> m_bed;
    std::vector<SceneRaycasterItem> m_volumes;
    std::vector<SceneRaycasterItem> m_gizmos;

    // When set to true, if checking gizmos returns a valid hit,
    // the search is not performed on other types
    bool m_gizmos_on_top{ false };

#if ENABLE_RAYCAST_PICKING_DEBUG
    GLModel m_sphere;
    GLModel m_line;
    std::optional<HitResult> m_last_hit;
#endif // ENABLE_RAYCAST_PICKING_DEBUG

public:
    SceneRaycaster();

    void add_raycaster(EType type, PickingId picking_id, const MeshRaycaster& raycaster, const Transform3d& trafo);
    void remove_raycasters(EType type, PickingId id);
    void remove_raycasters(EType type);

    void set_raycaster_active_state(EType type, PickingId picking_id, bool active);
    void set_raycaster_transform(EType type, PickingId picking_id, const Transform3d& trafo);

    void set_gizmos_on_top(bool value) { m_gizmos_on_top = value; }

    HitResult hit(const Vec2d& mouse_pos, const Camera& camera, const ClippingPlane* clipping_plane = nullptr);

#if ENABLE_RAYCAST_PICKING_DEBUG
    void render_hit(const Camera& camera);

    size_t beds_count() const    { return m_bed.size(); }
    size_t volumes_count() const { return m_volumes.size(); }
    size_t gizmos_count() const  { return m_gizmos.size(); }
#endif // ENABLE_RAYCAST_PICKING_DEBUG

private:
    std::vector<SceneRaycasterItem>* get_raycasters(EType type);

    static PickingId encode_id(EType type, PickingId id);
    static PickingId decode_id(EType type, PickingId id);
    static PickingId base_id(EType type);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SceneRaycaster_hpp_
