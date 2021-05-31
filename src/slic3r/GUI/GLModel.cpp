#include "libslic3r/libslic3r.h"
#include "GLModel.hpp"

#include "3DScene.hpp"
#if ENABLE_SEQUENTIAL_LIMITS
#include "GUI_App.hpp"
#include "GLShader.hpp"
#endif // ENABLE_SEQUENTIAL_LIMITS

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
void GLModel::init_from(const GLModelInitializationData& data)
{
    assert(!data.positions.empty() && !data.triangles.empty());
    assert(data.positions.size() == data.normals.size());

    if (m_vbo_id > 0) // call reset() if you want to reuse this model
        return;

    // vertices/normals data
    std::vector<float> vertices(6 * data.positions.size());
    for (size_t i = 0; i < data.positions.size(); ++i) {
        size_t offset = i * 6;
        ::memcpy(static_cast<void*>(&vertices[offset]), static_cast<const void*>(data.positions[i].data()), 3 * sizeof(float));
        ::memcpy(static_cast<void*>(&vertices[3 + offset]), static_cast<const void*>(data.normals[i].data()), 3 * sizeof(float));
    }

    // indices data
    std::vector<unsigned int> indices(3 * data.triangles.size());
    for (size_t i = 0; i < data.triangles.size(); ++i) {
        for (size_t j = 0; j < 3; ++j) {
            indices[i * 3 + j] = static_cast<unsigned int>(data.triangles[i][j]);
        }
    }

    m_indices_count = static_cast<unsigned int>(indices.size());
    m_bounding_box = BoundingBoxf3();
    for (size_t i = 0; i < data.positions.size(); ++i) {
        m_bounding_box.merge(data.positions[i].cast<double>());
    }

    send_to_gpu(vertices, indices);
#endif // ENABLE_SEQUENTIAL_LIMITS
}

void GLModel::init_from(const TriangleMesh& mesh)
{
#if ENABLE_SEQUENTIAL_LIMITS
    if (!m_render_data.empty()) // call reset() if you want to reuse this model
        return;

    RenderData data;
    data.type = PrimitiveType::Triangles;

    std::vector<float> vertices = std::vector<float>(18 * mesh.stl.stats.number_of_facets);
    std::vector<unsigned int> indices = std::vector<unsigned int>(3 * mesh.stl.stats.number_of_facets);

    unsigned int vertices_count = 0;
    for (uint32_t i = 0; i < mesh.stl.stats.number_of_facets; ++i) {
        const stl_facet& facet = mesh.stl.facet_start[i];
        for (size_t j = 0; j < 3; ++j) {
            size_t offset = i * 18 + j * 6;
            ::memcpy(static_cast<void*>(&vertices[offset]), static_cast<const void*>(facet.vertex[j].data()), 3 * sizeof(float));
            ::memcpy(static_cast<void*>(&vertices[3 + offset]), static_cast<const void*>(facet.normal.data()), 3 * sizeof(float));
        }
        for (size_t j = 0; j < 3; ++j) {
            indices[i * 3 + j] = vertices_count + j;
        }
        vertices_count += 3;
    }

    data.indices_count = static_cast<unsigned int>(indices.size());
    m_bounding_box = mesh.bounding_box();

    send_to_gpu(data, vertices, indices);
    m_render_data.emplace_back(data);
#else
    if (m_vbo_id > 0) // call reset() if you want to reuse this model
        return;

    std::vector<float> vertices = std::vector<float>(18 * mesh.stl.stats.number_of_facets);
    std::vector<unsigned int> indices = std::vector<unsigned int>(3 * mesh.stl.stats.number_of_facets);

    unsigned int vertices_count = 0;
    for (uint32_t i = 0; i < mesh.stl.stats.number_of_facets; ++i) {
        const stl_facet& facet = mesh.stl.facet_start[i];
        for (uint32_t j = 0; j < 3; ++j) {
            uint32_t offset = i * 18 + j * 6;
            ::memcpy(static_cast<void*>(&vertices[offset]), static_cast<const void*>(facet.vertex[j].data()), 3 * sizeof(float));
            ::memcpy(static_cast<void*>(&vertices[3 + offset]), static_cast<const void*>(facet.normal.data()), 3 * sizeof(float));
        }
        for (uint32_t j = 0; j < 3; ++j) {
            indices[i * 3 + j] = vertices_count + j;
        }
        vertices_count += 3;
    }

    m_indices_count = static_cast<unsigned int>(indices.size());
    m_bounding_box = mesh.bounding_box();

    send_to_gpu(vertices, indices);
#endif // ENABLE_SEQUENTIAL_LIMITS
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

    init_from(model.mesh());

    m_filename = filename;

    return true;
}

#if ENABLE_SEQUENTIAL_LIMITS
void GLModel::set_color(int entity_id, const std::array<float, 4>& color)
{
    for (size_t i = 0; i < m_render_data.size(); ++i) {
        if (entity_id == -1 || static_cast<int>(i) == entity_id)
            m_render_data[i].color = color;
    }
}
#endif // ENABLE_SEQUENTIAL_LIMITS

void GLModel::reset()
{
#if ENABLE_SEQUENTIAL_LIMITS
    for (RenderData& data : m_render_data) {
        // release gpu memory
        if (data.ibo_id > 0)
            glsafe(::glDeleteBuffers(1, &data.ibo_id));
        if (data.vbo_id > 0)
            glsafe(::glDeleteBuffers(1, &data.vbo_id));
    }

    m_render_data.clear();
#else
    // release gpu memory
    if (m_ibo_id > 0) {
        glsafe(::glDeleteBuffers(1, &m_ibo_id));
        m_ibo_id = 0;
    }

    if (m_vbo_id > 0) {
        glsafe(::glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }

    m_indices_count = 0;
#endif // ENABLE_SEQUENTIAL_LIMITS
    m_bounding_box = BoundingBoxf3();
    m_filename = std::string();
}

void GLModel::render() const
{
#if ENABLE_SEQUENTIAL_LIMITS
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

        GLShaderProgram* shader = wxGetApp().get_current_shader();
        if (shader != nullptr)
            shader->set_uniform("uniform_color", data.color);

        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ibo_id));
        glsafe(::glDrawElements(mode, static_cast<GLsizei>(data.indices_count), GL_UNSIGNED_INT, (const void*)0));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

        glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    }
#else
    if (m_vbo_id == 0 || m_ibo_id == 0)
        return;

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)0));
    glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float))));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo_id));
    glsafe(::glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices_count), GL_UNSIGNED_INT, (const void*)0));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
#endif // ENABLE_SEQUENTIAL_LIMITS
}

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
void GLModel::send_to_gpu(const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
{
    // vertex data -> send to gpu
    glsafe(::glGenBuffers(1, &m_vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

    // indices data -> send to gpu
    glsafe(::glGenBuffers(1, &m_ibo_id));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo_id));
    glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}
#endif // ENABLE_SEQUENTIAL_LIMITS

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
GLModelInitializationData stilized_arrow(int resolution, float tip_radius, float tip_height, float stem_radius, float stem_height)
{
    auto append_vertex = [](GLModelInitializationData& data, const Vec3f& position, const Vec3f& normal) {
        data.positions.emplace_back(position);
        data.normals.emplace_back(normal);
    };
#endif // ENABLE_SEQUENTIAL_LIMITS

    resolution = std::max(4, resolution);

#if ENABLE_SEQUENTIAL_LIMITS
    GLModel::InitializationData data;
    GLModel::InitializationData::Entity entity;
    entity.type = GLModel::PrimitiveType::Triangles;
#else
    GLModelInitializationData data;
#endif // ENABLE_SEQUENTIAL_LIMITS

    const float angle_step = 2.0f * M_PI / static_cast<float>(resolution);
    std::vector<float> cosines(resolution);
    std::vector<float> sines(resolution);

    for (int i = 0; i < resolution; ++i) {
        const float angle = angle_step * static_cast<float>(i);
        cosines[i] = ::cos(angle);
        sines[i] = -::sin(angle);
    }

    const float total_height = tip_height + stem_height;

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
    // tip vertices/normals
    append_vertex(data, { 0.0f, 0.0f, total_height }, Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i) {
        append_vertex(data, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // tip triangles
    for (int i = 0; i < resolution; ++i) {
        int v3 = (i < resolution - 1) ? i + 2 : 1;
        data.triangles.emplace_back(0, i + 1, v3);
    }

    // tip cap outer perimeter vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(data, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap inner perimeter vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap triangles
    for (int i = 0; i < resolution; ++i) {
        int v2 = (i < resolution - 1) ? i + resolution + 2 : resolution + 1;
        int v3 = (i < resolution - 1) ? i + 2 * resolution + 2 : 2 * resolution + 1;
        data.triangles.emplace_back(i + resolution + 1, v3, v2);
        data.triangles.emplace_back(i + resolution + 1, i + 2 * resolution + 1, v3);
    }

    // stem bottom vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // stem top vertices
    for (int i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, { sines[i], cosines[i], 0.0f });
    }

    // stem triangles
    for (int i = 0; i < resolution; ++i) {
        int v2 = (i < resolution - 1) ? i + 3 * resolution + 2 : 3 * resolution + 1;
        int v3 = (i < resolution - 1) ? i + 4 * resolution + 2 : 4 * resolution + 1;
        data.triangles.emplace_back(i + 3 * resolution + 1, v3, v2);
        data.triangles.emplace_back(i + 3 * resolution + 1, i + 4 * resolution + 1, v3);
    }

    // stem cap vertices
    append_vertex(data, Vec3f::Zero(), -Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, -Vec3f::UnitZ());
    }

    // stem cap triangles
    for (int i = 0; i < resolution; ++i) {
        int v3 = (i < resolution - 1) ? i + 5 * resolution + 3 : 5 * resolution + 2;
        data.triangles.emplace_back(5 * resolution + 1, v3, i + 5 * resolution + 2);
    }
#endif // ENABLE_SEQUENTIAL_LIMITS

    return data;
}

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
GLModelInitializationData circular_arrow(int resolution, float radius, float tip_height, float tip_width, float stem_width, float thickness)
{
    auto append_vertex = [](GLModelInitializationData& data, const Vec3f& position, const Vec3f& normal) {
        data.positions.emplace_back(position);
        data.normals.emplace_back(normal);
    };
#endif // ENABLE_SEQUENTIAL_LIMITS

    resolution = std::max(2, resolution);

#if ENABLE_SEQUENTIAL_LIMITS
    GLModel::InitializationData data;
    GLModel::InitializationData::Entity entity;
    entity.type = GLModel::PrimitiveType::Triangles;
#else
    GLModelInitializationData data;
#endif // ENABLE_SEQUENTIAL_LIMITS

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;

    const float outer_radius = radius + half_stem_width;
    const float inner_radius = radius - half_stem_width;
    const float step_angle = 0.5f * PI / static_cast<float>(resolution);

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
    // tip
    // top face vertices
    append_vertex(data, { 0.0f, outer_radius, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius + half_tip_width, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { -tip_height, radius, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius - half_tip_width, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0f, inner_radius, half_thickness }, Vec3f::UnitZ());

    // top face triangles
    data.triangles.emplace_back(0, 1, 2);
    data.triangles.emplace_back(0, 2, 4);
    data.triangles.emplace_back(4, 2, 3);

    // bottom face vertices
    append_vertex(data, { 0.0f, outer_radius, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius + half_tip_width, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -tip_height, radius, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius - half_tip_width, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0f, inner_radius, -half_thickness }, -Vec3f::UnitZ());

    // bottom face triangles
    data.triangles.emplace_back(5, 7, 6);
    data.triangles.emplace_back(5, 9, 7);
    data.triangles.emplace_back(9, 8, 7);

    // side faces vertices
    append_vertex(data, { 0.0f, outer_radius, -half_thickness }, Vec3f::UnitX());
    append_vertex(data, { 0.0f, radius + half_tip_width, -half_thickness }, Vec3f::UnitX());
    append_vertex(data, { 0.0f, outer_radius, half_thickness }, Vec3f::UnitX());
    append_vertex(data, { 0.0f, radius + half_tip_width, half_thickness }, Vec3f::UnitX());

    Vec3f normal(-half_tip_width, tip_height, 0.0f);
    normal.normalize();
    append_vertex(data, { 0.0f, radius + half_tip_width, -half_thickness }, normal);
    append_vertex(data, { -tip_height, radius, -half_thickness }, normal);
    append_vertex(data, { 0.0f, radius + half_tip_width, half_thickness }, normal);
    append_vertex(data, { -tip_height, radius, half_thickness }, normal);

    normal = Vec3f(-half_tip_width, -tip_height, 0.0f);
    normal.normalize();
    append_vertex(data, { -tip_height, radius, -half_thickness }, normal);
    append_vertex(data, { 0.0f, radius - half_tip_width, -half_thickness }, normal);
    append_vertex(data, { -tip_height, radius, half_thickness }, normal);
    append_vertex(data, { 0.0f, radius - half_tip_width, half_thickness }, normal);

    append_vertex(data, { 0.0f, radius - half_tip_width, -half_thickness }, Vec3f::UnitX());
    append_vertex(data, { 0.0f, inner_radius, -half_thickness }, Vec3f::UnitX());
    append_vertex(data, { 0.0f, radius - half_tip_width, half_thickness }, Vec3f::UnitX());
    append_vertex(data, { 0.0f, inner_radius, half_thickness }, Vec3f::UnitX());

    // side face triangles
    for (int i = 0; i < 4; ++i) {
        int ii = i * 4;
        data.triangles.emplace_back(10 + ii, 11 + ii, 13 + ii);
        data.triangles.emplace_back(10 + ii, 13 + ii, 12 + ii);
    }

    // stem
    // top face vertices
    for (int i = 0; i <= resolution; ++i) {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    for (int i = 0; i <= resolution; ++i) {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    // top face triangles
    for (int i = 0; i < resolution; ++i) {
        data.triangles.emplace_back(26 + i, 27 + i, 27 + resolution + i);
        data.triangles.emplace_back(27 + i, 28 + resolution + i, 27 + resolution + i);
    }

    // bottom face vertices
    for (int i = 0; i <= resolution; ++i) {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    for (int i = 0; i <= resolution; ++i) {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    // bottom face triangles
    for (int i = 0; i < resolution; ++i) {
        data.triangles.emplace_back(28 + 2 * resolution + i, 29 + 3 * resolution + i, 29 + 2 * resolution + i);
        data.triangles.emplace_back(29 + 2 * resolution + i, 29 + 3 * resolution + i, 30 + 3 * resolution + i);
    }

    // side faces vertices and triangles
    for (int i = 0; i <= resolution; ++i) {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { inner_radius * s, inner_radius * c, -half_thickness }, { -s, -c, 0.0f });
    }

    for (int i = 0; i <= resolution; ++i) {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { inner_radius * s, inner_radius * c, half_thickness }, { -s, -c, 0.0f });
    }

    int first_id = 26 + 4 * (resolution + 1);
    for (int i = 0; i < resolution; ++i) {
        int ii = first_id + i;
        data.triangles.emplace_back(ii, ii + 1, ii + resolution + 2);
        data.triangles.emplace_back(ii, ii + resolution + 2, ii + resolution + 1);
    }

    append_vertex(data, { inner_radius, 0.0f, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { outer_radius, 0.0f, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { inner_radius, 0.0f, half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { outer_radius, 0.0f, half_thickness }, -Vec3f::UnitY());

    first_id = 26 + 6 * (resolution + 1);
    data.triangles.emplace_back(first_id, first_id + 1, first_id + 3);
    data.triangles.emplace_back(first_id, first_id + 3, first_id + 2);

    for (int i = resolution; i >= 0; --i) {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { outer_radius * s, outer_radius * c, -half_thickness }, { s, c, 0.0f });
    }

    for (int i = resolution; i >= 0; --i) {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { outer_radius * s, outer_radius * c, +half_thickness }, { s, c, 0.0f });
    }

    first_id = 30 + 6 * (resolution + 1);
    for (int i = 0; i < resolution; ++i) {
        int ii = first_id + i;
        data.triangles.emplace_back(ii, ii + 1, ii + resolution + 2);
        data.triangles.emplace_back(ii, ii + resolution + 2, ii + resolution + 1);
    }
#endif // ENABLE_SEQUENTIAL_LIMITS

    return data;
}

#if ENABLE_SEQUENTIAL_LIMITS
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
#else
GLModelInitializationData straight_arrow(float tip_width, float tip_height, float stem_width, float stem_height, float thickness)
{
    auto append_vertex = [](GLModelInitializationData& data, const Vec3f& position, const Vec3f& normal) {
        data.positions.emplace_back(position);
        data.normals.emplace_back(normal);
    };

    GLModelInitializationData data;
#endif // ENABLE_SEQUENTIAL_LIMITS

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;
    const float total_height = tip_height + stem_height;

    // top face vertices
#if ENABLE_SEQUENTIAL_LIMITS
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
#else
    append_vertex(data, { half_stem_width, 0.0, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { half_stem_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { half_tip_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0, total_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { -half_tip_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { -half_stem_width, stem_height, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { -half_stem_width, 0.0, half_thickness }, Vec3f::UnitZ());

    // top face triangles
    data.triangles.emplace_back(0, 1, 6);
    data.triangles.emplace_back(6, 1, 5);
    data.triangles.emplace_back(4, 5, 3);
    data.triangles.emplace_back(5, 1, 3);
    data.triangles.emplace_back(1, 2, 3);

    // bottom face vertices
    append_vertex(data, { half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0, total_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitZ());

    // bottom face triangles
    data.triangles.emplace_back(7, 13, 8);
    data.triangles.emplace_back(13, 12, 8);
    data.triangles.emplace_back(12, 11, 10);
    data.triangles.emplace_back(8, 12, 10);
    data.triangles.emplace_back(9, 8, 10);

    // side faces vertices
    append_vertex(data, { half_stem_width, 0.0, -half_thickness }, Vec3f::UnitX());
    append_vertex(data, { half_stem_width, stem_height, -half_thickness }, Vec3f::UnitX());
    append_vertex(data, { half_stem_width, 0.0, half_thickness }, Vec3f::UnitX());
    append_vertex(data, { half_stem_width, stem_height, half_thickness }, Vec3f::UnitX());

    append_vertex(data, { half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { half_stem_width, stem_height, half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { half_tip_width, stem_height, half_thickness }, -Vec3f::UnitY());

    Vec3f normal(tip_height, half_tip_width, 0.0f);
    normal.normalize();
    append_vertex(data, { half_tip_width, stem_height, -half_thickness }, normal);
    append_vertex(data, { 0.0, total_height, -half_thickness }, normal);
    append_vertex(data, { half_tip_width, stem_height, half_thickness }, normal);
    append_vertex(data, { 0.0, total_height, half_thickness }, normal);

    normal = Vec3f(-tip_height, half_tip_width, 0.0f);
    normal.normalize();
    append_vertex(data, { 0.0, total_height, -half_thickness }, normal);
    append_vertex(data, { -half_tip_width, stem_height, -half_thickness }, normal);
    append_vertex(data, { 0.0, total_height, half_thickness }, normal);
    append_vertex(data, { -half_tip_width, stem_height, half_thickness }, normal);

    append_vertex(data, { -half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { -half_tip_width, stem_height, half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { -half_stem_width, stem_height, half_thickness }, -Vec3f::UnitY());

    append_vertex(data, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitX());
    append_vertex(data, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitX());
    append_vertex(data, { -half_stem_width, stem_height, half_thickness }, -Vec3f::UnitX());
    append_vertex(data, { -half_stem_width, 0.0, half_thickness }, -Vec3f::UnitX());

    append_vertex(data, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { -half_stem_width, 0.0, half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { half_stem_width, 0.0, half_thickness }, -Vec3f::UnitY());

    // side face triangles
    for (int i = 0; i < 7; ++i) {
        int ii = i * 4;
        data.triangles.emplace_back(14 + ii, 15 + ii, 17 + ii);
        data.triangles.emplace_back(14 + ii, 17 + ii, 16 + ii);
    }
#endif // ENABLE_SEQUENTIAL_LIMITS

    return data;
}

} // namespace GUI
} // namespace Slic3r
