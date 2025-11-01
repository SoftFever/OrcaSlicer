#ifndef slic3r_Format_DRC_hpp_
#define slic3r_Format_DRC_hpp_

namespace Slic3r {

class TriangleMesh;
class ModelObject;
class Model;

// Load a Draco file into a provided model.
extern bool load_drc(const char *path, TriangleMesh *meshptr);
extern bool load_drc(const char *path, Model *model, const char *object_name = nullptr);

extern bool store_drc(const char *path, TriangleMesh *mesh, int bits, int speed);
extern bool store_drc(const char *path, ModelObject *model_object, int bits, int speed);
extern bool store_drc(const char *path, Model *model, int bits, int speed);

}; // namespace Slic3r

#endif /* slic3r_Format_DRC_hpp_ */
