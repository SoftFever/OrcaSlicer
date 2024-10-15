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
void GLModel::Geometry::add_vertex(const Vec2f &position)
{
    assert(format.vertex_layout == EVertexLayout::P2);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
}

void GLModel::Geometry::add_vertex(const Vec2f &position, const Vec2f &tex_coord)
{
    assert(format.vertex_layout == EVertexLayout::P2T2);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(tex_coord.x());
    vertices.emplace_back(tex_coord.y());
}

void GLModel::Geometry::add_vertex(const Vec3f &position)
{
    assert(format.vertex_layout == EVertexLayout::P3);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
}

void GLModel::Geometry::add_vertex(const Vec3f &position, const Vec2f &tex_coord)
{
    assert(format.vertex_layout == EVertexLayout::P3T2);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(tex_coord.x());
    vertices.emplace_back(tex_coord.y());
}

void GLModel::Geometry::add_vertex(const Vec3f &position, const Vec3f &normal)
{
    assert(format.vertex_layout == EVertexLayout::P3N3);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(normal.x());
    vertices.emplace_back(normal.y());
    vertices.emplace_back(normal.z());
}

void GLModel::Geometry::add_vertex(const Vec3f &position, const Vec3f &normal, const Vec2f &tex_coord)
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

void GLModel::Geometry::add_vertex(const Vec4f &position)
{
    assert(format.vertex_layout == EVertexLayout::P4);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(position.w());
}

void GLModel::Geometry::add_index(unsigned int id) { indices.emplace_back(id); }

void GLModel::Geometry::add_line(unsigned int id1, unsigned int id2)
{
    indices.emplace_back(id1);
    indices.emplace_back(id2);
}

void GLModel::Geometry::add_triangle(unsigned int id1, unsigned int id2, unsigned int id3)
{
    indices.emplace_back(id1);
    indices.emplace_back(id2);
    indices.emplace_back(id3);
}

Vec2f GLModel::Geometry::extract_position_2(size_t id) const
{
    const size_t p_stride = position_stride_floats(format);
    if (p_stride != 2) {
        assert(false);
        return {FLT_MAX, FLT_MAX};
    }

    if (vertices_count() <= id) {
        assert(false);
        return {FLT_MAX, FLT_MAX};
    }

    const float *start = &vertices[id * vertex_stride_floats(format) + position_offset_floats(format)];
    return {*(start + 0), *(start + 1)};
}

Vec3f GLModel::Geometry::extract_position_3(size_t id) const
{
    const size_t p_stride = position_stride_floats(format);
    if (p_stride != 3) {
        assert(false);
        return {FLT_MAX, FLT_MAX, FLT_MAX};
    }

    if (vertices_count() <= id) {
        assert(false);
        return {FLT_MAX, FLT_MAX, FLT_MAX};
    }

    const float *start = &vertices[id * vertex_stride_floats(format) + position_offset_floats(format)];
    return {*(start + 0), *(start + 1), *(start + 2)};
}

Vec3f GLModel::Geometry::extract_normal_3(size_t id) const
{
    const size_t n_stride = normal_stride_floats(format);
    if (n_stride != 3) {
        assert(false);
        return {FLT_MAX, FLT_MAX, FLT_MAX};
    }

    if (vertices_count() <= id) {
        assert(false);
        return {FLT_MAX, FLT_MAX, FLT_MAX};
    }

    const float *start = &vertices[id * vertex_stride_floats(format) + normal_offset_floats(format)];
    return {*(start + 0), *(start + 1), *(start + 2)};
}

Vec2f GLModel::Geometry::extract_tex_coord_2(size_t id) const
{
    const size_t t_stride = tex_coord_stride_floats(format);
    if (t_stride != 2) {
        assert(false);
        return {FLT_MAX, FLT_MAX};
    }

    if (vertices_count() <= id) {
        assert(false);
        return {FLT_MAX, FLT_MAX};
    }

    const float *start = &vertices[id * vertex_stride_floats(format) + tex_coord_offset_floats(format)];
    return {*(start + 0), *(start + 1)};
}

void GLModel::Geometry::set_vertex(size_t id, const Vec3f &position, const Vec3f &normal)
{
    assert(format.vertex_layout == EVertexLayout::P3N3);
    assert(id < vertices_count());
    if (id < vertices_count()) {
        float *start = &vertices[id * vertex_stride_floats(format)];
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
    if (id < indices_count()) indices[id] = index;
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
        const size_t                       stride = vertex_stride_floats(format);
        std::vector<float>::const_iterator it     = vertices.begin() + id * stride;
        vertices.erase(it, it + stride);
    }
}

indexed_triangle_set GLModel::Geometry::get_as_indexed_triangle_set() const
{
    indexed_triangle_set its;
    its.vertices.reserve(vertices_count());
    for (size_t i = 0; i < vertices_count(); ++i) { its.vertices.emplace_back(extract_position_3(i)); }
    its.indices.reserve(indices_count() / 3);
    for (size_t i = 0; i < indices_count() / 3; ++i) {
        const size_t tri_id = i * 3;
        its.indices.emplace_back(extract_index(tri_id), extract_index(tri_id + 1), extract_index(tri_id + 2));
    }
    return its;
}

size_t GLModel::Geometry::vertex_stride_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2: {
        return 2;
    }
    case EVertexLayout::P2T2: {
        return 4;
    }
    case EVertexLayout::P3: {
        return 3;
    }
    case EVertexLayout::P3T2: {
        return 5;
    }
    case EVertexLayout::P3N3: {
        return 6;
    }
    case EVertexLayout::P3N3T2: {
        return 8;
    }
    case EVertexLayout::P4: {
        return 4;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::position_stride_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2: {
        return 2;
    }
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2: {
        return 3;
    }
    case EVertexLayout::P4: {
        return 4;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::position_offset_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P4: {
        return 0;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::normal_stride_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2: {
        return 3;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::normal_offset_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2: {
        return 3;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::tex_coord_stride_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2T2:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3T2: {
        return 2;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::tex_coord_offset_floats(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2T2: {
        return 2;
    }
    case EVertexLayout::P3T2: {
        return 3;
    }
    case EVertexLayout::P3N3T2: {
        return 6;
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

size_t GLModel::Geometry::index_stride_bytes(const Geometry &data)
{
    switch (data.index_type) {
    case EIndexType::UINT: {
        return sizeof(unsigned int);
    }
    case EIndexType::USHORT: {
        return sizeof(unsigned short);
    }
    case EIndexType::UBYTE: {
        return sizeof(unsigned char);
    }
    default: {
        assert(false);
        return 0;
    }
    };
}

bool GLModel::Geometry::has_position(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2:
    case EVertexLayout::P4: {
        return true;
    }
    default: {
        assert(false);
        return false;
    }
    };
}

bool GLModel::Geometry::has_normal(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P4: {
        return false;
    }
    case EVertexLayout::P3N3:
    case EVertexLayout::P3N3T2: {
        return true;
    }
    default: {
        assert(false);
        return false;
    }
    };
}

bool GLModel::Geometry::has_tex_coord(const Format &format)
{
    switch (format.vertex_layout) {
    case EVertexLayout::P2T2:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3T2: {
        return true;
    }
    case EVertexLayout::P2:
    case EVertexLayout::P3:
    case EVertexLayout::P3N3:
    case EVertexLayout::P4: {
        return false;
    }
    default: {
        assert(false);
        return false;
    }
    };
}

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

GLModel::~GLModel()
{
    reset();
    if (mesh) { delete mesh; }
}

size_t GLModel::get_vertices_count(int i) const {
    if (m_render_data.empty() || i >= m_render_data.size()) {
        return 0;
    }
    return m_render_data[i].vertices_count > 0 ? m_render_data[i].vertices_count : m_render_data[i].geometry.vertices_count();
}

size_t GLModel::get_indices_count(int i) const {
    if (m_render_data.empty() || i >= m_render_data.size()) {
        return 0;
    }
    return m_render_data[i].indices_count > 0 ? m_render_data[i].indices_count : m_render_data[i].geometry.indices_count();
}

void GLModel::init_from(Geometry &&data, bool generate_mesh)
{
    if (is_initialized()) {
        // call reset() if you want to reuse this model
        assert(false);
        return;
    }

    if (data.vertices.empty() || data.indices.empty()) {
        assert(false);
        return;
    }
    m_render_data.clear();
    m_render_data.push_back(RenderData());
    m_render_data.back().indices_count = data.indices.size();
    m_render_data.back().vertices_count = data.vertices.size();
    m_render_data.back().type          = data.format.type;
    m_render_data.back().color         = data.color.get_data();
    if (generate_mesh) {
        if (!mesh) { mesh = new TriangleMesh(); }
        mesh->its = std::move(data.get_as_indexed_triangle_set());
    }
    m_render_data.back().geometry = std::move(data);
    // update bounding box
    for (size_t i = 0; i < data.vertices_count(); ++i) {
        const size_t position_stride = Geometry::position_stride_floats(data.format);
        if (position_stride == 3)
            m_bounding_box.merge(m_render_data.back().geometry.extract_position_3(i).cast<double>());
        else if (position_stride == 2) {
            const Vec2f position = m_render_data.back().geometry.extract_position_2(i);
            m_bounding_box.merge(Vec3f(position.x(), position.y(), 0.0f).cast<double>());
        }
    }
}

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

bool GLModel::init_model_from_poly(const std::vector<Vec2f> &triangles, float z, bool generate_mesh)
{
    if (triangles.empty() || triangles.size() % 3 != 0)
        return false;

    GLModel::Geometry init_data;
    init_data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3T2};
    init_data.reserve_vertices(triangles.size());
    init_data.reserve_indices(triangles.size() / 3);

    Vec2f min = triangles.front();
    Vec2f max = min;
    for (const Vec2f &v : triangles) {
        min = min.cwiseMin(v).eval();
        max = max.cwiseMax(v).eval();
    }

    const Vec2f size = max - min;
    if (size.x() <= 0.0f || size.y() <= 0.0f)
        return false;

    Vec2f inv_size = size.cwiseInverse();
    inv_size.y() *= -1.0f;

    // vertices + indices
    unsigned int vertices_counter = 0;
    for (const Vec2f &v : triangles) {
        const Vec3f p = {v.x(), v.y(), z};
        init_data.add_vertex(p, (Vec2f) (v - min).cwiseProduct(inv_size).eval());
        ++vertices_counter;
        if (vertices_counter % 3 == 0)
            init_data.add_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
    }
    init_from(std::move(init_data), generate_mesh);
    return true;
}

bool GLModel::init_model_from_lines(const Lines &lines, float z, bool generate_mesh)
{
    GLModel::Geometry init_data;
    init_data.format = {GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
    init_data.reserve_vertices(2 * lines.size());
    init_data.reserve_indices(2 * lines.size());

    for (const auto &l : lines) {
        init_data.add_vertex(Vec3f(unscale<float>(l.a.x()), unscale<float>(l.a.y()), z));
        init_data.add_vertex(Vec3f(unscale<float>(l.b.x()), unscale<float>(l.b.y()), z));
        const unsigned int vertices_counter = (unsigned int) init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }
    init_from(std::move(init_data), generate_mesh);
    return true;
}

bool GLModel::init_model_from_lines(const Lines3 &lines, bool generate_mesh)
{
    GLModel::Geometry init_data;
    init_data.format = {GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
    init_data.reserve_vertices(2 * lines.size());
    init_data.reserve_indices(2 * lines.size());

    for (const auto &l : lines) {
        init_data.add_vertex(Vec3f(unscale<float>(l.a.x()), unscale<float>(l.a.y()), unscale<float>(l.a.z())));
        init_data.add_vertex(Vec3f(unscale<float>(l.b.x()), unscale<float>(l.b.y()), unscale<float>(l.b.z())));
        const unsigned int vertices_counter = (unsigned int) init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }
    init_from(std::move(init_data), generate_mesh);

    return true;
}

bool GLModel::init_model_from_lines(const Line3floats &lines, bool generate_mesh)
{
    GLModel::Geometry init_data;
    init_data.format = {GLModel::PrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3};
    init_data.reserve_vertices(2 * lines.size());
    init_data.reserve_indices(2 * lines.size());

    for (const auto &l : lines) {
        init_data.add_vertex(l.a);
        init_data.add_vertex(l.b);
        const unsigned int vertices_counter = (unsigned int) init_data.vertices_count();
        init_data.add_line(vertices_counter - 2, vertices_counter - 1);
    }
    init_from(std::move(init_data), generate_mesh);
    return true;
}

void GLModel::set_color(int entity_id, const std::array<float, 4>& color)
{
    for (size_t i = 0; i < m_render_data.size(); ++i) {
        if (entity_id == -1 || static_cast<int>(i) == entity_id) {
            m_render_data[i].color = color;
            m_render_data[i].geometry.color = color;
        }
    }
}

void GLModel::set_color(const ColorRGBA &color) {
    set_color(-1,color.get_data());
}

void GLModel::reset()
{
    for (RenderData& data : m_render_data) {
        // release gpu memory
        if (data.ibo_id > 0) {
            glsafe(::glDeleteBuffers(1, &data.ibo_id));
            data.ibo_id = 0;
        }
        if (data.vbo_id > 0) {
            glsafe(::glDeleteBuffers(1, &data.vbo_id));
            data.vbo_id = 0;
        }
    }

    m_render_data.clear();
    m_bounding_box = BoundingBoxf3();
    m_filename = std::string();
}

static GLenum get_primitive_mode(const GLModel::Geometry::Format &format)
{
    switch (format.type) {
    case GLModel::PrimitiveType::Points: {
        return GL_POINTS;
    }
    default:
    case GLModel::PrimitiveType::Triangles: {
        return GL_TRIANGLES;
    }
    case GLModel::PrimitiveType::TriangleStrip: {
        return GL_TRIANGLE_STRIP;
    }
    case GLModel::PrimitiveType::TriangleFan: {
        return GL_TRIANGLE_FAN;
    }
    case GLModel::PrimitiveType::Lines: {
        return GL_LINES;
    }
    case GLModel::PrimitiveType::LineStrip: {
        return GL_LINE_STRIP;
    }
    case GLModel::PrimitiveType::LineLoop: {
        return GL_LINE_LOOP;
    }
    }
}

static GLenum get_index_type(const GLModel::Geometry &data)
{
    switch (data.index_type) {
    default:
    case GLModel::Geometry::EIndexType::UINT: {
        return GL_UNSIGNED_INT;
    }
    case GLModel::Geometry::EIndexType::USHORT: {
        return GL_UNSIGNED_SHORT;
    }
    case GLModel::Geometry::EIndexType::UBYTE: {
        return GL_UNSIGNED_BYTE;
    }
    }
}

void GLModel::render() const
{
    GLShaderProgram* shader = wxGetApp().get_current_shader();

    for (const RenderData& data : m_render_data) {
        // sends data to gpu if not done yet
        if (data.vbo_id == 0 || data.ibo_id == 0) {
            auto origin_data = const_cast<RenderData *>(&data);
            if (data.geometry.vertices_count() > 0 && data.geometry.indices_count() > 0
                && !send_to_gpu(*origin_data, data.geometry.vertices, data.geometry.indices))
                continue;
        }
        bool has_normal = true;
        if (data.geometry.vertices_count() > 0) {
            has_normal = Geometry::has_normal(data.geometry.format);
        }

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
        if (has_normal) {
            glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void *) 0));
            glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), (const void *) (3 * sizeof(float))));

            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
            glsafe(::glEnableClientState(GL_NORMAL_ARRAY));
        } else {
            glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), (const void *) 0));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
        }

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

void GLModel::render_geometry() {
    render_geometry(0,std::make_pair<size_t, size_t>(0, get_indices_count()));
}

void GLModel::render_geometry(int i,const std::pair<size_t, size_t> &range)
{
    if (range.second == range.first) return;

    GLShaderProgram *shader = wxGetApp().get_current_shader();
    if (shader == nullptr) return;

    auto &render_data = m_render_data[i];
    // sends data to gpu if not done yet
    if (render_data.vbo_id == 0 || render_data.ibo_id == 0) {
        if (render_data.geometry.vertices_count() > 0 && render_data.geometry.indices_count() > 0 &&
            !send_to_gpu(render_data, render_data.geometry.vertices, render_data.geometry.indices))
            return;
    }
    const Geometry &data = render_data.geometry;


    const GLenum mode       = get_primitive_mode(data.format);
    const GLenum index_type = get_index_type(data);

    const size_t vertex_stride_bytes = Geometry::vertex_stride_bytes(data.format);
    const bool   position            = Geometry::has_position(data.format);
    const bool   normal              = Geometry::has_normal(data.format);
    const bool   tex_coord           = Geometry::has_tex_coord(data.format);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, render_data.vbo_id));

    int position_id  = -1;
    int normal_id    = -1;
    int tex_coord_id = -1;

    if (position) {
        position_id = shader->get_attrib_location("v_position");
        if (position_id != -1) {
            glsafe(::glVertexAttribPointer(position_id, Geometry::position_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes,
                                           (const void *) Geometry::position_offset_bytes(data.format)));
            glsafe(::glEnableVertexAttribArray(position_id));
        }
    }
    if (normal) {
        normal_id = shader->get_attrib_location("v_normal");
        if (normal_id != -1) {
            glsafe(::glVertexAttribPointer(normal_id, Geometry::normal_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes,
                                           (const void *) Geometry::normal_offset_bytes(data.format)));
            glsafe(::glEnableVertexAttribArray(normal_id));
        }
    }
    if (tex_coord) {
        tex_coord_id = shader->get_attrib_location("v_tex_coord");
        if (tex_coord_id != -1) {
            glsafe(::glVertexAttribPointer(tex_coord_id, Geometry::tex_coord_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes,
                                           (const void *) Geometry::tex_coord_offset_bytes(data.format)));
            glsafe(::glEnableVertexAttribArray(tex_coord_id));
        }
    }

    shader->set_uniform("uniform_color", data.color.get_data());

    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render_data.ibo_id));
    glsafe(::glDrawElements(mode, range.second - range.first, index_type, (const void *) (range.first * Geometry::index_stride_bytes(data))));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    if (tex_coord_id != -1) glsafe(::glDisableVertexAttribArray(tex_coord_id));
    if (normal_id != -1) glsafe(::glDisableVertexAttribArray(normal_id));
    if (position_id != -1) glsafe(::glDisableVertexAttribArray(position_id));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLModel::create_or_update_mats_vbo(unsigned int &vbo, const std::vector<Slic3r::Geometry::Transformation> &mats)
{ // first bind
    if (vbo>0) {
        glsafe(::glDeleteBuffers(1, &vbo));
        vbo = 0;
    }
    std::vector<Matrix4f> out_mats;
    out_mats.reserve(mats.size());
    for (size_t i = 0; i < mats.size(); i++) {
        out_mats.emplace_back(mats[i].get_matrix().matrix().cast<float>());
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    auto one_mat_all_size = sizeof(float) * 16;
    glBufferData(GL_ARRAY_BUFFER, mats.size() * one_mat_all_size, out_mats.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLModel::bind_mats_vbo(unsigned int instance_mats_vbo, unsigned int instances_count, int location)
{
    if (instance_mats_vbo == 0 || instances_count == 0) {
        return;
    }
    auto one_mat_all_size = sizeof(float) * 16;
    auto one_mat_col_size = sizeof(float) * 4;
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, instance_mats_vbo));
    for (unsigned int i = 0; i < instances_count; i++) { // set attribute pointers for matrix (4 times vec4)
        glsafe(glEnableVertexAttribArray(location));
        glsafe(glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, one_mat_all_size, (void *) 0));
        glsafe(glEnableVertexAttribArray(location + 1));
        glsafe(glVertexAttribPointer(location + 1, 4, GL_FLOAT, GL_FALSE, one_mat_all_size, (void *) (one_mat_col_size)));
        glsafe(glEnableVertexAttribArray(location + 2));
        glsafe(glVertexAttribPointer(location + 2, 4, GL_FLOAT, GL_FALSE, one_mat_all_size, (void *) (2 * one_mat_col_size)));
        glsafe(glEnableVertexAttribArray(location + 3));
        glsafe(glVertexAttribPointer(location + 3, 4, GL_FLOAT, GL_FALSE, one_mat_all_size, (void *) (3 * one_mat_col_size)));
        // Update the matrix every time after an object is drawn//useful
        glsafe(glVertexAttribDivisor(location, 1));
        glsafe(glVertexAttribDivisor(location + 1, 1));
        glsafe(glVertexAttribDivisor(location + 2, 1));
        glsafe(glVertexAttribDivisor(location + 3, 1));
    }
}

void GLModel::render_geometry_instance(unsigned int instance_mats_vbo, unsigned int instances_count)
{
    render_geometry_instance(instance_mats_vbo, instances_count,std::make_pair<size_t, size_t>(0, get_indices_count()));
}

void GLModel::render_geometry_instance(unsigned int instance_mats_vbo, unsigned int instances_count, const std::pair<size_t, size_t> &range)
{
    if (instance_mats_vbo == 0 || instances_count == 0) {
        return;
    }
    if (m_render_data.size() != 1) { return; }
    GLShaderProgram *shader = wxGetApp().get_current_shader();
    if (shader == nullptr) return;

    auto &render_data = m_render_data[0];
    // sends data to gpu if not done yet
    if (render_data.vbo_id == 0 || render_data.ibo_id == 0) {
        if (render_data.geometry.vertices_count() > 0 && render_data.geometry.indices_count() > 0 && !send_to_gpu(render_data.geometry))
            return;
    }
    if (instance_mats_vbo == 0) {
        return;
    }
    const Geometry &data = render_data.geometry;

    const GLenum mode       = get_primitive_mode(data.format);
    const GLenum index_type = get_index_type(data);

    const size_t vertex_stride_bytes = Geometry::vertex_stride_bytes(data.format);
    const bool   position            = Geometry::has_position(data.format);
    const bool   normal              = Geometry::has_normal(data.format);
    const bool   tex_coord           = Geometry::has_tex_coord(data.format);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, render_data.vbo_id));

    int position_id  = -1;
    int normal_id    = -1;
    int tex_coord_id = -1;
    int instace_mats_id = -1;
    if (position) {
        position_id = shader->get_attrib_location("v_position");
        if (position_id != -1) {
            glsafe(::glVertexAttribPointer(position_id, Geometry::position_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes,
                                           (const void *) Geometry::position_offset_bytes(data.format)));
            glsafe(::glEnableVertexAttribArray(position_id));
        }
    }
    if (normal) {
        normal_id = shader->get_attrib_location("v_normal");
        if (normal_id != -1) {
            glsafe(::glVertexAttribPointer(normal_id, Geometry::normal_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes,
                                           (const void *) Geometry::normal_offset_bytes(data.format)));
            glsafe(::glEnableVertexAttribArray(normal_id));
        }
    }
    if (tex_coord) {
        tex_coord_id = shader->get_attrib_location("v_tex_coord");
        if (tex_coord_id != -1) {
            glsafe(::glVertexAttribPointer(tex_coord_id, Geometry::tex_coord_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes,
                                           (const void *) Geometry::tex_coord_offset_bytes(data.format)));
            glsafe(::glEnableVertexAttribArray(tex_coord_id));
        }
    }
    //glBindAttribLocation(shader->get_id(), 2, "instanceMatrix");
    //glBindAttribLocation(2, "instanceMatrix");
    //shader->bind(shaderProgram, 0, 'position');
    instace_mats_id = shader->get_attrib_location("instanceMatrix");
    if (instace_mats_id != -1) {
        bind_mats_vbo(instance_mats_vbo, instances_count, instace_mats_id);
    }
    else {
        return;
    }
    auto res = shader->set_uniform("uniform_color", render_data.color);

    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render_data.ibo_id));
    glsafe(::glDrawElementsInstanced(mode, range.second - range.first, index_type, (const void *) (range.first * Geometry::index_stride_bytes(data)), instances_count));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    if (instace_mats_id != -1) glsafe(::glDisableVertexAttribArray(instace_mats_id));
    if (tex_coord_id != -1) glsafe(::glDisableVertexAttribArray(tex_coord_id));
    if (normal_id != -1) glsafe(::glDisableVertexAttribArray(normal_id));
    if (position_id != -1) glsafe(::glDisableVertexAttribArray(position_id));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
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

bool GLModel::send_to_gpu(Geometry &geometry)
{
    if (m_render_data.size() != 1) { return false; }
    auto& render_data = m_render_data[0];
    if (render_data.vbo_id > 0 || render_data.ibo_id > 0) {
        assert(false);
        return false;
    }

    Geometry &data = render_data.geometry;
    if (data.vertices.empty() || data.indices.empty()) {
        assert(false);
        return false;
    }

    // vertices
    glsafe(::glGenBuffers(1, &render_data.vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, render_data.vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, data.vertices_size_bytes(), data.vertices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    render_data.vertices_count = get_vertices_count();
    data.vertices              = std::vector<float>();

    // indices
    glsafe(::glGenBuffers(1, &render_data.ibo_id));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render_data.ibo_id));
    const size_t indices_count = data.indices.size();
    if (render_data.vertices_count <= 256) {
        // convert indices to unsigned char to save gpu memory
        std::vector<unsigned char> reduced_indices(indices_count);
        for (size_t i = 0; i < indices_count; ++i) { reduced_indices[i] = (unsigned char) data.indices[i]; }
        data.index_type = Geometry::EIndexType::UBYTE;
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_count * sizeof(unsigned char), reduced_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    } else if (render_data.vertices_count <= 65536) {
        // convert indices to unsigned short to save gpu memory
        std::vector<unsigned short> reduced_indices(indices_count);
        for (size_t i = 0; i < data.indices.size(); ++i) { reduced_indices[i] = (unsigned short) data.indices[i]; }
        data.index_type = Geometry::EIndexType::USHORT;
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_count * sizeof(unsigned short), reduced_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    } else {
        data.index_type = Geometry::EIndexType::UINT;
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices_size_bytes(), data.indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }
    render_data.indices_count = indices_count;
    data.indices                = std::vector<unsigned int>();

    return true;
}

bool GLModel::send_to_gpu(RenderData &data, const std::vector<float> &vertices, const std::vector<unsigned int> &indices) const
{
    if (data.vbo_id > 0 || data.ibo_id > 0) {
        assert(false);
        return false;
    }

    if (vertices.empty() || indices.empty()) {
        assert(false);
        return false;
    }

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

    return true;
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
    const float stackStep  = float(M_PI / stackCount);

    GLModel::Geometry data;
    data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
    data.reserve_vertices((stackCount - 1) * sectorCount + 2);
    data.reserve_indices((2 * (stackCount - 1) * sectorCount) * 3);

    // vertices
    for (unsigned int i = 0; i <= stackCount; ++i) {
        // from pi/2 to -pi/2
        const double stackAngle = 0.5 * M_PI - stackStep * i;
        const double xy         = double(radius) * ::cos(stackAngle);
        const double z          = double(radius) * ::sin(stackAngle);
        if (i == 0 || i == stackCount) {
            const Vec3f v(float(xy), 0.0f, float(z));
            data.add_vertex(v, (Vec3f) v.normalized());
        } else {
            for (unsigned int j = 0; j < sectorCount; ++j) {
                // from 0 to 2pi
                const double sectorAngle = sectorStep * j;
                const Vec3f  v(float(xy * std::cos(sectorAngle)), float(xy * std::sin(sectorAngle)), float(z));
                data.add_vertex(v, (Vec3f) v.normalized());
            }
        }
    }

    // triangles
    for (unsigned int i = 0; i < stackCount; ++i) {
        // Beginning of current stack.
        unsigned int       k1       = (i == 0) ? 0 : (1 + (i - 1) * sectorCount);
        const unsigned int k1_first = k1;
        // Beginning of next stack.
        unsigned int       k2       = (i == 0) ? 1 : (k1 + sectorCount);
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
    const float        sectorStep  = 2.0f * float(M_PI) / float(sectorCount);

    GLModel::Geometry data;
    data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
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
    const Vec3f              h             = height * Vec3f::UnitZ();

    // stem vertices
    for (unsigned int i = 0; i < sectorCount; ++i) {
        const Vec3f &v = base_vertices[i];
        const Vec3f  n = v.normalized();
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
    Vec3f        cap_center    = Vec3f::Zero();
    unsigned int cap_center_id = data.vertices_count();
    Vec3f        normal        = -Vec3f::UnitZ();

    data.add_vertex(cap_center, normal);
    for (unsigned int i = 0; i < sectorCount; ++i) { data.add_vertex(base_vertices[i], normal); }

    // bottom cap triangles
    for (unsigned int i = 0; i < sectorCount; ++i) { data.add_triangle(cap_center_id, (i < sectorCount - 1) ? cap_center_id + i + 2 : cap_center_id + 1, cap_center_id + i + 1); }

    // top cap vertices
    cap_center += h;
    cap_center_id = data.vertices_count();
    normal        = -normal;

    data.add_vertex(cap_center, normal);
    for (unsigned int i = 0; i < sectorCount; ++i) { data.add_vertex(base_vertices[i] + h, normal); }

    // top cap triangles
    for (unsigned int i = 0; i < sectorCount; ++i) { data.add_triangle(cap_center_id, cap_center_id + i + 1, (i < sectorCount - 1) ? cap_center_id + i + 2 : cap_center_id + 1); }

    return data;
}

GLModel::Geometry smooth_torus(unsigned int primary_resolution, unsigned int secondary_resolution, float radius, float thickness)
{
    const unsigned int torus_sector_count   = std::max<unsigned int>(4, primary_resolution);
    const float        torus_sector_step    = 2.0f * float(M_PI) / float(torus_sector_count);
    const unsigned int section_sector_count = std::max<unsigned int>(4, secondary_resolution);
    const float        section_sector_step  = 2.0f * float(M_PI) / float(section_sector_count);

    GLModel::Geometry data;
    data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
    data.reserve_vertices(torus_sector_count * section_sector_count);
    data.reserve_indices(torus_sector_count * section_sector_count * 2 * 3);

    // vertices
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const float section_angle = torus_sector_step * i;
        const float csa           = std::cos(section_angle);
        const float ssa           = std::sin(section_angle);
        const Vec3f section_center(radius * csa, radius * ssa, 0.0f);
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const float circle_angle = section_sector_step * j;
            const float thickness_xy = thickness * std::cos(circle_angle);
            const float thickness_z  = thickness * std::sin(circle_angle);
            const Vec3f v(thickness_xy * csa, thickness_xy * ssa, thickness_z);
            data.add_vertex(section_center + v, (Vec3f) v.normalized());
        }
    }

    // triangles
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const unsigned int ii      = i * section_sector_count;
        const unsigned int ii_next = ((i + 1) % torus_sector_count) * section_sector_count;
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const unsigned int j_next = (j + 1) % section_sector_count;
            const unsigned int i0     = ii + j;
            const unsigned int i1     = ii_next + j;
            const unsigned int i2     = ii_next + j_next;
            const unsigned int i3     = ii + j_next;
            data.add_triangle(i0, i1, i2);
            data.add_triangle(i0, i2, i3);
        }
    }

    return data;
}

std::shared_ptr<GLModel> init_plane_data(const indexed_triangle_set &its, const std::vector<int> &triangle_indices,  float normal_offset)
{
    GLModel::Geometry init_data;
    init_data.format = {GUI::GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
    init_data.reserve_indices(3 * triangle_indices.size());
    init_data.reserve_vertices(3 * triangle_indices.size());
    unsigned int i = 0;
    for (int idx : triangle_indices) {
        Vec3f v0 = its.vertices[its.indices[idx][0]];
        Vec3f v1 = its.vertices[its.indices[idx][1]];
        Vec3f v2 = its.vertices[its.indices[idx][2]];
        const Vec3f n = (v1 - v0).cross(v2 - v0).normalized();
        if (std::abs(normal_offset) > 0.0) {
            v0 = v0 + n * normal_offset;
            v1 = v1 + n * normal_offset;
            v2 = v2 + n * normal_offset;
        }
        init_data.add_vertex(v0, n);
        init_data.add_vertex(v1, n);
        init_data.add_vertex(v2, n);
        init_data.add_triangle(i, i + 1, i + 2);
        i += 3;
    }
    std::shared_ptr<GLModel> gl_data = std::make_shared<GLModel>();
    gl_data->init_from(std::move(init_data), true);
    return gl_data;
}

std::shared_ptr<GLModel> init_torus_data(unsigned int       primary_resolution,
                                         unsigned int       secondary_resolution,
                                         const Vec3f &      center,
                                         float              radius,
                                         float              thickness,
                                         const Vec3f &      model_axis,
                                         const Transform3f &world_trafo)
{
    const unsigned int torus_sector_count   = std::max<unsigned int>(4, primary_resolution);
    const unsigned int section_sector_count = std::max<unsigned int>(4, secondary_resolution);
    const float        torus_sector_step    = 2.0f * float(M_PI) / float(torus_sector_count);
    const float        section_sector_step  = 2.0f * float(M_PI) / float(section_sector_count);

    GLModel::Geometry data;
    data.format = {GLModel::PrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3};
    data.reserve_vertices(torus_sector_count * section_sector_count);
    data.reserve_indices(torus_sector_count * section_sector_count * 2 * 3);

    // vertices
    const Transform3f local_to_world_matrix = world_trafo * Geometry::translation_transform(center.cast<double>()).cast<float>() *
                                              Eigen::Quaternion<float>::FromTwoVectors(Vec3f::UnitZ(), model_axis);
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const float section_angle = torus_sector_step * i;
        const Vec3f radius_dir(std::cos(section_angle), std::sin(section_angle), 0.0f);
        const Vec3f local_section_center = radius * radius_dir;
        const Vec3f world_section_center = local_to_world_matrix * local_section_center;
        const Vec3f local_section_normal = local_section_center.normalized().cross(Vec3f::UnitZ()).normalized();
        const Vec3f world_section_normal = (Vec3f) (local_to_world_matrix.matrix().block(0, 0, 3, 3) * local_section_normal).normalized();
        const Vec3f base_v               = thickness * radius_dir;
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const Vec3f v = Eigen::AngleAxisf(section_sector_step * j, world_section_normal) * base_v;
            data.add_vertex(world_section_center + v, (Vec3f) v.normalized());
        }
    }

    // triangles
    for (unsigned int i = 0; i < torus_sector_count; ++i) {
        const unsigned int ii      = i * section_sector_count;
        const unsigned int ii_next = ((i + 1) % torus_sector_count) * section_sector_count;
        for (unsigned int j = 0; j < section_sector_count; ++j) {
            const unsigned int j_next = (j + 1) % section_sector_count;
            const unsigned int i0     = ii + j;
            const unsigned int i1     = ii_next + j;
            const unsigned int i2     = ii_next + j_next;
            const unsigned int i3     = ii + j_next;
            data.add_triangle(i0, i1, i2);
            data.add_triangle(i0, i2, i3);
        }
    }
    std::shared_ptr<GLModel> gl_data = std::make_shared<GLModel>();
    gl_data->init_from(std::move(data), true);
    return gl_data;
}
} // namespace GUI
} // namespace Slic3r
