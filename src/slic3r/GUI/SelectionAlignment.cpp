#include "SelectionAlignment.hpp"

#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"
#include "I18N.hpp"

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace SelectionAlignment {

namespace {

constexpr double EPSILON = 1e-6;

// One movable entity: either a whole object instance or a single part.
struct Item
{
    int           object_idx{ -1 };
    int           instance_idx{ -1 };
    int           volume_idx{ -1 }; // -1 for a whole instance
    BoundingBoxf3 bbox;

    bool is_part() const { return volume_idx >= 0; }
};

// Collects the selected instances (instance mode) or parts (volume mode). Returns an empty list for selections
// that cannot be aligned (nothing selected, wipe tower, mixed selections).
std::vector<Item> collect_items(const Selection& selection)
{
    std::vector<Item> items;
    if (selection.is_empty() || selection.is_wipe_tower())
        return items;

    if (selection.is_single_full_instance() || selection.is_multiple_full_instance()) {
        // Build per-instance bounding boxes of the model parts (modifiers do not count).
        std::map<std::pair<int, int>, BoundingBoxf3> boxes;
        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* v = selection.get_volume(idx);
            if (v == nullptr || v->is_modifier || v->object_idx() < 0 || v->instance_idx() < 0)
                continue;
            boxes[{ v->object_idx(), v->instance_idx() }].merge(v->transformed_convex_hull_bounding_box());
        }
        for (const auto& [id, bbox] : boxes) {
            Item item;
            item.object_idx   = id.first;
            item.instance_idx = id.second;
            item.bbox         = bbox;
            items.push_back(item);
        }
    } else if (selection.is_single_volume_or_modifier() || selection.is_multiple_volume() || selection.is_multiple_modifier()) {
        for (unsigned int idx : selection.get_volume_idxs()) {
            const GLVolume* v = selection.get_volume(idx);
            if (v == nullptr || v->object_idx() < 0 || v->instance_idx() < 0 || v->volume_idx() < 0)
                return {};
            Item item;
            item.object_idx   = v->object_idx();
            item.instance_idx = v->instance_idx();
            item.volume_idx   = v->volume_idx();
            item.bbox         = v->transformed_convex_hull_bounding_box();
            items.push_back(item);
        }
    }
    return items;
}

bool axis_allowed(const std::vector<Item>& items, int axis)
{
    if (axis < 0 || axis > 2)
        return false;
    // Whole objects always sit on the bed, so moving them along Z makes no sense.
    return axis != 2 || (!items.empty() && items.front().is_part());
}

double edge_or_center(const BoundingBoxf3& bb, int axis, Mode mode)
{
    switch (mode) {
    case Mode::Min: return bb.min[axis];
    case Mode::Max: return bb.max[axis];
    default:        return 0.5 * (bb.min[axis] + bb.max[axis]);
    }
}

// Moves one item by a displacement given in world coordinates.
void move_item(Selection& selection, const Item& item, int axis, double delta)
{
    if (std::abs(delta) < EPSILON)
        return;

    Vec3d world = Vec3d::Zero();
    world[axis] = delta;

    if (!item.is_part()) {
        selection.translate(item.object_idx, item.instance_idx, world);
        return;
    }

    // Volume offsets are stored in the instance coordinate system, so undo the instance rotation, scale and mirror.
    for (unsigned int idx : selection.get_volume_idxs()) {
        const GLVolume* v = selection.get_volume(idx);
        if (v != nullptr && v->object_idx() == item.object_idx && v->instance_idx() == item.instance_idx &&
            v->volume_idx() == item.volume_idx) {
            const Vec3d local = v->get_instance_transformation().get_matrix_no_offset().inverse() * world;
            selection.translate(item.object_idx, item.instance_idx, item.volume_idx, local);
            return;
        }
    }
}

} // namespace

bool can_align(const Selection& selection, int axis)
{
    const std::vector<Item> items = collect_items(selection);
    if (!axis_allowed(items, axis))
        return false;
    return items.size() >= 2 || (items.size() == 1 && items.front().is_part());
}

bool can_distribute(const Selection& selection, int axis)
{
    const std::vector<Item> items = collect_items(selection);
    return axis_allowed(items, axis) && items.size() >= 3;
}

bool align(GLCanvas3D& canvas, int axis, Mode mode)
{
    Selection&        selection = canvas.get_selection();
    std::vector<Item> items     = collect_items(selection);
    if (!axis_allowed(items, axis))
        return false;

    // The reference is the bounding box of the whole selection; a single part is aligned to its object instead.
    BoundingBoxf3 reference;
    if (items.size() >= 2) {
        for (const Item& item : items)
            reference.merge(item.bbox);
    } else if (items.size() == 1 && items.front().is_part()) {
        const Model* model = selection.get_model();
        const Item&  item  = items.front();
        if (model == nullptr || item.object_idx >= static_cast<int>(model->objects.size()))
            return false;
        const ModelObject* object = model->objects[item.object_idx];
        if (item.instance_idx >= static_cast<int>(object->instances.size()))
            return false;
        reference = object->instance_bounding_box(static_cast<size_t>(item.instance_idx));
    } else
        return false;

    if (!reference.defined)
        return false;

    const double target = edge_or_center(reference, axis, mode);
    bool         moved  = false;
    for (const Item& item : items) {
        const double delta = target - edge_or_center(item.bbox, axis, mode);
        if (std::abs(delta) >= EPSILON) {
            move_item(selection, item, axis, delta);
            moved = true;
        }
    }
    if (!moved)
        return false;

    // Marked for translation; the undo history translates the name when the snapshot is taken.
    static const char* const names[3][3] = {
        { L("Align X Min"), L("Align X Center"), L("Align X Max") },
        { L("Align Y Min"), L("Align Y Center"), L("Align Y Max") },
        { L("Align Z Min"), L("Align Z Center"), L("Align Z Max") },
    };
    canvas.do_move(names[axis][static_cast<int>(mode)]);
    return true;
}

bool distribute(GLCanvas3D& canvas, int axis)
{
    Selection&        selection = canvas.get_selection();
    std::vector<Item> items     = collect_items(selection);
    if (!axis_allowed(items, axis) || items.size() < 3)
        return false;

    // Keep the two outermost items in place and space the centers of the others evenly between them.
    std::sort(items.begin(), items.end(), [axis](const Item& a, const Item& b) {
        return edge_or_center(a.bbox, axis, Mode::Center) < edge_or_center(b.bbox, axis, Mode::Center);
    });

    const double first = edge_or_center(items.front().bbox, axis, Mode::Center);
    const double last  = edge_or_center(items.back().bbox, axis, Mode::Center);
    const double step  = (last - first) / double(items.size() - 1);

    bool moved = false;
    for (size_t i = 1; i + 1 < items.size(); ++i) {
        const double delta = (first + step * double(i)) - edge_or_center(items[i].bbox, axis, Mode::Center);
        if (std::abs(delta) >= EPSILON) {
            move_item(selection, items[i], axis, delta);
            moved = true;
        }
    }
    if (!moved)
        return false;

    static const char* const names[3] = { L("Distribute X"), L("Distribute Y"), L("Distribute Z") };
    canvas.do_move(names[axis]);
    return true;
}

} // namespace SelectionAlignment
} // namespace GUI
} // namespace Slic3r
