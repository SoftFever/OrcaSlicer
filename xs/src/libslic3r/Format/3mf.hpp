#ifndef slic3r_Format_3mf_hpp_
#define slic3r_Format_3mf_hpp_

namespace Slic3r {

    class Model;

    // Load an 3mf file into a provided model.
    extern bool load_3mf(const char* path, Model* model, const char* object_name = nullptr);
}; // namespace Slic3r

#endif /* slic3r_Format_3mf_hpp_ */
