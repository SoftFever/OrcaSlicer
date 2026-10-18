#ifndef slic3r_GLGizmoAlignment_hpp_
#define slic3r_GLGizmoAlignment_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "slic3r/GUI/Selection.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class GLGizmoAlignment
{
public:
    enum class AlignType {
        NONE = -1,
        CENTER_X,
        CENTER_Y,
        CENTER_Z,
        Y_MAX,
        Y_MIN,
        X_MAX,
        X_MIN,
        Z_MAX,
        Z_MIN,
        DISTRIBUTE_X,
        DISTRIBUTE_Y,
        DISTRIBUTE_Z
    };
    struct ObjectInfo {
        int object_idx;
        int instance_idx;
        BoundingBoxf3 bbox;
        Vec3d center;

        ObjectInfo(int obj_idx, int inst_idx, const BoundingBoxf3& bb)
            : object_idx(obj_idx), instance_idx(inst_idx), bbox(bb), center(bb.center()) {}
    };

    explicit GLGizmoAlignment(GLCanvas3D& canvas);
    ~GLGizmoAlignment() = default;

    bool align_objects(AlignType type,bool align_parent = false);
    bool distribute_objects(AlignType type);

    bool align_to_center(AlignType type,bool align_parent = false);
    bool align_to_y_max(bool align_parent = false);
    bool align_to_y_min(bool align_parent = false);
    bool align_to_x_max(bool align_parent = false);
    bool align_to_x_min(bool align_parent = false);
    bool align_to_z_max(bool align_parent = false);
    bool align_to_z_min(bool align_parent = false);

    bool distribute_x();
    bool distribute_y();
    bool distribute_z();

    bool can_align(AlignType type, bool align_parent = false) const;
    bool can_distribute(AlignType type) const;

    std::vector<ObjectInfo> get_selected_objects_info(BoundingBoxf3 &big_bb) const;
    double                  get_current_coord(std::string operation_name, BoundingBoxf3 &bbox);
    Vec3d                   generate_displacement(std::string operation_name, double offset);

    // A parts selection (one instance of one object) aligns the parts inside it; anything else aligns whole instances.
    // With align_parent, only a Volume-mode selection counts as parts: a whole instance aligns to its plate instead.
    bool                          is_parts_selection(bool align_parent) const;
    // The volumes a parts operation moves: the selected volumes, or only the model parts when a whole instance is
    // selected. Empty for a cut object, whose parts must keep their place.
    std::vector<const GLVolume *> get_selected_part_volumes() const;
    // World bounding box of the items an operation moves, the reference of every non-parent operation.
    BoundingBoxf3                 get_items_box() const;
    // World bounding box aligned to with align_parent: the instance of a parts selection, or the plate holding the
    // selected instances, up to their top.
    BoundingBoxf3                 get_parent_box() const;
    // Number of items the operation would move, or 0 when it cannot move them; why_not then gets the reason, if any.
    size_t                        get_items_count(AlignType type, bool align_parent, std::string *why_not = nullptr) const;

private:
    GLCanvas3D& m_canvas;

    template<typename GetCoordFunc, typename SetCoordFunc>
    bool align_objects_generic(GetCoordFunc get_coord, SetCoordFunc set_coord, const std::string &operation_name, bool align_parent = false);

    template<typename GetCoordFunc>
    bool distribute_objects_generic(GetCoordFunc get_coord, int axis,
                                  const std::string& operation_name);

    Selection& get_selection() const;
    bool take_snapshot(const std::string& name);
    void apply_transformation(int obj_idx, int inst_idx, const Vec3d& displacement);
    void       finish_operation(bool force_volume_move = false);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoAlignment_hpp_