#include <GL/glew.h>

#if ENABLE_SMOOTH_NORMALS
#include <igl/per_face_normals.h>
#include <igl/per_corner_normals.h>
#include <igl/per_vertex_normals.h>
#endif // ENABLE_SMOOTH_NORMALS

#include "3DScene.hpp"
#include "GLShader.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "BitmapCache.hpp"
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
#include "3DBed.hpp"
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionEntityCollection.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Slicing.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Tesselate.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <boost/log/trivial.hpp>

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <Eigen/Dense>

#ifdef HAS_GLSAFE
void glAssertRecentCallImpl(const char* file_name, unsigned int line, const char* function_name)
{
#if defined(NDEBUG)
    // In release mode, only show OpenGL errors if sufficiently high loglevel.
    if (Slic3r::get_logging_level() < 5)
        return;
#endif // NDEBUG

    GLenum err = glGetError();
    if (err == GL_NO_ERROR)
        return;
    const char* sErr = 0;
    switch (err) {
    case GL_INVALID_ENUM:       sErr = "Invalid Enum";      break;
    case GL_INVALID_VALUE:      sErr = "Invalid Value";     break;
    // be aware that GL_INVALID_OPERATION is generated if glGetError is executed between the execution of glBegin and the corresponding execution of glEnd 
    case GL_INVALID_OPERATION:  sErr = "Invalid Operation"; break;
    case GL_STACK_OVERFLOW:     sErr = "Stack Overflow";    break;
    case GL_STACK_UNDERFLOW:    sErr = "Stack Underflow";   break;
    case GL_OUT_OF_MEMORY:      sErr = "Out Of Memory";     break;
    default:                    sErr = "Unknown";           break;
    }
    BOOST_LOG_TRIVIAL(error) << "OpenGL error in " << file_name << ":" << line << ", function " << function_name << "() : " << (int)err << " - " << sErr;
    assert(false);
}
#endif // HAS_GLSAFE

namespace Slic3r {

#if ENABLE_SMOOTH_NORMALS
static void smooth_normals_corner(TriangleMesh& mesh, std::vector<stl_normal>& normals)
{
    mesh.repair();

    using MapMatrixXfUnaligned = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;
    using MapMatrixXiUnaligned = Eigen::Map<const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;

    std::vector<stl_normal> face_normals(mesh.stl.stats.number_of_facets);
    for (uint32_t i = 0; i < mesh.stl.stats.number_of_facets; ++i) {
        face_normals[i] = mesh.stl.facet_start[i].normal;
    }

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

static void smooth_normals_vertex(TriangleMesh& mesh, std::vector<stl_normal>& normals)
{
    mesh.repair();

    using MapMatrixXfUnaligned = Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;
    using MapMatrixXiUnaligned = Eigen::Map<const Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>>;

    Eigen::MatrixXd vertices = MapMatrixXfUnaligned(mesh.its.vertices.front().data(),
        Eigen::Index(mesh.its.vertices.size()), 3).cast<double>();
    Eigen::MatrixXi indices = MapMatrixXiUnaligned(mesh.its.indices.front().data(),
        Eigen::Index(mesh.its.indices.size()), 3);
    Eigen::MatrixXd out_normals;

//    igl::per_vertex_normals(vertices, indices, igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_UNIFORM, out_normals);
//    igl::per_vertex_normals(vertices, indices, igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_AREA, out_normals);
    igl::per_vertex_normals(vertices, indices, igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_ANGLE, out_normals);
//    igl::per_vertex_normals(vertices, indices, igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_DEFAULT, out_normals);

    normals = std::vector<stl_normal>(mesh.its.vertices.size());
    for (size_t i = 0; i < static_cast<size_t>(out_normals.rows()); ++i) {
        normals[i] = out_normals.row(i).cast<float>();
    }
}
#endif // ENABLE_SMOOTH_NORMALS

#if ENABLE_SMOOTH_NORMALS
void GLIndexedVertexArray::load_mesh_full_shading(const TriangleMesh& mesh, bool smooth_normals)
#else
void GLIndexedVertexArray::load_mesh_full_shading(const TriangleMesh& mesh)
#endif // ENABLE_SMOOTH_NORMALS
{
    assert(triangle_indices.empty() && vertices_and_normals_interleaved_size == 0);
    assert(quad_indices.empty() && triangle_indices_size == 0);
    assert(vertices_and_normals_interleaved.size() % 6 == 0 && quad_indices_size == vertices_and_normals_interleaved.size());

#if ENABLE_SMOOTH_NORMALS
    if (smooth_normals) {
        TriangleMesh new_mesh(mesh);
        std::vector<stl_normal> normals;
        smooth_normals_corner(new_mesh, normals);
//        smooth_normals_vertex(new_mesh, normals);

        this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 2 * new_mesh.its.vertices.size());
        for (size_t i = 0; i < new_mesh.its.vertices.size(); ++i) {
            const stl_vertex& v = new_mesh.its.vertices[i];
            const stl_normal& n = normals[i];
            this->push_geometry(v(0), v(1), v(2), n(0), n(1), n(2));
        }

        for (size_t i = 0; i < new_mesh.its.indices.size(); ++i) {
            const stl_triangle_vertex_indices& idx = new_mesh.its.indices[i];
            this->push_triangle(idx(0), idx(1), idx(2));
        }
    }
    else {
#endif // ENABLE_SMOOTH_NORMALS
        this->load_its_flat_shading(mesh.its);
#if ENABLE_SMOOTH_NORMALS
    }
#endif // ENABLE_SMOOTH_NORMALS
}

void GLIndexedVertexArray::load_its_flat_shading(const indexed_triangle_set &its)
{
    this->vertices_and_normals_interleaved.reserve(this->vertices_and_normals_interleaved.size() + 3 * 3 * 2 * its.indices.size());
    unsigned int vertices_count = 0;
    for (int i = 0; i < int(its.indices.size()); ++ i) {
        stl_triangle_vertex_indices face        = its.indices[i];
        stl_vertex                  vertex[3]   = { its.vertices[face[0]], its.vertices[face[1]], its.vertices[face[2]] };
        stl_vertex                  n           = face_normal_normalized(vertex);
        for (int j = 0; j < 3; ++j)
            this->push_geometry(vertex[j](0), vertex[j](1), vertex[j](2), n(0), n(1), n(2));
        this->push_triangle(vertices_count, vertices_count + 1, vertices_count + 2);
        vertices_count += 3;
    }
}

void GLIndexedVertexArray::finalize_geometry(bool opengl_initialized)
{
    assert(this->vertices_and_normals_interleaved_VBO_id == 0);
    assert(this->triangle_indices_VBO_id == 0);
    assert(this->quad_indices_VBO_id == 0);

	if (! opengl_initialized) {
		// Shrink the data vectors to conserve memory in case the data cannot be transfered to the OpenGL driver yet.
		this->shrink_to_fit();
		return;
	}

    if (! this->vertices_and_normals_interleaved.empty()) {
        glsafe(::glGenBuffers(1, &this->vertices_and_normals_interleaved_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved.size() * 4, this->vertices_and_normals_interleaved.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->vertices_and_normals_interleaved.clear();
    }
    if (! this->triangle_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->triangle_indices_VBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices.size() * 4, this->triangle_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->triangle_indices.clear();
    }
    if (! this->quad_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->quad_indices_VBO_id));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id));
        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices.size() * 4, this->quad_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        this->quad_indices.clear();
    }
}

void GLIndexedVertexArray::release_geometry()
{
    if (this->vertices_and_normals_interleaved_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->vertices_and_normals_interleaved_VBO_id));
        this->vertices_and_normals_interleaved_VBO_id = 0;
    }
    if (this->triangle_indices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->triangle_indices_VBO_id));
        this->triangle_indices_VBO_id = 0;
    }
    if (this->quad_indices_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->quad_indices_VBO_id));
        this->quad_indices_VBO_id = 0;
    }
    this->clear();
}

void GLIndexedVertexArray::render() const
{
    assert(this->vertices_and_normals_interleaved_VBO_id != 0);
    assert(this->triangle_indices_VBO_id != 0 || this->quad_indices_VBO_id != 0);

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float))));
    glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    // Render using the Vertex Buffer Objects.
    if (this->triangle_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(this->triangle_indices_size), GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }
    if (this->quad_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id));
        glsafe(::glDrawElements(GL_QUADS, GLsizei(this->quad_indices_size), GL_UNSIGNED_INT, nullptr));
        glsafe(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLIndexedVertexArray::render(
    const std::pair<size_t, size_t>& tverts_range,
    const std::pair<size_t, size_t>& qverts_range) const
{
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    // this method has been called before calling finalize() ?
    if (this->vertices_and_normals_interleaved_VBO_id == 0 && !this->vertices_and_normals_interleaved.empty())
        return;
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

    assert(this->vertices_and_normals_interleaved_VBO_id != 0);
    assert(this->triangle_indices_VBO_id != 0 || this->quad_indices_VBO_id != 0);

    // Render using the Vertex Buffer Objects.
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->vertices_and_normals_interleaved_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), (const void*)(3 * sizeof(float))));
    glsafe(::glNormalPointer(GL_FLOAT, 6 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
    glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

    if (this->triangle_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->triangle_indices_VBO_id));
        glsafe(::glDrawElements(GL_TRIANGLES, GLsizei(std::min(this->triangle_indices_size, tverts_range.second - tverts_range.first)), GL_UNSIGNED_INT, (const void*)(tverts_range.first * 4)));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }
    if (this->quad_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_indices_VBO_id));
        glsafe(::glDrawElements(GL_QUADS, GLsizei(std::min(this->quad_indices_size, qverts_range.second - qverts_range.first)), GL_UNSIGNED_INT, (const void*)(qverts_range.first * 4)));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
    glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
    
    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

const float GLVolume::SinkingContours::HalfWidth = 0.25f;

void GLVolume::SinkingContours::render()
{
    update();

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_shift.x(), m_shift.y(), m_shift.z()));
    m_model.render();
    glsafe(::glPopMatrix());
}

void GLVolume::SinkingContours::update()
{
    int object_idx = m_parent.object_idx();
    Model& model = GUI::wxGetApp().plater()->model();

    if (0 <= object_idx && object_idx < (int)model.objects.size() && m_parent.is_sinking() && !m_parent.is_below_printbed()) {
        const BoundingBoxf3& box = m_parent.transformed_convex_hull_bounding_box();
        if (!m_old_box.size().isApprox(box.size()) || m_old_box.min.z() != box.min.z()) {
            m_old_box = box;
            m_shift = Vec3d::Zero();

            const TriangleMesh& mesh = model.objects[object_idx]->volumes[m_parent.volume_idx()]->mesh();

            m_model.reset();
            GUI::GLModel::InitializationData init_data;
            MeshSlicingParams slicing_params;
            slicing_params.trafo = m_parent.world_matrix();
            Polygons polygons = union_(slice_mesh(mesh.its, 0.0f, slicing_params));
            for (ExPolygon &expoly : diff_ex(expand(polygons, float(scale_(HalfWidth))), shrink(polygons, float(scale_(HalfWidth))))) {
                GUI::GLModel::InitializationData::Entity entity;
                entity.type = GUI::GLModel::PrimitiveType::Triangles;
                const std::vector<Vec3d> triangulation = triangulate_expolygon_3d(expoly);
                for (const Vec3d& v : triangulation) {
                    entity.positions.emplace_back(v.cast<float>() + Vec3f(0.0f, 0.0f, 0.015f)); // add a small positive z to avoid z-fighting
                    entity.normals.emplace_back(Vec3f::UnitZ());
                    const size_t positions_count = entity.positions.size();
                    if (positions_count % 3 == 0) {
                        entity.indices.emplace_back(positions_count - 3);
                        entity.indices.emplace_back(positions_count - 2);
                        entity.indices.emplace_back(positions_count - 1);
                    }
                }
                init_data.entities.emplace_back(entity);
            }

            m_model.init_from(init_data);
        }
        else
            m_shift = box.center() - m_old_box.center();
    }
    else
        m_model.reset();
}

const std::array<float, 4> GLVolume::SELECTED_COLOR = { 0.0f, 1.0f, 0.0f, 1.0f };
const std::array<float, 4> GLVolume::HOVER_SELECT_COLOR = { 0.4f, 0.9f, 0.1f, 1.0f };
const std::array<float, 4> GLVolume::HOVER_DESELECT_COLOR = { 1.0f, 0.75f, 0.75f, 1.0f };
const std::array<float, 4> GLVolume::OUTSIDE_COLOR = { 0.0f, 0.38f, 0.8f, 1.0f };
const std::array<float, 4> GLVolume::SELECTED_OUTSIDE_COLOR = { 0.19f, 0.58f, 1.0f, 1.0f };
const std::array<float, 4> GLVolume::DISABLED_COLOR = { 0.25f, 0.25f, 0.25f, 1.0f };
const std::array<float, 4> GLVolume::SLA_SUPPORT_COLOR = { 0.75f, 0.75f, 0.75f, 1.0f };
const std::array<float, 4> GLVolume::SLA_PAD_COLOR = { 0.0f, 0.2f, 0.0f, 1.0f };
const std::array<float, 4> GLVolume::NEUTRAL_COLOR = { 0.9f, 0.9f, 0.9f, 1.0f };
const std::array<std::array<float, 4>, 4> GLVolume::MODEL_COLOR = { {
    { 1.0f, 1.0f, 0.0f, 1.f },
    { 1.0f, 0.5f, 0.5f, 1.f },
    { 0.5f, 1.0f, 0.5f, 1.f },
    { 0.5f, 0.5f, 1.0f, 1.f }
} };

GLVolume::GLVolume(float r, float g, float b, float a)
    : m_sla_shift_z(0.0)
    , m_sinking_contours(*this)
    // geometry_id == 0 -> invalid
    , geometry_id(std::pair<size_t, size_t>(0, 0))
    , extruder_id(0)
    , selected(false)
    , disabled(false)
    , printable(true)
    , is_active(true)
    , zoom_to_volumes(true)
    , shader_outside_printer_detection_enabled(false)
    , is_outside(false)
    , hover(HS_None)
    , is_modifier(false)
    , is_wipe_tower(false)
    , is_extrusion_path(false)
    , force_transparent(false)
    , force_native_color(false)
    , force_neutral_color(false)
    , force_sinking_contours(false)
    , tverts_range(0, size_t(-1))
    , qverts_range(0, size_t(-1))
{
    color = { r, g, b, a };
    set_render_color(color);
}

void GLVolume::set_color(const std::array<float, 4>& rgba)
{
    color = rgba;
}

void GLVolume::set_render_color(float r, float g, float b, float a)
{
    render_color = { r, g, b, a };
}

void GLVolume::set_render_color(const std::array<float, 4>& rgba)
{
    render_color = rgba;
}

void GLVolume::set_render_color()
{
    bool outside = is_outside || is_below_printbed();

    if (force_native_color || force_neutral_color) {
        if (outside && shader_outside_printer_detection_enabled)
            set_render_color(OUTSIDE_COLOR);
        else {
            if (force_native_color)
                set_render_color(color);
            else
                set_render_color(NEUTRAL_COLOR);
        }
    }
    else {
        if (hover == HS_Select)
            set_render_color(HOVER_SELECT_COLOR);
        else if (hover == HS_Deselect)
            set_render_color(HOVER_DESELECT_COLOR);
        else if (selected)
            set_render_color(outside ? SELECTED_OUTSIDE_COLOR : SELECTED_COLOR);
        else if (disabled)
            set_render_color(DISABLED_COLOR);
        else if (outside && shader_outside_printer_detection_enabled)
            set_render_color(OUTSIDE_COLOR);
        else
            set_render_color(color);
    }

    if (!printable) {
        render_color[0] /= 4;
        render_color[1] /= 4;
        render_color[2] /= 4;
    }

    if (force_transparent)
        render_color[3] = color[3];
}

std::array<float, 4> color_from_model_volume(const ModelVolume& model_volume)
{
    std::array<float, 4> color;
    if (model_volume.is_negative_volume()) {
        color[0] = 0.2f;
        color[1] = 0.2f;
        color[2] = 0.2f;
    }
    else if (model_volume.is_modifier()) {
#if ENABLE_MODIFIERS_ALWAYS_TRANSPARENT
        color[0] = 1.0f;
        color[1] = 1.0f;
        color[2] = 0.2f;
#else
        color[0] = 0.2f;
        color[1] = 1.0f;
        color[2] = 0.2f;
#endif // ENABLE_MODIFIERS_ALWAYS_TRANSPARENT
    }
    else if (model_volume.is_support_blocker()) {
        color[0] = 1.0f;
        color[1] = 0.2f;
        color[2] = 0.2f;
    }
    else if (model_volume.is_support_enforcer()) {
        color[0] = 0.2f;
        color[1] = 0.2f;
        color[2] = 1.0f;
    }
    color[3] = model_volume.is_model_part() ? 1.f : 0.5f;
    return color;
}

Transform3d GLVolume::world_matrix() const
{
    Transform3d m = m_instance_transformation.get_matrix() * m_volume_transformation.get_matrix();
    m.translation()(2) += m_sla_shift_z;
    return m;
}

bool GLVolume::is_left_handed() const
{
    const Vec3d &m1 = m_instance_transformation.get_mirror();
    const Vec3d &m2 = m_volume_transformation.get_mirror();
    return m1.x() * m1.y() * m1.z() * m2.x() * m2.y() * m2.z() < 0.;
}

const BoundingBoxf3& GLVolume::transformed_bounding_box() const
{
    if (!m_transformed_bounding_box.has_value()) {
        const BoundingBoxf3& box = bounding_box();
        assert(box.defined || box.min.x() >= box.max.x() || box.min.y() >= box.max.y() || box.min.z() >= box.max.z());
        std::optional<BoundingBoxf3>* trans_box = const_cast<std::optional<BoundingBoxf3>*>(&m_transformed_bounding_box);
        *trans_box = box.transformed(world_matrix());
    }
    return *m_transformed_bounding_box;
}

const BoundingBoxf3& GLVolume::transformed_convex_hull_bounding_box() const
{
    if (!m_transformed_convex_hull_bounding_box.has_value()) {
        std::optional<BoundingBoxf3>* trans_box = const_cast<std::optional<BoundingBoxf3>*>(&m_transformed_convex_hull_bounding_box);
        *trans_box = transformed_convex_hull_bounding_box(world_matrix());
    }
    return *m_transformed_convex_hull_bounding_box;
}

BoundingBoxf3 GLVolume::transformed_convex_hull_bounding_box(const Transform3d &trafo) const
{
	return (m_convex_hull && ! m_convex_hull->empty()) ?
		m_convex_hull->transformed_bounding_box(trafo) :
        bounding_box().transformed(trafo);
}

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
BoundingBoxf3 GLVolume::transformed_non_sinking_bounding_box(const Transform3d& trafo) const
{
    return GUI::wxGetApp().plater()->model().objects[object_idx()]->volumes[volume_idx()]->mesh().transformed_bounding_box(trafo, 0.0);
}

const BoundingBoxf3& GLVolume::transformed_non_sinking_bounding_box() const
{
    if (!m_transformed_non_sinking_bounding_box.has_value()) {
        std::optional<BoundingBoxf3>* trans_box = const_cast<std::optional<BoundingBoxf3>*>(&m_transformed_non_sinking_bounding_box);
        const Transform3d& trafo = world_matrix();
        *trans_box = transformed_non_sinking_bounding_box(trafo);
    }
    return *m_transformed_non_sinking_bounding_box;
}
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

void GLVolume::set_range(double min_z, double max_z)
{
    this->qverts_range.first = 0;
    this->qverts_range.second = this->indexed_vertex_array.quad_indices_size;
    this->tverts_range.first = 0;
    this->tverts_range.second = this->indexed_vertex_array.triangle_indices_size;
    if (! this->print_zs.empty()) {
        // The Z layer range is specified.
        // First test whether the Z span of this object is not out of (min_z, max_z) completely.
        if (this->print_zs.front() > max_z || this->print_zs.back() < min_z) {
            this->qverts_range.second = 0;
            this->tverts_range.second = 0;
        } else {
            // Then find the lowest layer to be displayed.
            size_t i = 0;
            for (; i < this->print_zs.size() && this->print_zs[i] < min_z; ++ i);
            if (i == this->print_zs.size()) {
                // This shall not happen.
                this->qverts_range.second = 0;
                this->tverts_range.second = 0;
            } else {
                // Remember start of the layer.
                this->qverts_range.first = this->offsets[i * 2];
                this->tverts_range.first = this->offsets[i * 2 + 1];
                // Some layers are above $min_z. Which?
                for (; i < this->print_zs.size() && this->print_zs[i] <= max_z; ++ i);
                if (i < this->print_zs.size()) {
                    this->qverts_range.second = this->offsets[i * 2];
                    this->tverts_range.second = this->offsets[i * 2 + 1];
                }
            }
        }
    }
}

void GLVolume::render() const
{
    if (!is_active)
        return;

    if (this->is_left_handed())
        glFrontFace(GL_CW);
    glsafe(::glCullFace(GL_BACK));
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(world_matrix().data()));

    this->indexed_vertex_array.render(this->tverts_range, this->qverts_range);

    glsafe(::glPopMatrix());
    if (this->is_left_handed())
        glFrontFace(GL_CCW);
}

bool GLVolume::is_sla_support() const { return this->composite_id.volume_id == -int(slaposSupportTree); }
bool GLVolume::is_sla_pad() const { return this->composite_id.volume_id == -int(slaposPad); }

bool GLVolume::is_sinking() const
{
    if (is_modifier || GUI::wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA)
        return false;
    const BoundingBoxf3& box = transformed_convex_hull_bounding_box();
    return box.min.z() < SINKING_Z_THRESHOLD && box.max.z() >= SINKING_Z_THRESHOLD;
}

bool GLVolume::is_below_printbed() const
{
    return transformed_convex_hull_bounding_box().max.z() < 0.0;
}

void GLVolume::render_sinking_contours()
{
    m_sinking_contours.render();
}

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
void GLVolume::calc_convex_hull_3d()
{
    const std::vector<float> &src = this->indexed_vertex_array.vertices_and_normals_interleaved;
    std::vector<Vec3f>        pts;
    assert(src.size() % 6 == 0);
    pts.reserve(src.size() / 6);
    for (auto it = src.begin(); it != src.end(); ) {
        it += 3;
        pts.push_back({ *it, *(it + 1), *(it + 2) });
        it += 3;
    }
    this->set_convex_hull(TriangleMesh(its_convex_hull(pts)));
}
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

std::vector<int> GLVolumeCollection::load_object(
    const ModelObject       *model_object,
    int                      obj_idx,
    const std::vector<int>  &instance_idxs,
    const std::string       &color_by,
    bool 					 opengl_initialized)
{
    std::vector<int> volumes_idx;
    for (int volume_idx = 0; volume_idx < int(model_object->volumes.size()); ++volume_idx)
        for (int instance_idx : instance_idxs)
            volumes_idx.emplace_back(this->GLVolumeCollection::load_object_volume(model_object, obj_idx, volume_idx, instance_idx, color_by, opengl_initialized));
    return volumes_idx;
}

int GLVolumeCollection::load_object_volume(
    const ModelObject   *model_object,
    int                  obj_idx,
    int                  volume_idx,
    int                  instance_idx,
    const std::string   &color_by,
    bool 				 opengl_initialized)
{
    const ModelVolume   *model_volume = model_object->volumes[volume_idx];
    const int            extruder_id  = model_volume->extruder_id();
    const ModelInstance *instance 	  = model_object->instances[instance_idx];
    const TriangleMesh  &mesh 		  = model_volume->mesh();
    std::array<float, 4> color = GLVolume::MODEL_COLOR[((color_by == "volume") ? volume_idx : obj_idx) % 4];
    color[3] = model_volume->is_model_part() ? 1.f : 0.5f;
    this->volumes.emplace_back(new GLVolume(color));
    GLVolume& v = *this->volumes.back();
    v.set_color(color_from_model_volume(*model_volume));
#if ENABLE_SMOOTH_NORMALS
    v.indexed_vertex_array.load_mesh(mesh, true);
#else
    v.indexed_vertex_array.load_mesh(mesh);
#endif // ENABLE_SMOOTH_NORMALS
    v.indexed_vertex_array.finalize_geometry(opengl_initialized);
    v.composite_id = GLVolume::CompositeID(obj_idx, volume_idx, instance_idx);
    if (model_volume->is_model_part())
    {
        // GLVolume will reference a convex hull from model_volume!
        v.set_convex_hull(model_volume->get_convex_hull_shared_ptr());
        if (extruder_id != -1)
            v.extruder_id = extruder_id;
    }
    v.is_modifier = !model_volume->is_model_part();
    v.shader_outside_printer_detection_enabled = model_volume->is_model_part();
    v.set_instance_transformation(instance->get_transformation());
    v.set_volume_transformation(model_volume->get_transformation());

    return int(this->volumes.size() - 1);
}

// Load SLA auxiliary GLVolumes (for support trees or pad).
// This function produces volumes for multiple instances in a single shot,
// as some object specific mesh conversions may be expensive.
void GLVolumeCollection::load_object_auxiliary(
    const SLAPrintObject 		   *print_object,
    int                             obj_idx,
    // pairs of <instance_idx, print_instance_idx>
    const std::vector<std::pair<size_t, size_t>>& instances,
    SLAPrintObjectStep              milestone,
    // Timestamp of the last change of the milestone
    size_t                          timestamp,
    bool 				 			opengl_initialized)
{
    assert(print_object->is_step_done(milestone));
    Transform3d  mesh_trafo_inv = print_object->trafo().inverse();
    // Get the support mesh.
    TriangleMesh mesh = print_object->get_mesh(milestone);
    mesh.transform(mesh_trafo_inv);
    // Convex hull is required for out of print bed detection.
    TriangleMesh convex_hull = mesh.convex_hull_3d();
    for (const std::pair<size_t, size_t>& instance_idx : instances) {
        const ModelInstance& model_instance = *print_object->model_object()->instances[instance_idx.first];
        this->volumes.emplace_back(new GLVolume((milestone == slaposPad) ? GLVolume::SLA_PAD_COLOR : GLVolume::SLA_SUPPORT_COLOR));
        GLVolume& v = *this->volumes.back();
#if ENABLE_SMOOTH_NORMALS
        v.indexed_vertex_array.load_mesh(mesh, true);
#else
        v.indexed_vertex_array.load_mesh(mesh);
#endif // ENABLE_SMOOTH_NORMALS
        v.indexed_vertex_array.finalize_geometry(opengl_initialized);
        v.composite_id = GLVolume::CompositeID(obj_idx, -int(milestone), (int)instance_idx.first);
        v.geometry_id = std::pair<size_t, size_t>(timestamp, model_instance.id().id);
        // Create a copy of the convex hull mesh for each instance. Use a move operator on the last instance.
        if (&instance_idx == &instances.back())
            v.set_convex_hull(std::move(convex_hull));
        else
            v.set_convex_hull(convex_hull);
        v.is_modifier = false;
        v.shader_outside_printer_detection_enabled = (milestone == slaposSupportTree);
        v.set_instance_transformation(model_instance.get_transformation());
        // Leave the volume transformation at identity.
        // v.set_volume_transformation(model_volume->get_transformation());
    }
}

int GLVolumeCollection::load_wipe_tower_preview(
    int obj_idx, float pos_x, float pos_y, float width, float depth, float height,
    float rotation_angle, bool size_unknown, float brim_width, bool opengl_initialized)
{
    if (depth < 0.01f)
        return int(this->volumes.size() - 1);
    if (height == 0.0f)
        height = 0.1f;

    TriangleMesh mesh;
    std::array<float, 4> color = { 0.5f, 0.5f, 0.0f, 1.0f };

    // In case we don't know precise dimensions of the wipe tower yet, we'll draw
    // the box with different color with one side jagged:
    if (size_unknown) {
        color[0] = 0.9f;
        color[1] = 0.6f;

        // Too narrow tower would interfere with the teeth. The estimate is not precise anyway.
        depth = std::max(depth, 10.f);
        float min_width = 30.f;
        // We'll now create the box with jagged edge. y-coordinates of the pre-generated model
        // are shifted so that the front edge has y=0 and centerline of the back edge has y=depth:
        float out_points_idx[][3] = { { 0, -depth, 0 }, { 0, 0, 0 }, { 38.453f, 0, 0 }, { 61.547f, 0, 0 }, { 100.0f, 0, 0 }, { 100.0f, -depth, 0 }, { 55.7735f, -10.0f, 0 }, { 44.2265f, 10.0f, 0 },
        { 38.453f, 0, 1 }, { 0, 0, 1 }, { 0, -depth, 1 }, { 100.0f, -depth, 1 }, { 100.0f, 0, 1 }, { 61.547f, 0, 1 }, { 55.7735f, -10.0f, 1 }, { 44.2265f, 10.0f, 1 } };
        static constexpr const int out_facets_idx[][3] = { 
            { 0, 1, 2 }, { 3, 4, 5 }, { 6, 5, 0 }, { 3, 5, 6 }, { 6, 2, 7 }, { 6, 0, 2 }, { 8, 9, 10 }, { 11, 12, 13 }, { 10, 11, 14 }, { 14, 11, 13 }, { 15, 8, 14 },
            { 8, 10, 14 }, { 3, 12, 4 }, { 3, 13, 12 }, { 6, 13, 3 }, { 6, 14, 13 }, { 7, 14, 6 }, { 7, 15, 14 }, { 2, 15, 7 }, { 2, 8, 15 }, { 1, 8, 2 }, { 1, 9, 8 },
            { 0, 9, 1 }, { 0, 10, 9 }, { 5, 10, 0 }, { 5, 11, 10 }, { 4, 11, 5 }, { 4, 12, 11 } };
        indexed_triangle_set its;
        for (int i = 0; i < 16; ++i)
            its.vertices.emplace_back(out_points_idx[i][0] / (100.f / min_width),
                                      out_points_idx[i][1] + depth, out_points_idx[i][2]);
        its.indices.reserve(28);
        for (const int *face : out_facets_idx)
            its.indices.emplace_back(face);
        TriangleMesh tooth_mesh(std::move(its));

        // We have the mesh ready. It has one tooth and width of min_width. We will now
        // append several of these together until we are close to the required width
        // of the block. Than we can scale it precisely.
        size_t n = std::max(1, int(width / min_width)); // How many shall be merged?
        for (size_t i = 0; i < n; ++i) {
            mesh.merge(tooth_mesh);
            tooth_mesh.translate(min_width, 0.f, 0.f);
        }

        mesh.scale(Vec3f(width / (n * min_width), 1.f, height)); // Scaling to proper width
    }
    else
        mesh = make_cube(width, depth, height);

    // We'll make another mesh to show the brim (fixed layer height):
    TriangleMesh brim_mesh = make_cube(width + 2.f * brim_width, depth + 2.f * brim_width, 0.2f);
    brim_mesh.translate(-brim_width, -brim_width, 0.f);
    mesh.merge(brim_mesh);

    volumes.emplace_back(new GLVolume(color));
    GLVolume& v = *volumes.back();
    v.indexed_vertex_array.load_mesh(mesh);
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    v.set_convex_hull(mesh.convex_hull_3d());
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    v.indexed_vertex_array.finalize_geometry(opengl_initialized);
    v.set_volume_offset(Vec3d(pos_x, pos_y, 0.0));
    v.set_volume_rotation(Vec3d(0., 0., (M_PI / 180.) * rotation_angle));
    v.composite_id = GLVolume::CompositeID(obj_idx, 0, 0);
    v.geometry_id.first = 0;
    v.geometry_id.second = wipe_tower_instance_id().id;
    v.is_wipe_tower = true;
    v.shader_outside_printer_detection_enabled = !size_unknown;
    return int(volumes.size() - 1);
}

GLVolume* GLVolumeCollection::new_toolpath_volume(const std::array<float, 4>& rgba, size_t reserve_vbo_floats)
{
	GLVolume *out = new_nontoolpath_volume(rgba, reserve_vbo_floats);
	out->is_extrusion_path = true;
	return out;
}

GLVolume* GLVolumeCollection::new_nontoolpath_volume(const std::array<float, 4>& rgba, size_t reserve_vbo_floats)
{
	GLVolume *out = new GLVolume(rgba);
	out->is_extrusion_path = false;
	// Reserving number of vertices (3x position + 3x color)
	out->indexed_vertex_array.reserve(reserve_vbo_floats / 6);
	this->volumes.emplace_back(out);
	return out;
}

GLVolumeWithIdAndZList volumes_to_render(const GLVolumePtrs& volumes, GLVolumeCollection::ERenderType type, const Transform3d& view_matrix, std::function<bool(const GLVolume&)> filter_func)
{
    GLVolumeWithIdAndZList list;
    list.reserve(volumes.size());

    for (unsigned int i = 0; i < (unsigned int)volumes.size(); ++i) {
        GLVolume* volume = volumes[i];
        bool is_transparent = (volume->render_color[3] < 1.0f);
        if (((type == GLVolumeCollection::ERenderType::Opaque && !is_transparent) ||
             (type == GLVolumeCollection::ERenderType::Transparent && is_transparent) ||
             type == GLVolumeCollection::ERenderType::All) &&
            (! filter_func || filter_func(*volume)))
            list.emplace_back(std::make_pair(volume, std::make_pair(i, 0.0)));
    }

    if (type == GLVolumeCollection::ERenderType::Transparent && list.size() > 1) {
        for (GLVolumeWithIdAndZ& volume : list) {
            volume.second.second = volume.first->bounding_box().transformed(view_matrix * volume.first->world_matrix()).max(2);
        }

        std::sort(list.begin(), list.end(),
            [](const GLVolumeWithIdAndZ& v1, const GLVolumeWithIdAndZ& v2) -> bool { return v1.second.second < v2.second.second; }
        );
    }
    else if (type == GLVolumeCollection::ERenderType::Opaque && list.size() > 1) {
        std::sort(list.begin(), list.end(),
            [](const GLVolumeWithIdAndZ& v1, const GLVolumeWithIdAndZ& v2) -> bool { return v1.first->selected && !v2.first->selected; }
        );
    }

    return list;
}

void GLVolumeCollection::render(GLVolumeCollection::ERenderType type, bool disable_cullface, const Transform3d& view_matrix, std::function<bool(const GLVolume&)> filter_func) const
{
    GLVolumeWithIdAndZList to_render = volumes_to_render(volumes, type, view_matrix, filter_func);
    if (to_render.empty())
        return;

    GLShaderProgram* shader = GUI::wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;

    if (type == ERenderType::Transparent) {
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    }

    glsafe(::glCullFace(GL_BACK));
    if (disable_cullface)
        glsafe(::glDisable(GL_CULL_FACE));

    for (GLVolumeWithIdAndZ& volume : to_render) {
#if ENABLE_MODIFIERS_ALWAYS_TRANSPARENT
        if (type == ERenderType::Transparent)
            volume.first->force_transparent = true;
#endif // ENABLE_MODIFIERS_ALWAYS_TRANSPARENT
        volume.first->set_render_color();
#if ENABLE_MODIFIERS_ALWAYS_TRANSPARENT
        if (type == ERenderType::Transparent)
            volume.first->force_transparent = false;
#endif // ENABLE_MODIFIERS_ALWAYS_TRANSPARENT

        // render sinking contours of non-hovered volumes
        if (m_show_sinking_contours)
            if (volume.first->is_sinking() && !volume.first->is_below_printbed() &&
                volume.first->hover == GLVolume::HS_None && !volume.first->force_sinking_contours) {
                shader->stop_using();
                volume.first->render_sinking_contours();
                shader->start_using();
            }

        glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
        glsafe(::glEnableClientState(GL_NORMAL_ARRAY));

        shader->set_uniform("uniform_color", volume.first->render_color);
        shader->set_uniform("z_range", m_z_range, 2);
        shader->set_uniform("clipping_plane", m_clipping_plane, 4);
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        shader->set_uniform("print_volume.type", static_cast<int>(m_print_volume.type));
        shader->set_uniform("print_volume.xy_data", m_print_volume.data);
        shader->set_uniform("print_volume.z_data", m_print_volume.zs);
        shader->set_uniform("volume_world_matrix", volume.first->world_matrix());
#else
        shader->set_uniform("print_box.min", m_print_box_min, 3);
        shader->set_uniform("print_box.max", m_print_box_max, 3);
        shader->set_uniform("print_box.actived", volume.first->shader_outside_printer_detection_enabled);
        shader->set_uniform("print_box.volume_world_matrix", volume.first->world_matrix());
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        shader->set_uniform("slope.actived", m_slope.active && !volume.first->is_modifier && !volume.first->is_wipe_tower);
        shader->set_uniform("slope.volume_world_normal_matrix", static_cast<Matrix3f>(volume.first->world_matrix().matrix().block(0, 0, 3, 3).inverse().transpose().cast<float>()));
        shader->set_uniform("slope.normal_z", m_slope.normal_z);

#if ENABLE_ENVIRONMENT_MAP
        unsigned int environment_texture_id = GUI::wxGetApp().plater()->get_environment_texture_id();
        bool use_environment_texture = environment_texture_id > 0 && GUI::wxGetApp().app_config->get("use_environment_map") == "1";
        shader->set_uniform("use_environment_tex", use_environment_texture);
        if (use_environment_texture)
            glsafe(::glBindTexture(GL_TEXTURE_2D, environment_texture_id));
#endif // ENABLE_ENVIRONMENT_MAP
        glcheck();

        volume.first->render();

#if ENABLE_ENVIRONMENT_MAP
        if (use_environment_texture)
            glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
#endif // ENABLE_ENVIRONMENT_MAP

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
        glsafe(::glDisableClientState(GL_NORMAL_ARRAY));
    }

    if (m_show_sinking_contours) {
        for (GLVolumeWithIdAndZ& volume : to_render) {
            // render sinking contours of hovered/displaced volumes
            if (volume.first->is_sinking() && !volume.first->is_below_printbed() &&
                (volume.first->hover != GLVolume::HS_None || volume.first->force_sinking_contours)) {
                shader->stop_using();
                glsafe(::glDepthFunc(GL_ALWAYS));
                volume.first->render_sinking_contours();
                glsafe(::glDepthFunc(GL_LESS));
                shader->start_using();
            }
        }
    }

    if (disable_cullface)
        glsafe(::glEnable(GL_CULL_FACE));

    if (type == ERenderType::Transparent)
        glsafe(::glDisable(GL_BLEND));
}

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
bool GLVolumeCollection::check_outside_state(const DynamicPrintConfig* config, ModelInstanceEPrintVolumeState* out_state, bool as_toolpaths) const
#else
bool GLVolumeCollection::check_outside_state(const DynamicPrintConfig* config, ModelInstanceEPrintVolumeState* out_state) const
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
{
    if (config == nullptr)
        return false;

    const ConfigOptionPoints* opt = dynamic_cast<const ConfigOptionPoints*>(config->option("bed_shape"));
    if (opt == nullptr)
        return false;

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    const Polygon bed_poly = offset(Polygon::new_scale(opt->values), static_cast<float>(scale_(BedEpsilon))).front();
    const float bed_height = config->opt_float("max_print_height");
    const BoundingBox bed_box_2D = get_extents(bed_poly);
    BoundingBoxf3 print_volume({ unscale<double>(bed_box_2D.min.x()), unscale<double>(bed_box_2D.min.y()), -1e10 },
                               { unscale<double>(bed_box_2D.max.x()), unscale<double>(bed_box_2D.max.y()), bed_height });

    auto check_against_rectangular_bed = [&print_volume](GLVolume& volume, ModelInstanceEPrintVolumeState& state) {
        const BoundingBoxf3* const bb = (volume.is_sinking() && volume.object_idx() != -1 && volume.volume_idx() != -1) ? &volume.transformed_non_sinking_bounding_box() : &volume.transformed_convex_hull_bounding_box();
        volume.is_outside = !print_volume.contains(*bb);
        if (volume.printable) {
            if (state == ModelInstancePVS_Inside && volume.is_outside)
                state = ModelInstancePVS_Fully_Outside;
            if (state == ModelInstancePVS_Fully_Outside && volume.is_outside && print_volume.intersects(*bb))
                state = ModelInstancePVS_Partly_Outside;
        }
    };

    auto check_against_circular_bed = [](GLVolume& volume, ModelInstanceEPrintVolumeState& state, const Vec2d& center, double radius) {
        const TriangleMesh* mesh = (volume.is_sinking() && volume.object_idx() != -1 && volume.volume_idx() != -1)? &GUI::wxGetApp().plater()->model().objects[volume.object_idx()]->volumes[volume.volume_idx()]->mesh() : volume.convex_hull();
        //FIXME 2D convex hull is O(n log n), while testing the 2D points against 2D circle is O(n).
        const Polygon volume_hull_2d = its_convex_hull_2d_above(mesh->its, volume.world_matrix().cast<float>(), 0.0f);
        size_t outside_count = 0;
        const double sq_radius = sqr(radius);
        for (const Point& p : volume_hull_2d.points) {
            if (sq_radius < (unscale(p) - center).squaredNorm())
                ++outside_count;
        }

        volume.is_outside = outside_count > 0;
        if (volume.printable) {
            if (state == ModelInstancePVS_Inside && volume.is_outside)
                state = ModelInstancePVS_Fully_Outside;
            if (state == ModelInstancePVS_Fully_Outside && volume.is_outside && outside_count < volume_hull_2d.size())
                state = ModelInstancePVS_Partly_Outside;
        }
    };

    auto check_against_convex_bed = [&bed_poly, bed_height](GLVolume& volume, ModelInstanceEPrintVolumeState& state) {
        const TriangleMesh* mesh = (volume.is_sinking() && volume.object_idx() != -1 && volume.volume_idx() != -1) ? &GUI::wxGetApp().plater()->model().objects[volume.object_idx()]->volumes[volume.volume_idx()]->mesh() : volume.convex_hull();
        const Polygon volume_hull_2d = its_convex_hull_2d_above(mesh->its, volume.world_matrix().cast<float>(), 0.0f);
        const BoundingBoxf3* const bb = (volume.is_sinking() && volume.object_idx() != -1 && volume.volume_idx() != -1) ? &volume.transformed_non_sinking_bounding_box() : &volume.transformed_convex_hull_bounding_box();
        // Using rotating callipers to check for collision of two convex polygons.
        ModelInstanceEPrintVolumeState volume_state = printbed_collision_state(bed_poly, bed_height, volume_hull_2d, bb->min.z(), bb->max.z());
        bool contained = (volume_state == ModelInstancePVS_Inside);
        bool intersects = (volume_state == ModelInstancePVS_Partly_Outside);

        volume.is_outside = !contained;
        if (volume.printable) {
            if (state == ModelInstancePVS_Inside && volume.is_outside)
                state = ModelInstancePVS_Fully_Outside;

            if (state == ModelInstancePVS_Fully_Outside && volume.is_outside && intersects)
                state = ModelInstancePVS_Partly_Outside;
        }
    };
#else
    const BoundingBox bed_box_2D = get_extents(Polygon::new_scale(opt->values));
    BoundingBoxf3 print_volume({ unscale<double>(bed_box_2D.min.x()), unscale<double>(bed_box_2D.min.y()), 0.0 },
        { unscale<double>(bed_box_2D.max.x()), unscale<double>(bed_box_2D.max.y()), config->opt_float("max_print_height") });
    // Allow the objects to protrude below the print bed
    print_volume.min.z() = -1e10;
    print_volume.min.x() -= BedEpsilon;
    print_volume.min.y() -= BedEpsilon;
    print_volume.max.x() += BedEpsilon;
    print_volume.max.y() += BedEpsilon;
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

    ModelInstanceEPrintVolumeState overall_state = ModelInstancePVS_Inside;
    bool contained_min_one = false;

    enum class BedShape { Rectangle, Circle, Convex, NonConvex };
    Vec2d  center;
    double radius;
    BedShape bed_shape = 
        GUI::Bed3D::is_rectangle(opt->values) ? BedShape::Rectangle :
        GUI::Bed3D::is_circle(opt->values, &center, &radius) ? BedShape::Circle :
        GUI::Bed3D::is_convex(opt->values) ? BedShape::Convex : BedShape::NonConvex;

    for (GLVolume* volume : this->volumes) {
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        if (as_toolpaths && !volume->is_extrusion_path)
            continue;
        else if (!as_toolpaths && (volume->is_modifier || (!volume->shader_outside_printer_detection_enabled && (volume->is_wipe_tower || volume->composite_id.volume_id < 0))))
            continue;

        switch (bed_shape) {
        case BedShape::Rectangle:   check_against_rectangular_bed(*volume, overall_state);              break;
        case BedShape::Circle:      check_against_circular_bed(*volume, overall_state, center, radius); break;
        case BedShape::Convex:      check_against_convex_bed(*volume, overall_state);                   break;
        default: break;
        }

        contained_min_one |= !volume->is_outside;
#else
        if (volume->is_modifier || (!volume->shader_outside_printer_detection_enabled && (volume->is_wipe_tower || volume->composite_id.volume_id < 0)))
            continue;

        const BoundingBoxf3& bb = volume->transformed_convex_hull_bounding_box();
        bool contained = print_volume.contains(bb);

        volume->is_outside = !contained;
        if (!volume->printable)
            continue;

        contained_min_one |= contained;

        if (overall_state == ModelInstancePVS_Inside && volume->is_outside)
            overall_state = ModelInstancePVS_Fully_Outside;

        if (overall_state == ModelInstancePVS_Fully_Outside && volume->is_outside && print_volume.intersects(bb))
            overall_state = ModelInstancePVS_Partly_Outside;
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    }

    if (out_state != nullptr)
        *out_state = overall_state;

    return contained_min_one;
}

void GLVolumeCollection::reset_outside_state()
{
    for (GLVolume* volume : this->volumes)
    {
        if (volume != nullptr)
            volume->is_outside = false;
    }
}

void GLVolumeCollection::update_colors_by_extruder(const DynamicPrintConfig* config)
{
    static const float inv_255 = 1.0f / 255.0f;

    struct Color
    {
        std::string text;
        unsigned char rgb[3];

        Color()
            : text("")
        {
            rgb[0] = 255;
            rgb[1] = 255;
            rgb[2] = 255;
        }

        void set(const std::string& text, unsigned char* rgb)
        {
            this->text = text;
            ::memcpy((void*)this->rgb, (const void*)rgb, 3 * sizeof(unsigned char));
        }
    };

    if (config == nullptr)
        return;

    const ConfigOptionStrings* extruders_opt = dynamic_cast<const ConfigOptionStrings*>(config->option("extruder_colour"));
    if (extruders_opt == nullptr)
        return;

    const ConfigOptionStrings* filamemts_opt = dynamic_cast<const ConfigOptionStrings*>(config->option("filament_colour"));
    if (filamemts_opt == nullptr)
        return;

    unsigned int colors_count = std::max((unsigned int)extruders_opt->values.size(), (unsigned int)filamemts_opt->values.size());
    if (colors_count == 0)
        return;

    std::vector<Color> colors(colors_count);

    unsigned char rgb[3];
    for (unsigned int i = 0; i < colors_count; ++i) {
        const std::string& txt_color = config->opt_string("extruder_colour", i);
        if (Slic3r::GUI::BitmapCache::parse_color(txt_color, rgb))
            colors[i].set(txt_color, rgb);
        else {
            const std::string& txt_color = config->opt_string("filament_colour", i);
            if (Slic3r::GUI::BitmapCache::parse_color(txt_color, rgb))
                colors[i].set(txt_color, rgb);
        }
    }

    for (GLVolume* volume : volumes) {
        if (volume == nullptr || volume->is_modifier || volume->is_wipe_tower || (volume->volume_idx() < 0))
            continue;

        int extruder_id = volume->extruder_id - 1;
        if (extruder_id < 0 || (int)colors.size() <= extruder_id)
            extruder_id = 0;

        const Color& color = colors[extruder_id];
        if (!color.text.empty()) {
            for (int i = 0; i < 3; ++i) {
                volume->color[i] = (float)color.rgb[i] * inv_255;
            }
        }
    }
}

std::vector<double> GLVolumeCollection::get_current_print_zs(bool active_only) const
{
    // Collect layer top positions of all volumes.
    std::vector<double> print_zs;
    for (GLVolume *vol : this->volumes)
    {
        if (!active_only || vol->is_active)
            append(print_zs, vol->print_zs);
    }
    std::sort(print_zs.begin(), print_zs.end());

    // Replace intervals of layers with similar top positions with their average value.
    int n = int(print_zs.size());
    int k = 0;
    for (int i = 0; i < n;) {
        int j = i + 1;
        coordf_t zmax = print_zs[i] + EPSILON;
        for (; j < n && print_zs[j] <= zmax; ++ j) ;
        print_zs[k ++] = (j > i + 1) ? (0.5 * (print_zs[i] + print_zs[j - 1])) : print_zs[i];
        i = j;
    }
    if (k < n)
        print_zs.erase(print_zs.begin() + k, print_zs.end());

    return print_zs;
}

size_t GLVolumeCollection::cpu_memory_used() const 
{
	size_t memsize = sizeof(*this) + this->volumes.capacity() * sizeof(GLVolume);
	for (const GLVolume *volume : this->volumes)
		memsize += volume->cpu_memory_used();
	return memsize;
}

size_t GLVolumeCollection::gpu_memory_used() const 
{
	size_t memsize = 0;
	for (const GLVolume *volume : this->volumes)
		memsize += volume->gpu_memory_used();
	return memsize;
}

std::string GLVolumeCollection::log_memory_info() const 
{ 
	return " (GLVolumeCollection RAM: " + format_memsize_MB(this->cpu_memory_used()) + " GPU: " + format_memsize_MB(this->gpu_memory_used()) + " Both: " + format_memsize_MB(this->gpu_memory_used()) + ")";
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_indexed_vertex_array(
    const Lines                 &lines, 
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLIndexedVertexArray        &volume)
{
    assert(! lines.empty());
    if (lines.empty())
        return;

#define LEFT    0
#define RIGHT   1
#define TOP     2
#define BOTTOM  3

    // right, left, top, bottom
    int     idx_prev[4]      = { -1, -1, -1, -1 };
    double  bottom_z_prev    = 0.;
    Vec2d   b1_prev(Vec2d::Zero());
    Vec2d   v_prev(Vec2d::Zero());
    int     idx_initial[4]   = { -1, -1, -1, -1 };
    double  width_initial    = 0.;
    double  bottom_z_initial = 0.0;
    double  len_prev = 0.0;

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ ii) {
        size_t i = (ii == lines.size()) ? 0 : ii;
        const Line &line = lines[i];
        double bottom_z = top_z - heights[i];
        double middle_z = 0.5 * (top_z + bottom_z);
        double width = widths[i];

        bool is_first = (ii == 0);
        bool is_last = (ii == lines_end - 1);
        bool is_closing = closed && is_last;

        Vec2d v = unscale(line.vector()).normalized();
        double len = unscale<double>(line.length());

        Vec2d a = unscale(line.a);
        Vec2d b = unscale(line.b);
        Vec2d a1 = a;
        Vec2d a2 = a;
        Vec2d b1 = b;
        Vec2d b2 = b;
        {
            double dist = 0.5 * width;  // scaled
            double dx = dist * v(0);
            double dy = dist * v(1);
            a1 += Vec2d(+dy, -dx);
            a2 += Vec2d(-dy, +dx);
            b1 += Vec2d(+dy, -dx);
            b2 += Vec2d(-dy, +dx);
        }

        // calculate new XY normals
        Vec2d xy_right_normal = unscale(line.normal()).normalized();

        int idx_a[4] = { 0, 0, 0, 0 }; // initialized to avoid warnings
        int idx_b[4] = { 0, 0, 0, 0 }; // initialized to avoid warnings
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool bottom_z_different = bottom_z_prev != bottom_z;
        bottom_z_prev = bottom_z;

        if (!is_first && bottom_z_different)
        {
            // Found a change of the layer thickness -> Add a cap at the end of the previous segment.
            volume.push_quad(idx_b[BOTTOM], idx_b[LEFT], idx_b[TOP], idx_b[RIGHT]);
        }

        // Share top / bottom vertices if possible.
        if (is_first) {
            idx_a[TOP] = idx_last++;
            volume.push_geometry(a(0), a(1), top_z   , 0., 0.,  1.); 
        } else {
            idx_a[TOP] = idx_prev[TOP];
        }

        if (is_first || bottom_z_different) {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[BOTTOM] = idx_last ++;
            volume.push_geometry(a(0), a(1), bottom_z, 0., 0., -1.);
            idx_a[LEFT ] = idx_last ++;
            volume.push_geometry(a2(0), a2(1), middle_z, -xy_right_normal(0), -xy_right_normal(1), 0.0);
            idx_a[RIGHT] = idx_last ++;
            volume.push_geometry(a1(0), a1(1), middle_z, xy_right_normal(0), xy_right_normal(1), 0.0);
        }
        else {
            idx_a[BOTTOM] = idx_prev[BOTTOM];
        }

        if (is_first) {
            // Start of the 1st line segment.
            width_initial    = width;
            bottom_z_initial = bottom_z;
            memcpy(idx_initial, idx_a, sizeof(int) * 4);
        } else {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
			double v_dot    = v_prev.dot(v);
            // To reduce gpu memory usage, we try to reuse vertices
            // To reduce the visual artifacts, due to averaged normals, we allow to reuse vertices only when any of two adjacent edges 
            // is longer than a fixed threshold.
            // The following value is arbitrary, it comes from tests made on a bunch of models showing the visual artifacts
            double len_threshold = 2.5;

            // Generate new vertices if the angle between adjacent edges is greater than 45 degrees or thresholds conditions are met
            bool sharp = (v_dot < 0.707) || (len_prev > len_threshold) || (len > len_threshold);
            if (sharp) {
                if (!bottom_z_different)
                {
                    // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                    idx_a[RIGHT] = idx_last++;
                    volume.push_geometry(a1(0), a1(1), middle_z, xy_right_normal(0), xy_right_normal(1), 0.0);
                    idx_a[LEFT] = idx_last++;
                    volume.push_geometry(a2(0), a2(1), middle_z, -xy_right_normal(0), -xy_right_normal(1), 0.0);
                    if (cross2(v_prev, v) > 0.) {
                        // Right turn. Fill in the right turn wedge.
                        volume.push_triangle(idx_prev[RIGHT], idx_a[RIGHT], idx_prev[TOP]);
                        volume.push_triangle(idx_prev[RIGHT], idx_prev[BOTTOM], idx_a[RIGHT]);
                    }
                    else {
                        // Left turn. Fill in the left turn wedge.
                        volume.push_triangle(idx_prev[LEFT], idx_prev[TOP], idx_a[LEFT]);
                        volume.push_triangle(idx_prev[LEFT], idx_a[LEFT], idx_prev[BOTTOM]);
                    }
                }
            }
            else
            {
                if (!bottom_z_different)
                {
                    // The two successive segments are nearly collinear.
                    idx_a[LEFT ] = idx_prev[LEFT];
                    idx_a[RIGHT] = idx_prev[RIGHT];
                }
            }
            if (is_closing) {
                if (!sharp) {
                    if (!bottom_z_different)
                    {
                        // Closing a loop with smooth transition. Unify the closing left / right vertices.
                        memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[LEFT ] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT ] * 6, sizeof(float) * 6);
                        memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[RIGHT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6, sizeof(float) * 6);
                        volume.vertices_and_normals_interleaved.erase(volume.vertices_and_normals_interleaved.end() - 12, volume.vertices_and_normals_interleaved.end());
                        // Replace the left / right vertex indices to point to the start of the loop. 
                        for (size_t u = volume.quad_indices.size() - 16; u < volume.quad_indices.size(); ++ u) {
                            if (volume.quad_indices[u] == idx_prev[LEFT])
                                volume.quad_indices[u] = idx_initial[LEFT];
                            else if (volume.quad_indices[u] == idx_prev[RIGHT])
                                volume.quad_indices[u] = idx_initial[RIGHT];
                        }
                    }
                }
                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (is_closing) {
            idx_b[TOP] = idx_initial[TOP];
        } else {
            idx_b[TOP] = idx_last ++;
            volume.push_geometry(b(0), b(1), top_z   , 0., 0.,  1.);
        }

        if (is_closing && (width == width_initial) && (bottom_z == bottom_z_initial)) {
            idx_b[BOTTOM] = idx_initial[BOTTOM];
        } else {
            idx_b[BOTTOM] = idx_last ++;
            volume.push_geometry(b(0), b(1), bottom_z, 0., 0., -1.);
        }
        // Generate new vertices for the end of this line segment.
        idx_b[LEFT  ] = idx_last ++;
        volume.push_geometry(b2(0), b2(1), middle_z, -xy_right_normal(0), -xy_right_normal(1), 0.0);
        idx_b[RIGHT ] = idx_last ++;
        volume.push_geometry(b1(0), b1(1), middle_z, xy_right_normal(0), xy_right_normal(1), 0.0);

        memcpy(idx_prev, idx_b, 4 * sizeof(int));
        bottom_z_prev = bottom_z;
        b1_prev = b1;
        v_prev = v;
        len_prev = len;

        if (bottom_z_different && (closed || (!is_first && !is_last)))
        {
            // Found a change of the layer thickness -> Add a cap at the beginning of this segment.
            volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);
        }

        if (! closed) {
            // Terminate open paths with caps.
            if (is_first)
                volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);
            // We don't use 'else' because both cases are true if we have only one line.
            if (is_last)
                volume.push_quad(idx_b[BOTTOM], idx_b[LEFT], idx_b[TOP], idx_b[RIGHT]);
        }

        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        volume.push_quad(idx_a[BOTTOM], idx_b[BOTTOM], idx_b[RIGHT], idx_a[RIGHT]);
        // top-right face
        volume.push_quad(idx_a[RIGHT], idx_b[RIGHT], idx_b[TOP], idx_a[TOP]);
        // top-left face
        volume.push_quad(idx_a[TOP], idx_b[TOP], idx_b[LEFT], idx_a[LEFT]);
        // bottom-left face
        volume.push_quad(idx_a[LEFT], idx_b[LEFT], idx_b[BOTTOM], idx_a[BOTTOM]);
    }

#undef LEFT
#undef RIGHT
#undef TOP
#undef BOTTOM
}

// caller is responsible for supplying NO lines with zero length
static void thick_lines_to_indexed_vertex_array(const Lines3& lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool closed,
    GLIndexedVertexArray& volume)
{
    assert(!lines.empty());
    if (lines.empty())
        return;

#define LEFT    0
#define RIGHT   1
#define TOP     2
#define BOTTOM  3

    // left, right, top, bottom
    int      idx_initial[4] = { -1, -1, -1, -1 };
    int      idx_prev[4] = { -1, -1, -1, -1 };
    double   z_prev = 0.0;
    double   len_prev = 0.0;
    Vec3d    n_right_prev = Vec3d::Zero();
    Vec3d    n_top_prev = Vec3d::Zero();
    Vec3d    unit_v_prev = Vec3d::Zero();
    double   width_initial = 0.0;

    // new vertices around the line endpoints
    // left, right, top, bottom
    Vec3d a[4] = { Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero() };
    Vec3d b[4] = { Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero(), Vec3d::Zero() };

    // loop once more in case of closed loops
    size_t lines_end = closed ? (lines.size() + 1) : lines.size();
    for (size_t ii = 0; ii < lines_end; ++ii)
    {
        size_t i = (ii == lines.size()) ? 0 : ii;

        const Line3& line = lines[i];
        double height = heights[i];
        double width = widths[i];

        Vec3d unit_v = unscale(line.vector()).normalized();
        double len = unscale<double>(line.length());

        Vec3d n_top = Vec3d::Zero();
        Vec3d n_right = Vec3d::Zero();
        
        if ((line.a(0) == line.b(0)) && (line.a(1) == line.b(1)))
        {
            // vertical segment
            n_top = Vec3d::UnitY();
            n_right = Vec3d::UnitX();
            if (line.a(2) < line.b(2))
                n_right = -n_right;
        }
        else
        {
            // horizontal segment
            n_right = unit_v.cross(Vec3d::UnitZ()).normalized();
            n_top = n_right.cross(unit_v).normalized();
        }

        Vec3d rl_displacement = 0.5 * width * n_right;
        Vec3d tb_displacement = 0.5 * height * n_top;
        Vec3d l_a = unscale(line.a);
        Vec3d l_b = unscale(line.b);

        a[RIGHT] = l_a + rl_displacement;
        a[LEFT] = l_a - rl_displacement;
        a[TOP] = l_a + tb_displacement;
        a[BOTTOM] = l_a - tb_displacement;
        b[RIGHT] = l_b + rl_displacement;
        b[LEFT] = l_b - rl_displacement;
        b[TOP] = l_b + tb_displacement;
        b[BOTTOM] = l_b - tb_displacement;

        Vec3d n_bottom = -n_top;
        Vec3d n_left = -n_right;

        int idx_a[4];
        int idx_b[4];
        int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);

        bool z_different = (z_prev != l_a(2));
        z_prev = l_b(2);

        // Share top / bottom vertices if possible.
        if (ii == 0)
        {
            idx_a[TOP] = idx_last++;
            volume.push_geometry(a[TOP], n_top);
        }
        else
            idx_a[TOP] = idx_prev[TOP];

        if ((ii == 0) || z_different)
        {
            // Start of the 1st line segment or a change of the layer thickness while maintaining the print_z.
            idx_a[BOTTOM] = idx_last++;
            volume.push_geometry(a[BOTTOM], n_bottom);
            idx_a[LEFT] = idx_last++;
            volume.push_geometry(a[LEFT], n_left);
            idx_a[RIGHT] = idx_last++;
            volume.push_geometry(a[RIGHT], n_right);
        }
        else
            idx_a[BOTTOM] = idx_prev[BOTTOM];

        if (ii == 0)
        {
            // Start of the 1st line segment.
            width_initial = width;
            ::memcpy(idx_initial, idx_a, sizeof(int) * 4);
        }
        else
        {
            // Continuing a previous segment.
            // Share left / right vertices if possible.
            double v_dot = unit_v_prev.dot(unit_v);
            bool is_right_turn = n_top_prev.dot(unit_v_prev.cross(unit_v)) > 0.0;

            // To reduce gpu memory usage, we try to reuse vertices
            // To reduce the visual artifacts, due to averaged normals, we allow to reuse vertices only when any of two adjacent edges 
            // is longer than a fixed threshold.
            // The following value is arbitrary, it comes from tests made on a bunch of models showing the visual artifacts
            double len_threshold = 2.5;

            // Generate new vertices if the angle between adjacent edges is greater than 45 degrees or thresholds conditions are met
            bool is_sharp = (v_dot < 0.707) || (len_prev > len_threshold) || (len > len_threshold);
            if (is_sharp)
            {
                // Allocate new left / right points for the start of this segment as these points will receive their own normals to indicate a sharp turn.
                idx_a[RIGHT] = idx_last++;
                volume.push_geometry(a[RIGHT], n_right);
                idx_a[LEFT] = idx_last++;
                volume.push_geometry(a[LEFT], n_left);

                if (is_right_turn)
                {
                    // Right turn. Fill in the right turn wedge.
                    volume.push_triangle(idx_prev[RIGHT], idx_a[RIGHT], idx_prev[TOP]);
                    volume.push_triangle(idx_prev[RIGHT], idx_prev[BOTTOM], idx_a[RIGHT]);
                }
                else
                {
                    // Left turn. Fill in the left turn wedge.
                    volume.push_triangle(idx_prev[LEFT], idx_prev[TOP], idx_a[LEFT]);
                    volume.push_triangle(idx_prev[LEFT], idx_a[LEFT], idx_prev[BOTTOM]);
                }
            }
            else
            {
                // The two successive segments are nearly collinear.
                idx_a[LEFT] = idx_prev[LEFT];
                idx_a[RIGHT] = idx_prev[RIGHT];
            }

            if (ii == lines.size())
            {
                if (!is_sharp)
                {
                    // Closing a loop with smooth transition. Unify the closing left / right vertices.
                    ::memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[LEFT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[LEFT] * 6, sizeof(float) * 6);
                    ::memcpy(volume.vertices_and_normals_interleaved.data() + idx_initial[RIGHT] * 6, volume.vertices_and_normals_interleaved.data() + idx_prev[RIGHT] * 6, sizeof(float) * 6);
                    volume.vertices_and_normals_interleaved.erase(volume.vertices_and_normals_interleaved.end() - 12, volume.vertices_and_normals_interleaved.end());
                    // Replace the left / right vertex indices to point to the start of the loop. 
                    for (size_t u = volume.quad_indices.size() - 16; u < volume.quad_indices.size(); ++u)
                    {
                        if (volume.quad_indices[u] == idx_prev[LEFT])
                            volume.quad_indices[u] = idx_initial[LEFT];
                        else if (volume.quad_indices[u] == idx_prev[RIGHT])
                            volume.quad_indices[u] = idx_initial[RIGHT];
                    }
                }

                // This is the last iteration, only required to solve the transition.
                break;
            }
        }

        // Only new allocate top / bottom vertices, if not closing a loop.
        if (closed && (ii + 1 == lines.size()))
            idx_b[TOP] = idx_initial[TOP];
        else
        {
            idx_b[TOP] = idx_last++;
            volume.push_geometry(b[TOP], n_top);
        }

        if (closed && (ii + 1 == lines.size()) && (width == width_initial))
            idx_b[BOTTOM] = idx_initial[BOTTOM];
        else
        {
            idx_b[BOTTOM] = idx_last++;
            volume.push_geometry(b[BOTTOM], n_bottom);
        }

        // Generate new vertices for the end of this line segment.
        idx_b[LEFT] = idx_last++;
        volume.push_geometry(b[LEFT], n_left);
        idx_b[RIGHT] = idx_last++;
        volume.push_geometry(b[RIGHT], n_right);

        ::memcpy(idx_prev, idx_b, 4 * sizeof(int));
        n_right_prev = n_right;
        n_top_prev = n_top;
        unit_v_prev = unit_v;
        len_prev = len;

        if (!closed)
        {
            // Terminate open paths with caps.
            if (i == 0)
                volume.push_quad(idx_a[BOTTOM], idx_a[RIGHT], idx_a[TOP], idx_a[LEFT]);

            // We don't use 'else' because both cases are true if we have only one line.
            if (i + 1 == lines.size())
                volume.push_quad(idx_b[BOTTOM], idx_b[LEFT], idx_b[TOP], idx_b[RIGHT]);
        }

        // Add quads for a straight hollow tube-like segment.
        // bottom-right face
        volume.push_quad(idx_a[BOTTOM], idx_b[BOTTOM], idx_b[RIGHT], idx_a[RIGHT]);
        // top-right face
        volume.push_quad(idx_a[RIGHT], idx_b[RIGHT], idx_b[TOP], idx_a[TOP]);
        // top-left face
        volume.push_quad(idx_a[TOP], idx_b[TOP], idx_b[LEFT], idx_a[LEFT]);
        // bottom-left face
        volume.push_quad(idx_a[LEFT], idx_b[LEFT], idx_b[BOTTOM], idx_a[BOTTOM]);
    }

#undef LEFT
#undef RIGHT
#undef TOP
#undef BOTTOM
}

static void point_to_indexed_vertex_array(const Vec3crd& point,
    double width,
    double height,
    GLIndexedVertexArray& volume)
{
    // builds a double piramid, with vertices on the local axes, around the point

    Vec3d center = unscale(point);

    double scale_factor = 1.0;
    double w = scale_factor * width;
    double h = scale_factor * height;

    // new vertices ids
    int idx_last = int(volume.vertices_and_normals_interleaved.size() / 6);
    int idxs[6];
    for (int i = 0; i < 6; ++i)
    {
        idxs[i] = idx_last + i;
    }

    Vec3d displacement_x(w, 0.0, 0.0);
    Vec3d displacement_y(0.0, w, 0.0);
    Vec3d displacement_z(0.0, 0.0, h);

    Vec3d unit_x(1.0, 0.0, 0.0);
    Vec3d unit_y(0.0, 1.0, 0.0);
    Vec3d unit_z(0.0, 0.0, 1.0);

    // vertices
    volume.push_geometry(center - displacement_x, -unit_x); // idxs[0]
    volume.push_geometry(center + displacement_x, unit_x);  // idxs[1]
    volume.push_geometry(center - displacement_y, -unit_y); // idxs[2]
    volume.push_geometry(center + displacement_y, unit_y);  // idxs[3]
    volume.push_geometry(center - displacement_z, -unit_z); // idxs[4]
    volume.push_geometry(center + displacement_z, unit_z);  // idxs[5]

    // top piramid faces
    volume.push_triangle(idxs[0], idxs[2], idxs[5]);
    volume.push_triangle(idxs[2], idxs[1], idxs[5]);
    volume.push_triangle(idxs[1], idxs[3], idxs[5]);
    volume.push_triangle(idxs[3], idxs[0], idxs[5]);

    // bottom piramid faces
    volume.push_triangle(idxs[2], idxs[0], idxs[4]);
    volume.push_triangle(idxs[1], idxs[2], idxs[4]);
    volume.push_triangle(idxs[3], idxs[1], idxs[4]);
    volume.push_triangle(idxs[0], idxs[3], idxs[4]);
}

void _3DScene::thick_lines_to_verts(
    const Lines                 &lines,
    const std::vector<double>   &widths,
    const std::vector<double>   &heights, 
    bool                         closed,
    double                       top_z,
    GLVolume                    &volume)
{
    thick_lines_to_indexed_vertex_array(lines, widths, heights, closed, top_z, volume.indexed_vertex_array);
}

void _3DScene::thick_lines_to_verts(const Lines3& lines,
    const std::vector<double>& widths,
    const std::vector<double>& heights,
    bool closed,
    GLVolume& volume)
{
    thick_lines_to_indexed_vertex_array(lines, widths, heights, closed, volume.indexed_vertex_array);
}

static void thick_point_to_verts(const Vec3crd& point,
    double width,
    double height,
    GLVolume& volume)
{
    point_to_indexed_vertex_array(point, width, height, volume.indexed_vertex_array);
}

void _3DScene::extrusionentity_to_verts(const Polyline &polyline, float width, float height, float print_z, GLVolume& volume)
{
	if (polyline.size() >= 2) {
		size_t num_segments = polyline.size() - 1;
		thick_lines_to_verts(polyline.lines(), std::vector<double>(num_segments, width), std::vector<double>(num_segments, height), false, print_z, volume);
	}
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, GLVolume &volume)
{
	extrusionentity_to_verts(extrusion_path.polyline, extrusion_path.width, extrusion_path.height, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionPath &extrusion_path, float print_z, const Point &copy, GLVolume &volume)
{
    Polyline            polyline = extrusion_path.polyline;
    polyline.remove_duplicate_points();
    polyline.translate(copy);
    Lines               lines = polyline.lines();
    std::vector<double> widths(lines.size(), extrusion_path.width);
    std::vector<double> heights(lines.size(), extrusion_path.height);
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_loop.
void _3DScene::extrusionentity_to_verts(const ExtrusionLoop &extrusion_loop, float print_z, const Point &copy, GLVolume &volume)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath &extrusion_path : extrusion_loop.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width);
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height);
    }
    thick_lines_to_verts(lines, widths, heights, true, print_z, volume);
}

// Fill in the qverts and tverts with quads and triangles for the extrusion_multi_path.
void _3DScene::extrusionentity_to_verts(const ExtrusionMultiPath &extrusion_multi_path, float print_z, const Point &copy, GLVolume &volume)
{
    Lines               lines;
    std::vector<double> widths;
    std::vector<double> heights;
    for (const ExtrusionPath &extrusion_path : extrusion_multi_path.paths) {
        Polyline            polyline = extrusion_path.polyline;
        polyline.remove_duplicate_points();
        polyline.translate(copy);
        Lines lines_this = polyline.lines();
        append(lines, lines_this);
        widths.insert(widths.end(), lines_this.size(), extrusion_path.width);
        heights.insert(heights.end(), lines_this.size(), extrusion_path.height);
    }
    thick_lines_to_verts(lines, widths, heights, false, print_z, volume);
}

void _3DScene::extrusionentity_to_verts(const ExtrusionEntityCollection &extrusion_entity_collection, float print_z, const Point &copy, GLVolume &volume)
{
    for (const ExtrusionEntity *extrusion_entity : extrusion_entity_collection.entities)
        extrusionentity_to_verts(extrusion_entity, print_z, copy, volume);
}

void _3DScene::extrusionentity_to_verts(const ExtrusionEntity *extrusion_entity, float print_z, const Point &copy, GLVolume &volume)
{
    if (extrusion_entity != nullptr) {
        auto *extrusion_path = dynamic_cast<const ExtrusionPath*>(extrusion_entity);
        if (extrusion_path != nullptr)
            extrusionentity_to_verts(*extrusion_path, print_z, copy, volume);
        else {
            auto *extrusion_loop = dynamic_cast<const ExtrusionLoop*>(extrusion_entity);
            if (extrusion_loop != nullptr)
                extrusionentity_to_verts(*extrusion_loop, print_z, copy, volume);
            else {
                auto *extrusion_multi_path = dynamic_cast<const ExtrusionMultiPath*>(extrusion_entity);
                if (extrusion_multi_path != nullptr)
                    extrusionentity_to_verts(*extrusion_multi_path, print_z, copy, volume);
                else {
                    auto *extrusion_entity_collection = dynamic_cast<const ExtrusionEntityCollection*>(extrusion_entity);
                    if (extrusion_entity_collection != nullptr)
                        extrusionentity_to_verts(*extrusion_entity_collection, print_z, copy, volume);
                    else {
                        throw Slic3r::RuntimeError("Unexpected extrusion_entity type in to_verts()");
                    }
                }
            }
        }
    }
}

void _3DScene::polyline3_to_verts(const Polyline3& polyline, double width, double height, GLVolume& volume)
{
    Lines3 lines = polyline.lines();
    std::vector<double> widths(lines.size(), width);
    std::vector<double> heights(lines.size(), height);
    thick_lines_to_verts(lines, widths, heights, false, volume);
}

void _3DScene::point3_to_verts(const Vec3crd& point, double width, double height, GLVolume& volume)
{
    thick_point_to_verts(point, width, height, volume);
}

} // namespace Slic3r
