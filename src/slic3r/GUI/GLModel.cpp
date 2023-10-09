#include "libslic3r/libslic3r.h"
#include "GLModel.hpp"

#include "3DScene.hpp"
#include "GUI_App.hpp"
#include "GLShader.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

size_t GLModel::InitializationData::vertices_count() const
{
    size_t ret = 0;
    for (const Entity& entity : entities) {
        ret += entity.positions.size();
    }
    return ret;
}

size_t GLModel::InitializationData::indices_count() const
{
    size_t ret = 0;
    for (const Entity& entity : entities) {
        ret += entity.indices.size();
    }
    return ret;
}


void GLModel::Geometry::add_vertex(const Vec2f& position)
{
    assert(format.vertex_layout == EVertexLayout::P2);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
}

void GLModel::Geometry::add_vertex(const Vec2f& position, const Vec2f& tex_coord)
{
    assert(format.vertex_layout == EVertexLayout::P2T2);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(tex_coord.x());
    vertices.emplace_back(tex_coord.y());
}

void GLModel::Geometry::add_vertex(const Vec3f& position)
{
    assert(format.vertex_layout == EVertexLayout::P3);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
}

void GLModel::Geometry::add_vertex(const Vec3f& position, const Vec2f& tex_coord)
{
    assert(format.vertex_layout == EVertexLayout::P3T2);
    vertices.insert(vertices.end(), position.data(), position.data() + 3);
    vertices.insert(vertices.end(), tex_coord.data(), tex_coord.data() + 2);
}

void GLModel::Geometry::add_vertex(const Vec3f& position, const Vec3f& normal, const Vec2f& tex_coord)
{
    assert(format.vertex_layout == EVertexLayout::P3N3T2);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(normal.x());
    vertices.emplace_back(normal.y());
    vertices.emplace_back(normal.z());
    vertices.emplace_back(tex_coord.x());
    vertices.emplace_back(tex_coord.y());
}

#if ENABLE_OPENGL_ES
void GLModel::Geometry::add_vertex(const Vec3f& position, const Vec3f& normal, const Vec3f& extra)
{
    assert(format.vertex_layout == EVertexLayout::P3N3E3);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(normal.x());
    vertices.emplace_back(normal.y());
    vertices.emplace_back(normal.z());
    vertices.emplace_back(extra.x());
    vertices.emplace_back(extra.y());
    vertices.emplace_back(extra.z());
}
#endif // ENABLE_OPENGL_ES

void GLModel::Geometry::add_vertex(const Vec4f& position)
{
    assert(format.vertex_layout == EVertexLayout::P4);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(position.w());
}

void GLModel::Geometry::add_index(unsigned int id)
{
    indices.emplace_back(id);
}

void GLModel::Geometry::add_line(unsigned int id1, unsigned int id2)
{
    indices.emplace_back(id1);
    indices.emplace_back(id2);
}

Vec2f GLModel::Geometry::extract_position_2(size_t id) const
{
    const size_t p_stride = position_stride_floats(format);
    if (p_stride != 2) {
        assert(false);
        return { FLT_MAX, FLT_MAX };
    }

    if (vertices_count() <= id) {
        assert(false);
        return { FLT_MAX, FLT_MAX };
    }

    const float* start = &vertices[id * vertex_stride_floats(format) + position_offset_floats(format)];
    return { *(start + 0), *(start + 1) };
}

Vec3f GLModel::Geometry::extract_position_3(size_t id) const
{
    const size_t p_stride = position_stride_floats(format);
    if (p_stride != 3) {
        assert(false);
        return { FLT_MAX, FLT_MAX, FLT_MAX };
    }

    if (vertices_count() <= id) {
        assert(false);
        return { FLT_MAX, FLT_MAX, FLT_MAX };
    }

    const float* start = &vertices[id * vertex_stride_floats(format) + position_offset_floats(format)];
    return { *(start + 0), *(start + 1), *(start + 2) };
}

Vec3f GLModel::Geometry::extract_normal_3(size_t id) const
{
    const size_t n_stride = normal_stride_floats(format);
    if (n_stride != 3) {
        assert(false);
        return { FLT_MAX, FLT_MAX, FLT_MAX };
    }

    if (vertices_count() <= id) {
        assert(false);
        return { FLT_MAX, FLT_MAX, FLT_MAX };
    }

    const float* start = &vertices[id * vertex_stride_floats(format) + normal_offset_floats(format)];
    return { *(start + 0), *(start + 1), *(start + 2) };
}

Vec2f GLModel::Geometry::extract_tex_coord_2(size_t id) const
{
    const size_t t_stride = tex_coord_stride_floats(format);
    if (t_stride != 2) {
        assert(false);
        return { FLT_MAX, FLT_MAX };
    }

    if (vertices_count() <= id) {
        assert(false);
        return { FLT_MAX, FLT_MAX };
    }

    const float* start = &vertices[id * vertex_stride_floats(format) + tex_coord_offset_floats(format)];
    return { *(start + 0), *(start + 1) };
}

void GLModel::Geometry::set_vertex(size_t id, const Vec3f& position, const Vec3f& normal)
{
    assert(format.vertex_layout == EVertexLayout::P3N3);
    assert(id < vertices_count());
    if (id < vertices_count()) {
        float* start = &vertices[id * vertex_stride_floats(format)];
        *(start + 0) = position.x();
        *(start + 1) = position.y();
        *(start + 2) = position.z();
        *(start + 3) = normal.x();
        *(start + 4) = normal.y();
        *(start + 5) = normal.z();
    }
}

void GLModel::Geometry::set_index(size_t id, unsigned int index)
{
    assert(id < indices_count());
    if (id < indices_count())
        indices[id] = index;
}

unsigned int GLModel::Geometry::extract_index(size_t id) const
{
    if (indices_count() <= id) {
        assert(false);
        return -1;
    }

    return indices[id];
}

void GLModel::Geometry::remove_vertex(size_t id)
{
    assert(id < vertices_count());
    if (id < vertices_count()) {
        const size_t stride = vertex_stride_floats(format);
        std::vector<float>::const_iterator it = vertices.begin() + id * stride;
        vertices.erase(it, it + stride);
    }
}

indexed_triangle_set GLModel::Geometry::get_as_indexed_triangle_set() const
{
    indexed_triangle_set its;
    its.vertices.reserve(vertices_count());
    for (size_t i = 0; i < vertices_count(); ++i) {
        its.vertices.emplace_back(extract_position_3(i));
    }
    its.indices.reserve(indices_count() / 3);
    for (size_t i = 0; i < indices_count() / 3; ++i) {
        const size_t tri_id = i * 3;
        its.indices.emplace_back(extract_index(tri_id), extract_index(tri_id + 1), extract_index(tri_id + 2));
    }
    return its;
}

size_t GLModel::Geometry::vertex_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:     { return 2; }
    case EVertexLayout::P2T2:   { return 4; }
    case EVertexLayout::P3:     { return 3; }
    case EVertexLayout::P3T2:   { return 5; }
    case EVertexLayout::P3N3:   { return 6; }
    case EVertexLayout::P3N3T2: { return 8; }
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3E3: { return 9; }
#endif // ENABLE_OPENGL_ES
    case EVertexLayout::P4:     { return 4; }
    default:                    { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::position_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:   { return 2; }
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P3N3E3: { return 3; }
#else
    case EVertexLayout::P3N3T2: { return 3; }
#endif // ENABLE_OPENGL_ES
    case EVertexLayout::P4:     { return 4; }
    default:                    { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::position_offset_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3E3:
#endif // ENABLE_OPENGL_ES
    case EVertexLayout::P4:   { return 0; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::normal_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P3N3E3: { return 3; }
#else
    case EVertexLayout::P3N3T2: { return 3; }
#endif // ENABLE_OPENGL_ES
    default:                    { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::normal_offset_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P3N3E3: { return 3; }
#else
    case EVertexLayout::P3N3T2: { return 3; }
#endif // ENABLE_OPENGL_ES
    default:                    { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::tex_coord_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2T2:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3T2: { return 2; }
    default:                    { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::tex_coord_offset_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2T2:   { return 2; }
    case EVertexLayout::P3T2:   { return 3; }
    case EVertexLayout::P3N3T2: { return 6; }
    default:                    { assert(false); return 0; }
    };
}

#if ENABLE_OPENGL_ES
size_t GLModel::Geometry::extra_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3E3: { return 3; }
    default:                    { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::extra_offset_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3E3: { return 6; }
    default:                    { assert(false); return 0; }
    };
}
#endif // ENABLE_OPENGL_ES

size_t GLModel::Geometry::index_stride_bytes(const Geometry& data)
{
    switch (data.index_type)
    {
    case EIndexType::UINT:   { return sizeof(unsigned int); }
    case EIndexType::USHORT: { return sizeof(unsigned short); }
    case EIndexType::UBYTE:  { return sizeof(unsigned char); }
    default:                 { assert(false); return 0; }
    };
}

bool GLModel::Geometry::has_position(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3E3:
#endif // ENABLE_OPENGL_ES
    case EVertexLayout::P4:   { return true; }
    default:                  { assert(false); return false; }
    };
}

bool GLModel::Geometry::has_normal(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P4:     { return false; }
    case EVertexLayout::P3N3:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P3N3E3: { return true; }
#else
    case EVertexLayout::P3N3T2: { return true; }
#endif // ENABLE_OPENGL_ES
    default:                    { assert(false); return false; }
    };
}

bool GLModel::Geometry::has_tex_coord(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2T2:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3T2: { return true; }
    case EVertexLayout::P2:
    case EVertexLayout::P3:
    case EVertexLayout::P3N3:
#if ENABLE_OPENGL_ES
    case EVertexLayout::P3N3E3:
#endif // ENABLE_OPENGL_ES
    case EVertexLayout::P4:     { return false; }
    default:                    { assert(false); return false; }
    };
}

#if ENABLE_OPENGL_ES
bool GLModel::Geometry::has_extra(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3E3: { return true; }
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P4:     { return false; }
    default:                    { assert(false); return false; }
    };
}
#endif // ENABLE_OPENGL_ES

#if ENABLE_GLMODEL_STATISTICS
GLModel::Statistics GLModel::s_statistics;
#endif // ENABLE_GLMODEL_STATISTICS

void GLModel::init_from(const InitializationData& data)
{
    if (!m_render_data.empty()) // call reset() if you want to reuse this model
        return;

    for (const InitializationData::Entity& entity : data.entities) {
        if (entity.positions.empty() || entity.indices.empty())
            continue;

        assert(entity.normals.empty() || entity.normals.size() == entity.positions.size());

        RenderData rdata;
        rdata.type = entity.type;
        rdata.color = entity.color;

        // vertices/normals data
        std::vector<float> vertices(6 * entity.positions.size());
        for (size_t i = 0; i < entity.positions.size(); ++i) {
            const size_t offset = i * 6;
            ::memcpy(static_cast<void*>(&vertices[offset]), static_cast<const void*>(entity.positions[i].data()), 3 * sizeof(float));
            if (!entity.normals.empty())
                ::memcpy(static_cast<void*>(&vertices[3 + offset]), static_cast<const void*>(entity.normals[i].data()), 3 * sizeof(float));
        }

        // indices data
        std::vector<unsigned int> indices = entity.indices;

        rdata.indices_count = static_cast<unsigned int>(indices.size());

        // update bounding box
        for (size_t i = 0; i < entity.positions.size(); ++i) {
            m_bounding_box.merge(entity.positions[i].cast<double>());
        }

        send_to_gpu(rdata, vertices, indices);
        m_render_data.emplace_back(rdata);
    }
}

void GLModel::init_from(Geometry& data)
{
    init_from(data.get_as_indexed_triangle_set());
}

void GLModel::init_from(const indexed_triangle_set& its, const BoundingBoxf3 &bbox)
{
    if (!m_render_data.empty()) // call reset() if you want to reuse this model
        return;

    RenderData data;
    data.type = PrimitiveType::Triangles;

    std::vector<float> vertices = std::vector<float>(18 * its.indices.size());
    std::vector<unsigned int> indices = std::vector<unsigned int>(3 * its.indices.size());

    unsigned int vertices_count = 0;
    for (uint32_t i = 0; i < its.indices.size(); ++i) {
        stl_triangle_vertex_indices face      = its.indices[i];
        stl_vertex                  vertex[3] = { its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]] };
        stl_vertex                  n         = face_normal_normalized(vertex);
        for (size_t j = 0; j < 3; ++ j) {
            size_t offset = i * 18 + j * 6;
            ::memcpy(static_cast<void*>(&vertices[offset]), static_cast<const void*>(vertex[j].data()), 3 * sizeof(float));
            ::memcpy(static_cast<void*>(&vertices[3 + offset]), static_cast<const void*>(n.data()), 3 * sizeof(float));
        }
        for (size_t j = 0; j < 3; ++j)
            indices[i * 3 + j] = vertices_count + j;
        vertices_count += 3;
    }

    data.indices_count = static_cast<unsigned int>(indices.size());
    m_bounding_box = bbox;

    send_to_gpu(data, vertices, indices);
    m_render_data.emplace_back(data);
}

void GLModel::init_from(const indexed_triangle_set& its)
{
    this->init_from(its, bounding_box(its));
}

void GLModel::init_from(const Polygons& polygons, float z)
{
    auto append_polygon = [](const Polygon& polygon, float z, GUI::GLModel::InitializationData& data) {
        if (!polygon.empty()) {
            GUI::GLModel::InitializationData::Entity entity;
            entity.type = GUI::GLModel::PrimitiveType::LineLoop;
            // contour
            entity.positions.reserve(polygon.size() + 1);
            entity.indices.reserve(polygon.size() + 1);
            unsigned int id = 0;
            for (const Point& p : polygon) {
                Vec3f position = unscale(p.x(), p.y(), 0.0).cast<float>();
                position.z() = z;
                entity.positions.emplace_back(position);
                entity.indices.emplace_back(id++);
            }
            data.entities.emplace_back(entity);
        }
    };

    InitializationData init_data;
    for (const Polygon& polygon : polygons) {
        append_polygon(polygon, z, init_data);
    }
    init_from(init_data);
}

bool GLModel::init_from_file(const std::string& filename)
{
    if (!boost::filesystem::exists(filename))
        return false;

    if (!boost::algorithm::iends_with(filename, ".stl"))
        return false;

    Model model;
    try
    {
        model = Model::read_from_file(filename);
    }
    catch (std::exception&)
    {
        return false;
    }

    TriangleMesh mesh = model.mesh();
    init_from(mesh.its, mesh.bounding_box());

    m_filename = filename;

    return true;
}

void GLModel::set_color(int entity_id, const std::array<float, 4>& color)
{
    for (size_t i = 0; i < m_render_data.size(); ++i) {
        if (entity_id == -1 || static_cast<int>(i) == entity_id)
            m_render_data[i].color = color;
    }
}

void GLModel::reset()
{
    for (RenderData& data : m_render_data) {
        // release gpu memory
        if (data.ibo_id > 0)
            glsafe(::glDeleteBuffers(1, &data.ibo_id));
        if (data.vbo_id > 0)
            glsafe(::glDeleteBuffers(1, &data.vbo_id));
    }

    m_render_data.clear();
    m_bounding_box = BoundingBoxf3();
    m_filename = std::string();
}

void GLModel::render() const
{
    GLShaderProgram* shader = wxGetApp().get_current_shader();

    for (const RenderData& data : m_render_data) {
        if (data.vbo_id == 0 || data.ibo_id == 0)
            continue;

        GLenum mode;
        switch (data.type)
        {
        default:
        case PrimitiveType::Triangles: { mode = GL_TRIANGLES; break; }
        case PrimitiveType::Lines:     { mode = GL_LINES; break; }
        case PrimitiveType::LineStrip: { mode = GL_LINE_STRIP; break; }
        case PrimitiveType::LineLoop:  { mode = GL_LINE_LOOP; break; }
        }

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, data.vbo_id));
        glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0));
        glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float))));

        glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
        glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

        if (shader != nullptr)
            shader->set_uniform("uniform_color", data.color);
        else
            glsafe(::glColor4fv(data.color.data()));

        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo_id));
        glsafe(::glDrawElements(mode, static_cast<GLsizei>(data.indices_count), GL_UNSIGNED_INT, (const void*)0));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

        glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    }
}

void GLModel::render_instanced(unsigned int instances_vbo, unsigned int instances_count) const
{
    if (instances_vbo == 0)
        return;

    GLShaderProgram* shader = wxGetApp().get_current_shader();
    assert(shader == nullptr || boost::algorithm::iends_with(shader->get_name(), "_instanced"));

    // vertex attributes
    GLint position_id = (shader != nullptr) ? shader->get_attrib_location("v_position") : -1;
    GLint normal_id = (shader != nullptr) ? shader->get_attrib_location("v_normal") : -1;
    assert(position_id != -1 && normal_id != -1);

    // instance attributes
    GLint offset_id = (shader != nullptr) ? shader->get_attrib_location("i_offset") : -1;
    GLint scales_id = (shader != nullptr) ? shader->get_attrib_location("i_scales") : -1;
    assert(offset_id != -1 && scales_id != -1);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, instances_vbo));
    if (offset_id != -1) {
        glsafe(::glVertexAttribPointer(offset_id, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (GLvoid*)0));
        glsafe(::glEnableVertexAttribArray(offset_id));
        glsafe(::glVertexAttribDivisor(offset_id, 1));
    }
    if (scales_id != -1) {
        glsafe(::glVertexAttribPointer(scales_id, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (GLvoid*)(3 * sizeof(float))));
        glsafe(::glEnableVertexAttribArray(scales_id));
        glsafe(::glVertexAttribDivisor(scales_id, 1));
    }

    for (const RenderData& data : m_render_data) {
        if (data.vbo_id == 0 || data.ibo_id == 0)
            continue;

        GLenum mode;
        switch (data.type)
        {
        default:
        case PrimitiveType::Triangles: { mode = GL_TRIANGLES; break; }
        case PrimitiveType::Lines:     { mode = GL_LINES; break; }
        case PrimitiveType::LineStrip: { mode = GL_LINE_STRIP; break; }
        case PrimitiveType::LineLoop:  { mode = GL_LINE_LOOP; break; }
        }

        if (shader != nullptr)
            shader->set_uniform("uniform_color", data.color);
        else
            glsafe(::glColor4fv(data.color.data()));

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, data.vbo_id));
        if (position_id != -1) {
            glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (GLvoid*)0));
            glsafe(::glEnableVertexAttribArray(position_id));
        }
        if (normal_id != -1) {
            glsafe(::glVertexAttribPointer(normal_id, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (GLvoid*)(3 * sizeof(float))));
            glsafe(::glEnableVertexAttribArray(normal_id));
        }

        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo_id));
        glsafe(::glDrawElementsInstanced(mode, static_cast<GLsizei>(data.indices_count), GL_UNSIGNED_INT, (const void*)0, instances_count));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

        if (normal_id != -1)
            glsafe(::glDisableVertexAttribArray(normal_id));
        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));
    }

    if (scales_id != -1)
        glsafe(::glDisableVertexAttribArray(scales_id));
    if (offset_id != -1)
        glsafe(::glDisableVertexAttribArray(offset_id));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLModel::send_to_gpu(RenderData& data, const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
{
    assert(data.vbo_id == 0);
    assert(data.ibo_id == 0);

    // vertex data -> send to gpu
    glsafe(::glGenBuffers(1, &data.vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, data.vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // indices data -> send to gpu
    glsafe(::glGenBuffers(1, &data.ibo_id));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo_id));
    glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}

GLModel::InitializationData stilized_arrow(int resolution, float tip_radius, float tip_height, float stem_radius, float stem_height)
{
    auto append_vertex = [](GLModel::InitializationData::Entity& entity, const Vec3f& position, const Vec3f& normal) {
        entity.positions.emplace_back(position);
        entity.normals.emplace_back(normal);
    };
    auto append_indices = [](GLModel::InitializationData::Entity& entity, unsigned int v1, unsigned int v2, unsigned int v3) {
        entity.indices.emplace_back(v1);
        entity.indices.emplace_back(v2);
        entity.indices.emplace_back(v3);
    };

    resolution = std::max(4, resolution);

    GLModel::InitializationData data;
    GLModel::InitializationData::Entity entity;
    entity.type = GLModel::PrimitiveType::Triangles;

    const float angle_step = 2.0f * M_PI / static_cast<float>(resolution);
    std::vector<float> cosines(resolution);
    std::vector<float> sines(resolution);

    for (int i = 0; i < resolution; ++i) {
        const float angle = angle_step * static_cast<float>(i);
        cosines[i] = ::cos(angle);
        sines[i] = -::sin(angle);
    }

    const float total_height = tip_height + stem_height;

    // tip vertices/normals
    append_vertex(entity, { 0.0f, 0.0f, total_height }, Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i) {
        append_vertex(entity, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // tip triangles
    for (int i = 0; i < resolution; ++i) {
        const int v3 = (i < resolution - 1) ? i + 2 : 1;
        append_indices(entity, 0, i + 1, v3);
    }

    // tip cap outer perimeter vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(entity, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap inner perimeter vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(entity, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap triangles
    for (int i = 0; i < resolution; ++i) {
        const int v2 = (i < resolution - 1) ? i + resolution + 2 : resolution + 1;
        const int v3 = (i < resolution - 1) ? i + 2 * resolution + 2 : 2 * resolution + 1;
        append_indices(entity, i + resolution + 1, v3, v2);
        append_indices(entity, i + resolution + 1, i + 2 * resolution + 1, v3);
    }

    // stem bottom vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(entity, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // stem top vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(entity, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, { sines[i], cosines[i], 0.0f });
    }

    // stem triangles
    for (int i = 0; i < resolution; ++i) {
        const int v2 = (i < resolution - 1) ? i + 3 * resolution + 2 : 3 * resolution + 1;
        const int v3 = (i < resolution - 1) ? i + 4 * resolution + 2 : 4 * resolution + 1;
        append_indices(entity, i + 3 * resolution + 1, v3, v2);
        append_indices(entity, i + 3 * resolution + 1, i + 4 * resolution + 1, v3);
    }

    // stem cap vertices
    append_vertex(entity, Vec3f::Zero(), -Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i) {
        append_vertex(entity, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, -Vec3f::UnitZ());
    }

    // stem cap triangles
    for (int i = 0; i < resolution; ++i) {
        const int v3 = (i < resolution - 1) ? i + 5 * resolution + 3 : 5 * resolution + 2;
        append_indices(entity, 5 * resolution + 1, v3, i + 5 * resolution + 2);
    }

    data.entities.emplace_back(entity);
    return data;
}

GLModel::InitializationData circular_arrow(int resolution, float radius, float tip_height, float tip_width, float stem_width, float thickness)
{
    auto append_vertex = [](GLModel::InitializationData::Entity& entity, const Vec3f& position, const Vec3f& normal) {
        entity.positions.emplace_back(position);
        entity.normals.emplace_back(normal);
    };
    auto append_indices = [](GLModel::InitializationData::Entity& entity, unsigned int v1, unsigned int v2, unsigned int v3) {
        entity.indices.emplace_back(v1);
        entity.indices.emplace_back(v2);
        entity.indices.emplace_back(v3);
    };

    resolution = std::max(2, resolution);

    GLModel::InitializationData data;
    GLModel::InitializationData::Entity entity;
    entity.type = GLModel::PrimitiveType::Triangles;

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;

    const float outer_radius = radius + half_stem_width;
    const float inner_radius = radius - half_stem_width;
    const float step_angle = 0.5f * PI / static_cast<float>(resolution);

    // tip
    // top face vertices
    append_vertex(entity, { 0.0f, outer_radius, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { 0.0f, radius + half_tip_width, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { -tip_height, radius, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { 0.0f, radius - half_tip_width, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { 0.0f, inner_radius, half_thickness }, Vec3f::UnitZ());

    // top face triangles
    append_indices(entity, 0, 1, 2);
    append_indices(entity, 0, 2, 4);
    append_indices(entity, 4, 2, 3);

    // bottom face vertices
    append_vertex(entity, { 0.0f, outer_radius, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { 0.0f, radius + half_tip_width, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { -tip_height, radius, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { 0.0f, radius - half_tip_width, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { 0.0f, inner_radius, -half_thickness }, -Vec3f::UnitZ());

    // bottom face triangles
    append_indices(entity, 5, 7, 6);
    append_indices(entity, 5, 9, 7);
    append_indices(entity, 9, 8, 7);

    // side faces vertices
    append_vertex(entity, { 0.0f, outer_radius, -half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { 0.0f, radius + half_tip_width, -half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { 0.0f, outer_radius, half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { 0.0f, radius + half_tip_width, half_thickness }, Vec3f::UnitX());

    Vec3f normal(-half_tip_width, tip_height, 0.0f);
    normal.normalize();
    append_vertex(entity, { 0.0f, radius + half_tip_width, -half_thickness }, normal);
    append_vertex(entity, { -tip_height, radius, -half_thickness }, normal);
    append_vertex(entity, { 0.0f, radius + half_tip_width, half_thickness }, normal);
    append_vertex(entity, { -tip_height, radius, half_thickness }, normal);

    normal = Vec3f(-half_tip_width, -tip_height, 0.0f);
    normal.normalize();
    append_vertex(entity, { -tip_height, radius, -half_thickness }, normal);
    append_vertex(entity, { 0.0f, radius - half_tip_width, -half_thickness }, normal);
    append_vertex(entity, { -tip_height, radius, half_thickness }, normal);
    append_vertex(entity, { 0.0f, radius - half_tip_width, half_thickness }, normal);

    append_vertex(entity, { 0.0f, radius - half_tip_width, -half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { 0.0f, inner_radius, -half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { 0.0f, radius - half_tip_width, half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { 0.0f, inner_radius, half_thickness }, Vec3f::UnitX());

    // side face triangles
    for (int i = 0; i < 4; ++i) {
        const int ii = i * 4;
        append_indices(entity, 10 + ii, 11 + ii, 13 + ii);
        append_indices(entity, 10 + ii, 13 + ii, 12 + ii);
    }

    // stem
    // top face vertices
    for (int i = 0; i <= resolution; ++i) {
        const float angle = static_cast<float>(i) * step_angle;
        append_vertex(entity, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    for (int i = 0; i <= resolution; ++i) {
        const float angle = static_cast<float>(i) * step_angle;
        append_vertex(entity, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    // top face triangles
    for (int i = 0; i < resolution; ++i) {
        append_indices(entity, 26 + i, 27 + i, 27 + resolution + i);
        append_indices(entity, 27 + i, 28 + resolution + i, 27 + resolution + i);
    }

    // bottom face vertices
    for (int i = 0; i <= resolution; ++i) {
        const float angle = static_cast<float>(i) * step_angle;
        append_vertex(entity, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    for (int i = 0; i <= resolution; ++i) {
        const float angle = static_cast<float>(i) * step_angle;
        append_vertex(entity, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    // bottom face triangles
    for (int i = 0; i < resolution; ++i) {
        append_indices(entity, 28 + 2 * resolution + i, 29 + 3 * resolution + i, 29 + 2 * resolution + i);
        append_indices(entity, 29 + 2 * resolution + i, 29 + 3 * resolution + i, 30 + 3 * resolution + i);
    }

    // side faces vertices and triangles
    for (int i = 0; i <= resolution; ++i) {
        const float angle = static_cast<float>(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(entity, { inner_radius * s, inner_radius * c, -half_thickness }, { -s, -c, 0.0f });
    }

    for (int i = 0; i <= resolution; ++i) {
        const float angle = static_cast<float>(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(entity, { inner_radius * s, inner_radius * c, half_thickness }, { -s, -c, 0.0f });
    }

    int first_id = 26 + 4 * (resolution + 1);
    for (int i = 0; i < resolution; ++i) {
        const int ii = first_id + i;
        append_indices(entity, ii, ii + 1, ii + resolution + 2);
        append_indices(entity, ii, ii + resolution + 2, ii + resolution + 1);
    }

    append_vertex(entity, { inner_radius, 0.0f, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { outer_radius, 0.0f, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { inner_radius, 0.0f, half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { outer_radius, 0.0f, half_thickness }, -Vec3f::UnitY());

    first_id = 26 + 6 * (resolution + 1);
    append_indices(entity, first_id, first_id + 1, first_id + 3);
    append_indices(entity, first_id, first_id + 3, first_id + 2);

    for (int i = resolution; i >= 0; --i) {
        const float angle = static_cast<float>(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(entity, { outer_radius * s, outer_radius * c, -half_thickness }, { s, c, 0.0f });
    }

    for (int i = resolution; i >= 0; --i) {
        const float angle = static_cast<float>(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(entity, { outer_radius * s, outer_radius * c, +half_thickness }, { s, c, 0.0f });
    }

    first_id = 30 + 6 * (resolution + 1);
    for (int i = 0; i < resolution; ++i) {
        const int ii = first_id + i;
        append_indices(entity, ii, ii + 1, ii + resolution + 2);
        append_indices(entity, ii, ii + resolution + 2, ii + resolution + 1);
    }

    data.entities.emplace_back(entity);
    return data;
}

GLModel::InitializationData straight_arrow(float tip_width, float tip_height, float stem_width, float stem_height, float thickness)
{
    auto append_vertex = [](GLModel::InitializationData::Entity& entity, const Vec3f& position, const Vec3f& normal) {
        entity.positions.emplace_back(position);
        entity.normals.emplace_back(normal);
    };
    auto append_indices = [](GLModel::InitializationData::Entity& entity, unsigned int v1, unsigned int v2, unsigned int v3) {
        entity.indices.emplace_back(v1);
        entity.indices.emplace_back(v2);
        entity.indices.emplace_back(v3);
    };

    GLModel::InitializationData data;
    GLModel::InitializationData::Entity entity;
    entity.type = GLModel::PrimitiveType::Triangles;

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;
    const float total_height = tip_height + stem_height;

    // top face vertices
    append_vertex(entity, { half_stem_width, 0.0, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { half_stem_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { half_tip_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { 0.0, total_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { -half_tip_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { -half_stem_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(entity, { -half_stem_width, 0.0, half_thickness }, Vec3f::UnitZ());

    // top face triangles
    append_indices(entity, 0, 1, 6);
    append_indices(entity, 6, 1, 5);
    append_indices(entity, 4, 5, 3);
    append_indices(entity, 5, 1, 3);
    append_indices(entity, 1, 2, 3);

    // bottom face vertices
    append_vertex(entity, { half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { 0.0, total_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { -half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(entity, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitZ());

    // bottom face triangles
    append_indices(entity, 7, 13, 8);
    append_indices(entity, 13, 12, 8);
    append_indices(entity, 12, 11, 10);
    append_indices(entity, 8, 12, 10);
    append_indices(entity, 9, 8, 10);

    // side faces vertices
    append_vertex(entity, { half_stem_width, 0.0, -half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { half_stem_width, stem_height, -half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { half_stem_width, 0.0, half_thickness }, Vec3f::UnitX());
    append_vertex(entity, { half_stem_width, stem_height, half_thickness }, Vec3f::UnitX());

    append_vertex(entity, { half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { half_stem_width, stem_height, half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { half_tip_width, stem_height, half_thickness }, -Vec3f::UnitY());

    Vec3f normal(tip_height, half_tip_width, 0.0f);
    normal.normalize();
    append_vertex(entity, { half_tip_width, stem_height, -half_thickness }, normal);
    append_vertex(entity, { 0.0, total_height, -half_thickness }, normal);
    append_vertex(entity, { half_tip_width, stem_height, half_thickness }, normal);
    append_vertex(entity, { 0.0, total_height, half_thickness }, normal);

    normal = Vec3f(-tip_height, half_tip_width, 0.0f);
    normal.normalize();
    append_vertex(entity, { 0.0, total_height, -half_thickness }, normal);
    append_vertex(entity, { -half_tip_width, stem_height, -half_thickness }, normal);
    append_vertex(entity, { 0.0, total_height, half_thickness }, normal);
    append_vertex(entity, { -half_tip_width, stem_height, half_thickness }, normal);

    append_vertex(entity, { -half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { -half_tip_width, stem_height, half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { -half_stem_width, stem_height, half_thickness }, -Vec3f::UnitY());

    append_vertex(entity, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitX());
    append_vertex(entity, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitX());
    append_vertex(entity, { -half_stem_width, stem_height, half_thickness }, -Vec3f::UnitX());
    append_vertex(entity, { -half_stem_width, 0.0, half_thickness }, -Vec3f::UnitX());

    append_vertex(entity, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { -half_stem_width, 0.0, half_thickness }, -Vec3f::UnitY());
    append_vertex(entity, { half_stem_width, 0.0, half_thickness }, -Vec3f::UnitY());

    // side face triangles
    for (int i = 0; i < 7; ++i) {
        const int ii = i * 4;
        append_indices(entity, 14 + ii, 15 + ii, 17 + ii);
        append_indices(entity, 14 + ii, 17 + ii, 16 + ii);
    }

    data.entities.emplace_back(entity);
    return data;
}

GLModel::InitializationData diamond(int resolution)
{
    resolution = std::max(4, resolution);

    GLModel::InitializationData data;
    GLModel::InitializationData::Entity entity;
    entity.type = GLModel::PrimitiveType::Triangles;

    const float step = 2.0f * float(PI) / float(resolution);

    // positions
    for (int i = 0; i < resolution; ++i) {
        float ii = float(i) * step;
        entity.positions.emplace_back(0.5f * ::cos(ii), 0.5f * ::sin(ii), 0.0f);
    }
    entity.positions.emplace_back(0.0f, 0.0f, 0.5f);
    entity.positions.emplace_back(0.0f, 0.0f, -0.5f);

    // normals
    for (const Vec3f& v : entity.positions) {
        entity.normals.emplace_back(v.normalized());
    }

    // triangles
    // top
    for (int i = 0; i < resolution; ++i) {
        entity.indices.push_back(i + 0);
        entity.indices.push_back(i + 1);
        entity.indices.push_back(resolution);
    }
    entity.indices.push_back(resolution - 1);
    entity.indices.push_back(0);
    entity.indices.push_back(resolution);

    // bottom
    for (int i = 0; i < resolution; ++i) {
        entity.indices.push_back(i + 0);
        entity.indices.push_back(resolution + 1);
        entity.indices.push_back(i + 1);
    }
    entity.indices.push_back(resolution - 1);
    entity.indices.push_back(resolution + 1);
    entity.indices.push_back(0);

    data.entities.emplace_back(entity);
    return data;
}


GLModel::Geometry smooth_sphere(unsigned int resolution, float radius)
{
    resolution = std::max<unsigned int>(4, resolution);

    const unsigned int sectorCount = resolution;
    const unsigned int stackCount  = resolution;

    const float sectorStep = float(2.0 * M_PI / sectorCount);
    const float stackStep = float(M_PI / stackCount);

    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    data.reserve_vertices((stackCount - 1) * sectorCount + 2);
    data.reserve_indices((2 * (stackCount - 1) * sectorCount) * 3);

    // vertices
    for (unsigned int i = 0; i <= stackCount; ++i) {
        // from pi/2 to -pi/2
        const double stackAngle = 0.5 * M_PI - stackStep * i;
        const double xy = double(radius) * ::cos(stackAngle);
        const double z = double(radius) * ::sin(stackAngle);
        if (i == 0 || i == stackCount) {
            const Vec3f v(float(xy), 0.0f, float(z));
            data.add_vertex(v, (Vec3f)v.normalized());
        }
        else {
            for (unsigned int j = 0; j < sectorCount; ++j) {
                // from 0 to 2pi
                const double sectorAngle = sectorStep * j;
                const Vec3f v(float(xy * std::cos(sectorAngle)), float(xy * std::sin(sectorAngle)), float(z));
                data.add_vertex(v, (Vec3f)v.normalized());
            }
        }
    }

    // triangles
    for (unsigned int i = 0; i < stackCount; ++i) {
        // Beginning of current stack.
        unsigned int k1 = (i == 0) ? 0 : (1 + (i - 1) * sectorCount);
        const unsigned int k1_first = k1;
        // Beginning of next stack.
        unsigned int k2 = (i == 0) ? 1 : (k1 + sectorCount);
        const unsigned int k2_first = k2;
        for (unsigned int j = 0; j < sectorCount; ++j) {
            // 2 triangles per sector excluding first and last stacks
            unsigned int k1_next = k1;
            unsigned int k2_next = k2;
            if (i != 0) {
                k1_next = (j + 1 == sectorCount) ? k1_first : (k1 + 1);
                data.add_triangle(k1, k2, k1_next);
            }
            if (i + 1 != stackCount) {
                k2_next = (j + 1 == sectorCount) ? k2_first : (k2 + 1);
                data.add_triangle(k1_next, k2, k2_next);
            }
            k1 = k1_next;
            k2 = k2_next;
        }
    }

    return data;
}

GLModel::Geometry smooth_cylinder(unsigned int resolution, float radius, float height)
{
    resolution = std::max<unsigned int>(4, resolution);

    const unsigned int sectorCount = resolution;
    const float sectorStep = 2.0f * float(M_PI) / float(sectorCount);

    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    data.reserve_vertices(sectorCount * 4 + 2);
    data.reserve_indices(sectorCount * 4 * 3);

    auto generate_vertices_on_circle = [sectorCount, sectorStep](float radius) {
        std::vector<Vec3f> ret;
        ret.reserve(sectorCount);
        for (unsigned int i = 0; i < sectorCount; ++i) {
            // from 0 to 2pi
            const float sectorAngle = sectorStep * i;
            ret.emplace_back(radius * std::cos(sectorAngle), radius * std::sin(sectorAngle), 0.0f);
        }
        return ret;
    };

    const std::vector<Vec3f> base_vertices = generate_vertices_on_circle(radius);
    const Vec3f h = height * Vec3f::UnitZ();

    // stem vertices
    for (unsigned int i = 0; i < sectorCount; ++i) {
        const Vec3f& v = base_vertices[i];
        const Vec3f n = v.normalized();
        data.add_vertex(v, n);
        data.add_vertex(v + h, n);
    }

    // stem triangles
    for (unsigned int i = 0; i < sectorCount; ++i) {
        unsigned int v1 = i * 2;
        unsigned int v2 = (i < sectorCount - 1) ? v1 + 2 : 0;
        unsigned int v3 = v2 + 1;
        unsigned int v4 = v1 + 1;
        data.add_triangle(v1, v2, v3);
        data.add_triangle(v1, v3, v4);
    }

    // bottom cap vertices
    Vec3f cap_center = Vec3f::Zero();
    unsigned int cap_center_id = data.vertices_count();
    Vec3f normal = -Vec3f::UnitZ();

    data.add_vertex(cap_center, normal);
    for (unsigned int i = 0; i < sectorCount; ++i) {
        data.add_vertex(base_vertices[i], normal);
    }

    // bottom cap triangles
    for (unsigned int i = 0; i < sectorCount; ++i) {
        data.add_triangle(cap_center_id, (i < sectorCount - 1) ? cap_center_id + i + 2 : cap_center_id + 1, cap_center_id + i + 1);
    }

    // top cap vertices
    cap_center += h;
    cap_center_id = data.vertices_count();
    normal = -normal;

    data.add_vertex(cap_center, normal);
    for (unsigned int i = 0; i < sectorCount; ++i) {
        data.add_vertex(base_vertices[i] + h, normal);
    }

    // top cap triangles
    for (unsigned int i = 0; i < sectorCount; ++i) {
        data.add_triangle(cap_center_id, cap_center_id + i + 1, (i < sectorCount - 1) ? cap_center_id + i + 2 : cap_center_id + 1);
    }

    return data;
}

GLModel::Geometry smooth_torus(unsigned int primary_resolution, unsigned int secondary_resolution, float radius, float thickness)
{
    const unsigned int torus_sector_count = std::max<unsigned int>(4, primary_resolution);
    const float torus_sector_step = 2.0f * float(M_PI) / float(torus_sector_count);
    const unsigned int section_sector_count = std::max<unsigned int>(4, secondary_resolution);
    const float section_sector_step = 2.0f * float(M_PI) / float(section_sector_count);

    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
    data.reserve_vertices(torus_sector_count * section_sector_count);
    data.reserve_indices(torus_sector_count * section_sector_count * 2 * 3);

    // vertices
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const float section_angle = torus_sector_step * i;
        const float csa = std::cos(section_angle);
        const float ssa = std::sin(section_angle);
        const Vec3f section_center(radius * csa, radius * ssa, 0.0f);
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const float circle_angle = section_sector_step * j;
            const float thickness_xy = thickness * std::cos(circle_angle);
            const float thickness_z  = thickness * std::sin(circle_angle);
            const Vec3f v(thickness_xy * csa, thickness_xy * ssa, thickness_z);
            data.add_vertex(section_center + v, (Vec3f)v.normalized());
        }
    }

    // triangles
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const unsigned int ii = i * section_sector_count;
        const unsigned int ii_next = ((i + 1) % torus_sector_count) * section_sector_count;
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const unsigned int j_next = (j + 1) % section_sector_count;
            const unsigned int i0 = ii + j;
            const unsigned int i1 = ii_next + j;
            const unsigned int i2 = ii_next + j_next;
            const unsigned int i3 = ii + j_next;
            data.add_triangle(i0, i1, i2);
            data.add_triangle(i0, i2, i3);
        }
    }

    return data;
}

} // namespace GUI
} // namespace Slic3r
