#include "MeshRepair.hpp"

#include "I18N.hpp"
#include "TriangleMesh.hpp"

#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_tree.h>
#include <CGAL/Bbox_3.h>
#include <CGAL/boost/graph/border.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/boost/graph/iterator.h>
#include <CGAL/box_intersection_d.h>
#include <CGAL/Box_intersection_d/Box_traits_d.h>
#include <CGAL/Box_intersection_d/Box_with_info_d.h>
#include <CGAL/Constrained_Delaunay_triangulation_2.h>
#include <CGAL/Constrained_triangulation_2.h>
#include <CGAL/Constrained_triangulation_face_base_2.h>
#include <CGAL/Default.h>
#include <CGAL/enum.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/exceptions.h>
#include <CGAL/global_functions_distance_3.h>
#include <CGAL/Kernel/global_functions_3.h>
#include <CGAL/Named_function_parameters.h>
#include <CGAL/number_utils.h>
#include <CGAL/Origin.h>
#include <CGAL/Polygon_mesh_processing/bbox.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/Polygon_mesh_processing/internal/Snapping/snap.h>
#include <CGAL/Polygon_mesh_processing/manifoldness.h>
#include <CGAL/Polygon_mesh_processing/measure.h>
#include <CGAL/Polygon_mesh_processing/merge_border_vertices.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/repair_degeneracies.h>
#include <CGAL/Polygon_mesh_processing/repair_self_intersections.h>
#include <CGAL/Polygon_mesh_processing/self_intersections.h>
#include <CGAL/Polygon_mesh_processing/stitch_borders.h>
#include <CGAL/Polygon_mesh_processing/triangulate_hole.h>
#include <CGAL/property_map.h>
#include <CGAL/Side_of_triangle_mesh.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Surface_mesh/Surface_mesh.h>
#include <CGAL/tags.h>
#include <CGAL/Triangulation_data_structure_2.h>
#include <CGAL/Triangulation_face_base_with_info_2.h>
#include <CGAL/Triangulation_vertex_base_with_info_2.h>

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_sort.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Slic3r {

namespace {

namespace PMP = CGAL::Polygon_mesh_processing;

using Kernel   = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point    = Kernel::Point_3;
using Vector   = Kernel::Vector_3;
using Mesh     = CGAL::Surface_mesh<Point>;
using Vertex   = Mesh::Vertex_index;
using Halfedge = Mesh::Halfedge_index;
using Face     = Mesh::Face_index;
using Triangle = std::array<std::size_t, 3>;
using Tree     = CGAL::AABB_tree<CGAL::AABB_traits_3<Kernel, CGAL::AABB_face_graph_triangle_primitive<Mesh>>>;

constexpr std::size_t NONE = std::size_t(-1);

class UnionFind
{
public:
    explicit UnionFind(std::size_t n) : m_parent(n) { std::iota(m_parent.begin(), m_parent.end(), std::size_t(0)); }

    std::size_t find(std::size_t i)
    {
        while (m_parent[i] != i)
            i = m_parent[i] = m_parent[m_parent[i]];
        return i;
    }

    // The smaller index becomes the root, so results do not depend on the order of unions.
    void unite(std::size_t a, std::size_t b)
    {
        a = find(a);
        b = find(b);
        if (a != b)
            m_parent[std::max(a, b)] = std::min(a, b);
    }

private:
    std::vector<std::size_t> m_parent;
};

template<class Container> void erase_marked(Container &items, const std::vector<bool> &marked)
{
    std::size_t out = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!marked[i])
            items[out++] = std::move(items[i]);
    items.resize(out);
}

double bbox_diagonal(const std::vector<Point> &points)
{
    CGAL::Bbox_3 bb;
    for (const Point &p : points)
        bb += p.bbox();
    return points.empty() ? 0. : std::sqrt(CGAL::square(bb.x_span()) + CGAL::square(bb.y_span()) + CGAL::square(bb.z_span()));
}

// orient_polygon_soup() needs resizable polygons.
void orient_with_cgal(std::vector<Point> &points, std::vector<Triangle> &triangles)
{
    std::vector<std::vector<std::size_t>> polygons(triangles.size());
    for (std::size_t i = 0; i < triangles.size(); ++i)
        polygons[i].assign(triangles[i].begin(), triangles[i].end());
    PMP::orient_polygon_soup(points, polygons);
    for (std::size_t i = 0; i < triangles.size(); ++i)
        std::copy(polygons[i].begin(), polygons[i].end(), triangles[i].begin());
}

// The triangles of a group renumbered over the vertices they use.
void extract_group(const std::vector<Point> &points, const std::vector<Triangle> &triangles, const std::vector<std::size_t> &group,
                   std::vector<Point> &group_points, std::vector<Triangle> &group_triangles)
{
    std::vector<std::size_t> vertices;
    vertices.reserve(3 * group.size());
    for (std::size_t f : group)
        vertices.insert(vertices.end(), triangles[f].begin(), triangles[f].end());
    std::sort(vertices.begin(), vertices.end());
    vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
    group_points.clear();
    for (std::size_t v : vertices)
        group_points.push_back(points[v]);
    group_triangles.clear();
    for (std::size_t f : group) {
        Triangle &t = group_triangles.emplace_back();
        for (std::size_t k = 0; k < 3; ++k)
            t[k] = std::lower_bound(vertices.begin(), vertices.end(), triangles[f][k]) - vertices.begin();
    }
}

// Groups not known to be valid polygon meshes are made into ones first.
std::vector<Mesh> soup_to_meshes(const std::vector<Point> &points, const std::vector<Triangle> &triangles,
                                 const std::vector<std::vector<std::size_t>> &groups, bool known_valid)
{
    std::vector<Mesh> meshes(groups.size());
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, groups.size(), 1), [&](const tbb::blocked_range<std::size_t> &range) {
        for (std::size_t i = range.begin(); i < range.end(); ++i) {
            std::vector<Point>    group_points;
            std::vector<Triangle> group_triangles;
            extract_group(points, triangles, groups[i], group_points, group_triangles);
            if (!known_valid && !PMP::is_polygon_soup_a_polygon_mesh(group_triangles))
                orient_with_cgal(group_points, group_triangles);
            PMP::polygon_soup_to_polygon_mesh(group_points, group_triangles, meshes[i]);
        }
    });
    return meshes;
}

std::vector<Mesh> split_shells(Mesh &mesh)
{
    mesh.collect_garbage();
    std::vector<std::size_t> face_part(mesh.number_of_faces());
    const std::size_t num_parts = PMP::connected_components(mesh, CGAL::make_property_map(face_part));
    std::vector<std::size_t> part_size(num_parts, 0);
    for (std::size_t part : face_part)
        ++part_size[part];
    const std::size_t largest = std::max_element(part_size.begin(), part_size.end()) - part_size.begin();

    // The small shells are copied out and the largest one stays in place.
    std::vector<Mesh> shells;
    if (num_parts > 1) {
        std::vector<Point> points;
        for (Vertex v : mesh.vertices())
            points.push_back(mesh.point(v));
        std::vector<Triangle> triangles;
        std::vector<std::vector<std::size_t>> groups(num_parts);
        for (Face f : mesh.faces())
            if (face_part[f] != largest) {
                Triangle &t = triangles.emplace_back();
                std::size_t k = 0;
                for (Vertex v : CGAL::vertices_around_face(mesh.halfedge(f), mesh))
                    t[k++] = v;
                groups[face_part[f]].push_back(triangles.size() - 1);
            }
        groups.erase(groups.begin() + largest);
        std::vector<Halfedge> nonmanifold;
        PMP::non_manifold_vertices(mesh, std::back_inserter(nonmanifold));
        shells = soup_to_meshes(points, triangles, groups, nonmanifold.empty());
        PMP::keep_connected_components(mesh, std::vector<std::size_t>{largest}, CGAL::make_property_map(face_part));
        mesh.collect_garbage();
    }
    shells.insert(shells.begin(), std::move(mesh));
    return shells;
}

struct SoupTopology
{
    std::vector<bool> open_face;   // on an edge used by one triangle
    std::vector<bool> bad_face;    // on an edge used by more than two triangles, or twice in one direction
    std::vector<bool> pinched;     // vertex whose triangles form several fans
};

// The fans around each vertex are found by joining the corners of triangles across their shared edges.
SoupTopology analyze_topology(std::size_t num_points, const std::vector<Triangle> &polygons)
{
    struct EdgeUse
    {
        std::size_t a, b, corner;
        bool operator<(const EdgeUse &rhs) const { return std::tie(a, b, corner) < std::tie(rhs.a, rhs.b, rhs.corner); }
    };
    std::vector<EdgeUse> uses;
    uses.reserve(3 * polygons.size());
    for (std::size_t f = 0; f < polygons.size(); ++f)
        for (std::size_t k = 0; k < 3; ++k) {
            const auto [a, b] = std::minmax(polygons[f][k], polygons[f][(k + 1) % 3]);
            uses.push_back({a, b, 3 * f + k});
        }
    tbb::parallel_sort(uses.begin(), uses.end());

    SoupTopology topology{std::vector<bool>(polygons.size(), false), std::vector<bool>(polygons.size(), false), std::vector<bool>(num_points, false)};
    UnionFind fans(3 * polygons.size());
    for (std::size_t i = 0; i < uses.size();) {
        std::size_t j = i + 1;
        while (j < uses.size() && uses[j].a == uses[i].a && uses[j].b == uses[i].b)
            ++j;
        const std::size_t f = uses[i].corner / 3, fk = uses[i].corner % 3;
        const std::size_t g = uses[i + 1 < j ? i + 1 : i].corner / 3, gk = uses[i + 1 < j ? i + 1 : i].corner % 3;
        if (j - i == 1 && uses[i].a != uses[i].b)
            topology.open_face[f] = true;
        else if (j - i != 2 || uses[i].a == uses[i].b || polygons[f][fk] == polygons[g][gk]) {
            for (std::size_t k = i; k < j; ++k)
                topology.bad_face[uses[k].corner / 3] = true;
        } else {
            fans.unite(3 * f + fk, 3 * g + (gk + 1) % 3);
            fans.unite(3 * f + (fk + 1) % 3, 3 * g + gk);
        }
        i = j;
    }
    std::vector<std::size_t> fan_of(num_points, NONE);
    for (std::size_t c = 0; c < 3 * polygons.size(); ++c) {
        const std::size_t v = polygons[c / 3][c % 3], root = fans.find(c);
        if (fan_of[v] == NONE)
            fan_of[v] = root;
        else if (fan_of[v] != root)
            topology.pinched[v] = true;
    }
    return topology;
}

// Valid closed parts skip the soup repair, which would weld them to coincident parts into non-manifold junctions.
std::vector<Mesh> take_closed_shells(const std::vector<Point> &points, std::vector<Triangle> &polygons)
{
    const SoupTopology topology = analyze_topology(points.size(), polygons);
    UnionFind parts(points.size());
    for (const Triangle &p : polygons) {
        parts.unite(p[0], p[1]);
        parts.unite(p[0], p[2]);
    }
    std::vector<bool> part_ok(points.size(), true);
    for (std::size_t f = 0; f < polygons.size(); ++f)
        for (std::size_t v : polygons[f])
            if (topology.open_face[f] || topology.bad_face[f] || topology.pinched[v])
                part_ok[parts.find(v)] = false;

    std::vector<std::size_t> group_of(points.size(), NONE);
    std::vector<std::vector<std::size_t>> groups;
    std::vector<bool> taken(polygons.size(), false);
    for (std::size_t f = 0; f < polygons.size(); ++f)
        if (const std::size_t r = parts.find(polygons[f][0]); part_ok[r]) {
            if (group_of[r] == NONE) {
                group_of[r] = groups.size();
                groups.emplace_back();
            }
            groups[group_of[r]].push_back(f);
            taken[f] = true;
        }
    std::vector<Mesh> shells = soup_to_meshes(points, polygons, groups, true);
    erase_marked(polygons, taken);
    return shells;
}

// Opposite coincident triangles cancel in pairs, unless that empties a whole region (a two-sided export).
void remove_duplicate_triangles(std::size_t num_points, std::vector<Triangle> &polygons)
{
    struct Entry
    {
        std::array<std::size_t, 3> key;
        bool                       odd;
        std::size_t                idx;
    };
    std::vector<Entry> entries;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        const Triangle &p = polygons[i];
        if (p[0] == p[1] || p[1] == p[2] || p[0] == p[2])
            continue;
        const std::size_t r = std::min_element(p.begin(), p.end()) - p.begin();
        const std::size_t b = p[(r + 1) % 3], c = p[(r + 2) % 3];
        entries.push_back({{p[r], std::min(b, c), std::max(b, c)}, b > c, i});
    }
    tbb::parallel_sort(entries.begin(), entries.end(), [](const Entry &l, const Entry &r) { return std::tie(l.key, l.idx) < std::tie(r.key, r.idx); });

    std::vector<bool> remove(polygons.size(), false);
    std::vector<std::size_t> cancelled;
    for (std::size_t i = 0; i < entries.size();) {
        std::size_t j = i + 1;
        while (j < entries.size() && entries[j].key == entries[i].key)
            ++j;
        if (j - i > 1) {
            std::size_t num_odd = 0;
            for (std::size_t k = i; k < j; ++k)
                num_odd += entries[k].odd;
            const bool odd_wins = 2 * num_odd > j - i;
            bool kept = 2 * num_odd == j - i;
            if (kept)
                cancelled.push_back(entries[i].idx);
            for (std::size_t k = i; k < j; ++k) {
                if (!kept && entries[k].odd == odd_wins)
                    kept = true;
                else
                    remove[entries[k].idx] = true;
            }
        }
        i = j;
    }
    if (!cancelled.empty()) {
        UnionFind regions(num_points);
        for (const Triangle &p : polygons) {
            regions.unite(p[0], p[1]);
            regions.unite(p[0], p[2]);
        }
        std::vector<bool> survives(num_points, false);
        for (std::size_t i = 0; i < polygons.size(); ++i)
            if (!remove[i])
                survives[regions.find(polygons[i][0])] = true;
        for (std::size_t i : cancelled)
            if (!survives[regions.find(polygons[i][0])])
                remove[i] = false;
    }
    erase_marked(polygons, remove);
}

void clean_soup(std::vector<Point> &points, std::vector<Triangle> &polygons)
{
    std::vector<std::size_t> order(points.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    tbb::parallel_sort(order.begin(), order.end(), [&points](std::size_t a, std::size_t b) {
        return std::make_tuple(points[a].x(), points[a].y(), points[a].z(), a) < std::make_tuple(points[b].x(), points[b].y(), points[b].z(), b);
    });
    std::vector<std::size_t> merged(points.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        merged[order[i]] = i > 0 && points[order[i]] == points[order[i - 1]] ? merged[order[i - 1]] : order[i];
    std::vector<bool> degenerate(polygons.size());
    for (std::size_t f = 0; f < polygons.size(); ++f) {
        Triangle &p = polygons[f];
        for (std::size_t &v : p)
            v = merged[v];
        degenerate[f] = p[0] == p[1] || p[1] == p[2] || p[0] == p[2];
    }
    erase_marked(polygons, degenerate);
    remove_duplicate_triangles(points.size(), polygons);

    std::vector<std::size_t> compact(points.size(), NONE);
    std::vector<Point> used;
    for (Triangle &p : polygons)
        for (std::size_t &v : p) {
            if (compact[v] == NONE) {
                compact[v] = used.size();
                used.push_back(points[v]);
            }
            v = compact[v];
        }
    points = std::move(used);
}

// Closes cracks between open edges; no vertex moves by more than a quarter of its shortest edge.
std::size_t weld_border_vertices(const std::vector<Point> &points, std::vector<Triangle> &polygons, double tolerance, double &open_fraction)
{
    std::vector<double> radius(points.size(), tolerance);
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const Triangle &p : polygons)
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t a = p[k], b = p[(k + 1) % 3];
            const double r = 0.25 * std::sqrt(CGAL::squared_distance(points[a], points[b]));
            radius[a] = std::min(radius[a], r);
            radius[b] = std::min(radius[b], r);
            edges.emplace_back(std::minmax(a, b));
        }
    tbb::parallel_sort(edges.begin(), edges.end());
    std::vector<std::size_t> border;
    std::size_t num_edges = 0;
    for (std::size_t i = 0; i < edges.size();) {
        std::size_t j = i + 1;
        while (j < edges.size() && edges[j] == edges[i])
            ++j;
        if (j - i == 1) {
            border.push_back(edges[i].first);
            border.push_back(edges[i].second);
        }
        ++num_edges;
        i = j;
    }
    open_fraction = num_edges == 0 ? 0. : double(border.size() / 2) / double(num_edges);
    if (border.empty())
        return 0;

    using Cell = std::array<int64_t, 3>;
    auto cell_of = [tolerance](const Point &p) {
        return Cell{int64_t(std::floor(p.x() / tolerance)), int64_t(std::floor(p.y() / tolerance)), int64_t(std::floor(p.z() / tolerance))};
    };
    std::vector<std::pair<Cell, std::size_t>> grid;
    for (std::size_t v : border)
        grid.emplace_back(cell_of(points[v]), v);
    tbb::parallel_sort(grid.begin(), grid.end());
    grid.erase(std::unique(grid.begin(), grid.end()), grid.end());

    UnionFind uf(points.size());
    for (const auto &[c, v] : grid)
        for (int64_t dx = -1; dx <= 1; ++dx)
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dz = -1; dz <= 1; ++dz) {
                    const Cell n{c[0] + dx, c[1] + dy, c[2] + dz};
                    for (auto it = std::lower_bound(grid.begin(), grid.end(), std::make_pair(n, std::size_t(0))); it != grid.end() && it->first == n; ++it)
                        if (const std::size_t u = it->second;
                            u < v && CGAL::squared_distance(points[u], points[v]) <= CGAL::square(std::min(radius[u], radius[v])))
                            uf.unite(u, v);
                }
    std::size_t welded = 0;
    for (const auto &[c, v] : grid)
        welded += uf.find(v) != v;
    if (welded > 0)
        for (Triangle &p : polygons)
            for (std::size_t &v : p)
                v = uf.find(v);
    return welded;
}

double open_edge_fraction(const std::vector<Triangle> &polygons)
{
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const Triangle &p : polygons)
        for (std::size_t k = 0; k < 3; ++k)
            edges.emplace_back(std::minmax(p[k], p[(k + 1) % 3]));
    tbb::parallel_sort(edges.begin(), edges.end());
    std::size_t unique = 0, open = 0;
    for (std::size_t i = 0; i < edges.size();) {
        std::size_t j = i + 1;
        while (j < edges.size() && edges[j] == edges[i])
            ++j;
        ++unique;
        open += j - i == 1;
        i = j;
    }
    return unique == 0 ? 0. : double(open) / double(unique);
}

// orient_polygon_soup() orients each part arbitrarily; give it back the orientation most of its input had.
std::size_t keep_input_orientation(Mesh &mesh, const std::vector<Point> &points_before, const std::vector<Triangle> &polygons_before,
                                   const std::vector<Point> &points, const std::vector<Triangle> &polygons)
{
    if (mesh.number_of_faces() != polygons.size())
        return 0;
    auto fcc = mesh.add_property_map<Face, std::size_t>("f:repair_part").first;
    const std::size_t num_parts = PMP::connected_components(mesh, fcc);
    std::vector<double> vote(num_parts, 0.);
    std::size_t i = 0;
    for (Face f : mesh.faces()) {
        const Triangle &a = polygons_before[i];
        const Triangle &b = polygons[i++];
        bool same = false;
        for (std::size_t r = 0; r < 3 && !same; ++r) {
            same = true;
            for (std::size_t k = 0; k < 3 && same; ++k)
                same = points_before[a[k]] == points[b[(k + r) % 3]];
        }
        vote[fcc[f]] += same ? PMP::face_area(f, mesh) : -PMP::face_area(f, mesh);
    }
    std::vector<Face> to_reverse;
    for (Face f : mesh.faces())
        if (vote[fcc[f]] < 0.)
            to_reverse.push_back(f);
    mesh.remove_property_map(fcc);
    if (!to_reverse.empty())
        PMP::reverse_face_orientations(to_reverse, mesh);
    return std::count_if(vote.begin(), vote.end(), [](double v) { return v < 0.; });
}

void close_cracks(Mesh &mesh, double tolerance)
{
    PMP::merge_reversible_connected_components(mesh);
    PMP::stitch_borders(mesh);
    if (CGAL::is_closed(mesh))
        return;
    // T-junctions, which welding cannot close.
    PMP::experimental::snap_borders(mesh, CGAL::Constant_property_map<Vertex, double>(tolerance));
    PMP::stitch_borders(mesh);
}

struct BorderLoop
{
    Halfedge              halfedge;
    std::vector<Halfedge> halfedges;
    Vector                normal; // unit normal a face closing the loop would have
    double                offset = 0.;
    bool                  planar = false;
    CGAL::Bbox_3          bbox;
};

struct CdtFaceInfo
{
    int nesting = -1;
};
using CdtVb = CGAL::Triangulation_vertex_base_with_info_2<Vertex, Kernel>;
using CdtFb = CGAL::Constrained_triangulation_face_base_2<Kernel, CGAL::Triangulation_face_base_with_info_2<CdtFaceInfo, Kernel>>;
using Cdt   = CGAL::Constrained_Delaunay_triangulation_2<Kernel, CGAL::Triangulation_data_structure_2<CdtVb, CdtFb>, CGAL::No_constraint_intersection_tag>;

// Number of constrained edges crossed to reach each face from the outside.
void mark_nesting(Cdt &cdt)
{
    std::vector<std::pair<Cdt::Face_handle, int>> seeds{{cdt.infinite_face(), 0}};
    while (!seeds.empty()) {
        auto [seed, level] = seeds.back();
        seeds.pop_back();
        if (seed->info().nesting != -1)
            continue;
        seed->info().nesting = level;
        std::vector<Cdt::Face_handle> queue{seed};
        while (!queue.empty()) {
            Cdt::Face_handle f = queue.back();
            queue.pop_back();
            for (int i = 0; i < 3; ++i) {
                Cdt::Face_handle n = f->neighbor(i);
                if (n->info().nesting != -1)
                    continue;
                if (cdt.is_constrained({f, i}))
                    seeds.emplace_back(n, level + 1);
                else {
                    n->info().nesting = level;
                    queue.push_back(n);
                }
            }
        }
    }
}

// Fills a planar region bounded by nested loops, such as a missing annulus, leaving its openings open.
bool fill_planar_region(Mesh &mesh, const std::vector<const BorderLoop *> &loops)
{
    const Vector &n  = loops.front()->normal;
    const Vector  u  = std::abs(n.x()) < 0.9 ? CGAL::cross_product(n, Vector(1, 0, 0)) : CGAL::cross_product(n, Vector(0, 1, 0));
    const Vector  e1 = u / std::sqrt(u.squared_length());
    const Vector  e2 = CGAL::cross_product(n, e1);

    Cdt cdt;
    std::size_t num_vertices = 0;
    std::set<std::pair<Vertex, Vertex>> border_edges;
    try {
        for (const BorderLoop *loop : loops) {
            std::vector<Cdt::Vertex_handle> vh;
            for (Halfedge h : loop->halfedges) {
                const Vector p = mesh.point(mesh.source(h)) - CGAL::ORIGIN;
                vh.push_back(cdt.insert(Kernel::Point_2(p * e1, p * e2)));
                vh.back()->info() = mesh.source(h);
                border_edges.emplace(mesh.source(h), mesh.target(h));
            }
            num_vertices += vh.size();
            for (std::size_t k = 0; k < vh.size(); ++k)
                cdt.insert_constraint(vh[k], vh[(k + 1) % vh.size()]);
        }
    } catch (const std::exception &) {
        return false;
    }
    // Loop vertices projecting onto the same point were merged by the triangulation.
    if (cdt.number_of_vertices() != num_vertices)
        return false;
    mark_nesting(cdt);

    std::vector<std::array<Vertex, 3>> patch;
    std::set<std::pair<Vertex, Vertex>> patch_edges;
    for (Cdt::Face_handle f : cdt.finite_face_handles())
        if (f->info().nesting % 2 == 1) {
            const std::array<Vertex, 3> &t = patch.emplace_back(std::array<Vertex, 3>{f->vertex(0)->info(), f->vertex(1)->info(), f->vertex(2)->info()});
            for (int k = 0; k < 3; ++k)
                patch_edges.emplace(t[k], t[(k + 1) % 3]);
        }
    // The patch has to close every loop with the orientation of the triangles around it.
    for (const auto &e : border_edges)
        if (patch_edges.count(e) == 0)
            return false;

    std::vector<Face> added;
    for (const auto &t : patch) {
        const Face f = mesh.add_face(t[0], t[1], t[2]);
        if (f == Mesh::null_face()) {
            for (Face a : added)
                mesh.remove_face(a);
            return false;
        }
        added.push_back(f);
    }
    return true;
}

// Last resort for a loop the hole filling could not close: a fan around its centroid.
void fill_with_fan(Mesh &mesh, const BorderLoop &loop)
{
    Vector sum(0, 0, 0);
    for (Halfedge h : loop.halfedges)
        sum = sum + (mesh.point(mesh.source(h)) - CGAL::ORIGIN);
    const Vertex c = mesh.add_vertex(CGAL::ORIGIN + sum / double(loop.halfedges.size()));
    for (Halfedge h : loop.halfedges)
        mesh.add_face(mesh.source(h), mesh.target(h), c);
}

std::size_t fill_holes(Mesh &mesh, double tolerance)
{
    std::vector<Halfedge> borders;
    CGAL::extract_boundary_cycles(mesh, std::back_inserter(borders));
    std::vector<BorderLoop> loops(borders.size());
    for (std::size_t i = 0; i < borders.size(); ++i) {
        BorderLoop &loop = loops[i];
        loop.halfedge = borders[i];
        Vector newell(0, 0, 0);
        for (Halfedge h : CGAL::halfedges_around_face(borders[i], mesh)) {
            loop.halfedges.push_back(h);
            newell = newell + CGAL::cross_product(mesh.point(mesh.source(h)) - CGAL::ORIGIN, mesh.point(mesh.target(h)) - CGAL::ORIGIN);
            loop.bbox += mesh.point(mesh.source(h)).bbox();
        }
        if (const double len = std::sqrt(newell.squared_length()); len > 0.) {
            loop.normal = newell / len;
            loop.offset = (mesh.point(mesh.source(loop.halfedge)) - CGAL::ORIGIN) * loop.normal;
            loop.planar = std::all_of(loop.halfedges.begin(), loop.halfedges.end(), [&](Halfedge h) {
                return std::abs((mesh.point(mesh.source(h)) - CGAL::ORIGIN) * loop.normal - loop.offset) <= tolerance;
            });
        }
    }

    // Planar loops facing each other in the same plane, one inside the other, bound one region.
    UnionFind regions(loops.size());
    std::vector<bool> grouped(loops.size(), false);
    auto contains = [tolerance](const CGAL::Bbox_3 &a, const CGAL::Bbox_3 &b) {
        return a.xmin() - tolerance <= b.xmin() && a.ymin() - tolerance <= b.ymin() && a.zmin() - tolerance <= b.zmin() &&
               a.xmax() + tolerance >= b.xmax() && a.ymax() + tolerance >= b.ymax() && a.zmax() + tolerance >= b.zmax();
    };
    for (std::size_t a = 0; a < loops.size(); ++a)
        for (std::size_t b = 0; b < loops.size(); ++b) {
            const BorderLoop &la = loops[a], &lb = loops[b];
            if (a == b || !la.planar || !lb.planar || la.normal * lb.normal > -0.99 || !contains(la.bbox, lb.bbox))
                continue;
            if (std::all_of(lb.halfedges.begin(), lb.halfedges.end(), [&](Halfedge h) {
                    return std::abs((mesh.point(mesh.source(h)) - CGAL::ORIGIN) * la.normal - la.offset) <= tolerance;
                })) {
                regions.unite(a, b);
                grouped[a] = grouped[b] = true;
            }
        }
    std::map<std::size_t, std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < loops.size(); ++i)
        if (grouped[i])
            groups[regions.find(i)].push_back(i);

    std::size_t filled = 0;
    std::vector<bool> done(loops.size(), false);
    for (auto &[root, members] : groups) {
        // The outermost loop sets the orientation of the region.
        auto extent = [&loops](std::size_t i) { return loops[i].bbox.x_span() + loops[i].bbox.y_span() + loops[i].bbox.z_span(); };
        std::sort(members.begin(), members.end(), [&extent](std::size_t l, std::size_t r) { return extent(l) > extent(r); });
        std::vector<const BorderLoop *> group;
        for (std::size_t i : members)
            group.push_back(&loops[i]);
        if (fill_planar_region(mesh, group)) {
            filled += members.size();
            for (std::size_t i : members)
                done[i] = true;
        }
    }

    for (std::size_t i = 0; i < loops.size(); ++i) {
        if (done[i])
            continue;
        std::vector<Face> patch;
        // The exhaustive fallback search is cubic in the length of the loop.
        PMP::triangulate_hole(mesh, loops[i].halfedge,
                              CGAL::parameters::use_2d_constrained_delaunay_triangulation(true)
                                  .do_not_use_cubic_algorithm(loops[i].halfedges.size() > 200)
                                  .face_output_iterator(std::back_inserter(patch)));
        if (patch.empty() && CGAL::is_border(loops[i].halfedge, mesh))
            fill_with_fan(mesh, loops[i]);
        ++filled;
    }
    return filled;
}

double signed_volume(const Mesh &mesh)
{
    double vol = 0.;
    for (Face f : mesh.faces()) {
        const Halfedge h = mesh.halfedge(f);
        vol += CGAL::determinant(mesh.point(mesh.source(h)) - CGAL::ORIGIN, mesh.point(mesh.target(h)) - CGAL::ORIGIN,
                                 mesh.point(mesh.target(mesh.next(h))) - CGAL::ORIGIN);
    }
    return vol / 6.;
}

// Returns the pairs removed. Widespread overlaps are left to slicing: remeshing them can cut a part down to a sliver.
std::size_t remove_local_self_intersections(Mesh &shell)
{
    std::vector<std::pair<Face, Face>> pairs;
    PMP::self_intersections<CGAL::Parallel_if_available_tag>(shell, std::back_inserter(pairs));
    if (pairs.empty())
        return 0;
    std::set<Face> faces;
    for (const auto &[a, b] : pairs) {
        faces.insert(a);
        faces.insert(b);
    }
    if (20 * faces.size() > shell.number_of_faces())
        return 0;
    const double volume = signed_volume(shell);
    Mesh backup = shell;
    try {
        PMP::experimental::remove_self_intersections(faces, shell, CGAL::parameters::preserve_genus(false));
        shell.collect_garbage();
        if (CGAL::is_valid_polygon_mesh(shell) && CGAL::is_closed(shell) && std::abs(signed_volume(shell) - volume) <= 1e-3 * std::abs(volume))
            return pairs.size();
    } catch (const std::exception &) {
    }
    shell = std::move(backup);
    return 0;
}

bool surfaces_intersect(const Mesh &mesh, const Tree &other)
{
    for (Face f : mesh.faces()) {
        const Halfedge h = mesh.halfedge(f);
        if (other.do_intersect(Kernel::Triangle_3(mesh.point(mesh.source(h)), mesh.point(mesh.target(h)), mesh.point(mesh.target(mesh.next(h))))))
            return true;
    }
    return false;
}

bool bbox_contains(const CGAL::Bbox_3 &outer, const CGAL::Bbox_3 &inner)
{
    return outer.xmin() <= inner.xmin() && outer.ymin() <= inner.ymin() && outer.zmin() <= inner.zmin() &&
           outer.xmax() >= inner.xmax() && outer.ymax() >= inner.ymax() && outer.zmax() >= inner.zmax();
}

Point inner_sample(const Mesh &shell)
{
    Face   best;
    double best_area = -1.;
    for (Face f : shell.faces())
        if (const double a = PMP::face_area(f, shell); a > best_area) {
            best_area = a;
            best      = f;
        }
    const Halfedge h = shell.halfedge(best);
    return CGAL::centroid(shell.point(shell.source(h)), shell.point(shell.target(h)), shell.point(shell.target(shell.next(h))));
}

// Orients and filters the shells by the nonzero winding rule of slicing. Crossing shells stay apart, as parts do.
void resolve_shells(std::vector<Mesh> &shells, MeshRepairStats &stats, const std::function<void()> &throw_on_cancel)
{
    const std::size_t n = shells.size();
    std::vector<CGAL::Bbox_3> bbox(n);
    std::vector<int> sign(n);
    for (std::size_t i = 0; i < n; ++i) {
        bbox[i] = PMP::bbox(shells[i]);
        sign[i] = signed_volume(shells[i]) < 0. ? -1 : 1;
    }

    using Box = CGAL::Box_intersection_d::Box_with_info_d<double, 3, std::size_t>;
    std::vector<Box> boxes;
    for (std::size_t i = 0; i < n; ++i)
        boxes.emplace_back(bbox[i], i);
    std::vector<std::pair<std::size_t, std::size_t>> overlaps;
    CGAL::box_self_intersection_d(boxes.begin(), boxes.end(), [&overlaps](const Box &a, const Box &b) {
        overlaps.emplace_back(std::min(a.info(), b.info()), std::max(a.info(), b.info()));
    }, std::ptrdiff_t(10), CGAL::Box_intersection_d::CLOSED);
    std::sort(overlaps.begin(), overlaps.end());

    std::vector<std::vector<std::size_t>> enclosing(n);
    std::vector<bool> needs_tree(n, false);
    for (const auto &[a, b] : overlaps) {
        needs_tree[shells[a].number_of_faces() < shells[b].number_of_faces() ? b : a] = true;
        if (bbox_contains(bbox[a], bbox[b]))
            enclosing[b].push_back(a);
        if (bbox_contains(bbox[b], bbox[a]))
            enclosing[a].push_back(b);
    }
    for (const std::vector<std::size_t> &outer : enclosing)
        for (std::size_t k : outer)
            needs_tree[k] = true;
    // Built before any query: an AABB tree finishes its construction lazily on the first one.
    std::vector<std::unique_ptr<Tree>> trees(n);
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, n, 1), [&](const tbb::blocked_range<std::size_t> &range) {
        for (std::size_t i = range.begin(); i < range.end(); ++i)
            if (needs_tree[i]) {
                trees[i] = std::make_unique<Tree>(shells[i].faces().begin(), shells[i].faces().end(), shells[i]);
                trees[i]->build();
            }
    });

    std::vector<char> hit(overlaps.size(), 0);
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, overlaps.size(), 1), [&](const tbb::blocked_range<std::size_t> &range) {
        for (std::size_t i = range.begin(); i < range.end(); ++i) {
            const auto [a, b]     = overlaps[i];
            const bool a_smaller = shells[a].number_of_faces() < shells[b].number_of_faces();
            hit[i] = surfaces_intersect(shells[a_smaller ? a : b], *trees[a_smaller ? b : a]);
        }
    });
    std::vector<bool> crossing(n, false);
    for (std::size_t i = 0; i < overlaps.size(); ++i)
        if (hit[i])
            crossing[overlaps[i].first] = crossing[overlaps[i].second] = true;
    throw_on_cancel();

    // Crossing shells are solids; any other shell must separate zero from nonzero winding.
    std::vector<bool> drop(n, false);
    std::vector<int> target(n, 1);
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, n, 1), [&](const tbb::blocked_range<std::size_t> &range) {
        for (std::size_t j = range.begin(); j < range.end(); ++j) {
            if (crossing[j])
                continue;
            const Point sample  = inner_sample(shells[j]);
            int         winding = 0;
            for (std::size_t k : enclosing[j])
                if (CGAL::Side_of_triangle_mesh<Mesh, Kernel, CGAL::Default, Tree>(*trees[k])(sample) == CGAL::ON_BOUNDED_SIDE)
                    winding += crossing[k] ? 1 : sign[k];
            const bool solid_outside = winding != 0;
            const bool solid_inside  = winding + sign[j] != 0;
            drop[j]   = solid_outside == solid_inside;
            target[j] = solid_inside ? 1 : -1;
        }
    });
    trees.clear();
    throw_on_cancel();

    for (std::size_t i = 0; i < n; ++i)
        if (drop[i])
            ++stats.removed_shells;
        else if (target[i] != sign[i]) {
            PMP::reverse_face_orientations(shells[i]);
            ++stats.reversed_shells;
        }
    erase_marked(shells, drop);
}

// Orients each part as most of its area was and gives each fan its own vertex; false if the mesh is still invalid.
bool orient_soup(std::vector<Point> &points, std::vector<Triangle> &triangles, std::size_t &reversed_parts)
{
    struct EdgeUse
    {
        std::size_t a, b, corner;
        bool operator<(const EdgeUse &rhs) const { return std::tie(a, b, corner) < std::tie(rhs.a, rhs.b, rhs.corner); }
    };
    const std::size_t num_faces = triangles.size();
    std::vector<EdgeUse> uses;
    uses.reserve(3 * num_faces);
    for (std::size_t f = 0; f < num_faces; ++f)
        for (std::size_t k = 0; k < 3; ++k) {
            const auto [a, b] = std::minmax(triangles[f][k], triangles[f][(k + 1) % 3]);
            uses.push_back({a, b, 3 * f + k});
        }
    tbb::parallel_sort(uses.begin(), uses.end());

    // Triangles meet through edges two of them share; other edges are cut.
    std::vector<std::size_t> twin(3 * num_faces, NONE);
    std::vector<std::array<std::size_t, 4>> shared; // corners of both triangles, then the two vertices of the edge
    for (std::size_t i = 0; i < uses.size();) {
        std::size_t j = i + 1;
        while (j < uses.size() && uses[j].a == uses[i].a && uses[j].b == uses[i].b)
            ++j;
        if (j - i == 2) {
            twin[uses[i].corner]     = uses[i + 1].corner;
            twin[uses[i + 1].corner] = uses[i].corner;
            shared.push_back({uses[i].corner, uses[i + 1].corner, uses[i].a, uses[i].b});
        }
        i = j;
    }

    std::vector<signed char> flip(num_faces, -1);
    std::vector<std::size_t> part_of(num_faces);
    std::vector<double> vote;
    std::vector<char> conflict(3 * num_faces, 0);
    std::vector<std::size_t> stack;
    for (std::size_t seed = 0; seed < num_faces; ++seed) {
        if (flip[seed] >= 0)
            continue;
        const std::size_t part = vote.size();
        vote.push_back(0.);
        flip[seed] = 0;
        stack.push_back(seed);
        while (!stack.empty()) {
            const std::size_t f = stack.back();
            stack.pop_back();
            part_of[f] = part;
            const Triangle &t = triangles[f];
            const double area = std::sqrt(CGAL::squared_area(points[t[0]], points[t[1]], points[t[2]]));
            vote[part] += flip[f] ? -area : area;
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t c = 3 * f + k, n = twin[c];
                if (n == NONE)
                    continue;
                const std::size_t g = n / 3;
                // Triangles agree when they run along their shared edge in opposite directions.
                const signed char want = flip[f] ^ (t[k] == triangles[g][n % 3]);
                if (flip[g] < 0) {
                    flip[g] = want;
                    stack.push_back(g);
                } else if (flip[g] != want)
                    conflict[c] = conflict[n] = 1;
            }
        }
    }
    std::vector<char> part_reversed(vote.size(), 0);
    for (std::size_t f = 0; f < num_faces; ++f)
        if (flip[f] != (vote[part_of[f]] < 0.)) {
            std::swap(triangles[f][1], triangles[f][2]);
            part_reversed[part_of[f]] = 1;
        }
    reversed_parts = std::count(part_reversed.begin(), part_reversed.end(), 1);

    auto corner_of = [&triangles](std::size_t f, std::size_t v) {
        const Triangle &t = triangles[f];
        return 3 * f + (t[0] == v ? 0 : t[1] == v ? 1 : 2);
    };
    UnionFind fans(3 * num_faces);
    for (const auto &[c, n, a, b] : shared)
        if (!conflict[c]) {
            fans.unite(corner_of(c / 3, a), corner_of(n / 3, a));
            fans.unite(corner_of(c / 3, b), corner_of(n / 3, b));
        }
    std::vector<std::size_t> fan_vertex(3 * num_faces, NONE);
    std::vector<char> used(points.size(), 0);
    for (std::size_t c = 0; c < 3 * num_faces; ++c) {
        std::size_t &v = triangles[c / 3][c % 3];
        std::size_t &copy = fan_vertex[fans.find(c)];
        if (copy == NONE) {
            if (!used[v]) {
                used[v] = 1;
                copy    = v;
            } else {
                copy = points.size();
                points.push_back(points[v]);
            }
        }
        v = copy;
    }
    const SoupTopology topology = analyze_topology(points.size(), triangles);
    return std::none_of(topology.bad_face.begin(), topology.bad_face.end(), [](bool b) { return b; }) &&
           std::none_of(topology.pinched.begin(), topology.pinched.end(), [](bool b) { return b; });
}

// Connected parts of the soup; open parts with touching bounding boxes share a unit, so cracks between them close.
std::vector<std::vector<std::size_t>> soup_units(const std::vector<Point> &points, const std::vector<Triangle> &polygons,
                                                 const SoupTopology &topology, double tolerance)
{
    UnionFind parts(points.size());
    for (const Triangle &p : polygons) {
        parts.unite(p[0], p[1]);
        parts.unite(p[0], p[2]);
    }
    std::vector<std::size_t> part_index(points.size(), NONE);
    std::vector<std::size_t> part_of_face(polygons.size());
    std::vector<CGAL::Bbox_3> part_bbox;
    std::vector<bool> part_open;
    for (std::size_t f = 0; f < polygons.size(); ++f) {
        std::size_t &index = part_index[parts.find(polygons[f][0])];
        if (index == NONE) {
            index = part_bbox.size();
            part_bbox.emplace_back();
            part_open.push_back(false);
        }
        part_of_face[f] = index;
        for (std::size_t v : polygons[f])
            part_bbox[index] += points[v].bbox();
        part_open[index] = part_open[index] || topology.open_face[f];
    }

    using Box = CGAL::Box_intersection_d::Box_with_info_d<double, 3, std::size_t>;
    std::vector<Box> boxes;
    for (std::size_t i = 0; i < part_bbox.size(); ++i)
        if (part_open[i]) {
            const CGAL::Bbox_3 &b = part_bbox[i];
            boxes.emplace_back(CGAL::Bbox_3(b.xmin() - tolerance, b.ymin() - tolerance, b.zmin() - tolerance, b.xmax() + tolerance,
                                            b.ymax() + tolerance, b.zmax() + tolerance), i);
        }
    UnionFind units(part_bbox.size());
    CGAL::box_self_intersection_d(boxes.begin(), boxes.end(), [&units](const Box &a, const Box &b) { units.unite(a.info(), b.info()); },
                                  std::ptrdiff_t(10), CGAL::Box_intersection_d::CLOSED);

    std::vector<std::size_t> unit_index(part_bbox.size(), NONE);
    std::vector<std::vector<std::size_t>> out;
    for (std::size_t f = 0; f < polygons.size(); ++f) {
        std::size_t &index = unit_index[units.find(part_of_face[f])];
        if (index == NONE) {
            index = out.size();
            out.emplace_back();
        }
        out[index].push_back(f);
    }
    return out;
}

struct UnitResult
{
    std::vector<Mesh> shells;
    std::size_t       reversed = 0;
    std::size_t       filled   = 0;
    bool              closed   = true;
};

UnitResult repair_unit(const std::vector<Point> &points, const std::vector<Triangle> &polygons, const std::vector<std::size_t> &faces,
                       const SoupTopology &topology, double tolerance)
{
    std::vector<Point>    unit_points;
    std::vector<Triangle> unit_polygons;
    extract_group(points, polygons, faces, unit_points, unit_polygons);
    bool oriented_manifold = true;
    for (std::size_t f : faces) {
        oriented_manifold = oriented_manifold && !topology.bad_face[f];
        for (std::size_t v : polygons[f])
            oriented_manifold = oriented_manifold && !topology.pinched[v];
    }

    UnitResult result;
    Mesh mesh;
    if (oriented_manifold)
        PMP::polygon_soup_to_polygon_mesh(unit_points, unit_polygons, mesh);
    else {
        std::vector<Point>    oriented_points    = unit_points;
        std::vector<Triangle> oriented_triangles = unit_polygons;
        if (orient_soup(oriented_points, oriented_triangles, result.reversed))
            PMP::polygon_soup_to_polygon_mesh(oriented_points, oriented_triangles, mesh);
        else {
            const std::vector<Point>    points_before   = unit_points;
            const std::vector<Triangle> polygons_before = unit_polygons;
            orient_with_cgal(unit_points, unit_polygons);
            PMP::polygon_soup_to_polygon_mesh(unit_points, unit_polygons, mesh);
            result.reversed = keep_input_orientation(mesh, points_before, polygons_before, unit_points, unit_polygons);
        }
    }

    if (!CGAL::is_closed(mesh))
        close_cracks(mesh, tolerance);
    PMP::remove_degenerate_faces(mesh);
    if (!CGAL::is_closed(mesh)) {
        // Rejoin vertices the orientation split where holes break their fan, so each gap is one loop.
        PMP::merge_duplicated_vertices_in_boundary_cycles(mesh);
        result.filled = fill_holes(mesh, tolerance);
        PMP::remove_degenerate_faces(mesh);
    }
    PMP::remove_isolated_vertices(mesh);
    mesh.collect_garbage();
    result.closed = CGAL::is_closed(mesh);
    if (result.closed && !mesh.is_empty())
        result.shells = split_shells(mesh);
    return result;
}

// Returns false when no closed shell was left.
bool repair_soup(std::vector<Point> &points, std::vector<Triangle> &polygons, Mesh &out, MeshRepairStats &stats,
                 const std::function<void()> &throw_on_cancel)
{
    const std::size_t num_triangles = polygons.size();
    const double tolerance = 1e-4 * bbox_diagonal(points);
    std::vector<Mesh> shells = take_closed_shells(points, polygons);
    std::size_t num_taken = 0;
    for (const Mesh &s : shells)
        num_taken += s.number_of_faces();
    stats.modified = !polygons.empty();
    throw_on_cancel();

    if (!polygons.empty()) {
        clean_soup(points, polygons);
        // A soup that stays mostly open has cracks wider than the tolerance, so try wider ones.
        for (double t = tolerance; t > 0. && t <= 100. * tolerance; t *= 10.) {
            double open_fraction = 0.;
            if (const std::size_t welded = weld_border_vertices(points, polygons, t, open_fraction); welded > 0) {
                stats.welded_vertices += welded;
                clean_soup(points, polygons);
                open_fraction = open_edge_fraction(polygons);
            }
            if (open_fraction < 0.5)
                break;
        }
        stats.removed_triangles = num_triangles - num_taken - polygons.size();
        throw_on_cancel();

        // Independent parts of the soup are repaired in parallel.
        const SoupTopology topology = analyze_topology(points.size(), polygons);
        const std::vector<std::vector<std::size_t>> units = soup_units(points, polygons, topology, tolerance);
        std::vector<UnitResult> results(units.size());
        tbb::parallel_for(tbb::blocked_range<std::size_t>(0, units.size(), 1), [&](const tbb::blocked_range<std::size_t> &range) {
            for (std::size_t i = range.begin(); i < range.end(); ++i)
                results[i] = repair_unit(points, polygons, units[i], topology, tolerance);
        });
        for (UnitResult &result : results) {
            if (!result.closed)
                return false;
            stats.reversed_shells += result.reversed;
            stats.filled_holes += result.filled;
            for (Mesh &shell : result.shells)
                shells.emplace_back(std::move(shell));
        }
        throw_on_cancel();
    }

    // Shells are independent from here on. Hole filling closes stray flat patches into shells enclosing nothing.
    std::vector<bool> flat(shells.size());
    std::vector<std::size_t> pairs(shells.size(), 0);
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, shells.size(), 1), [&](const tbb::blocked_range<std::size_t> &range) {
        for (std::size_t i = range.begin(); i < range.end(); ++i)
            if (!(flat[i] = std::abs(signed_volume(shells[i])) <= 0.1 * tolerance * PMP::area(shells[i])))
                pairs[i] = remove_local_self_intersections(shells[i]);
    });
    stats.removed_shells += std::count(flat.begin(), flat.end(), true);
    stats.self_intersections += std::accumulate(pairs.begin(), pairs.end(), std::size_t(0));
    erase_marked(shells, flat);
    throw_on_cancel();
    resolve_shells(shells, stats, throw_on_cancel);

    stats.modified = stats.modified || stats.removed_shells > 0 || stats.reversed_shells > 0 || stats.self_intersections > 0;
    out.clear();
    for (const Mesh &s : shells)
        out.join(s);
    return !out.is_empty();
}

// Repairs its into result, which is left empty when the input needs no change.
bool repair_its(const indexed_triangle_set &its, indexed_triangle_set &result, MeshRepairStats &stats, const std::function<void()> &throw_on_cancel)
{
    std::vector<Point> points;
    points.reserve(its.vertices.size());
    for (const stl_vertex &v : its.vertices)
        points.emplace_back(v.x(), v.y(), v.z());
    std::vector<Triangle> polygons;
    polygons.reserve(its.indices.size());
    for (const stl_triangle_vertex_indices &f : its.indices)
        if (its.vertices[f[0]].allFinite() && its.vertices[f[1]].allFinite() && its.vertices[f[2]].allFinite())
            polygons.push_back({std::size_t(f[0]), std::size_t(f[1]), std::size_t(f[2])});
    const bool dropped = polygons.size() != its.indices.size();

    Mesh out;
    const bool ok = repair_soup(points, polygons, out, stats, throw_on_cancel);
    stats.modified = stats.modified || dropped;
    if (!ok || !stats.modified)
        return ok;

    out.collect_garbage();
    result.vertices.reserve(out.number_of_vertices());
    for (Vertex v : out.vertices()) {
        const Point &p = out.point(v);
        result.vertices.emplace_back(float(p.x()), float(p.y()), float(p.z()));
    }
    result.indices.reserve(out.number_of_faces());
    for (Face f : out.faces()) {
        const Halfedge h = out.halfedge(f);
        result.indices.emplace_back(int(out.source(h)), int(out.target(h)), int(out.target(out.next(h))));
    }
    return true;
}

} // namespace

bool repair_mesh(TriangleMesh &mesh, MeshRepairStats *stats_out, std::string *error, const std::function<void()> &throw_on_cancel)
{
    if (mesh.its.indices.empty())
        return true;

    MeshRepairStats      stats;
    indexed_triangle_set result;
    bool                 ok = false;
    try {
        ok = repair_its(mesh.its, result, stats, throw_on_cancel ? throw_on_cancel : [] {});
    } catch (const CGAL::Failure_exception &ex) {
        if (error)
            *error = ex.what();
        return false;
    }
    if (stats_out)
        *stats_out = stats;
    if (!ok) {
        if (error)
            *error = _u8L("The mesh does not enclose any volume.");
        return false;
    }
    BOOST_LOG_TRIVIAL(info) << "repair_mesh: welded " << stats.welded_vertices << " vertices, removed " << stats.removed_triangles
                            << " triangles, filled " << stats.filled_holes << " holes, reversed " << stats.reversed_shells << " and removed "
                            << stats.removed_shells << " shells, fixed " << stats.self_intersections << " self-intersections";
    if (stats.modified)
        mesh = TriangleMesh(std::move(result));
    return true;
}

} // namespace Slic3r
