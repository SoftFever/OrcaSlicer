#include "libslic3r/libslic3r.h"
#include "GLModel.hpp"

#include "3DScene.hpp"
#include "GUI_App.hpp"
#include "GLShader.hpp"

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/predicate.hpp>

#if ENABLE_SMOOTH_NORMALS
#include <igl/per_face_normals.h>
#include <igl/per_corner_normals.h>
#include <igl/per_vertex_normals.h>
#endif // ENABLE_SMOOTH_NORMALS

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

#if ENABLE_SMOOTH_NORMALS
static void smooth_normals_corner(const TriangleMesh& mesh, std::vector<stl_normal>& normals)
{
    using MapMatrixXfUnaligned = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;
    using MapMatrixXiUnaligned = Eigen::Map<const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;

    std::vector<Vec3f> face_normals = its_face_normals(mesh.its);

    Eigen::MatrixXd vertices = MapMatrixXfUnaligned(mesh.its.vertices.front().data(),
        Eigen::Index(mesh.its.vertices.size()), 3).cast<double>();
    Eigen::MatrixXi indices = MapMatrixXiUnaligned(mesh.its.indices.front().data(),
        Eigen::Index(mesh.its.indices.size()), 3);
    Eigen::MatrixXd in_normals = MapMatrixXfUnaligned(face_normals.front().data(),
        Eigen::Index(face_normals.size()), 3).cast<double>();
    Eigen::MatrixXd out_normals;

    igl::per_corner_normals(vertices, indices, in_normals, 1.0, out_normals);

    normals = std::vector<stl_normal>(mesh.its.vertices.size());
    for (size_t i = 0; i < mesh.its.indices.size(); ++i) {
        for (size_t j = 0; j < 3; ++j) {
            normals[mesh.its.indices[i][j]] = out_normals.row(i * 3 + j).cast<float>();
        }
    }
}
#endif // ENABLE_SMOOTH_NORMALS

void GLModel::Geometry::reserve_vertices(size_t vertices_count)
{
    vertices.reserve(vertices_count * vertex_stride_floats(format));
}

void GLModel::Geometry::reserve_indices(size_t indices_count)
{
    indices.reserve(indices_count * index_stride_bytes(format));
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
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(tex_coord.x());
    vertices.emplace_back(tex_coord.y());
}

void GLModel::Geometry::add_vertex(const Vec3f& position, const Vec3f& normal)
{
    assert(format.vertex_layout == EVertexLayout::P3N3);
    vertices.emplace_back(position.x());
    vertices.emplace_back(position.y());
    vertices.emplace_back(position.z());
    vertices.emplace_back(normal.x());
    vertices.emplace_back(normal.y());
    vertices.emplace_back(normal.z());
}

void GLModel::Geometry::add_ushort_index(unsigned short id)
{
    if (format.index_type != EIndexType::USHORT) {
        assert(false);
        return;
    }
    indices.resize(indices.size() + sizeof(unsigned short));
    ::memcpy(indices.data() + indices.size() - sizeof(unsigned short), &id, sizeof(unsigned short));
}

void GLModel::Geometry::add_uint_index(unsigned int id)
{
    if (format.index_type != EIndexType::UINT) {
        assert(false);
        return;
    }
    indices.resize(indices.size() + sizeof(unsigned int));
    ::memcpy(indices.data() + indices.size() - sizeof(unsigned int), &id, sizeof(unsigned int));
}

void GLModel::Geometry::add_ushort_line(unsigned short id1, unsigned short id2)
{
    if (format.index_type != EIndexType::USHORT) {
        assert(false);
        return;
    }
    indices.resize(indices.size() + 2 * sizeof(unsigned short));
    ::memcpy(indices.data() + indices.size() - 2 * sizeof(unsigned short), &id1, sizeof(unsigned short));
    ::memcpy(indices.data() + indices.size() - sizeof(unsigned short), &id2, sizeof(unsigned short));
}

void GLModel::Geometry::add_uint_line(unsigned int id1, unsigned int id2)
{
    if (format.index_type != EIndexType::UINT) {
        assert(false);
        return;
    }
    indices.resize(indices.size() + 2 * sizeof(unsigned int));
    ::memcpy(indices.data() + indices.size() - 2 * sizeof(unsigned int), &id1, sizeof(unsigned int));
    ::memcpy(indices.data() + indices.size() - sizeof(unsigned int), &id2, sizeof(unsigned int));
}

void GLModel::Geometry::add_ushort_triangle(unsigned short id1, unsigned short id2, unsigned short id3)
{
    if (format.index_type != EIndexType::USHORT) {
        assert(false);
        return;
    }
    indices.resize(indices.size() + 3 * sizeof(unsigned short));
    ::memcpy(indices.data() + indices.size() - 3 * sizeof(unsigned short), &id1, sizeof(unsigned short));
    ::memcpy(indices.data() + indices.size() - 2 * sizeof(unsigned short), &id2, sizeof(unsigned short));
    ::memcpy(indices.data() + indices.size() - sizeof(unsigned short), &id3, sizeof(unsigned short));
}

void GLModel::Geometry::add_uint_triangle(unsigned int id1, unsigned int id2, unsigned int id3)
{
    if (format.index_type != EIndexType::UINT) {
        assert(false);
        return;
    }
    indices.resize(indices.size() + 3 * sizeof(unsigned int));
    ::memcpy(indices.data() + indices.size() - 3 * sizeof(unsigned int), &id1, sizeof(unsigned int));
    ::memcpy(indices.data() + indices.size() - 2 * sizeof(unsigned int), &id2, sizeof(unsigned int));
    ::memcpy(indices.data() + indices.size() - sizeof(unsigned int), &id3, sizeof(unsigned int));
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

void GLModel::Geometry::set_ushort_index(size_t id, unsigned short index)
{
    assert(id < indices_count());
    if (id < indices_count())
        ::memcpy(indices.data() + id * sizeof(unsigned short), &index, sizeof(unsigned short));
}

void GLModel::Geometry::set_uint_index(size_t id, unsigned int index)
{
    assert(id < indices_count());
    if (id < indices_count())
        ::memcpy(indices.data() + id * sizeof(unsigned int), &index, sizeof(unsigned int));
}

unsigned int GLModel::Geometry::extract_uint_index(size_t id) const
{
    if (format.index_type != EIndexType::UINT) {
        assert(false);
        return -1;
    }

    if (indices_count() <= id) {
        assert(false);
        return -1;
    }

    unsigned int ret = (unsigned int)-1;
    ::memcpy(&ret, indices.data() + id * index_stride_bytes(format), sizeof(unsigned int));
    return ret;
}

unsigned short GLModel::Geometry::extract_ushort_index(size_t id) const
{
    if (format.index_type != EIndexType::USHORT) {
        assert(false);
        return -1;
    }

    if (indices_count() <= id) {
        assert(false);
        return -1;
    }

    unsigned short ret = (unsigned short)-1;
    ::memcpy(&ret, indices.data() + id * index_stride_bytes(format), sizeof(unsigned short));
    return ret;
}

void GLModel::Geometry::remove_vertex(size_t id)
{
    assert(id < vertices_count());
    if (id < vertices_count()) {
        size_t stride = vertex_stride_floats(format);
        std::vector<float>::iterator it = vertices.begin() + id * stride;
        vertices.erase(it, it + stride);
    }
}

size_t GLModel::Geometry::vertex_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:   { return 2; }
    case EVertexLayout::P2T2: { return 4; }
    case EVertexLayout::P3:   { return 3; }
    case EVertexLayout::P3T2: { return 5; }
    case EVertexLayout::P3N3: { return 6; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::position_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2: { return 2; }
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3: { return 3; }
    default:                  { assert(false); return 0; }
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
    case EVertexLayout::P3N3: { return 0; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::normal_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3: { return 3; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::normal_offset_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P3N3: { return 3; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::tex_coord_stride_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2T2:
    case EVertexLayout::P3T2: { return 2; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::tex_coord_offset_floats(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2T2: { return 2; }
    case EVertexLayout::P3T2: { return 3; }
    default:                  { assert(false); return 0; }
    };
}

size_t GLModel::Geometry::index_stride_bytes(const Format& format)
{
    switch (format.index_type)
    {
    case EIndexType::UINT:   { return sizeof(unsigned int); }
    case EIndexType::USHORT: { return sizeof(unsigned short); }
    default:                 { assert(false); return 0; }
    };
}

GLModel::Geometry::EIndexType GLModel::Geometry::index_type(size_t vertices_count)
{
    return (vertices_count < 65536) ? EIndexType::USHORT : EIndexType::UINT;
}

bool GLModel::Geometry::has_position(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2:
    case EVertexLayout::P2T2:
    case EVertexLayout::P3:
    case EVertexLayout::P3T2:
    case EVertexLayout::P3N3: { return true; }
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
    case EVertexLayout::P3T2: { return false; }
    case EVertexLayout::P3N3: { return true; }
    default:                  { assert(false); return false; }
    };
}

bool GLModel::Geometry::has_tex_coord(const Format& format)
{
    switch (format.vertex_layout)
    {
    case EVertexLayout::P2T2:
    case EVertexLayout::P3T2: { return true; }
    case EVertexLayout::P2:
    case EVertexLayout::P3:
    case EVertexLayout::P3N3: { return false; }
    default:                  { assert(false); return false; }
    };
}

void GLModel::init_from(Geometry&& data)
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

    m_render_data.geometry = std::move(data);

    // update bounding box
    for (size_t i = 0; i < vertices_count(); ++i) {
        const size_t position_stride = Geometry::position_stride_floats(data.format);
        if (position_stride == 3)
            m_bounding_box.merge(m_render_data.geometry.extract_position_3(i).cast<double>());
        else if (position_stride == 2) {
            const Vec2f position = m_render_data.geometry.extract_position_2(i);
            m_bounding_box.merge(Vec3f(position.x(), position.y(), 0.0f).cast<double>());
        }
    }
}

void GLModel::init_from(const TriangleMesh& mesh)
{
    init_from(mesh.its);
}

void GLModel::init_from(const indexed_triangle_set& its)
{
    if (is_initialized()) {
        // call reset() if you want to reuse this model
        assert(false);
        return;
    }

    if (its.vertices.empty() || its.indices.empty()){
        assert(false);
        return;
    }

    Geometry& data = m_render_data.geometry;
    data.format = { Geometry::EPrimitiveType::Triangles, Geometry::EVertexLayout::P3N3, GLModel::Geometry::index_type(3 * its.indices.size()) };
    data.reserve_vertices(3 * its.indices.size());
    data.reserve_indices(3 * its.indices.size());

    // vertices + indices
    unsigned int vertices_counter = 0;
    for (uint32_t i = 0; i < its.indices.size(); ++i) {
        const stl_triangle_vertex_indices face = its.indices[i];
        const stl_vertex                  vertex[3] = { its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]] };
        const stl_vertex                  n = face_normal_normalized(vertex);
        for (size_t j = 0; j < 3; ++j) {
            data.add_vertex(vertex[j], n);
        }
        vertices_counter += 3;
        if (data.format.index_type == GLModel::Geometry::EIndexType::USHORT)
            data.add_ushort_triangle((unsigned short)vertices_counter - 3, (unsigned short)vertices_counter - 2, (unsigned short)vertices_counter - 1);
        else
            data.add_uint_triangle(vertices_counter - 3, vertices_counter - 2, vertices_counter - 1);
    }

    // update bounding box
    for (size_t i = 0; i < vertices_count(); ++i) {
        m_bounding_box.merge(m_render_data.geometry.extract_position_3(i).cast<double>());
    }
}

void GLModel::init_from(const Polygons& polygons, float z)
{
    if (is_initialized()) {
        // call reset() if you want to reuse this model
        assert(false);
        return;
    }

    if (polygons.empty()) {
        assert(false);
        return;
    }

    Geometry& data = m_render_data.geometry;
    data.format = { Geometry::EPrimitiveType::Lines, Geometry::EVertexLayout::P3, Geometry::EIndexType::UINT };

    size_t segments_count = 0;
    for (const Polygon& polygon : polygons) {
        segments_count += polygon.points.size();
    }

    data.reserve_vertices(2 * segments_count);
    data.reserve_indices(2 * segments_count);

    // vertices + indices
    unsigned int vertices_counter = 0;
    for (const Polygon& poly : polygons) {
        for (size_t i = 0; i < poly.points.size(); ++i) {
            const Point& p0 = poly.points[i];
            const Point& p1 = (i == poly.points.size() - 1) ? poly.points.front() : poly.points[i + 1];
            data.add_vertex(Vec3f(unscale<float>(p0.x()), unscale<float>(p0.y()), z));
            data.add_vertex(Vec3f(unscale<float>(p1.x()), unscale<float>(p1.y()), z));
            vertices_counter += 2;
            data.add_uint_line(vertices_counter - 2, vertices_counter - 1);
        }
    }

    // update bounding box
    for (size_t i = 0; i < vertices_count(); ++i) {
        m_bounding_box.merge(m_render_data.geometry.extract_position_3(i).cast<double>());
    }
}

bool GLModel::init_from_file(const std::string& filename)
{
    if (!boost::filesystem::exists(filename))
        return false;

    if (!boost::algorithm::iends_with(filename, ".stl"))
        return false;

    Model model;
    try {
        model = Model::read_from_file(filename);
    }
    catch (std::exception&) {
        return false;
    }

    init_from(model.mesh());

    m_filename = filename;

    return true;
}

void GLModel::reset()
{
    // release gpu memory
    if (m_render_data.ibo_id > 0) {
        glsafe(::glDeleteBuffers(1, &m_render_data.ibo_id));
        m_render_data.ibo_id = 0;
    }
    if (m_render_data.vbo_id > 0) {
        glsafe(::glDeleteBuffers(1, &m_render_data.vbo_id));
        m_render_data.vbo_id = 0;
    }

    m_render_data.vertices_count = 0;
    m_render_data.indices_count  = 0;
    m_render_data.geometry.vertices = std::vector<float>();
    m_render_data.geometry.indices  = std::vector<unsigned char>();
    m_bounding_box = BoundingBoxf3();
    m_filename = std::string();
}

static GLenum get_primitive_mode(const GLModel::Geometry::Format& format)
{
    switch (format.type)
    {
    case GLModel::Geometry::EPrimitiveType::Points:        { return GL_POINTS; }
    default:
    case GLModel::Geometry::EPrimitiveType::Triangles:     { return GL_TRIANGLES; }
    case GLModel::Geometry::EPrimitiveType::TriangleStrip: { return GL_TRIANGLE_STRIP; }
    case GLModel::Geometry::EPrimitiveType::TriangleFan:   { return GL_TRIANGLE_FAN; }
    case GLModel::Geometry::EPrimitiveType::Lines:         { return GL_LINES; }
    case GLModel::Geometry::EPrimitiveType::LineStrip:     { return GL_LINE_STRIP; }
    case GLModel::Geometry::EPrimitiveType::LineLoop:      { return GL_LINE_LOOP; }
    }
}

static GLenum get_index_type(const GLModel::Geometry::Format& format)
{
    switch (format.index_type)
    {
    default:
    case GLModel::Geometry::EIndexType::UINT:   { return GL_UNSIGNED_INT; }
    case GLModel::Geometry::EIndexType::USHORT: { return GL_UNSIGNED_SHORT; }
    }
}

void GLModel::render()
{
    render(std::make_pair<size_t, size_t>(0, indices_count()));
}

void GLModel::render(const std::pair<size_t, size_t>& range)
{
    if (m_render_disabled)
        return;

    if (range.second == range.first)
        return;

    GLShaderProgram* shader = wxGetApp().get_current_shader();

    if (shader == nullptr)
        return;

    // sends data to gpu if not done yet
    if (m_render_data.vbo_id == 0 || m_render_data.ibo_id == 0) {
        if (m_render_data.geometry.vertices_count() > 0 && m_render_data.geometry.indices_count() > 0 && !send_to_gpu())
            return;
    }

    const Geometry& data = m_render_data.geometry;

    const GLenum mode = get_primitive_mode(data.format);
    const GLenum index_type = get_index_type(data.format);

    const size_t vertex_stride_bytes = Geometry::vertex_stride_bytes(data.format);
    const bool position = Geometry::has_position(data.format);
    const bool normal = Geometry::has_normal(data.format);
    const bool tex_coord = Geometry::has_tex_coord(data.format);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_render_data.vbo_id));

    bool use_attributes = boost::algorithm::iends_with(shader->get_name(), "_attr");

    int position_id = -1;
    int normal_id = -1;
    int tex_coord_id = -1;

    if (position) {
        if (use_attributes) {
            position_id = shader->get_attrib_location("v_position");
            if (position_id != -1) {
                glsafe(::glVertexAttribPointer(position_id, Geometry::position_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes, (GLvoid*)Geometry::position_offset_bytes(data.format)));
                glsafe(::glEnableVertexAttribArray(position_id));
            }
        }
        else {
            glsafe(::glVertexPointer(Geometry::position_stride_floats(data.format), GL_FLOAT, vertex_stride_bytes, (const void*)Geometry::position_offset_bytes(data.format)));
            glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
        }
    }
    if (normal) {
        if (use_attributes) {
            normal_id = shader->get_attrib_location("v_normal");
            if (normal_id != -1) {
                glsafe(::glVertexAttribPointer(normal_id, Geometry::normal_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes, (GLvoid*)Geometry::normal_offset_bytes(data.format)));
                glsafe(::glEnableVertexAttribArray(normal_id));
            }
        }
        else {
            glsafe(::glNormalPointer(GL_FLOAT, vertex_stride_bytes, (const void*)Geometry::normal_offset_bytes(data.format)));
            glsafe(::glEnableClientState(GL_NORMAL_ARRAY));
        }
    }
    if (tex_coord) {
        if (use_attributes) {
            tex_coord_id = shader->get_attrib_location("v_tex_coord");
            if (tex_coord_id != -1) {
                glsafe(::glVertexAttribPointer(tex_coord_id, Geometry::tex_coord_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes, (GLvoid*)Geometry::tex_coord_offset_bytes(data.format)));
                glsafe(::glEnableVertexAttribArray(tex_coord_id));
            }
        }
        else {
            glsafe(::glTexCoordPointer(Geometry::tex_coord_stride_floats(data.format), GL_FLOAT, vertex_stride_bytes, (const void*)Geometry::tex_coord_offset_bytes(data.format)));
            glsafe(::glEnableClientState(GL_TEXTURE_COORD_ARRAY));
        }
    }

    shader->set_uniform("uniform_color", data.color);

    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_render_data.ibo_id));
    glsafe(::glDrawElements(mode, range.second - range.first, index_type, (const void*)(range.first * Geometry::index_stride_bytes(data.format))));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    if (use_attributes) {
        if (tex_coord_id != -1)
            glsafe(::glDisableVertexAttribArray(tex_coord_id));
        if (normal_id != -1)
            glsafe(::glDisableVertexAttribArray(normal_id));
        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));
    }
    else {
        if (tex_coord)
            glsafe(::glDisableClientState(GL_TEXTURE_COORD_ARRAY));
        if (normal)
            glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
        if (position)
            glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    }

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLModel::render_instanced(unsigned int instances_vbo, unsigned int instances_count)
{
    if (instances_vbo == 0)
        return;

    GLShaderProgram* shader = wxGetApp().get_current_shader();
    if (shader == nullptr || !boost::algorithm::iends_with(shader->get_name(), "_instanced"))
        return;

    // vertex attributes
    GLint position_id = shader->get_attrib_location("v_position");
    GLint normal_id   = shader->get_attrib_location("v_normal");
    if (position_id == -1 || normal_id == -1)
        return;

    // instance attributes
    GLint offset_id = shader->get_attrib_location("i_offset");
    GLint scales_id = shader->get_attrib_location("i_scales");
    if (offset_id == -1 || scales_id == -1)
        return;

    if (m_render_data.vbo_id == 0 || m_render_data.ibo_id == 0) {
        if (!send_to_gpu())
            return;
    }

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, instances_vbo));
    glsafe(::glVertexAttribPointer(offset_id, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (GLvoid*)0));
    glsafe(::glEnableVertexAttribArray(offset_id));
    glsafe(::glVertexAttribDivisor(offset_id, 1));

    glsafe(::glVertexAttribPointer(scales_id, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (GLvoid*)(3 * sizeof(float))));
    glsafe(::glEnableVertexAttribArray(scales_id));
    glsafe(::glVertexAttribDivisor(scales_id, 1));

    const Geometry& data = m_render_data.geometry;

    GLenum mode = get_primitive_mode(data.format);
    GLenum index_type = get_index_type(data.format);

    shader->set_uniform("uniform_color", data.color);

    const size_t vertex_stride_bytes = Geometry::vertex_stride_bytes(data.format);
    const bool position = Geometry::has_position(data.format);
    const bool normal   = Geometry::has_normal(data.format);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_render_data.vbo_id));

    if (position) {
        glsafe(::glVertexAttribPointer(position_id, Geometry::position_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes, (GLvoid*)Geometry::position_offset_bytes(data.format)));
        glsafe(::glEnableVertexAttribArray(position_id));
    }

    if (normal) {
        glsafe(::glVertexAttribPointer(normal_id, Geometry::normal_stride_floats(data.format), GL_FLOAT, GL_FALSE, vertex_stride_bytes, (GLvoid*)Geometry::normal_offset_bytes(data.format)));
        glsafe(::glEnableVertexAttribArray(normal_id));
    }

    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_render_data.ibo_id));
    glsafe(::glDrawElementsInstanced(mode, indices_count(), index_type, (const void*)0, instances_count));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

    if (normal)
        glsafe(::glDisableVertexAttribArray(normal_id));
    if (position)
        glsafe(::glDisableVertexAttribArray(position_id));

    glsafe(::glDisableVertexAttribArray(scales_id));
    glsafe(::glDisableVertexAttribArray(offset_id));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

bool GLModel::send_to_gpu()
{
    if (m_render_data.vbo_id > 0 || m_render_data.ibo_id > 0) {
        assert(false);
        return false;
    }

    Geometry& data = m_render_data.geometry;
    if (data.vertices.empty() || data.indices.empty()) {
        assert(false);
        return false;
    }

    // vertices
    glsafe(::glGenBuffers(1, &m_render_data.vbo_id));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_render_data.vbo_id));
    glsafe(::glBufferData(GL_ARRAY_BUFFER, data.vertices_size_bytes(), data.vertices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
    m_render_data.vertices_count = vertices_count();
    data.vertices = std::vector<float>();

    // indices
    glsafe(::glGenBuffers(1, &m_render_data.ibo_id));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_render_data.ibo_id));
    glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, data.indices_size_bytes(), data.indices.data(), GL_STATIC_DRAW));
    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    m_render_data.indices_count = indices_count();
    data.indices = std::vector<unsigned char>();

    return true;
}

static void append_vertex(GLModel::Geometry& data, const Vec3f& position, const Vec3f& normal)
{
    data.add_vertex(position, normal);
}

static void append_triangle(GLModel::Geometry& data, unsigned short v1, unsigned short v2, unsigned short v3)
{
    data.add_ushort_index(v1);
    data.add_ushort_index(v2);
    data.add_ushort_index(v3);
}

template<typename Fn>
inline bool all_vertices_inside(const GLModel::Geometry& geometry, Fn fn)
{
    const size_t position_stride_floats = geometry.position_stride_floats(geometry.format);
    const size_t position_offset_floats = geometry.position_offset_floats(geometry.format);
    assert(position_stride_floats == 3);
    if (geometry.vertices.empty() || position_stride_floats != 3)
        return false;

    for (auto it = geometry.vertices.begin(); it != geometry.vertices.end(); ) {
        it += position_offset_floats;
        if (!fn({ *it, *(it + 1), *(it + 2) }))
            return false;
        it += (geometry.vertex_stride_floats(geometry.format) - position_offset_floats - position_stride_floats);
    }
    return true;
}

bool contains(const BuildVolume& volume, const GLModel& model, bool ignore_bottom)
{
    static constexpr const double epsilon = BuildVolume::BedEpsilon;
    switch (volume.type()) {
    case BuildVolume_Type::Rectangle:
    {
        BoundingBox3Base<Vec3d> build_volume = volume.bounding_volume().inflated(epsilon);
        if (volume.printable_height() == 0.0)
            build_volume.max.z() = std::numeric_limits<double>::max();
        if (ignore_bottom)
            build_volume.min.z() = -std::numeric_limits<double>::max();
        const BoundingBoxf3& model_box = model.get_bounding_box();
        return build_volume.contains(model_box.min) && build_volume.contains(model_box.max);
    }
    case BuildVolume_Type::Circle:
    {
        const Geometry::Circled& circle = volume.circle();
        const Vec2f c = unscaled<float>(circle.center);
        const float r = unscaled<double>(circle.radius) + float(epsilon);
        const float r2 = sqr(r);
        return volume.printable_height() == 0.0 ?
            all_vertices_inside(model.get_geometry(), [c, r2](const Vec3f& p) { return (to_2d(p) - c).squaredNorm() <= r2; }) :

            all_vertices_inside(model.get_geometry(), [c, r2, z = volume.printable_height() + epsilon](const Vec3f& p) { return (to_2d(p) - c).squaredNorm() <= r2 && p.z() <= z; });
    }
    case BuildVolume_Type::Convex:
        //FIXME doing test on convex hull until we learn to do test on non-convex polygons efficiently.
    case BuildVolume_Type::Custom:
        return volume.printable_height() == 0.0 ?
            all_vertices_inside(model.get_geometry(), [&volume](const Vec3f& p) { return Geometry::inside_convex_polygon(volume.top_bottom_convex_hull_decomposition_bed(), to_2d(p).cast<double>()); }) :
            all_vertices_inside(model.get_geometry(), [&volume, z = volume.printable_height() + epsilon](const Vec3f& p) { return Geometry::inside_convex_polygon(volume.top_bottom_convex_hull_decomposition_bed(), to_2d(p).cast<double>()) && p.z() <= z; });
    default:
        return true;
    }
}

GLModel::Geometry stilized_arrow(unsigned short resolution, float tip_radius, float tip_height, float stem_radius, float stem_height)
{
    resolution = std::max<unsigned short>(4, resolution);
    resolution = std::min<unsigned short>(10922, resolution); // ensure no unsigned short overflow of indices

    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3, GLModel::Geometry::EIndexType::USHORT };
    data.reserve_vertices(6 * resolution + 2);
    data.reserve_indices(6 * resolution * 3);

    const float angle_step = 2.0f * float(PI) / float(resolution);
    std::vector<float> cosines(resolution);
    std::vector<float> sines(resolution);

    for (unsigned short i = 0; i < resolution; ++i) {
        const float angle = angle_step * float(i);
        cosines[i] = ::cos(angle);
        sines[i] = -::sin(angle);
    }

    const float total_height = tip_height + stem_height;

    // tip vertices/normals
    append_vertex(data, { 0.0f, 0.0f, total_height }, Vec3f::UnitZ());
    for (unsigned short i = 0; i < resolution; ++i) {
        append_vertex(data, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // tip triangles
    for (unsigned short i = 0; i < resolution; ++i) {
        const unsigned short v3 = (i < resolution - 1) ? i + 2 : 1;
        append_triangle(data, 0, i + 1, v3);
    }

    // tip cap outer perimeter vertices
    for (unsigned short i = 0; i < resolution; ++i) {
        append_vertex(data, { tip_radius * sines[i], tip_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap inner perimeter vertices
    for (unsigned short i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, -Vec3f::UnitZ());
    }

    // tip cap triangles
    for (unsigned short i = 0; i < resolution; ++i) {
        const unsigned short v2 = (i < resolution - 1) ? i + resolution + 2 : resolution + 1;
        const unsigned short v3 = (i < resolution - 1) ? i + 2 * resolution + 2 : 2 * resolution + 1;
        append_triangle(data, i + resolution + 1, v3, v2);
        append_triangle(data, i + resolution + 1, i + 2 * resolution + 1, v3);
    }

    // stem bottom vertices
    for (unsigned short i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], stem_height }, { sines[i], cosines[i], 0.0f });
    }

    // stem top vertices
    for (unsigned short i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, { sines[i], cosines[i], 0.0f });
    }

    // stem triangles
    for (unsigned short i = 0; i < resolution; ++i) {
        const unsigned short v2 = (i < resolution - 1) ? i + 3 * resolution + 2 : 3 * resolution + 1;
        const unsigned short v3 = (i < resolution - 1) ? i + 4 * resolution + 2 : 4 * resolution + 1;
        append_triangle(data, i + 3 * resolution + 1, v3, v2);
        append_triangle(data, i + 3 * resolution + 1, i + 4 * resolution + 1, v3);
    }

    // stem cap vertices
    append_vertex(data, Vec3f::Zero(), -Vec3f::UnitZ());
    for (unsigned short i = 0; i < resolution; ++i) {
        append_vertex(data, { stem_radius * sines[i], stem_radius * cosines[i], 0.0f }, -Vec3f::UnitZ());
    }

    // stem cap triangles
    for (unsigned short i = 0; i < resolution; ++i) {
        const unsigned short v3 = (i < resolution - 1) ? i + 5 * resolution + 3 : 5 * resolution + 2;
        append_triangle(data, 5 * resolution + 1, v3, i + 5 * resolution + 2);
    }

    return data;
}

GLModel::Geometry circular_arrow(unsigned short resolution, float radius, float tip_height, float tip_width, float stem_width, float thickness)
{
    resolution = std::max<unsigned short>(2, resolution);
    resolution = std::min<unsigned short>(8188, resolution); // ensure no unsigned short overflow of indices

    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3, GLModel::Geometry::EIndexType::USHORT };
    data.reserve_vertices(8 * (resolution + 1) + 30);
    data.reserve_indices((8 * resolution + 16) * 3);

    const float half_thickness = 0.5f * thickness;
    const float half_stem_width = 0.5f * stem_width;
    const float half_tip_width = 0.5f * tip_width;

    const float outer_radius = radius + half_stem_width;
    const float inner_radius = radius - half_stem_width;
    const float step_angle = 0.5f * float(PI) / float(resolution);

    // tip
    // top face vertices
    append_vertex(data, { 0.0f, outer_radius, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius + half_tip_width, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { -tip_height, radius, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius - half_tip_width, half_thickness }, Vec3f::UnitZ());
    append_vertex(data, { 0.0f, inner_radius, half_thickness }, Vec3f::UnitZ());

    // top face triangles
    append_triangle(data, 0, 1, 2);
    append_triangle(data, 0, 2, 4);
    append_triangle(data, 4, 2, 3);

    // bottom face vertices
    append_vertex(data, { 0.0f, outer_radius, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius + half_tip_width, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -tip_height, radius, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0f, radius - half_tip_width, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0f, inner_radius, -half_thickness }, -Vec3f::UnitZ());

    // bottom face triangles
    append_triangle(data, 5, 7, 6);
    append_triangle(data, 5, 9, 7);
    append_triangle(data, 9, 8, 7);

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

    normal = { -half_tip_width, -tip_height, 0.0f };
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
    for (unsigned short i = 0; i < 4; ++i) {
        const unsigned short ii = i * 4;
        append_triangle(data, 10 + ii, 11 + ii, 13 + ii);
        append_triangle(data, 10 + ii, 13 + ii, 12 + ii);
    }

    // stem
    // top face vertices
    for (unsigned short i = 0; i <= resolution; ++i) {
        const float angle = float(i) * step_angle;
        append_vertex(data, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    for (unsigned short i = 0; i <= resolution; ++i) {
        const float angle = float(i) * step_angle;
        append_vertex(data, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), half_thickness }, Vec3f::UnitZ());
    }

    // top face triangles
    for (unsigned short i = 0; i < resolution; ++i) {
        append_triangle(data, 26 + i, 27 + i, 27 + resolution + i);
        append_triangle(data, 27 + i, 28 + resolution + i, 27 + resolution + i);
    }

    // bottom face vertices
    for (unsigned short i = 0; i <= resolution; ++i) {
        const float angle = float(i) * step_angle;
        append_vertex(data, { inner_radius * ::sin(angle), inner_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    for (unsigned short i = 0; i <= resolution; ++i) {
        const float angle = float(i) * step_angle;
        append_vertex(data, { outer_radius * ::sin(angle), outer_radius * ::cos(angle), -half_thickness }, -Vec3f::UnitZ());
    }

    // bottom face triangles
    for (unsigned short i = 0; i < resolution; ++i) {
        append_triangle(data, 28 + 2 * resolution + i, 29 + 3 * resolution + i, 29 + 2 * resolution + i);
        append_triangle(data, 29 + 2 * resolution + i, 29 + 3 * resolution + i, 30 + 3 * resolution + i);
    }

    // side faces vertices and triangles
    for (unsigned short i = 0; i <= resolution; ++i) {
        const float angle = float(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(data, { inner_radius * s, inner_radius * c, -half_thickness }, { -s, -c, 0.0f });
    }

    for (unsigned short i = 0; i <= resolution; ++i) {
        const float angle = float(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(data, { inner_radius * s, inner_radius * c, half_thickness }, { -s, -c, 0.0f });
    }

    unsigned short first_id = 26 + 4 * (resolution + 1);
    for (unsigned short i = 0; i < resolution; ++i) {
        const unsigned short ii = first_id + i;
        append_triangle(data, ii, ii + 1, ii + resolution + 2);
        append_triangle(data, ii, ii + resolution + 2, ii + resolution + 1);
    }

    append_vertex(data, { inner_radius, 0.0f, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { outer_radius, 0.0f, -half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { inner_radius, 0.0f, half_thickness }, -Vec3f::UnitY());
    append_vertex(data, { outer_radius, 0.0f, half_thickness }, -Vec3f::UnitY());

    first_id = 26 + 6 * (resolution + 1);
    append_triangle(data, first_id, first_id + 1, first_id + 3);
    append_triangle(data, first_id, first_id + 3, first_id + 2);

    for (short i = resolution; i >= 0; --i) {
        const float angle = float(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(data, { outer_radius * s, outer_radius * c, -half_thickness }, { s, c, 0.0f });
    }

    for (short i = resolution; i >= 0; --i) {
        const float angle = float(i) * step_angle;
        const float c = ::cos(angle);
        const float s = ::sin(angle);
        append_vertex(data, { outer_radius * s, outer_radius * c, +half_thickness }, { s, c, 0.0f });
    }

    first_id = 30 + 6 * (resolution + 1);
    for (unsigned short i = 0; i < resolution; ++i) {
        const unsigned short ii = first_id + i;
        append_triangle(data, ii, ii + 1, ii + resolution + 2);
        append_triangle(data, ii, ii + resolution + 2, ii + resolution + 1);
    }

    return data;
}

GLModel::Geometry straight_arrow(float tip_width, float tip_height, float stem_width, float stem_height, float thickness)
{
    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3, GLModel::Geometry::EIndexType::USHORT };
    data.reserve_vertices(42);
    data.reserve_indices(72);

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
    append_triangle(data, 0, 1, 6);
    append_triangle(data, 6, 1, 5);
    append_triangle(data, 4, 5, 3);
    append_triangle(data, 5, 1, 3);
    append_triangle(data, 1, 2, 3);

    // bottom face vertices
    append_vertex(data, { half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { 0.0, total_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -half_tip_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -half_stem_width, stem_height, -half_thickness }, -Vec3f::UnitZ());
    append_vertex(data, { -half_stem_width, 0.0, -half_thickness }, -Vec3f::UnitZ());

    // bottom face triangles
    append_triangle(data, 7, 13, 8);
    append_triangle(data, 13, 12, 8);
    append_triangle(data, 12, 11, 10);
    append_triangle(data, 8, 12, 10);
    append_triangle(data, 9, 8, 10);

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

    normal = { -tip_height, half_tip_width, 0.0f };
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
    for (unsigned short i = 0; i < 7; ++i) {
        const unsigned short ii = i * 4;
        append_triangle(data, 14 + ii, 15 + ii, 17 + ii);
        append_triangle(data, 14 + ii, 17 + ii, 16 + ii);
    }

    return data;
}

GLModel::Geometry diamond(unsigned short resolution)
{
    resolution = std::max<unsigned short>(4, resolution);
    resolution = std::min<unsigned short>(65534, resolution); // ensure no unsigned short overflow of indices

    GLModel::Geometry data;
    data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3, GLModel::Geometry::EIndexType::USHORT };
    data.reserve_vertices(resolution + 2);
    data.reserve_indices((2 * (resolution + 1)) * 3);

    const float step = 2.0f * float(PI) / float(resolution);

    // vertices
    for (unsigned short i = 0; i < resolution; ++i) {
        float ii = float(i) * step;
        const Vec3f p = { 0.5f * ::cos(ii), 0.5f * ::sin(ii), 0.0f };
        append_vertex(data, p, p.normalized());
    }
    Vec3f p = { 0.0f, 0.0f, 0.5f };
    append_vertex(data, p, p.normalized());
    p = { 0.0f, 0.0f, -0.5f };
    append_vertex(data, p, p.normalized());

    // triangles
    // top
    for (unsigned short i = 0; i < resolution; ++i) {
        append_triangle(data, i + 0, i + 1, resolution);
    }
    append_triangle(data, resolution - 1, 0, resolution);

    // bottom
    for (unsigned short i = 0; i < resolution; ++i) {
        append_triangle(data, i + 0, resolution + 1, i + 1);
    }
    append_triangle(data, resolution - 1, resolution + 1, 0);

    return data;
}

} // namespace GUI
} // namespace Slic3r
