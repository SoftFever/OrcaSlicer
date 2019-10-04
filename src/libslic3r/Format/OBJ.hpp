#ifndef slic3r_Format_OBJ_hpp_
#define slic3r_Format_OBJ_hpp_

namespace Slic3r {

class TriangleMesh;
class Model;
class ModelObject;

// Load an OBJ file into a provided model.
extern bool load_obj(const char *path, TriangleMesh *mesh);
extern bool load_obj(const char *path, Model *model, const char *object_name = nullptr);

extern bool store_obj(const char *path, TriangleMesh *mesh);
extern bool store_obj(const char *path, ModelObject *model);
extern bool store_obj(const char *path, Model *model);

}; // namespace Slic3r

#endif /* slic3r_Format_OBJ_hpp_ */
