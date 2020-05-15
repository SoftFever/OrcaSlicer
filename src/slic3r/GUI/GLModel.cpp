#include "libslic3r/libslic3r.h"
#include "GLModel.hpp"

#include "3DScene.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

void GL_Model::init_from(const GLModelInitializationData& data)
{
    assert(!data.positions.empty() && !data.triangles.empty());
    assert(data.positions.size() == data.normals.size());

    if (m_vbo_id > 0) // call reset() if you want to reuse this model
        return;

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
    m_bounding_box = BoundingBoxf3();
    for (size_t i = 0; i < data.positions.size(); ++i) {
        m_bounding_box.merge(data.positions[i].cast<double>());
    }

    send_to_gpu(vertices, indices);
}

void GL_Model::init_from(const TriangleMesh& mesh)
{
    auto get_normal = [](const std::array<stl_vertex, 3>& triangle) {
        return (triangle[1] - triangle[0]).cross(triangle[2] - triangle[0]).normalized();
    };

    if (m_vbo_id > 0) // call reset() if you want to reuse this model
        return;

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
    m_bounding_box = mesh.bounding_box();

    send_to_gpu(vertices, indices);
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
    m_bounding_box = BoundingBoxf3();
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
    auto append_vertex = [](GLModelInitializationData& data, const Vec3f& position, const Vec3f& normal) {
        data.positions.emplace_back(position);
        data.normals.emplace_back(normal);
    };

    resolution = std::max(4, resolution);

    GLModelInitializationData data;

    const float angle_step = 2.0f * M_PI / static_cast<float>(resolution);
    std::vector<float> cosines(resolution);
    std::vector<float> sines(resolution);

    for (int i = 0; i < resolution; ++i)
    {
        float angle = angle_step * static_cast<float>(i);
        cosines[i] = ::cos(angle);
        sines[i] = -::sin(angle);
    }

    const float total_height = tip_height + stem_height;

    // tip vertices/normals
    append_vertex(data, { 0.0f, 0.0f, total_height }, Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i)
    {
        append_vertex(data, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // tip triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v3 = (i < resolution - 1) ? i + 2 : 1;
        data.triangles.emplace_back(0, i + 1, v3);
    }

    // tip cap outer perimeter vertices
    for (int i = 0; i < resolution; ++i)
    {
        append_vertex(data, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap inner perimeter vertices
    for (int i = 0; i < resolution; ++i)
    {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v2 = (i < resolution - 1) ? i + resolution + 2 : resolution + 1;
        int v3 = (i < resolution - 1) ? i + 2 * resolution + 2 : 2 * resolution + 1;
        data.triangles.emplace_back(i + resolution + 1, v3, v2);
        data.triangles.emplace_back(i + resolution + 1, i + 2 * resolution + 1, v3);
    }

    // stem bottom vertices
    for (int i = 0; i < resolution; ++i)
    {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // stem top vertices
    for (int i = 0; i < resolution; ++i)
    {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, { sines[i], cosines[i], 0.0f });
    }

    // stem triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v2 = (i < resolution - 1) ? i + 3 * resolution + 2 : 3 * resolution + 1;
        int v3 = (i < resolution - 1) ? i + 4 * resolution + 2 : 4 * resolution + 1;
        data.triangles.emplace_back(i + 3 * resolution + 1, v3, v2);
        data.triangles.emplace_back(i + 3 * resolution + 1, i + 4 * resolution + 1, v3);
    }

    // stem cap vertices
    append_vertex(data, Vec3f::Zero(), -Vec3f::UnitZ());
    for (int i = 0; i < resolution; ++i)
    {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, -Vec3f::UnitZ());
    }

    // stem cap triangles
    for (int i = 0; i < resolution; ++i)
    {
        int v3 = (i < resolution - 1) ? i + 5 * resolution + 3 : 5 * resolution + 2;
        data.triangles.emplace_back(5 * resolution + 1, v3, i + 5 * resolution + 2);
    }

    return data;
}

GLModelInitializationData circular_arrow(int resolution, float radius, float tip_height, float tip_width, float stem_width, float thickness)
{
    auto append_vertex = [](GLModelInitializationData& data, const Vec3f& position, const Vec3f& normal) {
        data.positions.emplace_back(position);
        data.normals.emplace_back(normal);
    };

    resolution = std::max(2, resolution);

    GLModelInitializationData data;

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;

    const float outer_radius = radius + half_stem_width;
    const float inner_radius = radius - half_stem_width;
    const float step_angle = 0.5f * PI / static_cast<float>(resolution);

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
    for (int i = 0; i < 4; ++i)
    {
        int ii = i * 4;
        data.triangles.emplace_back(10 + ii, 11 + ii, 13 + ii);
        data.triangles.emplace_back(10 + ii, 13 + ii, 12 + ii);
    }

    // stem
    // top face vertices
    for (int i = 0; i <= resolution; ++i)
    {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    for (int i = 0; i <= resolution; ++i)
    {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    // top face triangles
    for (int i = 0; i < resolution; ++i)
    {
        data.triangles.emplace_back(26 + i, 27 + i, 27 + resolution + i);
        data.triangles.emplace_back(27 + i, 28 + resolution + i, 27 + resolution + i);
    }

    // bottom face vertices
    for (int i = 0; i <= resolution; ++i)
    {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    for (int i = 0; i <= resolution; ++i)
    {
        float angle = static_cast<float>(i) * step_angle;
        append_vertex(data, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    // bottom face triangles
    for (int i = 0; i < resolution; ++i)
    {
        data.triangles.emplace_back(28 + 2 * resolution + i, 29 + 3 * resolution + i, 29 + 2 * resolution + i);
        data.triangles.emplace_back(29 + 2 * resolution + i, 29 + 3 * resolution + i, 30 + 3 * resolution + i);
    }

    // side faces vertices and triangles
    for (int i = 0; i <= resolution; ++i)
    {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { inner_radius * s, inner_radius * c, -half_thickness }, { -s, -c, 0.0f });
    }

    for (int i = 0; i <= resolution; ++i)
    {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { inner_radius * s, inner_radius * c, half_thickness }, { -s, -c, 0.0f });
    }

    int first_id = 26 + 4 * (resolution + 1);
    for (int i = 0; i < resolution; ++i)
    {
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

    for (int i = resolution; i >= 0; --i)
    {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { outer_radius * s, outer_radius * c, -half_thickness }, { s, c, 0.0f });
    }

    for (int i = resolution; i >= 0; --i)
    {
        float angle = static_cast<float>(i) * step_angle;
        float c = ::cos(angle);
        float s = ::sin(angle);
        append_vertex(data, { outer_radius * s, outer_radius * c, +half_thickness }, { s, c, 0.0f });
    }

    first_id = 30 + 6 * (resolution + 1);
    for (int i = 0; i < resolution; ++i)
    {
        int ii = first_id + i;
        data.triangles.emplace_back(ii, ii + 1, ii + resolution + 2);
        data.triangles.emplace_back(ii, ii + resolution + 2, ii + resolution + 1);
    }

    return data;
}

GLModelInitializationData straight_arrow(float tip_width, float tip_height, float stem_width, float stem_height, float thickness)
{
    auto append_vertex = [](GLModelInitializationData& data, const Vec3f& position, const Vec3f& normal) {
        data.positions.emplace_back(position);
        data.normals.emplace_back(normal);
    };

    GLModelInitializationData data;

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;
    const float total_height = tip_height + stem_height;

    // top face vertices
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
    for (int i = 0; i < 7; ++i)
    {
        int ii = i * 4;
        data.triangles.emplace_back(14 + ii, 15 + ii, 17 + ii);
        data.triangles.emplace_back(14 + ii, 17 + ii, 16 + ii);
    }

    return data;
}

} // namespace GUI
} // namespace Slic3r
