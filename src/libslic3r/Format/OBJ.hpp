#ifndef slic3r_Format_OBJ_hpp_
#define slic3r_Format_OBJ_hpp_

namespace Slic3r {

class TriangleMesh;
class Model;

// Load an OBJ file into a provided model.
extern bool load_obj(const char *path, Model *model, const char *object_name = nullptr);

}; // namespace Slic3r

#endif /* slic3r_Format_OBJ_hpp_ */
