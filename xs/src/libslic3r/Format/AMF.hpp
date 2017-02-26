#ifndef slic3r_Format_AMF_hpp_
#define slic3r_Format_AMF_hpp_

namespace Slic3r {

class Model;

// Load an AMF file into a provided model.
extern bool load_amf(const char *path, Model *model);

extern bool store_amf(const char *path, Model *model);

}; // namespace Slic3r

#endif /* slic3r_Format_AMF_hpp_ */