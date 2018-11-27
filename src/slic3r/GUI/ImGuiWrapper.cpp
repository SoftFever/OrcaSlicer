#include "ImGuiWrapper.hpp"

#include <cstdio>
#include <vector>

#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <wx/string.h>
#include <wx/event.h>
#include <wx/debug.h>

#include <GL/glew.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"

namespace Slic3r {
namespace GUI {


ImGuiWrapper::ImGuiWrapper()
    : m_glsl_version_string("")
    , m_glsl_version(0)
    , m_shader_handle(0)
    , m_vert_handle(0)
    , m_frag_handle(0)
    , m_font_texture(0)
    , m_vbo_handle(0)
    , m_elements_handle(0)
    , m_attrib_location_tex(0)
    , m_attrib_location_proj_mtx(0)
    , m_attrib_location_position(0)
    , m_attrib_location_uv(0)
    , m_attrib_location_color(0)
    , m_mouse_buttons(0)
{
}

ImGuiWrapper::~ImGuiWrapper()
{
    destroy_device_objects();
    ImGui::DestroyContext();
}

bool ImGuiWrapper::init()
{
    // Store GLSL version string so we can refer to it later in case we recreate shaders. Note: GLSL version is NOT the same as GL version. Leave this to NULL if unsure.
    // std::string glsl_version;

// #ifdef USE_GL_ES3
//     glsl_version = "#version 300 es";
// #else
//     glsl_version = "#version 130";
// #endif

//     m_glsl_version_string = glsl_version + "\n";

    // const GLubyte *glsl_version_string = glGetString(GL_SHADING_LANGUAGE_VERSION);
    // if (glsl_version_string != nullptr) {
    //     unsigned v_maj = 0;
    //     unsigned v_min = 0;
    //     if (std::sscanf(reinterpret_cast<const char*>(glsl_version_string), "%u.%u", &v_maj, &v_min) == 2) {
    //         m_glsl_version = 100 * v_maj + v_min % 100;
    //     }
    // }

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    ImFont* font = io.Fonts->AddFontFromFileTTF((Slic3r::resources_dir() + "/fonts/NotoSans-Regular.ttf").c_str(), 18.0f);
    if (font == nullptr) {
        font = io.Fonts->AddFontDefault();
        if (font == nullptr)
            return false;
    }
    else {
        m_fonts.insert(FontsMap::value_type("Noto Sans Regular 18", font));
    }

    io.IniFilename = nullptr;

    return true;
}

void ImGuiWrapper::read_glsl_version()
{
    const GLubyte *glsl_version_string = glGetString(GL_SHADING_LANGUAGE_VERSION);
    wxCHECK_RET(glsl_version_string != nullptr, "Could not get GLSL version string, glGetString(GL_SHADING_LANGUAGE_VERSION) failed");

    unsigned v_maj = 0;
    unsigned v_min = 0;
    int res = std::sscanf(reinterpret_cast<const char*>(glsl_version_string), "%u.%u", &v_maj, &v_min);
    wxCHECK_RET(res == 2, wxString::Format("Could not parse GLSL version: %s", glsl_version_string));

    m_glsl_version = 100 * v_maj + v_min % 100;
    m_glsl_version_string = (boost::format("#version %1%") % m_glsl_version).str();
}

void ImGuiWrapper::set_display_size(float w, float h)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(w, h);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

bool ImGuiWrapper::update_mouse_data(wxMouseEvent& evt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.MousePos = ImVec2((float)evt.GetX(), (float)evt.GetY());
    io.MouseDown[0] = evt.LeftDown();
    io.MouseDown[1] = evt.RightDown();
    io.MouseDown[2] = evt.MiddleDown();

    unsigned buttons = (evt.LeftDown() ? 1 : 0) | (evt.RightDown() ? 2 : 0) | (evt.MiddleDown() ? 4 : 0);
    bool res = buttons != m_mouse_buttons;
    m_mouse_buttons = buttons;
    return res;
}

void ImGuiWrapper::new_frame()
{
    if (m_font_texture == 0)
        create_device_objects();

    ImGui::NewFrame();
}

void ImGuiWrapper::render()
{
    ImGui::Render();
    render_draw_data(ImGui::GetDrawData());
}

void ImGuiWrapper::set_next_window_pos(float x, float y, int flag)
{
    ImGui::SetNextWindowPos(ImVec2(x, y), (ImGuiCond)flag);
}

void ImGuiWrapper::set_next_window_bg_alpha(float alpha)
{
    ImGui::SetNextWindowBgAlpha(alpha);
}

bool ImGuiWrapper::begin(const std::string &name, int flags)
{
    return ImGui::Begin(name.c_str(), nullptr, (ImGuiWindowFlags)flags);
}

bool ImGuiWrapper::begin(const wxString &name, int flags)
{
    return begin(into_u8(name), flags);
}

void ImGuiWrapper::end()
{
    ImGui::End();
}

bool ImGuiWrapper::button(const wxString &label)
{
    auto label_utf8 = into_u8(label);
    return ImGui::Button(label_utf8.c_str());
}

bool ImGuiWrapper::input_double(const std::string &label, const double &value, const std::string &format)
{
    return ImGui::InputDouble(label.c_str(), const_cast<double*>(&value), 0.0f, 0.0f, format.c_str());
}

bool ImGuiWrapper::input_vec3(const std::string &label, const Vec3d &value, float width, const std::string &format)
{
    bool value_changed = false;

    ImGui::BeginGroup();

    for (int i = 0; i < 3; ++i)
    {
        std::string item_label = (i == 0) ? "X" : ((i == 1) ? "Y" : "Z");
        ImGui::PushID(i);
        ImGui::PushItemWidth(width);
        value_changed |= ImGui::InputDouble(item_label.c_str(), const_cast<double*>(&value(i)), 0.0f, 0.0f, format.c_str());
        ImGui::PopID();
    }
    ImGui::EndGroup();

    return value_changed;
}

bool ImGuiWrapper::checkbox(const wxString &label, bool &value)
{
    auto label_utf8 = into_u8(label);
    return ImGui::Checkbox(label_utf8.c_str(), &value);
}

void ImGuiWrapper::text(const wxString &label)
{
    auto label_utf8 = into_u8(label);
    ImGui::Text(label_utf8.c_str(), NULL);
}

bool ImGuiWrapper::want_mouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiWrapper::want_keyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiWrapper::want_text_input() const
{
    return ImGui::GetIO().WantTextInput;
}

bool ImGuiWrapper::want_any_input() const
{
    const auto io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput;
}

void ImGuiWrapper::create_device_objects()
{
    // // Backup GL state
    // GLint last_texture, last_array_buffer, last_vertex_array;
    // glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    // glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    // glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);

    // // Parse GLSL version string
    // // int glsl_version = 130;
    // // ::sscanf(m_glsl_version_string.c_str(), "#version %d", &glsl_version);
    // read_glsl_version();

    // const GLchar* vertex_shader_glsl_120 =
    //     "uniform mat4 ProjMtx;\n"
    //     "attribute vec2 Position;\n"
    //     "attribute vec2 UV;\n"
    //     "attribute vec4 Color;\n"
    //     "varying vec2 Frag_UV;\n"
    //     "varying vec4 Frag_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Frag_UV = UV;\n"
    //     "    Frag_Color = Color;\n"
    //     "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    //     "}\n";

    // const GLchar* vertex_shader_glsl_130 =
    //     "uniform mat4 ProjMtx;\n"
    //     "in vec2 Position;\n"
    //     "in vec2 UV;\n"
    //     "in vec4 Color;\n"
    //     "out vec2 Frag_UV;\n"
    //     "out vec4 Frag_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Frag_UV = UV;\n"
    //     "    Frag_Color = Color;\n"
    //     "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    //     "}\n";

    // const GLchar* vertex_shader_glsl_300_es =
    //     "precision mediump float;\n"
    //     "layout (location = 0) in vec2 Position;\n"
    //     "layout (location = 1) in vec2 UV;\n"
    //     "layout (location = 2) in vec4 Color;\n"
    //     "uniform mat4 ProjMtx;\n"
    //     "out vec2 Frag_UV;\n"
    //     "out vec4 Frag_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Frag_UV = UV;\n"
    //     "    Frag_Color = Color;\n"
    //     "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    //     "}\n";

    // const GLchar* vertex_shader_glsl_410_core =
    //     "layout (location = 0) in vec2 Position;\n"
    //     "layout (location = 1) in vec2 UV;\n"
    //     "layout (location = 2) in vec4 Color;\n"
    //     "uniform mat4 ProjMtx;\n"
    //     "out vec2 Frag_UV;\n"
    //     "out vec4 Frag_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Frag_UV = UV;\n"
    //     "    Frag_Color = Color;\n"
    //     "    gl_Position = ProjMtx * vec4(Position.xy,0,1);\n"
    //     "}\n";

    // const GLchar* fragment_shader_glsl_120 =
    //     "#ifdef GL_ES\n"
    //     "    precision mediump float;\n"
    //     "#endif\n"
    //     "uniform sampler2D Texture;\n"
    //     "varying vec2 Frag_UV;\n"
    //     "varying vec4 Frag_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    gl_FragColor = Frag_Color * texture2D(Texture, Frag_UV.st);\n"
    //     "}\n";

    // const GLchar* fragment_shader_glsl_130 =
    //     "uniform sampler2D Texture;\n"
    //     "in vec2 Frag_UV;\n"
    //     "in vec4 Frag_Color;\n"
    //     "out vec4 Out_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
    //     "}\n";

    // const GLchar* fragment_shader_glsl_300_es =
    //     "precision mediump float;\n"
    //     "uniform sampler2D Texture;\n"
    //     "in vec2 Frag_UV;\n"
    //     "in vec4 Frag_Color;\n"
    //     "layout (location = 0) out vec4 Out_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
    //     "}\n";

    // const GLchar* fragment_shader_glsl_410_core =
    //     "in vec2 Frag_UV;\n"
    //     "in vec4 Frag_Color;\n"
    //     "uniform sampler2D Texture;\n"
    //     "layout (location = 0) out vec4 Out_Color;\n"
    //     "void main()\n"
    //     "{\n"
    //     "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
    //     "}\n";

    // // Select shaders matching our GLSL versions
    // const GLchar* vertex_shader = nullptr;
    // const GLchar* fragment_shader = nullptr;
    // if (m_glsl_version < 130)
    // {
    //     vertex_shader = vertex_shader_glsl_120;
    //     fragment_shader = fragment_shader_glsl_120;
    // }
    // else if (m_glsl_version == 410)
    // {
    //     vertex_shader = vertex_shader_glsl_410_core;
    //     fragment_shader = fragment_shader_glsl_410_core;
    // }
    // else if (m_glsl_version == 300)
    // {
    //     vertex_shader = vertex_shader_glsl_300_es;
    //     fragment_shader = fragment_shader_glsl_300_es;
    // }
    // else
    // {
    //     vertex_shader = vertex_shader_glsl_130;
    //     fragment_shader = fragment_shader_glsl_130;
    // }

    // // Create shaders
    // const GLchar* vertex_shader_with_version[2] = { m_glsl_version_string.c_str(), vertex_shader };
    // m_vert_handle = glCreateShader(GL_VERTEX_SHADER);
    // glShaderSource(m_vert_handle, 2, vertex_shader_with_version, nullptr);
    // glCompileShader(m_vert_handle);
    // wxASSERT(check_shader(m_vert_handle, "vertex shader"));

    // const GLchar* fragment_shader_with_version[2] = { m_glsl_version_string.c_str(), fragment_shader };
    // m_frag_handle = glCreateShader(GL_FRAGMENT_SHADER);
    // glShaderSource(m_frag_handle, 2, fragment_shader_with_version, nullptr);
    // glCompileShader(m_frag_handle);
    // wxASSERT(check_shader(m_frag_handle, "fragment shader"));

    // m_shader_handle = glCreateProgram();
    // glAttachShader(m_shader_handle, m_vert_handle);
    // glAttachShader(m_shader_handle, m_frag_handle);
    // glLinkProgram(m_shader_handle);
    // wxASSERT(check_program(m_shader_handle, "shader program"));

    // m_attrib_location_tex = glGetUniformLocation(m_shader_handle, "Texture");
    // m_attrib_location_proj_mtx = glGetUniformLocation(m_shader_handle, "ProjMtx");
    // m_attrib_location_position = glGetAttribLocation(m_shader_handle, "Position");
    // m_attrib_location_uv = glGetAttribLocation(m_shader_handle, "UV");
    // m_attrib_location_color = glGetAttribLocation(m_shader_handle, "Color");

    // // Create buffers
    // glGenBuffers(1, &m_vbo_handle);
    // glGenBuffers(1, &m_elements_handle);

    create_fonts_texture();

    // // Restore modified GL state
    // glBindTexture(GL_TEXTURE_2D, last_texture);
    // glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    // glBindVertexArray(last_vertex_array);
}

void ImGuiWrapper::create_fonts_texture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bits (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    // Upload texture to graphics system
    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    glGenTextures(1, &m_font_texture);
    glBindTexture(GL_TEXTURE_2D, m_font_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Store our identifier
    io.Fonts->TexID = (ImTextureID)(intptr_t)m_font_texture;

    // Restore state
    glBindTexture(GL_TEXTURE_2D, last_texture);
}

bool ImGuiWrapper::check_program(unsigned int handle, const char* desc)
{
    GLint status = 0, log_length = 0;
    glGetProgramiv(handle, GL_LINK_STATUS, &status);
    glGetProgramiv(handle, GL_INFO_LOG_LENGTH, &log_length);

    if (status == GL_FALSE) {
        BOOST_LOG_TRIVIAL(error) << boost::format("ImGuiWrapper::check_program(): failed to link %1% (GLSL `%1%`)") % desc, m_glsl_version_string;
    }

    if (log_length > 0) {
        std::vector<GLchar> buf(log_length + 1, 0);
        glGetProgramInfoLog(handle, log_length, nullptr, buf.data());
        BOOST_LOG_TRIVIAL(error) << boost::format("ImGuiWrapper::check_program(): error log:\n%1%\n") % buf.data();
    }

    return status == GL_TRUE;
}

bool ImGuiWrapper::check_shader(unsigned int handle, const char *desc)
{
    GLint status = 0, log_length = 0;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
    glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);

    if (status == GL_FALSE) {
        BOOST_LOG_TRIVIAL(error) << boost::format("ImGuiWrapper::check_shader(): failed to compile %1%") % desc;
    }

    if (log_length > 0) {
        std::vector<GLchar> buf(log_length + 1, 0);
        glGetProgramInfoLog(handle, log_length, nullptr, buf.data());
        BOOST_LOG_TRIVIAL(error) << boost::format("ImGuiWrapper::check_program(): error log:\n%1%\n") % buf.data();
    }

    return status == GL_TRUE;
}

void ImGuiWrapper::render_draw_data(ImDrawData *draw_data)
{
        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    ImGuiIO& io = ImGui::GetIO();
    int fb_width = (int)(draw_data->DisplaySize.x * io.DisplayFramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * io.DisplayFramebufferScale.y);
    if (fb_width == 0 || fb_height == 0)
        return;
    draw_data->ScaleClipRects(io.DisplayFramebufferScale);

    // We are using the OpenGL fixed pipeline to make the example code simpler to read!
    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, vertex/texcoord/color pointers, polygon fill.
    GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_polygon_mode[2]; glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box); 
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glEnable(GL_SCISSOR_TEST);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnable(GL_TEXTURE_2D);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    //glUseProgram(0); // You may want this if using this code in an OpenGL 3+ context where shaders may be bound

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPps (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is typically (0,0) for single viewport apps.
    glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(draw_data->DisplayPos.x, draw_data->DisplayPos.x + draw_data->DisplaySize.x, draw_data->DisplayPos.y + draw_data->DisplaySize.y, draw_data->DisplayPos.y, -1.0f, +1.0f);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Render command lists
    ImVec2 pos = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;
        const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;
        glVertexPointer(2, GL_FLOAT, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, pos)));
        glTexCoordPointer(2, GL_FLOAT, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, uv)));
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(ImDrawVert), (const GLvoid*)((const char*)vtx_buffer + IM_OFFSETOF(ImDrawVert, col)));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback)
            {
                // User callback (registered via ImDrawList::AddCallback)
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                ImVec4 clip_rect = ImVec4(pcmd->ClipRect.x - pos.x, pcmd->ClipRect.y - pos.y, pcmd->ClipRect.z - pos.x, pcmd->ClipRect.w - pos.y);
                if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
                {
                    // Apply scissor/clipping rectangle
                    glScissor((int)clip_rect.x, (int)(fb_height - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));

                    // Bind texture, Draw
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
                    glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer);
                }
            }
            idx_buffer += pcmd->ElemCount;
        }
    }

    // Restore modified state
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glPolygonMode(GL_FRONT, (GLenum)last_polygon_mode[0]); glPolygonMode(GL_BACK, (GLenum)last_polygon_mode[1]);
    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
}

// void ImGuiWrapper::render_draw_data(ImDrawData *draw_data)
// {
//     // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
//     ImGuiIO& io = ImGui::GetIO();
//     int fb_width = (int)(draw_data->DisplaySize.x * io.DisplayFramebufferScale.x);
//     int fb_height = (int)(draw_data->DisplaySize.y * io.DisplayFramebufferScale.y);
//     if (fb_width <= 0 || fb_height <= 0)
//         return;
//     draw_data->ScaleClipRects(io.DisplayFramebufferScale);

//     // Backup GL state
//     GLenum last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, (GLint*)&last_active_texture);
//     glActiveTexture(GL_TEXTURE0);
//     GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
//     GLint last_texture; glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
// #ifdef GL_SAMPLER_BINDING
//     GLint last_sampler; glGetIntegerv(GL_SAMPLER_BINDING, &last_sampler);
// #endif
//     GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
//     GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
// #ifdef GL_POLYGON_MODE
//     GLint last_polygon_mode[2]; glGetIntegerv(GL_POLYGON_MODE, last_polygon_mode);
// #endif
//     GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
//     GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
//     GLenum last_blend_src_rgb; glGetIntegerv(GL_BLEND_SRC_RGB, (GLint*)&last_blend_src_rgb);
//     GLenum last_blend_dst_rgb; glGetIntegerv(GL_BLEND_DST_RGB, (GLint*)&last_blend_dst_rgb);
//     GLenum last_blend_src_alpha; glGetIntegerv(GL_BLEND_SRC_ALPHA, (GLint*)&last_blend_src_alpha);
//     GLenum last_blend_dst_alpha; glGetIntegerv(GL_BLEND_DST_ALPHA, (GLint*)&last_blend_dst_alpha);
//     GLenum last_blend_equation_rgb; glGetIntegerv(GL_BLEND_EQUATION_RGB, (GLint*)&last_blend_equation_rgb);
//     GLenum last_blend_equation_alpha; glGetIntegerv(GL_BLEND_EQUATION_ALPHA, (GLint*)&last_blend_equation_alpha);
//     GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
//     GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
//     GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
//     GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

//     // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
//     glEnable(GL_BLEND);
//     glBlendEquation(GL_FUNC_ADD);
//     glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//     glDisable(GL_CULL_FACE);
//     glDisable(GL_DEPTH_TEST);
//     glEnable(GL_SCISSOR_TEST);
// #ifdef GL_POLYGON_MODE
//     glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
// #endif

//     // Setup viewport, orthographic projection matrix
//     // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is typically (0,0) for single viewport apps.
//     glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);
//     float L = draw_data->DisplayPos.x;
//     float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
//     float T = draw_data->DisplayPos.y;
//     float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
//     const float ortho_projection[4][4] =
//     {
//         { 2.0f / (R - L), 0.0f, 0.0f, 0.0f },
//         { 0.0f, 2.0f / (T - B), 0.0f, 0.0f },
//         { 0.0f, 0.0f, -1.0f, 0.0f },
//         { (R + L) / (L - R), (T + B) / (B - T), 0.0f, 1.0f },
//     };
//     glUseProgram(m_shader_handle);
//     glUniform1i(m_attrib_location_tex, 0);
//     glUniformMatrix4fv(m_attrib_location_proj_mtx, 1, GL_FALSE, &ortho_projection[0][0]);
// #ifdef GL_SAMPLER_BINDING
//     glBindSampler(0, 0); // We use combined texture/sampler state. Applications using GL 3.3 may set that otherwise.
// #endif
//     // Recreate the VAO every time
//     // (This is to easily allow multiple GL contexts. VAO are not shared among GL contexts, and we don't track creation/deletion of windows so we don't have an obvious key to use to cache them.)
//     GLuint vao_handle = 0;
//     glGenVertexArrays(1, &vao_handle);
//     glBindVertexArray(vao_handle);
//     glBindBuffer(GL_ARRAY_BUFFER, m_vbo_handle);
//     glEnableVertexAttribArray(m_attrib_location_position);
//     glEnableVertexAttribArray(m_attrib_location_uv);
//     glEnableVertexAttribArray(m_attrib_location_color);
//     glVertexAttribPointer(m_attrib_location_position, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos));
//     glVertexAttribPointer(m_attrib_location_uv, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv));
//     glVertexAttribPointer(m_attrib_location_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col));

//     // Draw
//     ImVec2 pos = draw_data->DisplayPos;
//     for (int n = 0; n < draw_data->CmdListsCount; n++)
//     {
//         const ImDrawList* cmd_list = draw_data->CmdLists[n];
//         const ImDrawIdx* idx_buffer_offset = 0;

//         glBindBuffer(GL_ARRAY_BUFFER, m_vbo_handle);
//         glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), (const GLvoid*)cmd_list->VtxBuffer.Data, GL_STREAM_DRAW);

//         glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_elements_handle);
//         glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), (const GLvoid*)cmd_list->IdxBuffer.Data, GL_STREAM_DRAW);

//         for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
//         {
//             const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
//             if (pcmd->UserCallback)
//             {
//                 // User callback (registered via ImDrawList::AddCallback)
//                 pcmd->UserCallback(cmd_list, pcmd);
//             }
//             else
//             {
//                 ImVec4 clip_rect = ImVec4(pcmd->ClipRect.x - pos.x, pcmd->ClipRect.y - pos.y, pcmd->ClipRect.z - pos.x, pcmd->ClipRect.w - pos.y);
//                 if (clip_rect.x < fb_width && clip_rect.y < fb_height && clip_rect.z >= 0.0f && clip_rect.w >= 0.0f)
//                 {
//                     // Apply scissor/clipping rectangle
//                     glScissor((int)clip_rect.x, (int)(fb_height - clip_rect.w), (int)(clip_rect.z - clip_rect.x), (int)(clip_rect.w - clip_rect.y));

//                     // Bind texture, Draw
//                     glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->TextureId);
//                     glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, idx_buffer_offset);
//                 }
//             }
//             idx_buffer_offset += pcmd->ElemCount;
//         }
//     }
//     glDeleteVertexArrays(1, &vao_handle);

//     // Restore modified GL state
//     glUseProgram(last_program);
//     glBindTexture(GL_TEXTURE_2D, last_texture);
// #ifdef GL_SAMPLER_BINDING
//     glBindSampler(0, last_sampler);
// #endif
//     glActiveTexture(last_active_texture);
//     glBindVertexArray(last_vertex_array);
//     glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
//     glBlendEquationSeparate(last_blend_equation_rgb, last_blend_equation_alpha);
//     glBlendFuncSeparate(last_blend_src_rgb, last_blend_dst_rgb, last_blend_src_alpha, last_blend_dst_alpha);
//     if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
//     if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
//     if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
//     if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
// #ifdef GL_POLYGON_MODE
//     glPolygonMode(GL_FRONT_AND_BACK, (GLenum)last_polygon_mode[0]);
// #endif
//     glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
//     glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
// }

void ImGuiWrapper::destroy_device_objects()
{
    if (m_vbo_handle != 0)
    {
        glDeleteBuffers(1, &m_vbo_handle);
        m_vbo_handle = 0;
    }
    if (m_elements_handle != 0)
    {
        glDeleteBuffers(1, &m_elements_handle);
        m_elements_handle = 0;
    }

    if ((m_shader_handle != 0) && (m_vert_handle != 0))
        glDetachShader(m_shader_handle, m_vert_handle);

    if (m_vert_handle != 0)
    {
        glDeleteShader(m_vert_handle);
        m_vert_handle = 0;
    }
    if ((m_shader_handle != 0) && (m_frag_handle != 0))
        glDetachShader(m_shader_handle, m_frag_handle);

    if (m_frag_handle != 0)
    {
        glDeleteShader(m_frag_handle);
        m_frag_handle = 0;
    }

    if (m_shader_handle != 0)
    {
        glDeleteProgram(m_shader_handle);
        m_shader_handle = 0;
    }

    destroy_fonts_texture();
}

void ImGuiWrapper::destroy_fonts_texture()
{
    if (m_font_texture) {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->TexID = 0;
        glDeleteTextures(1, &m_font_texture);
        m_font_texture = 0;
    }
}

} // namespace GUI
} // namespace Slic3r

