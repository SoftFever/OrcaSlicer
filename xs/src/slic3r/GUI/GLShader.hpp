#ifndef slic3r_GLShader_hpp_
#define slic3r_GLShader_hpp_

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Point.hpp"

namespace Slic3r {

class GLShader
{
public:
    GLShader() :
        fragment_program_id(0),
        vertex_program_id(0),
        shader_program_id(0)
        {}
    ~GLShader();

    bool load_from_text(const char *fragment_shader, const char *vertex_shader);
    bool load_from_file(const char* fragment_shader_filename, const char* vertex_shader_filename);

    void release();

    int  get_attrib_location(const char *name) const;
    int  get_uniform_location(const char *name) const;

    bool set_uniform(const char *name, float value) const;
    bool set_uniform(const char* name, const float* matrix) const;

    void enable() const;
    void disable() const;

    unsigned int    fragment_program_id;
    unsigned int    vertex_program_id;
    unsigned int    shader_program_id;
    std::string     last_error;
};

}

#endif /* slic3r_GLShader_hpp_ */
