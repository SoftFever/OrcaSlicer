#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include <libslic3r/Arrange.hpp>

namespace Slic3r {

class Model;
class ModelInstance;
using ModelInstancePtrs = std::vector<ModelInstance*>;

using arrangement::ArrangePolygon;
using arrangement::ArrangePolygons;
using arrangement::ArrangeParams;
using arrangement::InfiniteBed;
using arrangement::CircleBed;

// Do something with ArrangePolygons in virtual beds
using VirtualBedFn = std::function<void(arrangement::ArrangePolygon&)>;

[[noreturn]] inline void throw_if_out_of_bed(arrangement::ArrangePolygon&) 
{
    throw std::runtime_error("Objects could not fit on the bed");
}

ArrangePolygons get_arrange_polys(const Model &model, ModelInstancePtrs &instances);
ArrangePolygon  get_arrange_poly(const Model &model);
bool apply_arrange_polys(ArrangePolygons &polys, ModelInstancePtrs &instances, VirtualBedFn);

void duplicate(Model &model, ArrangePolygons &copies, VirtualBedFn);
void duplicate_objects(Model &model, size_t copies_num);

template<class TBed>
bool arrange_objects(Model &              model,
                     const TBed &         bed,
                     const ArrangeParams &params,
                     VirtualBedFn         vfn = throw_if_out_of_bed)
{
    ModelInstancePtrs instances;
    auto&& input = get_arrange_polys(model, instances);
    arrangement::arrange(input, bed, params);
    
    return apply_arrange_polys(input, instances, vfn);
}

template<class TBed>
void duplicate(Model &              model,
               size_t               copies_num,
               const TBed &         bed,
               const ArrangeParams &params,
               VirtualBedFn         vfn = throw_if_out_of_bed)
{
    ArrangePolygons copies(copies_num, get_arrange_poly(model));
    arrangement::arrange(copies, bed, params);
    duplicate(model, copies, vfn);
}

template<class TBed>
void duplicate_objects(Model &              model,
                       size_t               copies_num,
                       const TBed &         bed,
                       const ArrangeParams &params,
                       VirtualBedFn         vfn = throw_if_out_of_bed)
{
    duplicate_objects(model, copies_num);
    arrange_objects(model, bed, params, vfn);
}

}

#endif // MODELARRANGE_HPP
