#include "libslic3r/libslic3r.h"
#include "GLModel.hpp"

#include "3DScene.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

bool GL_Model::init_from(const GLModelInitializationData& data)
{
    assert(!data.positions.empty() && !data.triangles.empty());
    assert(data.positions.size() == data.normals.size());

    reset();

    // vertices/normals data
    std::vector<float> vertices(6 * data.positions.size());
    for (size_t i = 0; i < data.positions.size(); ++i) {
        ::memcpy(static_cast<void*>(&vertices[i * 6]), static_cast<const void*>(data.positions[i].data()), 3 * sizeof(float));
        ::memcpy(static_cast<void*>(&vertices[3 + i * 6]), static_cast<const void*>(data.normals[i].data()), 3 * sizeof(float));
    }

    // indices data
    std::vector<unsigned int> indices(3 * data.triangles.size());
    for (size_t i = 0; i < data.triangles.size(); ++i) {
        for (size_t j = 0; j < 3; ++j) {
            indices[i * 3 + j] = static_cast<unsigned int>(data.triangles[i][j]);
        }
    }

    m_indices_count = static_cast<unsigned int>(indices.size());

    send_to_gpu(vertices, indices);

    return true;
}

bool GL_Model::init_from(const TriangleMesh& mesh)
{
    auto get_normal = [](const std::array<stl_vertex, 3>& triangle) {
        return (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]).normalized();
    };

    reset();

    assert(!mesh.its.vertices.empty() && !mesh.its.indices.empty()); // call require_shared_vertices() before to pass the mesh to this method

    // vertices data -> load from mesh
    std::vector<float> vertices(6 * mesh.its.vertices.size());
    for (size_t i = 0; i < mesh.its.vertices.size(); ++i) {
        ::memcpy(static_cast<void*>(&vertices[i * 6]), static_cast<const void*>(mesh.its.vertices[i].data()), 3 * sizeof(float));
    }

    // indices/normals data -> load from mesh
    std::vector<unsigned int> indices(3 * mesh.its.indices.size());
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& triangle = mesh.its.indices[i];
        for (size_t j = 0; j < 3; ++j) {
            indices[i * 3 + j] = static_cast<unsigned int>(triangle[j]);
        }
        Vec3f normal = get_normal({ mesh.its.vertices[triangle[0]], mesh.its.vertices[triangle[1]], mesh.its.vertices[triangle[2]] });
        ::memcpy(static_cast<void*>(&vertices[3 + static_cast<size_t>(triangle[0]) * 6]), static_cast<const void*>(normal.data()), 3 * sizeof(float));
        ::memcpy(static_cast<void*>(&vertices[3 + static_cast<size_t>(triangle[1]) * 6]), static_cast<const void*>(normal.data()), 3 * sizeof(float));
        ::memcpy(static_cast<void*>(&vertices[3 + static_cast<size_t>(triangle[2]) * 6]), static_cast<const void*>(normal.data()), 3 * sizeof(float));
    }

    m_indices_count = static_cast<unsigned int>(indices.size());

    send_to_gpu(vertices, indices);

    return true;
}

void GL_Model::reset()
{
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
}

void GL_Model::render() const
{
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
}

void GL_Model::send_to_gpu(const std::vector<float>& vertices, const std::vector<unsigned int>& indices)
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

GLModelInitializationData stilized_arrow(int resolution, float tip_radius, float tip_height, float stem_radius, float stem_height)
{
    GLModelInitializationData data;

    float angle_step = 2.0f * M_PI / static_cast<float>(resolution);
    std::vector<float> cosines(resolution);
    std::vector<float> sines(resolution);

    for (int i = 0; i < resolution; ++i)
    {
        float angle = angle_step * static_cast<float>(i);
        cosines[i] = ::cos(angle);
        sines[i] = -::sin(angle);
    }

    // tip vertices/normals
    data.positions.emplace_back(0.0f, 0.0f, 0.0f);
    data.normals.emplace_back(-Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i)
    {
        data.positions.emplace_back(tip_radius * sines[i], tip_radius * cosines[i], tip_height);
        data.normals.emplace_back(sines[i], cosines[i], 0.0f);
    }

    // tip triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v3 = (i < resolution - 1) ? i + 2 : 1;
        data.triangles.emplace_back(0, v3, i + 1);
    }

    // tip cap outer perimeter vertices
    for (int i = 0; i < resolution; ++i)
    {
        data.positions.emplace_back(tip_radius * sines[i], tip_radius * cosines[i], tip_height);
        data.normals.emplace_back(Vec3f::UnitZ());
    }

    // tip cap inner perimeter vertices
    for (int i = 0; i < resolution; ++i)
    {
        data.positions.emplace_back(stem_radius * sines[i], stem_radius * cosines[i], tip_height);
        data.normals.emplace_back(Vec3f::UnitZ());
    }

    // tip cap triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v2 = (i < resolution - 1) ? i + resolution + 2 : resolution + 1;
        int v3 = (i < resolution - 1) ? i + 2 * resolution + 2 : 2 * resolution + 1;
        data.triangles.emplace_back(i + resolution + 1, v2, v3);
        data.triangles.emplace_back(i + resolution + 1, v3, i + 2 * resolution + 1);
    }

    // stem bottom vertices
    for (int i = 0; i < resolution; ++i)
    {
        data.positions.emplace_back(stem_radius * sines[i], stem_radius * cosines[i], tip_height);
        data.normals.emplace_back(sines[i], cosines[i], 0.0f);
    }

    float total_height = tip_height + stem_height;

    // stem top vertices
    for (int i = 0; i < resolution; ++i)
    {
        data.positions.emplace_back(stem_radius * sines[i], stem_radius * cosines[i], total_height);
        data.normals.emplace_back(sines[i], cosines[i], 0.0f);
    }

    // stem triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v2 = (i < resolution - 1) ? i + 3 * resolution + 2 : 3 * resolution + 1;
        int v3 = (i < resolution - 1) ? i + 4 * resolution + 2 : 4 * resolution + 1;
        data.triangles.emplace_back(i + 3 * resolution + 1, v2, v3);
        data.triangles.emplace_back(i + 3 * resolution + 1, v3, i + 4 * resolution + 1);
    }

    // stem cap vertices
    data.positions.emplace_back(0.0f, 0.0f, total_height);
    data.normals.emplace_back(Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i)
    {
        data.positions.emplace_back(stem_radius * sines[i], stem_radius * cosines[i], total_height);
        data.normals.emplace_back(Vec3f::UnitZ());
    }

    // stem cap triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v3 = (i < resolution - 1) ? i + 5 * resolution + 3 : 5 * resolution + 2;
        data.triangles.emplace_back(5 * resolution + 1, i + 5 * resolution + 2, v3);
    }

    return data;
}

} // namespace GUI
} // namespace Slic3r
