#ifndef slic3r_Format_AMF_hpp_
#define slic3r_Format_AMF_hpp_

namespace Slic3r {

class Model;
class Print;

// Load the content of an amf file into the given model and configuration.
extern bool load_amf(const char *path, DynamicPrintConfig *config, Model *model);

// Save the given model and the config data contained in the given Print into an amf file.
// The model could be modified during the export process if meshes are not repaired or have no shared vertices
extern bool store_amf(const char *path, Model *model, Print* print, bool export_print_config);

}; // namespace Slic3r

#endif /* slic3r_Format_AMF_hpp_ */
