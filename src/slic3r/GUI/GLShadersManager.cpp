#include "libslic3r/libslic3r.h"
#include "GLShadersManager.hpp"
#include "3DScene.hpp"

#include <cassert>
#include <algorithm>

#include <GL/glew.h>

namespace Slic3r {

std::pair<bool, std::string> GLShadersManager::init()
{
    std::string error;

    auto append_shader = [this, &error](const std::string& name, const GLShaderProgram::ShaderFilenames& filenames) {
        m_shaders.push_back(std::make_unique<GLShaderProgram>());
        if (!m_shaders.back()->init_from_files(name, filenames)) {
            error += name + "\n";
            // if any error happens while initializating the shader, we remove it from the list
            m_shaders.pop_back();
            return false;
        }
        return true;
    };

    assert(m_shaders.empty());

    bool valid = true;

    // used to render bed axes and model, selection hints, gcode sequential view marker model
    valid &= append_shader("gouraud_light", { "gouraud_light.vs", "gouraud_light.fs" });
    // used to render printbed
    valid &= append_shader("printbed", { "printbed.vs", "printbed.fs" });
    // used to render options in gcode preview
    valid &= append_shader("options_110", { "options_110.vs", "options_110.fs" });
    valid &= append_shader("options_120", { "options_120.vs", "options_120.fs" });
    // used to render extrusion paths in gcode preview
    valid &= append_shader("extrusions", { "extrusions.vs", "extrusions.fs" });
    // used to render travel paths in gcode preview
    valid &= append_shader("travels", { "travels.vs", "travels.fs" });
    // used to render shells in gcode preview
    valid &= append_shader("shells", { "shells.vs", "shells.fs" });
    // used to render objects in 3d editor
    valid &= append_shader("gouraud", { "gouraud.vs", "gouraud.fs" });
    // used to render variable layers heights in 3d editor
    valid &= append_shader("variable_layer_height", { "variable_layer_height.vs", "variable_layer_height.fs" });

    return { valid, error };
}

void GLShadersManager::shutdown()
{
    for (std::unique_ptr<GLShaderProgram>& shader : m_shaders) {
        shader.reset();
    }
}

GLShaderProgram* GLShadersManager::get_shader(const std::string& shader_name)
{
    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [shader_name](std::unique_ptr<GLShaderProgram>& p) { return p->get_name() == shader_name; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

GLShaderProgram* GLShadersManager::get_current_shader()
{
    GLint id = 0;
    glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &id));
    if (id == 0)
        return nullptr;

    auto it = std::find_if(m_shaders.begin(), m_shaders.end(), [id](std::unique_ptr<GLShaderProgram>& p) { return p->get_id() == id; });
    return (it != m_shaders.end()) ? it->get() : nullptr;
}

} // namespace Slic3r

