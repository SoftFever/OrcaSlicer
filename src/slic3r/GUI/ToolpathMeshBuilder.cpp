#include "ToolpathMeshBuilder.hpp"

#include "LibVGCode/LibVGCodeWrapper.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/GCode/MachineFrameTransform.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <Eigen/Geometry>
#include <libvgcode/include/PathVertex.hpp>
#include <libvgcode/include/Viewer.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

namespace Slic3r::GUI {
namespace {

// Belt printers: compute the full machine->model back-transform from the print
// config, so the "designed" (upright) G-code preview maps each toolpath vertex
// back to Cartesian space. The G-code forward pipeline is (BeltKinematics::
// to_machine_coords):  gcode = MachineFrame( AxisRemap( X ) ), with X the model
// already un-rotated to Cartesian by the back-transform. So the inverse is:
//   model = AxisRemap^-1 . MachineFrame^-1
// (origin-snap is a per-instance translation that only shifts position, not
// orientation, so it is intentionally omitted.)
static Transform3d compute_belt_back_transform(const PrintConfig& cfg)
{
    if (!cfg.belt_printer.value)
        return Transform3d::Identity();

    MachineFrameTransform mft;
    mft.init_from_config(cfg);
    const Transform3d mf_inv = mft.is_active() ? Transform3d(mft.transform().inverse())
                                               : Transform3d::Identity();

    // Forward G-code axis remap as an affine matrix (out_i = sign * in[r_i % 3], plus a
    // build-volume offset for Rev axes). This is the matrix form of the per-point
    // GCodeWriter::apply_axis_remap (row convention: each OUTPUT axis selects an input
    // axis + sign) and MUST stay in sync with it. The build-volume max matches what the
    // writer is fed in GCode.cpp (printable_area max + printable_height). (Follow-up:
    // precompute this matrix once in GCodeWriter and share it with apply_axis_remap to
    // remove the parallel encoding.)
    Transform3d ar = Transform3d::Identity();
    const int rr[3] = { int(cfg.gcode_remap_x.value), int(cfg.gcode_remap_y.value), int(cfg.gcode_remap_z.value) };
    if (rr[0] != 0 || rr[1] != 1 || rr[2] != 2) {
        BoundingBoxf bbox_bed(cfg.printable_area.values);
        const Vec3d vmax(bbox_bed.max.x(), bbox_bed.max.y(), cfg.printable_height.value);
        Matrix3d M = Matrix3d::Zero();
        Vec3d    t = Vec3d::Zero();
        for (int i = 0; i < 3; ++i) {
            const int axis = rr[i] % 3;
            if (rr[i] < 3)      M(i, axis) =  1.0;
            else if (rr[i] < 6) M(i, axis) = -1.0;
            else              { M(i, axis) = -1.0; t[i] = vmax[axis]; }
        }
        ar.linear()      = M;
        ar.translation() = t;
    }
    const Transform3d ar_inv = ar.inverse();

    return ar_inv * mf_inv;
}

constexpr unsigned char Flag_First = 0x01;
constexpr unsigned char Flag_Last = 0x02;
constexpr float Cap_Rounding_Factor = 0.25f;
constexpr float Direction_Epsilon = 1e-6f;

struct SegmentLocalAxes { Vec3f forward; Vec3f right; Vec3f up; };
struct Vertex { Vec3f position; Vec3f normal; };
struct CrossSection { Vertex right; Vertex top; Vertex left; Vertex bottom; };
enum class CornerType : unsigned char { RightTurn, LeftTurn, Straight };

SegmentLocalAxes segment_local_axes(const Vec3f& v1, const Vec3f& v2)
{
    SegmentLocalAxes axes;
    const Vec3f delta = v2 - v1;
    if (delta.squaredNorm() <= Direction_Epsilon * Direction_Epsilon)
        throw std::runtime_error("Degenerate libvgcode extrusion segment");
    axes.forward = delta.normalized();
    axes.right = axes.forward.cross(Vec3f::UnitZ());
    if (axes.right.squaredNorm() <= Direction_Epsilon * Direction_Epsilon)
        axes.right = axes.forward.cross(Vec3f::UnitY());
    axes.right.normalize();
    axes.up = axes.right.cross(axes.forward);
    return axes;
}

CrossSection cross_section(const Vec3f& v, const Vec3f& right, const Vec3f& up, float width, float height)
{
    const Vec3f w_shift = 0.5f * width * right;
    const Vec3f h_shift = 0.5f * height * up;
    return {{v + w_shift, right}, {v + h_shift, up}, {v - w_shift, -right}, {v - h_shift, -up}};
}

CrossSection normal_cross_section(const Vec3f& v, const SegmentLocalAxes& axes, float width, float height)
{
    return cross_section(v, axes.right, axes.up, width, height);
}

CrossSection corner_cross_section(const Vec3f& v, const SegmentLocalAxes& axes1, const SegmentLocalAxes& axes2,
                                  float width, float height, CornerType& corner_type)
{
    if (std::abs(std::abs(axes1.forward.dot(axes2.forward)) - 1.0f) < Direction_Epsilon)
        corner_type = CornerType::Straight;
    else if (axes1.up.dot(axes1.forward.cross(axes2.forward)) < 0.0f)
        corner_type = CornerType::RightTurn;
    else
        corner_type = CornerType::LeftTurn;
    Vec3f right = axes1.right + axes2.right;
    if (right.squaredNorm() <= Direction_Epsilon * Direction_Epsilon)
        right = axes1.right;
    else
        right.normalize();
    return cross_section(v, right, axes1.up, width, height);
}

class Builder
{
public:
    Builder(const libvgcode::Viewer& viewer, PreviewMeshScope scope, std::FILE* geometry = nullptr, std::FILE* materials = nullptr)
        : m_viewer(&viewer), m_scope(scope), m_geometry(geometry), m_materials(materials) {}
    explicit Builder(const libvgcode::GCodeInputData& data) : m_data(&data) {}

    PreviewTriangleMesh build()
    {
        const size_t count = vertices_count();
        if (count < 2)
            return {};
        libvgcode::Interval range{0, static_cast<uint32_t>(count - 1)};
        if (m_viewer && m_scope == PreviewMeshScope::Visible) {
            range = m_viewer->get_view_visible_range();
            if (range[0] >= count || range[1] <= range[0] || range[1] >= count)
                range = {0, static_cast<uint32_t>(count - 1)};
            if (m_viewer->is_top_layer_only_view_range())
                range[0] = m_viewer->get_view_full_range()[0];
        }

        const size_t last = std::min<size_t>(range[1] - 1, count - 2);
        if (range[0] > last)
            return {};
        for (size_t i = range[0]; i <= last; ++i) {
            const auto& curr = vertex_at(i);
            const auto& next = vertex_at(i + 1);
            if (!curr.is_extrusion() || !next.is_extrusion()) {
                m_need_start = true;
                continue;
            }
            const Vec3f first = libvgcode::convert(curr.position);
            const Vec3f second = libvgcode::convert(next.position);
            if ((second - first).squaredNorm() <= Direction_Epsilon * Direction_Epsilon) {
                m_need_start = true;
                continue;
            }
            const auto& nextnext = i + 2 < count ? vertex_at(i + 2) : next;
            unsigned char flags = 0;
            if (m_need_start || curr.gcode_id == next.gcode_id)
                flags |= Flag_First;
            if (i == last || !nextnext.is_extrusion())
                flags |= Flag_Last;
            emit_segment(flags, i, curr, next, nextnext);
            m_need_start = (flags & Flag_Last) != 0;
        }
        return std::move(m_mesh);
    }

private:
    const libvgcode::Viewer* m_viewer{};
    const libvgcode::GCodeInputData* m_data{};
    PreviewMeshScope m_scope{PreviewMeshScope::Complete};
    PreviewTriangleMesh m_mesh;
    bool m_need_start{true};
    size_t m_vertex_count{0};
    std::FILE* m_geometry{nullptr};
    std::FILE* m_materials{nullptr};
    std::vector<libvgcode::Color> m_colors;
    size_t m_active_material{0};

    size_t vertices_count() const { return m_viewer ? m_viewer->get_vertices_count() : m_data->vertices.size(); }
    const libvgcode::PathVertex& vertex_at(size_t index) const {
        return m_viewer ? m_viewer->get_vertex_at(index) : m_data->vertices[index];
    }
    libvgcode::Color vertex_color(size_t vertex_id, const libvgcode::PathVertex& vertex) const {
        if (m_viewer) {
            const size_t top_layer = m_viewer->is_top_layer_only_view_range() ? m_viewer->get_layers_view_range()[1] : 0;
            if (m_scope == PreviewMeshScope::Visible && m_viewer->get_view_full_range()[1] != m_viewer->get_view_visible_range()[1] &&
                vertex.layer_id < top_layer && (!m_viewer->is_spiral_vase_mode() || vertex_id != m_viewer->get_view_enabled_range()[0]))
                return libvgcode::DUMMY_COLOR;
            return m_viewer->get_vertex_color(vertex);
        }
        return vertex.extruder_id < m_data->tools_colors.size() ? m_data->tools_colors[vertex.extruder_id] : libvgcode::DUMMY_COLOR;
    }

    uint32_t add_vertex(const Vertex& vertex)
    {
        if (!vertex.position.allFinite() || !vertex.normal.allFinite())
            throw std::runtime_error("Non-finite libvgcode preview mesh vertex");
        const uint32_t index = static_cast<uint32_t>(m_vertex_count++);
        if (m_geometry) {
            fprintf(m_geometry, "v %g %g %g\nvn %g %g %g\n", vertex.position.x(), vertex.position.y(), vertex.position.z(),
                    vertex.normal.x(), vertex.normal.y(), vertex.normal.z());
        } else {
            m_mesh.vertices.push_back({vertex.position, vertex.normal, Vec2f::Zero()});
            m_mesh.bounds.merge(vertex.position.cast<double>());
        }
        return index;
    }

    void add_triangle(uint32_t a, uint32_t b, uint32_t c)
    {
        if (m_geometry) {
            const size_t first = size_t(a) + 1;
            const size_t second = size_t(b) + 1;
            const size_t third = size_t(c) + 1;
            fprintf(m_geometry, "f %zu//%zu %zu//%zu %zu//%zu\n", first, first, second, second, third, third);
            return;
        }
        m_mesh.indices.insert(m_mesh.indices.end(), {a, b, c});
        m_mesh.groups.back().index_count += 3;
    }

    void begin_group(size_t vertex_id, const libvgcode::PathVertex& vertex)
    {
        const libvgcode::Color color = vertex_color(vertex_id, vertex);
        if (m_geometry) {
            auto material = std::find(m_colors.begin(), m_colors.end(), color);
            if (material == m_colors.end()) {
                m_colors.push_back(color);
                material = std::prev(m_colors.end());
                constexpr float inv_255 = 1.0f / 255.0f;
                fprintf(m_materials, "\nnewmtl material_%zu\nKa 1 1 1\nKd %g %g %g\nKs 0 0 0\n", m_colors.size(),
                        float(color[0]) * inv_255, float(color[1]) * inv_255, float(color[2]) * inv_255);
            }
            const size_t material_id = size_t(std::distance(m_colors.begin(), material)) + 1;
            if (material_id != m_active_material) {
                fprintf(m_geometry, "\nusemtl material_%zu\n", material_id);
                m_active_material = material_id;
            }
            return;
        }
        const uint16_t material = vertex.extruder_id;
        const uint8_t role = static_cast<uint8_t>(vertex.role);
        const uint8_t extruder = vertex.extruder_id;
        const uint8_t colour = vertex.color_id;
        const uint32_t layer = vertex.layer_id;
        if (!m_mesh.groups.empty()) {
            auto& group = m_mesh.groups.back();
            if (group.material_slot == material && group.extrusion_role == role && group.extruder_id == extruder &&
                group.colour_id == colour && group.layer_id == layer && group.color == color)
                return;
        }
        PreviewTriangleGroup group;
        group.first_index = static_cast<uint32_t>(m_mesh.indices.size());
        group.material_slot = material;
        group.extrusion_role = role;
        group.extruder_id = extruder;
        group.colour_id = colour;
        group.layer_id = layer;
        group.color = color;
        m_mesh.groups.push_back(group);
    }

    void emit_segment(unsigned char flags, size_t v1_id, const libvgcode::PathVertex& v1,
                      const libvgcode::PathVertex& v2, const libvgcode::PathVertex& v3)
    {
        const Vec3f p1 = libvgcode::convert(v1.position);
        const Vec3f p2 = libvgcode::convert(v2.position);
        const Vec3f p3 = libvgcode::convert(v3.position);
        const SegmentLocalAxes axes12 = segment_local_axes(p1, p2);
        const SegmentLocalAxes axes23 = (p3 - p2).squaredNorm() > Direction_Epsilon * Direction_Epsilon ?
            segment_local_axes(p2, p3) : axes12;

        if ((flags & Flag_First) != 0) {
            begin_group(v1_id, v1);
            const uint32_t base = static_cast<uint32_t>(m_vertex_count);
            const Vertex cap = {p1 - Cap_Rounding_Factor * v1.width * axes12.forward, -axes12.forward};
            const CrossSection section = normal_cross_section(p1, axes12, v1.width, v1.height);
            add_vertex(cap); add_vertex(section.right); add_vertex(section.top); add_vertex(section.left); add_vertex(section.bottom);
            add_triangle(base, base + 1, base + 2); add_triangle(base, base + 2, base + 3);
            add_triangle(base, base + 3, base + 4); add_triangle(base, base + 4, base + 1);
        }

        begin_group(v1_id + 1, v2);
        if ((flags & Flag_Last) != 0) {
            const uint32_t previous = static_cast<uint32_t>(m_vertex_count) - 4;
            const uint32_t base = static_cast<uint32_t>(m_vertex_count);
            const Vertex cap = {p2 + Cap_Rounding_Factor * v2.width * axes12.forward, axes12.forward};
            const CrossSection section = normal_cross_section(p2, axes12, v2.width, v2.height);
            add_vertex(cap); add_vertex(section.right); add_vertex(section.top); add_vertex(section.left); add_vertex(section.bottom);
            add_triangle(previous, base + 1, base + 2); add_triangle(previous, base + 2, previous + 1);
            add_triangle(previous + 1, base + 2, base + 3); add_triangle(previous + 1, base + 3, previous + 2);
            add_triangle(previous + 2, base + 3, base + 4); add_triangle(previous + 2, base + 4, previous + 3);
            add_triangle(previous + 3, base + 4, base + 1); add_triangle(previous + 3, base + 1, previous);
            add_triangle(base, base + 3, base + 2); add_triangle(base, base + 2, base + 1);
            add_triangle(base, base + 1, base + 4); add_triangle(base, base + 4, base + 3);
            return;
        }

        CornerType corner_type;
        const CrossSection corner = corner_cross_section(p2, axes12, axes23, v2.width, v2.height, corner_type);
        const CrossSection before = normal_cross_section(p2, axes12, v2.width, v2.height);
        const CrossSection after = normal_cross_section(p2, axes23, v2.width, v2.height);
        const uint32_t previous = static_cast<uint32_t>(m_vertex_count) - 4;
        const uint32_t base = static_cast<uint32_t>(m_vertex_count);
        if (corner_type == CornerType::Straight) {
            add_vertex(before.right); add_vertex(before.top); add_vertex(before.left); add_vertex(before.bottom);
            add_triangle(previous, base, base + 1); add_triangle(previous, base + 1, previous + 1);
            add_triangle(previous + 1, base + 1, base + 2); add_triangle(previous + 1, base + 2, previous + 2);
            add_triangle(previous + 2, base + 2, base + 3); add_triangle(previous + 2, base + 3, previous + 3);
            add_triangle(previous + 3, base + 3, base); add_triangle(previous + 3, base, previous);
        } else if (corner_type == CornerType::RightTurn) {
            add_vertex(before.left); add_vertex(corner.left); add_vertex(corner.right); add_vertex(before.top);
            add_vertex(after.left); add_vertex(before.bottom);
            add_triangle(previous, base + 2, base + 3); add_triangle(previous, base + 3, previous + 1);
            add_triangle(previous + 1, base + 3, base); add_triangle(previous + 1, base, previous + 2);
            add_triangle(previous + 2, base, base + 5); add_triangle(previous + 2, base + 5, previous + 3);
            add_triangle(previous + 3, base + 5, base + 2); add_triangle(previous + 3, base + 2, previous);
            add_triangle(base + 1, base, base + 3); add_triangle(base + 1, base + 3, base + 4);
            add_triangle(base + 1, base + 4, base + 5); add_triangle(base + 1, base + 5, base);
        } else {
            add_vertex(before.right); add_vertex(corner.right); add_vertex(after.right); add_vertex(before.top);
            add_vertex(corner.left); add_vertex(before.bottom);
            add_triangle(previous, base, base + 3); add_triangle(previous, base + 3, previous + 1);
            add_triangle(previous + 1, base + 3, base + 4); add_triangle(previous + 1, base + 4, previous + 2);
            add_triangle(previous + 2, base + 4, base + 5); add_triangle(previous + 2, base + 5, previous + 3);
            add_triangle(previous + 3, base + 5, base); add_triangle(previous + 3, base, previous);
            add_triangle(base + 1, base + 2, base + 3); add_triangle(base + 1, base + 3, base);
            add_triangle(base + 1, base, base + 5); add_triangle(base + 1, base + 5, base + 2);
        }
    }
};

} // namespace

Transform3d toolpath_model_transform(const GCodeProcessorResult& gcode_result, const Print& print)
{
    if (gcode_result.belt_tilt_angle <= 0.f || !print.config().belt_printer.value)
        return Transform3d::Identity();

    Transform3d belt_inv = compute_belt_back_transform(print.config());
    // G92 belt-origin resets are baked into stored moves; undo them before the inverse.
    if (gcode_result.belt_z_origin != 0.0f)
        belt_inv = belt_inv * Transform3d(Eigen::Translation3d(Vec3d(0.0, 0.0, -double(gcode_result.belt_z_origin))));
    const bool apply_belt = !belt_inv.matrix().isApprox(Transform3d::Identity().matrix());
    if (apply_belt) {
        // The linear belt back-transform recovers the print's shape and orientation but not
        // the per-object placement/lift translation: the object's position on the belt, the
        // BeltSliceStrategy min-Z lift, and the centering pre-translate are applied OUTSIDE
        // build_forward_transform() (see PrintObjectSlice.cpp), so its linear inverse leaves
        // them un-undone. Uncorrected, the upright toolpaths float a fixed offset from the
        // model shell. Recover the translation generally — independent of the offset's exact
        // source or the axis remap — by anchoring the back-transformed object body onto the
        // upright model bounding box (the same space the shells render in).
        BoundingBoxf3 model_bb;
        for (const PrintObject* po : print.objects())
            for (const ModelInstance* mi : po->model_object()->instances)
                model_bb.merge(po->model_object()->instance_bounding_box(*mi));
        // Build the anchor bbox from surface toolpaths only. After the belt_z_origin
        // correction the surface back-transforms onto the model, but a few elevated
        // features (bridges/overhangs over the chevron gap) are mis-mapped by the linear
        // inverse to well outside the model body; if one becomes the bbox minimum it
        // drags the min-corner anchor by ~20mm. Drop moves that land clearly outside the
        // (correct) model bbox — a geometric filter, not a role guess.
        // Tolerance around the model bbox for outlier rejection, and the minimum fraction
        // of MOVES (by count) the clip must retain before we trust it over the full bbox.
        // Count, not bbox extent: a single far-flung outlier move (e.g. a belt-entry purge
        // that maps to y~0) inflates the full bbox so much that a size-fraction test would
        // wrongly reject the clip and re-include the outlier, dragging the min-corner anchor
        // by ~the offset. By count the clip keeps ~100% of moves here and ~0% only when the
        // object is genuinely offset from the belt entry (the real fall-back-to-full case).
        constexpr double ANCHOR_CLIP_MARGIN_MM   = 10.0;
        constexpr double ANCHOR_CLIP_MIN_KEEP    = 0.5;
        BoundingBoxf3 tp_bb_clip, tp_bb_full;
        size_t n_filtered = 0, n_clip = 0;
        const double y_lo = model_bb.defined ? model_bb.min.y() - ANCHOR_CLIP_MARGIN_MM : -1e30;
        const double y_hi = model_bb.defined ? model_bb.max.y() + ANCHOR_CLIP_MARGIN_MM :  1e30;
        // The anchor aligns the toolpath body onto the object-only model bbox, so it must be
        // built from OBJECT-body toolpaths only. Support, skirt, brim and wipe-tower extrusions
        // are not part of the model mesh and extend past it (support especially reaches well
        // beyond the object on a belt), so including them drags tp_bb.min and shifts the whole
        // back-transformed g-code off the mesh — but only when those features are present. That
        // is the "g-code shifts vs mesh as soon as supports are enabled" bug. Filter them out.
        auto is_object_body = [](ExtrusionRole r) {
            return r != erSupportMaterial && r != erSupportMaterialInterface
                && r != erSupportTransition
                && r != erSkirt && r != erBrim && r != erWipeTower;
        };
        auto accumulate = [&](bool body_only) {
            tp_bb_clip = BoundingBoxf3();
            tp_bb_full = BoundingBoxf3();
            n_filtered = 0;
            n_clip     = 0;
            for (const GCodeProcessorResult::MoveVertex& mv : gcode_result.moves)
                if (mv.type == EMoveType::Extrude && mv.layer_id >= 1 // skip layer-0 prime/skirt
                    && (!body_only || is_object_body(mv.extrusion_role))) {
                    ++n_filtered;
                    const Vec3d p = belt_inv * mv.position.cast<double>();
                    tp_bb_full.merge(p);
                    if (p.y() >= y_lo && p.y() <= y_hi) {
                        tp_bb_clip.merge(p);
                        ++n_clip;
                    }
                }
        };
        accumulate(/*body_only=*/true);
        // Fallback: a plate with no object-body extrusions above layer 0 (e.g. a
        // support-only object) would leave tp_bb undefined and silently skip the
        // anchor. Re-accumulate over ALL extrude moves so the min-corner anchor is
        // still computed rather than letting the preview float by the placement offset.
        if (n_filtered == 0)
            accumulate(/*body_only=*/false);
        // Use the clipped bbox when it still holds the bulk of the moves (outliers removed).
        // If the object sits far from the belt entry the toolpaths are grossly offset and the
        // clip drops most of them — fall back to the full bbox so the min-corner anchor still
        // recovers that gross translation rather than breaking.
        const BoundingBoxf3& tp_bb =
            (tp_bb_clip.defined && n_filtered > 0 &&
             double(n_clip) >= ANCHOR_CLIP_MIN_KEEP * double(n_filtered)) ? tp_bb_clip : tp_bb_full;
        if (model_bb.defined && tp_bb.defined) {
            // Anchor the back-transformed toolpath body onto the upright model bbox by
            // its MIN corner. (Center anchoring was tried and regressed when the toolpath
            // and model bounding boxes differ in extent.) With the belt_z_origin
            // correction above and the outlier-robust bbox below, the surface overlaps the
            // shell to well under a millimetre when the object is at the belt entry; an
            // object placed elsewhere in global-rotation mode still carries the placement
            // translation, which this min-corner step recovers.
            const Vec3d d = model_bb.min - tp_bb.min;
            belt_inv = Transform3d(Eigen::Translation3d(d)) * belt_inv;
        }
    }
    return belt_inv;
}

PreviewTriangleMesh build_preview_triangle_mesh(const libvgcode::Viewer& viewer, PreviewMeshScope scope)
{
    return Builder(viewer, scope).build();
}

PreviewTriangleMesh build_preview_triangle_mesh(const libvgcode::GCodeInputData& data)
{
    return Builder(data).build();
}

void export_preview_triangle_mesh_to_obj(const libvgcode::Viewer& viewer, std::FILE& geometry, std::FILE& materials)
{
    Builder(viewer, PreviewMeshScope::Visible, &geometry, &materials).build();
}

} // namespace Slic3r::GUI
