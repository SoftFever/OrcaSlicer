// Calculate extents of the extrusions assigned to Print / PrintObject.
// The extents are used for assessing collisions of the print with the priming towers,
// to decide whether to pause the print after the priming towers are extruded
// to let the operator remove them from the print bed.

#include "../BoundingBox.hpp"
#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Print.hpp"

#include "PrintExtents.hpp"
#include "WipeTower.hpp"

namespace Slic3r {

static inline BoundingBox extrusion_polyline_extents(const Polyline &polyline, const coord_t radius)
{
    BoundingBox bbox;
    if (! polyline.points.empty())
        bbox.merge(polyline.points.front());
    for (const Point &pt : polyline.points) {
        bbox.min.x = std::min(bbox.min.x, pt.x - radius);
        bbox.min.y = std::min(bbox.min.y, pt.y - radius);
        bbox.max.x = std::max(bbox.max.x, pt.x + radius);
        bbox.max.y = std::max(bbox.max.y, pt.y + radius);
    }
    return bbox;
}

static inline BoundingBoxf extrusionentity_extents(const ExtrusionPath &extrusion_path)
{
    BoundingBox bbox = extrusion_polyline_extents(extrusion_path.polyline, scale_(0.5 * extrusion_path.width));
    BoundingBoxf bboxf;
    if (! empty(bbox)) {
        bboxf.min = Pointf::new_unscale(bbox.min);
        bboxf.max = Pointf::new_unscale(bbox.max);
		bboxf.defined = true;
    }
    return bboxf;
}

static inline BoundingBoxf extrusionentity_extents(const ExtrusionLoop &extrusion_loop)
{
    BoundingBox bbox;
    for (const ExtrusionPath &extrusion_path : extrusion_loop.paths)
        bbox.merge(extrusion_polyline_extents(extrusion_path.polyline, scale_(0.5 * extrusion_path.width)));
    BoundingBoxf bboxf;
    if (! empty(bbox)) {
        bboxf.min = Pointf::new_unscale(bbox.min);
        bboxf.max = Pointf::new_unscale(bbox.max);
		bboxf.defined = true;
	}
    return bboxf;
}

static inline BoundingBoxf extrusionentity_extents(const ExtrusionMultiPath &extrusion_multi_path)
{
    BoundingBox bbox;
    for (const ExtrusionPath &extrusion_path : extrusion_multi_path.paths)
        bbox.merge(extrusion_polyline_extents(extrusion_path.polyline, scale_(0.5 * extrusion_path.width)));
    BoundingBoxf bboxf;
    if (! empty(bbox)) {
        bboxf.min = Pointf::new_unscale(bbox.min);
        bboxf.max = Pointf::new_unscale(bbox.max);
		bboxf.defined = true;
	}
    return bboxf;
}

static BoundingBoxf extrusionentity_extents(const ExtrusionEntity *extrusion_entity);

static inline BoundingBoxf extrusionentity_extents(const ExtrusionEntityCollection &extrusion_entity_collection)
{
    BoundingBoxf bbox;
    for (const ExtrusionEntity *extrusion_entity : extrusion_entity_collection.entities)
        bbox.merge(extrusionentity_extents(extrusion_entity));
    return bbox;
}

static BoundingBoxf extrusionentity_extents(const ExtrusionEntity *extrusion_entity)
{
    if (extrusion_entity == nullptr)
        return BoundingBoxf();
    auto *extrusion_path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);
    if (extrusion_path != nullptr)
        return extrusionentity_extents(*extrusion_path);
    auto *extrusion_loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);
    if (extrusion_loop != nullptr)
        return extrusionentity_extents(*extrusion_loop);
    auto *extrusion_multi_path = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);
    if (extrusion_multi_path != nullptr)
        return extrusionentity_extents(*extrusion_multi_path);
    auto *extrusion_entity_collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);
    if (extrusion_entity_collection != nullptr)
        return extrusionentity_extents(*extrusion_entity_collection);
    CONFESS("Unexpected extrusion_entity type in extrusionentity_extents()");
    return BoundingBoxf();
}

BoundingBoxf get_print_extrusions_extents(const Print &print)
{
    BoundingBoxf bbox(extrusionentity_extents(print.brim));
    bbox.merge(extrusionentity_extents(print.skirt));
    return bbox;
}

BoundingBoxf get_print_object_extrusions_extents(const PrintObject &print_object, const coordf_t max_print_z)
{
    BoundingBoxf bbox;
    for (const Layer *layer : print_object.layers) {
        if (layer->print_z > max_print_z)
            break;
        BoundingBoxf bbox_this;
        for (const LayerRegion *layerm : layer->regions) {
            bbox_this.merge(extrusionentity_extents(layerm->perimeters));
            for (const ExtrusionEntity *ee : layerm->fills.entities)
                // fill represents infill extrusions of a single island.
                bbox_this.merge(extrusionentity_extents(*dynamic_cast<const ExtrusionEntityCollection*>(ee)));
        }
        const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(layer);
        if (support_layer)
            for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
                bbox_this.merge(extrusionentity_extents(extrusion_entity));
        for (const Point &offset : print_object._shifted_copies) {
            BoundingBoxf bbox_translated(bbox_this);
            bbox_translated.translate(Pointf::new_unscale(offset));
            bbox.merge(bbox_translated);
        }
    }
    return bbox;
}

// Returns a bounding box of a projection of the wipe tower for the layers <= max_print_z.
// The projection does not contain the priming regions.
BoundingBoxf get_wipe_tower_extrusions_extents(const Print &print, const coordf_t max_print_z)
{
    // Wipe tower extrusions are saved as if the tower was at the origin with no rotation
    // We need to get position and angle of the wipe tower to transform them to actual position.
    Pointf wipe_tower_pos(print.config.wipe_tower_x.value, print.config.wipe_tower_y.value);
    float wipe_tower_angle = print.config.wipe_tower_rotation_angle.value;

    BoundingBoxf bbox;
    for (const std::vector<WipeTower::ToolChangeResult> &tool_changes : print.m_wipe_tower_tool_changes) {
        if (! tool_changes.empty() && tool_changes.front().print_z > max_print_z)
            break;
        for (const WipeTower::ToolChangeResult &tcr : tool_changes) {
            for (size_t i = 1; i < tcr.extrusions.size(); ++ i) {
                const WipeTower::Extrusion &e = tcr.extrusions[i];
                if (e.width > 0) {
                    Pointf  p1((&e - 1)->pos.x, (&e - 1)->pos.y);
                    Pointf  p2(e.pos.x, e.pos.y);
                    p1.rotate(wipe_tower_angle);
                    p1.translate(wipe_tower_pos);
                    p2.rotate(wipe_tower_angle);
                    p2.translate(wipe_tower_pos);

                    bbox.merge(p1);
                    coordf_t radius = 0.5 * e.width;
                    bbox.min.x = std::min(bbox.min.x, std::min(p1.x, p2.x) - radius);
                    bbox.min.y = std::min(bbox.min.y, std::min(p1.y, p2.y) - radius);
                    bbox.max.x = std::max(bbox.max.x, std::max(p1.x, p2.x) + radius);
                    bbox.max.y = std::max(bbox.max.y, std::max(p1.y, p2.y) + radius);
                }
            }
        }
    }
    return bbox;
}

// Returns a bounding box of the wipe tower priming extrusions.
BoundingBoxf get_wipe_tower_priming_extrusions_extents(const Print &print)
{
    BoundingBoxf bbox;
    if (print.m_wipe_tower_priming) {
        const WipeTower::ToolChangeResult &tcr = *print.m_wipe_tower_priming.get();
        for (size_t i = 1; i < tcr.extrusions.size(); ++ i) {
            const WipeTower::Extrusion &e = tcr.extrusions[i];
            if (e.width > 0) {
                Pointf  p1((&e - 1)->pos.x, (&e - 1)->pos.y);
                Pointf  p2(e.pos.x, e.pos.y);
                bbox.merge(p1);
                coordf_t radius = 0.5 * e.width;
                bbox.min.x = std::min(bbox.min.x, std::min(p1.x, p2.x) - radius);
                bbox.min.y = std::min(bbox.min.y, std::min(p1.y, p2.y) - radius);
                bbox.max.x = std::max(bbox.max.x, std::max(p1.x, p2.x) + radius);
                bbox.max.y = std::max(bbox.max.y, std::max(p1.y, p2.y) + radius);
            }
        }
    }
    return bbox;
}

}
