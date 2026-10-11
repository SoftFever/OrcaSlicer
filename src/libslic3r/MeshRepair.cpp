#include "MeshRepair.hpp"

#include "I18N.hpp"
#include "TriangleMesh.hpp"

#include <Eigen/Core>
#include <igl/MeshBooleanType.h>
#include <igl/copyleft/cgal/mesh_boolean.h>

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
using Polygon  = std::vector<std::size_t>;
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

// Groups not known to be valid polygon meshes are made into ones first.
std::vector<Mesh> soup_to_meshes(const std::vector<Point> &points, const std::vector<Polygon> &polygons,
                                 const std::vector<std::vector<std::size_t>> &groups, bool known_valid)
{
    std::vector<Mesh> meshes;
    std::vector<std::size_t> remap(points.size(), NONE);
    for (const std::vector<std::size_t> &ids : groups) {
        std::vector<Point>   group_points;
        std::vector<Polygon> group_polygons;
        for (std::size_t i : ids) {
            Polygon &p = group_polygons.emplace_back();
            for (std::size_t v : polygons[i]) {
                if (remap[v] == NONE) {
                    remap[v] = group_points.size();
                    group_points.push_back(points[v]);
                }
                p.push_back(remap[v]);
            }
        }
        for (std::size_t i : ids)
            for (std::size_t v : polygons[i])
                remap[v] = NONE;
        if (!known_valid && !PMP::is_polygon_soup_a_polygon_mesh(group_polygons))
            PMP::orient_polygon_soup(group_points, group_polygons);
        PMP::polygon_soup_to_polygon_mesh(group_points, group_polygons, meshes.emplace_back());
    }
    return meshes;
}

std::vector<Mesh> split_shells(Mesh &mesh)
{
    mesh.collect_garbage();
    std::vector<std::size_t> face_part(mesh.number_of_faces());
    const std::size_t num_parts = PMP::connected_components(mesh, CGAL::make_property_map(face_part));
    if (num_parts == 1) {
        std::vector<Mesh> one;
        one.emplace_back(std::move(mesh));
        return one;
    }
    std::vector<Point> points;
    for (Vertex v : mesh.vertices())
        points.push_back(mesh.point(v));
    std::vector<Polygon> polygons;
    std::vector<std::vector<std::size_t>> groups(num_parts);
    for (Face f : mesh.faces()) {
        Polygon &p = polygons.emplace_back();
        for (Vertex v : CGAL::vertices_around_face(mesh.halfedge(f), mesh))
            p.push_back(v);
        groups[face_part[f]].push_back(polygons.size() - 1);
    }
    return soup_to_meshes(points, polygons, groups, false);
}

// Valid closed parts skip the soup repair, which would weld them to coincident parts into non-manifold junctions.
std::vector<Mesh> take_closed_shells(const std::vector<Point> &points, std::vector<Polygon> &polygons)
{
    // Each edge used twice in opposite directions, and a single fan of corners around each vertex.
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
    std::sort(uses.begin(), uses.end());

    std::vector<bool> bad(polygons.size(), false);
    UnionFind fans(3 * polygons.size());
    for (std::size_t i = 0; i < uses.size();) {
        std::size_t j = i + 1;
        while (j < uses.size() && uses[j].a == uses[i].a && uses[j].b == uses[i].b)
            ++j;
        const std::size_t f = uses[i].corner / 3, fk = uses[i].corner % 3;
        const std::size_t g = uses[i + 1 < j ? i + 1 : i].corner / 3, gk = uses[i + 1 < j ? i + 1 : i].corner % 3;
        if (j - i != 2 || uses[i].a == uses[i].b || polygons[f][fk] == polygons[g][gk]) {
            for (std::size_t k = i; k < j; ++k)
                bad[uses[k].corner / 3] = true;
        } else {
            fans.unite(3 * f + fk, 3 * g + (gk + 1) % 3);
            fans.unite(3 * f + (fk + 1) % 3, 3 * g + gk);
        }
        i = j;
    }
    std::vector<std::size_t> fan_of(points.size(), NONE);
    std::vector<bool> nonmanifold(points.size(), false);
    for (std::size_t c = 0; c < 3 * polygons.size(); ++c) {
        const std::size_t v = polygons[c / 3][c % 3], root = fans.find(c);
        if (fan_of[v] == NONE)
            fan_of[v] = root;
        else if (fan_of[v] != root)
            nonmanifold[v] = true;
    }

    UnionFind parts(points.size());
    for (const Polygon &p : polygons) {
        parts.unite(p[0], p[1]);
        parts.unite(p[0], p[2]);
    }
    std::vector<bool> part_ok(points.size(), true);
    for (std::size_t f = 0; f < polygons.size(); ++f)
        for (std::size_t v : polygons[f])
            if (bad[f] || nonmanifold[v])
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
void remove_duplicate_triangles(std::size_t num_points, std::vector<Polygon> &polygons)
{
    struct Entry
    {
        std::array<std::size_t, 3> key;
        bool                       odd;
        std::size_t                idx;
    };
    std::vector<Entry> entries;
    for (std::size_t i = 0; i < polygons.size(); ++i) {
        const Polygon &p = polygons[i];
        if (p.size() != 3 || p[0] == p[1] || p[1] == p[2] || p[0] == p[2])
            continue;
        const std::size_t r = std::min_element(p.begin(), p.end()) - p.begin();
        const std::size_t b = p[(r + 1) % 3], c = p[(r + 2) % 3];
        entries.push_back({{p[r], std::min(b, c), std::max(b, c)}, b > c, i});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &l, const Entry &r) { return std::tie(l.key, l.idx) < std::tie(r.key, r.idx); });

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
        for (const Polygon &p : polygons)
            for (std::size_t k = 1; k < p.size(); ++k)
                regions.unite(p[0], p[k]);
        std::vector<bool> survives(num_points, false);
        for (std::size_t i = 0; i < polygons.size(); ++i)
            if (!remove[i] && !polygons[i].empty())
                survives[regions.find(polygons[i][0])] = true;
        for (std::size_t i : cancelled)
            if (!survives[regions.find(polygons[i][0])])
                remove[i] = false;
    }
    erase_marked(polygons, remove);
}

void clean_soup(std::vector<Point> &points, std::vector<Polygon> &polygons)
{
    std::vector<std::size_t> order(points.size());
    std::iota(order.begin(), order.end(), std::size_t(0));
    std::sort(order.begin(), order.end(), [&points](std::size_t a, std::size_t b) {
        return std::make_tuple(points[a].x(), points[a].y(), points[a].z(), a) < std::make_tuple(points[b].x(), points[b].y(), points[b].z(), b);
    });
    std::vector<std::size_t> merged(points.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        merged[order[i]] = i > 0 && points[order[i]] == points[order[i - 1]] ? merged[order[i - 1]] : order[i];
    std::vector<bool> degenerate(polygons.size());
    for (std::size_t f = 0; f < polygons.size(); ++f) {
        Polygon &p = polygons[f];
        for (std::size_t &v : p)
            v = merged[v];
        degenerate[f] = p[0] == p[1] || p[1] == p[2] || p[0] == p[2];
    }
    erase_marked(polygons, degenerate);
    remove_duplicate_triangles(points.size(), polygons);

    std::vector<std::size_t> compact(points.size(), NONE);
    std::vector<Point> used;
    for (Polygon &p : polygons)
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
std::size_t weld_border_vertices(const std::vector<Point> &points, std::vector<Polygon> &polygons, double tolerance)
{
    std::vector<double> radius(points.size(), tolerance);
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const Polygon &p : polygons)
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t a = p[k], b = p[(k + 1) % 3];
            const double r = 0.25 * std::sqrt(CGAL::squared_distance(points[a], points[b]));
            radius[a] = std::min(radius[a], r);
            radius[b] = std::min(radius[b], r);
            edges.emplace_back(std::minmax(a, b));
        }
    std::sort(edges.begin(), edges.end());
    std::vector<std::size_t> border;
    for (std::size_t i = 0; i < edges.size();) {
        std::size_t j = i + 1;
        while (j < edges.size() && edges[j] == edges[i])
            ++j;
        if (j - i == 1) {
            border.push_back(edges[i].first);
            border.push_back(edges[i].second);
        }
        i = j;
    }
    if (border.empty())
        return 0;

    using Cell = std::array<int64_t, 3>;
    auto cell_of = [tolerance](const Point &p) {
        return Cell{int64_t(std::floor(p.x() / tolerance)), int64_t(std::floor(p.y() / tolerance)), int64_t(std::floor(p.z() / tolerance))};
    };
    std::vector<std::pair<Cell, std::size_t>> grid;
    for (std::size_t v : border)
        grid.emplace_back(cell_of(points[v]), v);
    std::sort(grid.begin(), grid.end());
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
        for (Polygon &p : polygons)
            for (std::size_t &v : p)
                v = uf.find(v);
    return welded;
}

double open_edge_fraction(const std::vector<Polygon> &polygons)
{
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (const Polygon &p : polygons)
        for (std::size_t k = 0; k < 3; ++k)
            edges.emplace_back(std::minmax(p[k], p[(k + 1) % 3]));
    std::sort(edges.begin(), edges.end());
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
std::size_t keep_input_orientation(Mesh &mesh, const std::vector<Point> &points_before, const std::vector<Polygon> &polygons_before,
                                   const std::vector<Point> &points, const std::vector<Polygon> &polygons)
{
    if (mesh.number_of_faces() != polygons.size())
        return 0;
    auto fcc = mesh.add_property_map<Face, std::size_t>("f:repair_part").first;
    const std::size_t num_parts = PMP::connected_components(mesh, fcc);
    std::vector<double> vote(num_parts, 0.);
    std::size_t i = 0;
    for (Face f : mesh.faces()) {
        const Polygon &a = polygons_before[i];
        const Polygon &b = polygons[i++];
        bool same = false;
        for (std::size_t r = 0; r < a.size() && !same; ++r) {
            same = true;
            for (std::size_t k = 0; k < a.size() && same; ++k)
                same = points_before[a[k]] == points[b[(k + r) % b.size()]];
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

std::size_t remove_local_self_intersections(Mesh &shell, bool &still_intersecting)
{
    std::vector<std::pair<Face, Face>> pairs;
    PMP::self_intersections<CGAL::Parallel_if_available_tag>(shell, std::back_inserter(pairs));
    still_intersecting = false;
    if (pairs.empty())
        return 0;
    std::set<Face> faces;
    for (const auto &[a, b] : pairs) {
        faces.insert(a);
        faces.insert(b);
    }
    Mesh backup = shell;
    try {
        PMP::experimental::remove_self_intersections(faces, shell, CGAL::parameters::preserve_genus(false));
        shell.collect_garbage();
        if (!CGAL::is_valid_polygon_mesh(shell) || !CGAL::is_closed(shell))
            shell = std::move(backup);
    } catch (const std::exception &) {
        shell = std::move(backup);
    }
    still_intersecting = PMP::does_self_intersect<CGAL::Parallel_if_available_tag>(shell);
    return pairs.size();
}

class ShellTrees
{
public:
    explicit ShellTrees(const std::vector<Mesh> &shells) : m_shells(shells), m_trees(shells.size()) {}

    const Tree &operator[](std::size_t i)
    {
        if (!m_trees[i])
            m_trees[i] = std::make_unique<Tree>(m_shells[i].faces().begin(), m_shells[i].faces().end(), m_shells[i]);
        return *m_trees[i];
    }

private:
    const std::vector<Mesh>            &m_shells;
    std::vector<std::unique_ptr<Tree>>  m_trees;
};

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

// libigl's exact boolean, as corefinement can crash on shells touching along coplanar faces.
bool unite(const std::vector<Mesh *> &parts, Mesh &united)
{
    std::size_t num_vertices = 0, num_faces = 0;
    for (Mesh *part : parts) {
        part->collect_garbage();
        num_vertices += part->number_of_vertices();
        num_faces += part->number_of_faces();
    }
    Eigen::MatrixXd V(num_vertices, 3);
    Eigen::MatrixXi F(num_faces, 3);
    std::size_t vertex_offset = 0, face = 0;
    for (const Mesh *part : parts) {
        for (Vertex v : part->vertices()) {
            const Point &p = part->point(v);
            V.row(vertex_offset + std::size_t(v)) << p.x(), p.y(), p.z();
        }
        for (Face f : part->faces()) {
            int k = 0;
            for (Vertex v : CGAL::vertices_around_face(part->halfedge(f), *part))
                F(face, k++) = int(vertex_offset + std::size_t(v));
            ++face;
        }
        vertex_offset += part->number_of_vertices();
    }
    Eigen::MatrixXd VC;
    Eigen::MatrixXi FC;
    try {
        if (!igl::copyleft::cgal::mesh_boolean(V, F, Eigen::MatrixXd(), Eigen::MatrixXi(), igl::MESH_BOOLEAN_TYPE_UNION, VC, FC))
            return false;
    } catch (const std::exception &) {
        return false;
    }
    std::vector<Point>   points;
    std::vector<Polygon> polygons;
    for (Eigen::Index r = 0; r < VC.rows(); ++r)
        points.emplace_back(VC(r, 0), VC(r, 1), VC(r, 2));
    for (Eigen::Index r = 0; r < FC.rows(); ++r)
        polygons.push_back({std::size_t(FC(r, 0)), std::size_t(FC(r, 1)), std::size_t(FC(r, 2))});
    // Shells touching along an edge or at a vertex unite into a non-manifold mesh.
    if (!PMP::is_polygon_soup_a_polygon_mesh(polygons))
        PMP::orient_polygon_soup(points, polygons);
    PMP::polygon_soup_to_polygon_mesh(points, polygons, united);
    return !united.is_empty() && CGAL::is_valid_polygon_mesh(united) && CGAL::is_closed(united);
}

// Unites each group of crossing shells, and alone each shell that still folds through itself. A failed group stays as it is.
void merge_crossing_shells(std::vector<Mesh> &shells, UnionFind &groups, const std::vector<bool> &folded, MeshRepairStats &stats)
{
    std::map<std::size_t, std::vector<std::size_t>> members;
    for (std::size_t i = 0; i < shells.size(); ++i)
        members[groups.find(i)].push_back(i);

    std::vector<Mesh> out;
    for (const auto &[root, group] : members) {
        if (group.size() > 1 || folded[group.front()]) {
            std::vector<Mesh *> parts;
            for (std::size_t i : group)
                parts.push_back(&shells[i]);
            if (Mesh united; unite(parts, united)) {
                out.emplace_back(std::move(united));
                stats.merged_shells += group.size() - 1;
                continue;
            }
        }
        for (std::size_t i : group)
            out.emplace_back(std::move(shells[i]));
    }
    shells = std::move(out);
}

// Orients and filters the shells by the nonzero winding rule of slicing, then unites the crossing ones.
void resolve_shells(std::vector<Mesh> &shells, const std::vector<bool> &folded, MeshRepairStats &stats, const std::function<void()> &throw_on_cancel)
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

    UnionFind groups(n);
    std::vector<bool> crossing = folded;
    std::vector<bool> drop(n, false);
    std::vector<int> target(n, 1);
    {
        ShellTrees trees(shells);
        for (const auto &[a, b] : overlaps) {
            const bool a_smaller = shells[a].number_of_faces() < shells[b].number_of_faces();
            if (surfaces_intersect(shells[a_smaller ? a : b], trees[a_smaller ? b : a])) {
                crossing[a] = crossing[b] = true;
                groups.unite(a, b);
            }
        }
        throw_on_cancel();

        // Crossing shells are solids; any other shell must separate zero from nonzero winding.
        std::vector<std::vector<std::size_t>> enclosing(n);
        for (const auto &[a, b] : overlaps) {
            if (bbox_contains(bbox[a], bbox[b]))
                enclosing[b].push_back(a);
            if (bbox_contains(bbox[b], bbox[a]))
                enclosing[a].push_back(b);
        }
        for (std::size_t j = 0; j < n; ++j) {
            if (crossing[j])
                continue;
            const Point sample  = inner_sample(shells[j]);
            int         winding = 0;
            for (std::size_t k : enclosing[j])
                if (CGAL::Side_of_triangle_mesh<Mesh, Kernel, CGAL::Default, Tree>(trees[k])(sample) == CGAL::ON_BOUNDED_SIDE)
                    winding += crossing[k] ? 1 : sign[k];
            const bool solid_outside = winding != 0;
            const bool solid_inside  = winding + sign[j] != 0;
            drop[j]   = solid_outside == solid_inside;
            target[j] = solid_inside ? 1 : -1;
        }
        throw_on_cancel();
    }

    for (std::size_t i = 0; i < n; ++i)
        if (drop[i])
            ++stats.removed_shells;
        else if (target[i] != sign[i]) {
            PMP::reverse_face_orientations(shells[i]);
            ++stats.reversed_shells;
        }
    std::vector<Mesh> kept;
    std::vector<bool> kept_folded;
    UnionFind kept_groups(n);
    std::vector<std::size_t> first_of_group(n, NONE);
    for (std::size_t i = 0; i < n; ++i) {
        if (drop[i])
            continue;
        std::size_t &first = first_of_group[groups.find(i)];
        if (first == NONE)
            first = kept.size();
        else
            kept_groups.unite(first, kept.size());
        kept.emplace_back(std::move(shells[i]));
        kept_folded.push_back(folded[i]);
    }
    shells = std::move(kept);
    merge_crossing_shells(shells, kept_groups, kept_folded, stats);
}

// Returns false when no closed shell was left.
bool repair_soup(std::vector<Point> &points, std::vector<Polygon> &polygons, Mesh &out, MeshRepairStats &stats,
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
            if (const std::size_t welded = weld_border_vertices(points, polygons, t); welded > 0) {
                stats.welded_vertices += welded;
                clean_soup(points, polygons);
            }
            if (open_edge_fraction(polygons) < 0.5)
                break;
        }
        stats.removed_triangles = num_triangles - num_taken - polygons.size();
        throw_on_cancel();

        const std::vector<Point>   points_before   = points;
        const std::vector<Polygon> polygons_before = polygons;
        PMP::orient_polygon_soup(points, polygons);
        Mesh mesh;
        PMP::polygon_soup_to_polygon_mesh(points, polygons, mesh);
        stats.reversed_shells += keep_input_orientation(mesh, points_before, polygons_before, points, polygons);
        throw_on_cancel();

        if (!CGAL::is_closed(mesh))
            close_cracks(mesh, tolerance);
        PMP::remove_degenerate_faces(mesh);
        if (!CGAL::is_closed(mesh)) {
            // Rejoin vertices orient_polygon_soup() split where holes break their fan, so each gap is one loop.
            PMP::merge_duplicated_vertices_in_boundary_cycles(mesh);
            stats.filled_holes = fill_holes(mesh, tolerance);
            PMP::remove_degenerate_faces(mesh);
        }
        PMP::remove_isolated_vertices(mesh);
        mesh.collect_garbage();
        if (!CGAL::is_closed(mesh))
            return false;
        throw_on_cancel();
        if (!mesh.is_empty())
            for (Mesh &part : split_shells(mesh))
                shells.emplace_back(std::move(part));
    }

    // Hole filling closes stray flat patches into shells enclosing nothing.
    std::vector<bool> flat(shells.size());
    for (std::size_t i = 0; i < shells.size(); ++i)
        if ((flat[i] = std::abs(signed_volume(shells[i])) <= 0.1 * tolerance * PMP::area(shells[i])))
            ++stats.removed_shells;
    erase_marked(shells, flat);

    std::vector<bool> folded(shells.size());
    for (std::size_t i = 0; i < shells.size(); ++i) {
        bool still_intersecting = false;
        stats.self_intersections += remove_local_self_intersections(shells[i], still_intersecting);
        folded[i] = still_intersecting;
        throw_on_cancel();
    }
    resolve_shells(shells, folded, stats, throw_on_cancel);

    stats.modified = stats.modified || stats.removed_shells > 0 || stats.reversed_shells > 0 || stats.self_intersections > 0 || stats.merged_shells > 0;
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
    std::vector<Polygon> polygons;
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
                            << stats.removed_shells << " shells, fixed " << stats.self_intersections << " self-intersections, merged "
                            << stats.merged_shells << " shells";
    if (stats.modified)
        mesh = TriangleMesh(std::move(result));
    return true;
}

} // namespace Slic3r
