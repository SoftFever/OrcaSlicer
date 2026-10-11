#include <catch2/catch_all.hpp>
#include "test_utils.hpp"
#include <libslic3r/Utils.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/Format/OBJ.hpp>
#include <boost/nowide/cstdio.hpp>

#include <fstream>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <slic3r/GUI/ToolpathMeshBuilder.hpp>
#include <libvgcode/include/GCodeInputData.hpp>
#include <libvgcode/include/Types.hpp>

// libvgcode::Viewer loading owns OpenGL resources. This focused test only needs its normalized
// CPU vertices and range state, so expose those internals without requiring a test GL context.
#define private public
#include <libvgcode/include/Viewer.hpp>
#include "../../src/libvgcode/src/ViewerImpl.hpp"
#undef private

namespace {

libvgcode::PathVertex extrusion(float x, uint32_t gcode_id)
{
    libvgcode::PathVertex vertex;
    vertex.position = {x, 0.0f, 0.2f};
    vertex.width = 0.45f;
    vertex.height = 0.2f;
    vertex.type = libvgcode::EMoveType::Extrude;
    vertex.role = libvgcode::EGCodeExtrusionRole::Perimeter;
    vertex.gcode_id = gcode_id;
    return vertex;
}

libvgcode::PathVertex travel(float x, uint32_t gcode_id)
{
    libvgcode::PathVertex vertex = extrusion(x, gcode_id);
    vertex.type = libvgcode::EMoveType::Travel;
    return vertex;
}

} // namespace

TEST_CASE("Viewer mesh scope separates complete and visible toolpaths", "[ToolpathMeshBuilder]")
{
    libvgcode::Viewer viewer;
    viewer.m_impl->m_vertices = {
        extrusion(0.0f, 1), extrusion(10.0f, 2), travel(20.0f, 3),
        travel(90.0f, 4), extrusion(100.0f, 5), extrusion(110.0f, 6), travel(120.0f, 7)
    };
    viewer.m_impl->m_view_range.set_full(0, 6);
    viewer.m_impl->m_view_range.set_enabled(0, 6);
    viewer.m_impl->m_view_range.set_visible(4, 6);

    const auto complete = Slic3r::GUI::build_preview_triangle_mesh(
        viewer, Slic3r::GUI::PreviewMeshScope::Complete);
    const auto visible = Slic3r::GUI::build_preview_triangle_mesh(
        viewer, Slic3r::GUI::PreviewMeshScope::Visible);

    CHECK(complete.vertices.size() == 20);
    CHECK(complete.indices.size() == 96);
    CHECK(complete.bounds.min.x() < 0.0);
    CHECK(complete.bounds.max.x() > 110.0);

    CHECK(visible.vertices.size() == 10);
    CHECK(visible.indices.size() == 48);
    CHECK(visible.bounds.min.x() > 90.0);
    CHECK(visible.bounds.max.x() > 110.0);
    CHECK(visible.bounds.max.x() < 120.0);
}

TEST_CASE("Visible mesh stops at the selected range and preserves empty-range fallback", "[ToolpathMeshBuilder]")
{
    libvgcode::Viewer viewer;
    viewer.m_impl->m_vertices = {
        extrusion(0.0f, 1), extrusion(10.0f, 2), extrusion(20.0f, 3),
        extrusion(30.0f, 4), extrusion(40.0f, 5), extrusion(50.0f, 6)
    };
    viewer.m_impl->m_view_range.set_full(0, 5);
    viewer.m_impl->m_view_range.set_enabled(0, 5);
    viewer.m_impl->m_view_range.set_visible(1, 3);

    const auto selected = Slic3r::GUI::build_preview_triangle_mesh(
        viewer, Slic3r::GUI::PreviewMeshScope::Visible);
    CHECK(selected.bounds.min.x() < 10.0);
    CHECK(selected.bounds.max.x() > 30.0);
    CHECK(selected.bounds.max.x() < 35.0);

    viewer.m_impl->m_view_range.set_visible(0, 0);
    const auto fallback = Slic3r::GUI::build_preview_triangle_mesh(
        viewer, Slic3r::GUI::PreviewMeshScope::Visible);
    CHECK_FALSE(fallback.vertices.empty());
    CHECK(fallback.bounds.max.x() > 50.0);
}

TEST_CASE("Complete mesh includes and caps a terminal extrusion segment", "[ToolpathMeshBuilder]")
{
    libvgcode::GCodeInputData data;
    data.tools_colors.push_back({255, 0, 0});
    data.vertices = {extrusion(0.0f, 1), extrusion(10.0f, 2), extrusion(20.0f, 3)};

    const auto mesh = Slic3r::GUI::build_preview_triangle_mesh(data);
    CHECK(mesh.vertices.size() == 14);
    CHECK(mesh.indices.size() == 72);
    CHECK(mesh.bounds.max.x() > 20.0);
}

TEST_CASE("A single extrusion produces a capped mesh", "[ToolpathMeshBuilder][Regression]")
{
    libvgcode::GCodeInputData data;
    data.vertices = {extrusion(0.0f, 1), extrusion(10.0f, 1)};

    const auto mesh = Slic3r::GUI::build_preview_triangle_mesh(data);

    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    CHECK(mesh.bounds.min.x() < 0.0);
    CHECK(mesh.bounds.max.x() > 10.0);
    CHECK(mesh.indices.size() % 3 == 0);
    for (uint32_t index : mesh.indices)
        CHECK(index < mesh.vertices.size());
}

TEST_CASE("Complete mesh supports vertical extrusion segments", "[ToolpathMeshBuilder][Regression]")
{
    libvgcode::GCodeInputData data;
    data.tools_colors.push_back({255, 0, 0});
    auto bottom = extrusion(0.0f, 1);
    auto middle = extrusion(0.0f, 2);
    auto top = extrusion(0.0f, 3);
    bottom.position = {0.0f, 0.0f, 0.0f};
    middle.position = {0.0f, 0.0f, 10.0f};
    top.position = {0.0f, 0.0f, 20.0f};
    data.vertices = {bottom, middle, top};

    const auto mesh = Slic3r::GUI::build_preview_triangle_mesh(data);
    REQUIRE_FALSE(mesh.vertices.empty());
    for (const auto& vertex : mesh.vertices) {
        CHECK(vertex.position.allFinite());
        CHECK(vertex.normal.allFinite());
    }
    CHECK(mesh.bounds.min.z() < 0.0);
    CHECK(mesh.bounds.max.z() > 20.0);
}

TEST_CASE("Visible toolpaths retain lower-layer grey coloring during playback", "[ToolpathMeshBuilder][Regression]")
{
    libvgcode::Viewer viewer;
    viewer.m_impl->m_vertices = {
        extrusion(0.0f, 1), extrusion(10.0f, 2), travel(20.0f, 3),
        extrusion(100.0f, 4), extrusion(110.0f, 5), extrusion(120.0f, 6), travel(130.0f, 7)
    };
    for (size_t i = 3; i < viewer.m_impl->m_vertices.size(); ++i)
        viewer.m_impl->m_vertices[i].layer_id = 1;
    viewer.m_impl->m_layers.set_view_range(0, 1);
    viewer.m_impl->m_settings.top_layer_only_view_range = true;
    viewer.m_impl->m_view_range.set_full(0, 6);
    viewer.m_impl->m_view_range.set_enabled(3, 6);
    viewer.m_impl->m_view_range.set_visible(3, 4);

    const auto visible = Slic3r::GUI::build_preview_triangle_mesh(viewer, Slic3r::GUI::PreviewMeshScope::Visible);
    REQUIRE_FALSE(visible.groups.empty());
    CHECK(visible.groups.front().color == libvgcode::DUMMY_COLOR);
    CHECK(visible.groups.back().color != libvgcode::DUMMY_COLOR);

    const auto complete = Slic3r::GUI::build_preview_triangle_mesh(viewer, Slic3r::GUI::PreviewMeshScope::Complete);
    REQUIRE_FALSE(complete.groups.empty());
    CHECK(complete.groups.front().color != libvgcode::DUMMY_COLOR);

    viewer.m_impl->m_settings.spiral_vase_mode = true;
    viewer.m_impl->m_view_range.set_enabled(0, 6);
    const auto spiral = Slic3r::GUI::build_preview_triangle_mesh(viewer, Slic3r::GUI::PreviewMeshScope::Visible);
    REQUIRE_FALSE(spiral.groups.empty());
    CHECK(spiral.groups.front().color != libvgcode::DUMMY_COLOR);
}

TEST_CASE("Streamed OBJ toolpaths retain corner geometry and reuse materials across layers", "[ToolpathMeshBuilder][Regression]")
{
    libvgcode::Viewer viewer;
    viewer.m_impl->m_vertices = {
        extrusion(0.0f, 1), extrusion(10.0f, 2), extrusion(10.0f, 3), extrusion(20.0f, 4), extrusion(20.0f, 5)
    };
    viewer.m_impl->m_vertices[2].position[1] = 10;
    viewer.m_impl->m_vertices[3].position[1] = 10;
    viewer.m_impl->m_vertices[4].position[1] = 20;
    viewer.m_impl->m_vertices[3].layer_id = 1;
    viewer.m_impl->m_vertices[4].layer_id = 1;
    viewer.m_impl->m_view_range.set_full(0, 4);
    viewer.m_impl->m_view_range.set_enabled(0, 4);
    viewer.m_impl->m_view_range.set_visible(0, 4);

    ScopedTemporaryFile geometry_path(".obj");
    ScopedTemporaryFile materials_path(".mtl");
    {
        Slic3r::FilePtr geometry(boost::nowide::fopen(geometry_path.string().c_str(), "w"));
        Slic3r::FilePtr materials(boost::nowide::fopen(materials_path.string().c_str(), "w"));
        REQUIRE(geometry.f != nullptr);
        REQUIRE(materials.f != nullptr);
        Slic3r::GUI::export_preview_triangle_mesh_to_obj(viewer, *geometry.f, *materials.f);
    }
    Slic3r::TriangleMesh exported;
    Slic3r::ObjInfo info;
    std::string error;
    REQUIRE(Slic3r::load_obj(geometry_path.string().c_str(), &exported, info, error));
    const auto expected = Slic3r::GUI::build_preview_triangle_mesh(viewer, Slic3r::GUI::PreviewMeshScope::Visible);
    CHECK(exported.facets_count() == expected.indices.size() / 3);
    const auto bounds = exported.bounding_box();
    for (int axis = 0; axis < 3; ++axis) {
        CHECK_THAT(bounds.min[axis], Catch::Matchers::WithinAbs(expected.bounds.min[axis], 1e-4));
        CHECK_THAT(bounds.max[axis], Catch::Matchers::WithinAbs(expected.bounds.max[axis], 1e-4));
    }

    std::ifstream materials(materials_path.string());
    size_t material_count = 0;
    for (std::string line; std::getline(materials, line);)
        if (line.compare(0, 7, "newmtl ") == 0)
            ++material_count;
    CHECK(material_count == 1);

    std::ifstream geometry(geometry_path.string());
    size_t material_switches = 0;
    for (std::string line; std::getline(geometry, line);)
        if (line.compare(0, 7, "usemtl ") == 0)
            ++material_switches;
    CHECK(material_switches == 1);
}
