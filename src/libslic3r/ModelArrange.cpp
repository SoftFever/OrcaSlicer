#include "ModelArrange.hpp"
#include "MTUtils.hpp"

namespace Slic3r {

arrangement::ArrangePolygons get_arrange_polys(const Model &model, ModelInstancePtrs &instances)
{
    size_t count = 0;
    for (auto obj : model.objects) count += obj->instances.size();
    
    ArrangePolygons input;
    input.reserve(count);
    instances.clear(); instances.reserve(count);
    for (ModelObject *mo : model.objects)
        for (ModelInstance *minst : mo->instances) {
            input.emplace_back(minst->get_arrange_polygon());
            instances.emplace_back(minst);
        }
    
    return input;
}

bool apply_arrange_polys(ArrangePolygons &input, ModelInstancePtrs &instances, VirtualBedFn vfn)
{
    bool ret = true;
    
    for(size_t i = 0; i < input.size(); ++i) {
        if (input[i].bed_idx != 0) { ret = false; if (vfn) vfn(input[i]); }
        if (input[i].bed_idx >= 0)
            instances[i]->apply_arrange_result(input[i].translation,
                                               input[i].rotation);
    }
    
    return ret;
}

Slic3r::arrangement::ArrangePolygon get_arrange_poly(const Model &model)
{
    ArrangePolygon ap;
    Points &apts = ap.poly.contour.points;
    for (const ModelObject *mo : model.objects)
        for (const ModelInstance *minst : mo->instances) {
            ArrangePolygon obj_ap = minst->get_arrange_polygon();
            ap.poly.contour.rotate(obj_ap.rotation);
            ap.poly.contour.translate(obj_ap.translation.x(), obj_ap.translation.y());
            const Points &pts = obj_ap.poly.contour.points;
            std::copy(pts.begin(), pts.end(), std::back_inserter(apts));
        }
    
    apts = Geometry::convex_hull(apts);
    return ap;
}

void duplicate(Model &model, Slic3r::arrangement::ArrangePolygons &copies, VirtualBedFn vfn)
{
    for (ModelObject *o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        o->instances.clear();
        for (const ModelInstance *i : instances) {
            for (arrangement::ArrangePolygon &ap : copies) {
                if (ap.bed_idx != 0) vfn(ap);
                ModelInstance *instance = o->add_instance(*i);
                Vec2d pos = unscale(ap.translation);
                instance->set_offset(instance->get_offset() + to_3d(pos, 0.));
            }
        }
        o->invalidate_bounding_box();
    }
}

void duplicate_objects(Model &model, size_t copies_num)
{
    for (ModelObject *o : model.objects) {
        // make a copy of the pointers in order to avoid recursion when appending their copies
        ModelInstancePtrs instances = o->instances;
        for (const ModelInstance *i : instances)
            for (size_t k = 2; k <= copies_num; ++ k)
                o->add_instance(*i);
    }
}

} // namespace Slic3r
