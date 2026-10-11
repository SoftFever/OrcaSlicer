#ifndef slic3r_ModelObjectRepair_hpp_
#define slic3r_ModelObjectRepair_hpp_

#include <functional>
#include <string>

namespace Slic3r {

class ModelObject;

// Orca: What repair_model_object() could not fix.
struct ModelObjectRepairReport
{
    // A repaired part still intersects itself: its shells overlap in a way the mesh booleans cannot resolve.
    bool self_intersections_remain = false;
};

// Orca: Repairs the volumes of a model object with CGAL ("Fix model"): every volume when volume_idx is -1, otherwise that one.
// A volume without open edges and self-intersections is left as it is. A broken volume made of disconnected shells is
// split into parts first. Parts that enclose no volume are dropped and parts with open edges or self-intersections are
// repaired. With keep_painting, the paint of split and repaired parts is remapped onto their new meshes, otherwise it is
// removed from the repaired parts. on_progress receives a translated message and a percentage. throw_on_cancel is called
// before each volume and part and aborts the repair by throwing, leaving the object partly repaired: repair a copy to
// keep the object unchanged, see apply_repaired_volumes(). Throws Slic3r::InvalidArgument when volume_idx is neither -1
// nor a volume, and Slic3r::RuntimeError when no part of the object encloses a volume (the object is left unchanged),
// when a part cannot be repaired, or when dropping a part would leave the object without volumes.
ModelObjectRepairReport repair_model_object(ModelObject                                                      &object,
                                            int                                                               volume_idx,
                                            bool                                                              keep_painting,
                                            const std::function<void(const std::string &message, int percent)> &on_progress,
                                            const std::function<void()>                                      &throw_on_cancel);

// Orca: Makes object look like repaired, a copy of it (Model::add_object()) that repair_model_object() has repaired, so
// that the repair can run on another thread while object stays unchanged. The volumes whose mesh and transformation the
// repair left unchanged stay the very volumes of object, the others are replaced by copies of the repaired ones, as is
// the last volume whose transformation dropping the other volumes moved into the instances; the instance
// transformations are copied too.
void apply_repaired_volumes(ModelObject &object, const ModelObject &repaired);

} // namespace Slic3r

#endif // slic3r_ModelObjectRepair_hpp_
