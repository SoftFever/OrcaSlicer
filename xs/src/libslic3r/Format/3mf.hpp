#ifndef slic3r_Format_3mf_hpp_
#define slic3r_Format_3mf_hpp_

namespace Slic3r {

    class Model;
    class Print;
    class PresetBundle;

    // Load the content of a 3mf file into the given model and preset bundle.
    extern bool load_3mf(const char* path, PresetBundle* bundle, Model* model);

    // Save the given model and the config data contained in the given Print into a 3mf file.
    // The model could be modified during the export process if meshes are not repaired or have no shared vertices
    extern bool store_3mf(const char* path, Model* model, Print* print);

}; // namespace Slic3r

#endif /* slic3r_Format_3mf_hpp_ */
