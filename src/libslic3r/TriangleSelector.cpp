#include "TriangleSelector.hpp"
#include "Model.hpp"

#include <boost/container/small_vector.hpp>

#ifndef NDEBUG
    #define EXPENSIVE_DEBUG_CHECKS
#endif // NDEBUG

namespace Slic3r {

static inline Vec3i root_neighbors(const TriangleMesh &mesh, int triangle_id)
{
    Vec3i neighbors;
    const stl_neighbors& neighbors_src = mesh.stl.neighbors_start[triangle_id];
    for (int i = 0; i < 3; ++i)
        // Refuse a neighbor with a flipped normal.
        neighbors(i) = neighbors_src.neighbor[i];
    return neighbors;
}

#ifndef NDEBUG
bool TriangleSelector::verify_triangle_midpoints(const Triangle &tr) const
{
    for (int i = 0; i < 3; ++ i) {
        int v1   = tr.verts_idxs[i];
        int v2   = tr.verts_idxs[next_idx_modulo(i, 3)];
        int vmid = this->triangle_midpoint(tr, v1, v2);
        assert(vmid >= -1);
        if (vmid != -1) {
            Vec3f c1 = 0.5f * (m_vertices[v1].v + m_vertices[v2].v);
            Vec3f c2 = m_vertices[vmid].v;
            float d  = (c2 - c1).norm();
            assert(std::abs(d) < EPSILON);
        }
    }
    return true;
}

bool TriangleSelector::verify_triangle_neighbors(const Triangle &tr, const Vec3i &neighbors) const
{
    assert(neighbors(0) >= -1);
    assert(neighbors(1) >= -1);
    assert(neighbors(2) >= -1);
    assert(verify_triangle_midpoints(tr));

    for (int i = 0; i < 3; ++i)
        if (neighbors(i) != -1) {
            const Triangle &tr2 = m_triangles[neighbors(i)];
            assert(verify_triangle_midpoints(tr2));
            int v1 = tr.verts_idxs[i];
            int v2 = tr.verts_idxs[next_idx_modulo(i, 3)];
            assert(tr2.verts_idxs[0] == v1 || tr2.verts_idxs[1] == v1 || tr2.verts_idxs[2] == v1);
            int j = tr2.verts_idxs[0] == v1 ? 0 : tr2.verts_idxs[1] == v1 ? 1 : 2;
            assert(tr2.verts_idxs[j] == v1);
            assert(tr2.verts_idxs[prev_idx_modulo(j, 3)] == v2);
        }
    return true;
}
#endif // NDEBUG

// sides_to_split==-1 : just restore previous split
void TriangleSelector::Triangle::set_division(int sides_to_split, int special_side_idx)
{
    assert(sides_to_split >= 0 && sides_to_split <= 3);
    assert(special_side_idx >= 0 && special_side_idx < 3);
    assert(sides_to_split == 1 || sides_to_split == 2 || special_side_idx == 0);
    this->number_of_splits = sides_to_split;
    this->special_side_idx = special_side_idx;
}



void TriangleSelector::select_patch(const Vec3f& hit, int facet_start,
                                    const Vec3f& source, float radius,
                                    CursorType cursor_type, EnforcerBlockerType new_state,
                                    const Transform3d& trafo, bool triangle_splitting)
{
    assert(facet_start < m_orig_size_indices);

    // Save current cursor center, squared radius and camera direction, so we don't
    // have to pass it around.
    m_cursor = Cursor(hit, source, radius, cursor_type, trafo);

    // In case user changed cursor size since last time, update triangle edge limit.
    // It is necessary to compare the internal radius in m_cursor! radius is in
    // world coords and does not change after scaling.
    if (m_old_cursor_radius_sqr != m_cursor.radius_sqr) {
        set_edge_limit(std::sqrt(m_cursor.radius_sqr) / 5.f);
        m_old_cursor_radius_sqr = m_cursor.radius_sqr;
    }

    // Now start with the facet the pointer points to and check all adjacent facets.
    std::vector<int> facets_to_check;
    facets_to_check.reserve(16);
    facets_to_check.emplace_back(facet_start);
    // Keep track of facets of the original mesh we already processed.
    std::vector<bool> visited(m_orig_size_indices, false);
    // Breadth-first search around the hit point. facets_to_check may grow significantly large.
    // Head of the bread-first facets_to_check FIFO.
    int facet_idx = 0;
    while (facet_idx < int(facets_to_check.size())) {
        int facet = facets_to_check[facet_idx];
        if (! visited[facet]) {
            if (select_triangle(facet, new_state, triangle_splitting)) {
                // add neighboring facets to list to be proccessed later
                for (int n=0; n<3; ++n) {
                    int neighbor_idx = m_mesh->stl.neighbors_start[facet].neighbor[n];
                    if (neighbor_idx >=0 && (m_cursor.type == SPHERE || faces_camera(neighbor_idx)))
                        facets_to_check.push_back(neighbor_idx);
                }
            }
        }
        visited[facet] = true;
        ++facet_idx;
    }
}

void TriangleSelector::seed_fill_select_triangles(const Vec3f& hit, int facet_start, float seed_fill_angle)
{
    assert(facet_start < m_orig_size_indices);
    this->seed_fill_unselect_all_triangles();

    std::vector<bool> visited(m_triangles.size(), false);
    std::queue<int> facet_queue;
    facet_queue.push(facet_start);

    const double facet_angle_limit = cos(Geometry::deg2rad(seed_fill_angle)) - EPSILON;

    // Depth-first traversal of neighbors of the face hit by the ray thrown from the mouse cursor.
    while(!facet_queue.empty()) {
        int current_facet = facet_queue.front();
        facet_queue.pop();

        if (!visited[current_facet]) {
            if (m_triangles[current_facet].is_split()) {
                for (int split_triangle_idx = 0; split_triangle_idx <= m_triangles[current_facet].number_of_split_sides(); ++split_triangle_idx) {
                    assert(split_triangle_idx < int(m_triangles[current_facet].children.size()));
                    assert(m_triangles[current_facet].children[split_triangle_idx] < int(m_triangles.size()));
                    if (int child = m_triangles[current_facet].children[split_triangle_idx]; !visited[child])
                        // Child triangle shares normal with its parent. Select it.
                        facet_queue.push(child);
                }
            } else
                m_triangles[current_facet].select_by_seed_fill();

            if (current_facet < m_orig_size_indices)
                // Propagate over the original triangles.
                for (int neighbor_idx : m_mesh->stl.neighbors_start[current_facet].neighbor) {
                    assert(neighbor_idx >= -1);
                    if (neighbor_idx >= 0 && !visited[neighbor_idx]) {
                        // Check if neighbour_facet_idx is satisfies angle in seed_fill_angle and append it to facet_queue if it do.
                        const Vec3f &n1 = m_mesh->stl.facet_start[m_triangles[neighbor_idx].source_triangle].normal;
                        const Vec3f &n2 = m_mesh->stl.facet_start[m_triangles[current_facet].source_triangle].normal;
                        if (std::clamp(n1.dot(n2), 0.f, 1.f) >= facet_angle_limit)
                            facet_queue.push(neighbor_idx);
                    }
                }
        }
        visited[current_facet] = true;
    }
}

// Selects either the whole triangle (discarding any children it had), or divides
// the triangle recursively, selecting just subtriangles truly inside the circle.
// This is done by an actual recursive call. Returns false if the triangle is
// outside the cursor.
// Called by select_patch() and by itself.
bool TriangleSelector::select_triangle(int facet_idx, EnforcerBlockerType type, bool triangle_splitting)
{
    assert(facet_idx < int(m_triangles.size()));

    if (! m_triangles[facet_idx].valid())
        return false;

    Vec3i neighbors = root_neighbors(*m_mesh, facet_idx);
    assert(this->verify_triangle_neighbors(m_triangles[facet_idx], neighbors));

    if (! select_triangle_recursive(facet_idx, neighbors, type, triangle_splitting))
        return false;

    // In case that all children are leafs and have the same state now,
    // they may be removed and substituted by the parent triangle.
    remove_useless_children(facet_idx);

#ifdef EXPENSIVE_DEBUG_CHECKS
    // Make sure that we did not lose track of invalid triangles.
    assert(m_invalid_triangles == std::count_if(m_triangles.begin(), m_triangles.end(),
               [](const Triangle& tr) { return ! tr.valid(); }));
#endif // EXPENSIVE_DEBUG_CHECKS

    // Do garbage collection maybe?
    if (2*m_invalid_triangles > int(m_triangles.size()))
        garbage_collect();

    return true;
}

// Return child of itriangle at a CCW oriented side (vertexi, vertexj), either first or 2nd part.
// If the side sharing (vertexi, vertexj) is not split, return -1.
int TriangleSelector::neighbor_child(const Triangle &tr, int vertexi, int vertexj, Partition partition) const
{
    if (tr.number_of_split_sides() == 0)
        // If this triangle is not split, then there is no upper / lower subtriangle sharing the edge.
        return -1;

    // Find the triangle edge.
    int edge = tr.verts_idxs[0] == vertexi ? 0 : tr.verts_idxs[1] == vertexi ? 1 : 2;
    assert(tr.verts_idxs[edge] == vertexi);
    assert(tr.verts_idxs[next_idx_modulo(edge, 3)] == vertexj);

    int child_idx;
    if (tr.number_of_split_sides() == 1) {
        if (edge != next_idx_modulo(tr.special_side(), 3))
            // A child may or may not be split at this side.
            return this->neighbor_child(m_triangles[tr.children[edge == tr.special_side() ? 0 : 1]], vertexi, vertexj, partition);
        child_idx = partition == Partition::First ? 0 : 1;
    } else if (tr.number_of_split_sides() == 2) {
        if (edge == next_idx_modulo(tr.special_side(), 3))
            // A child may or may not be split at this side.
            return this->neighbor_child(m_triangles[tr.children[2]], vertexi, vertexj, partition);
        child_idx = edge == tr.special_side() ?
            (partition == Partition::First ? 0 : 1) :
            (partition == Partition::First ? 2 : 0);
    } else {
        assert(tr.number_of_split_sides() == 3);
        assert(tr.special_side() == 0);
        switch(edge) {
        case 0:  child_idx = partition == Partition::First ? 0 : 1; break;
        case 1:  child_idx = partition == Partition::First ? 1 : 2; break;
        default: assert(edge == 2);
                 child_idx = partition == Partition::First ? 2 : 0; break;
        }
    }
    return tr.children[child_idx];
}

// Return child of itriangle at a CCW oriented side (vertexi, vertexj), either first or 2nd part.
// If itriangle == -1 or if the side sharing (vertexi, vertexj) is not split, return -1.
int TriangleSelector::neighbor_child(int itriangle, int vertexi, int vertexj, Partition partition) const
{
    return itriangle == -1 ? -1 : this->neighbor_child(m_triangles[itriangle], vertexi, vertexj, partition);
}

// Return existing midpoint of CCW oriented side (vertexi, vertexj).
// If itriangle == -1 or if the side sharing (vertexi, vertexj) is not split, return -1.
int TriangleSelector::triangle_midpoint(const Triangle &tr, int vertexi, int vertexj) const
{
    if (tr.number_of_split_sides() == 0)
        // If this triangle is not split, then there is no upper / lower subtriangle sharing the edge.
        return -1;

    // Find the triangle edge.
    int edge = tr.verts_idxs[0] == vertexi ? 0 : tr.verts_idxs[1] == vertexi ? 1 : 2;
    assert(tr.verts_idxs[edge] == vertexi);
    assert(tr.verts_idxs[next_idx_modulo(edge, 3)] == vertexj);

    if (tr.number_of_split_sides() == 1) {
        return edge == next_idx_modulo(tr.special_side(), 3) ?
            m_triangles[tr.children[0]].verts_idxs[2] :
            this->triangle_midpoint(m_triangles[tr.children[edge == tr.special_side() ? 0 : 1]], vertexi, vertexj);
    } else if (tr.number_of_split_sides() == 2) {
        return edge == next_idx_modulo(tr.special_side(), 3) ?
                    this->triangle_midpoint(m_triangles[tr.children[2]], vertexi, vertexj) :
               edge == tr.special_side() ?
                    m_triangles[tr.children[0]].verts_idxs[1] :
                    m_triangles[tr.children[1]].verts_idxs[2];
    } else {
        assert(tr.number_of_split_sides() == 3);
        assert(tr.special_side() == 0);
        return
            (edge == 0) ? m_triangles[tr.children[0]].verts_idxs[1] :
            (edge == 1) ? m_triangles[tr.children[1]].verts_idxs[2] :
                          m_triangles[tr.children[2]].verts_idxs[2];
    }
}

// Return existing midpoint of CCW oriented side (vertexi, vertexj).
// If itriangle == -1 or if the side sharing (vertexi, vertexj) is not split, return -1.
int TriangleSelector::triangle_midpoint(int itriangle, int vertexi, int vertexj) const
{
    return itriangle == -1 ? -1 : this->triangle_midpoint(m_triangles[itriangle], vertexi, vertexj);
}

int TriangleSelector::triangle_midpoint_or_allocate(int itriangle, int vertexi, int vertexj)
{
    int midpoint = this->triangle_midpoint(itriangle, vertexi, vertexj);
    if (midpoint == -1) {
        Vec3f c = 0.5f * (m_vertices[vertexi].v + m_vertices[vertexj].v);
#ifdef EXPENSIVE_DEBUG_CHECKS
        // Verify that the vertex is really a new one.
        auto it = std::find_if(m_vertices.begin(), m_vertices.end(), [this, c](const Vertex &v) {
            return v.ref_cnt > 0 && (v.v - c).norm() < EPSILON; });
        assert(it == m_vertices.end());
#endif // EXPENSIVE_DEBUG_CHECKS
        // Allocate a new vertex, possibly reusing the free list.
        if (m_free_vertices_head == -1) {
            // Allocate a new vertex.
            midpoint = int(m_vertices.size());
            m_vertices.emplace_back(c);
        } else {
            // Reuse a vertex from the free list.
            assert(m_free_vertices_head >= -1 && m_free_vertices_head < int(m_vertices.size()));
            midpoint = m_free_vertices_head;
            memcpy(&m_free_vertices_head, &m_vertices[midpoint].v[0], sizeof(m_free_vertices_head));
            assert(m_free_vertices_head >= -1 && m_free_vertices_head < int(m_vertices.size()));
            m_vertices[midpoint].v = c;
        }
        assert(m_vertices[midpoint].ref_cnt == 0);
    } else {
#ifndef NDEBUG
        Vec3f c1 = 0.5f * (m_vertices[vertexi].v + m_vertices[vertexj].v);
        Vec3f c2 = m_vertices[midpoint].v;
        float d = (c2 - c1).norm();
        assert(std::abs(d) < EPSILON);
#endif // NDEBUG
        assert(m_vertices[midpoint].ref_cnt > 0);
    }
    return midpoint;
}

// Return neighbors of ith child of a triangle given neighbors of the triangle.
// Returns -1 if such a neighbor does not exist at all, or it does not exist
// at the same depth as the ith child.
// Using the same splitting strategy as TriangleSelector::split_triangle()
Vec3i TriangleSelector::child_neighbors(const Triangle &tr, const Vec3i &neighbors, int child_idx) const
{
    assert(this->verify_triangle_neighbors(tr, neighbors));

    assert(child_idx >= 0 && child_idx <= tr.number_of_split_sides());
    int   i = tr.special_side();
    int   j = i + 1;
    if (j >= 3)
        j = 0;
    int   k = j + 1;
    if (k >= 3)
        k = 0;

    Vec3i out;
    switch (tr.number_of_split_sides()) {
    case 1:
        switch (child_idx) {
        case 0:
            out(0) = neighbors(i);
            out(1) = this->neighbor_child(neighbors(j), tr.verts_idxs[k], tr.verts_idxs[j], Partition::Second);
            out(2) = tr.children[1];
            break;
        default:
            assert(child_idx == 1);
            out(0) = this->neighbor_child(neighbors(j), tr.verts_idxs[k], tr.verts_idxs[j], Partition::First);
            out(1) = neighbors(k);
            out(2) = tr.children[0];
            break;
        }
        break;

    case 2:
        switch (child_idx) {
        case 0:
            out(0) = this->neighbor_child(neighbors(i), tr.verts_idxs[j], tr.verts_idxs[i], Partition::Second);
            out(1) = tr.children[1];
            out(2) = this->neighbor_child(neighbors(k), tr.verts_idxs[i], tr.verts_idxs[k], Partition::First);
            break;
        case 1:
            assert(child_idx == 1);
            out(0) = this->neighbor_child(neighbors(i), tr.verts_idxs[j], tr.verts_idxs[i], Partition::First);
            out(1) = tr.children[2];
            out(2) = tr.children[0];
            break;
        default:
            assert(child_idx == 2);
            out(0) = neighbors(j);
            out(1) = this->neighbor_child(neighbors(k), tr.verts_idxs[i], tr.verts_idxs[k], Partition::Second);
            out(2) = tr.children[1];
            break;
        }
        break;

    case 3:
        assert(tr.special_side() == 0);
        switch (child_idx) {
        case 0:
            out(0) = this->neighbor_child(neighbors(0), tr.verts_idxs[1], tr.verts_idxs[0], Partition::Second);
            out(1) = tr.children[3];
            out(2) = this->neighbor_child(neighbors(2), tr.verts_idxs[0], tr.verts_idxs[2], Partition::First);
            break;
        case 1:
            out(0) = this->neighbor_child(neighbors(0), tr.verts_idxs[1], tr.verts_idxs[0], Partition::First);
            out(1) = this->neighbor_child(neighbors(1), tr.verts_idxs[2], tr.verts_idxs[1], Partition::Second);
            out(2) = tr.children[3];
            break;
        case 2:
            out(0) = this->neighbor_child(neighbors(1), tr.verts_idxs[2], tr.verts_idxs[1], Partition::First);
            out(1) = this->neighbor_child(neighbors(2), tr.verts_idxs[0], tr.verts_idxs[2], Partition::Second);
            out(2) = tr.children[3];
            break;
        default:
            assert(child_idx == 3);
            out(0) = tr.children[1];
            out(1) = tr.children[2];
            out(2) = tr.children[0];
            break;
        }
        break;

    default:
        assert(false);
    }

    assert(this->verify_triangle_neighbors(tr, neighbors));
    assert(this->verify_triangle_neighbors(m_triangles[tr.children[child_idx]], out));
    return out;
}

bool TriangleSelector::select_triangle_recursive(int facet_idx, const Vec3i &neighbors, EnforcerBlockerType type, bool triangle_splitting)
{
    assert(facet_idx < int(m_triangles.size()));

    Triangle* tr = &m_triangles[facet_idx];
    if (! tr->valid())
        return false;

    assert(this->verify_triangle_neighbors(*tr, neighbors));

    int num_of_inside_vertices = vertices_inside(facet_idx);

    if (num_of_inside_vertices == 0
     && ! is_pointer_in_triangle(facet_idx)
     && ! is_edge_inside_cursor(facet_idx))
        return false;

    if (num_of_inside_vertices == 3) {
        // dump any subdivision and select whole triangle
        undivide_triangle(facet_idx);
        tr->set_state(type);
    } else {
        // the triangle is partially inside, let's recursively divide it
        // (if not already) and try selecting its children.

        if (! tr->is_split() && tr->get_state() == type) {
            // This is leaf triangle that is already of correct type as a whole.
            // No need to split, all children would end up selected anyway.
            return true;
        }

        if (triangle_splitting)
            split_triangle(facet_idx, neighbors);
        else if (!m_triangles[facet_idx].is_split())
            m_triangles[facet_idx].set_state(type);
        tr = &m_triangles[facet_idx]; // might have been invalidated by split_triangle().

        int num_of_children = tr->number_of_split_sides() + 1;
        if (num_of_children != 1) {
            for (int i=0; i<num_of_children; ++i) {
                assert(i < int(tr->children.size()));
                assert(tr->children[i] < int(m_triangles.size()));
                // Recursion, deep first search over the children of this triangle.
                // All children of this triangle were created by splitting a single source triangle of the original mesh.
                select_triangle_recursive(tr->children[i], this->child_neighbors(*tr, neighbors, i), type, triangle_splitting);
                tr = &m_triangles[facet_idx]; // might have been invalidated
            }
        }
    }

    return true;
}

void TriangleSelector::set_facet(int facet_idx, EnforcerBlockerType state)
{
    assert(facet_idx < m_orig_size_indices);
    undivide_triangle(facet_idx);
    assert(! m_triangles[facet_idx].is_split());
    m_triangles[facet_idx].set_state(state);
}

// called by select_patch()->select_triangle()...select_triangle()
// to decide which sides of the traingle to split and to actually split it calling set_division() and perform_split().
void TriangleSelector::split_triangle(int facet_idx, const Vec3i &neighbors)
{
    if (m_triangles[facet_idx].is_split()) {
        // The triangle is divided already.
        return;
    }

    Triangle* tr = &m_triangles[facet_idx];
    assert(this->verify_triangle_neighbors(*tr, neighbors));

    EnforcerBlockerType old_type = tr->get_state();

    // If we got here, we are about to actually split the triangle.
    const double limit_squared = m_edge_limit_sqr;

    std::array<int, 3>& facet = tr->verts_idxs;
    std::array<const stl_vertex*, 3> pts = { &m_vertices[facet[0]].v,
                                             &m_vertices[facet[1]].v,
                                             &m_vertices[facet[2]].v};
    std::array<stl_vertex, 3> pts_transformed; // must stay in scope of pts !!!

    // In case the object is non-uniformly scaled, transform the
    // points to world coords.
    if (! m_cursor.uniform_scaling) {
        for (size_t i=0; i<pts.size(); ++i) {
            pts_transformed[i] = m_cursor.trafo * (*pts[i]);
            pts[i] = &pts_transformed[i];
        }
    }

    std::array<double, 3> sides;
    sides = { (*pts[2]-*pts[1]).squaredNorm(),
              (*pts[0]-*pts[2]).squaredNorm(),
              (*pts[1]-*pts[0]).squaredNorm() };

    boost::container::small_vector<int, 3> sides_to_split;
    int side_to_keep = -1;
    for (int pt_idx = 0; pt_idx<3; ++pt_idx) {
        if (sides[pt_idx] > limit_squared)
            sides_to_split.push_back(pt_idx);
        else
            side_to_keep = pt_idx;
    }
    if (sides_to_split.empty()) {
        // This shall be unselected.
        tr->set_division(0, 0);
        return;
    }

    // Save how the triangle will be split. Second argument makes sense only for one
    // or two split sides, otherwise the value is ignored.
    tr->set_division(sides_to_split.size(),
        sides_to_split.size() == 2 ? side_to_keep : sides_to_split[0]);

    perform_split(facet_idx, neighbors, old_type);
}



// Is pointer in a triangle?
bool TriangleSelector::is_pointer_in_triangle(int facet_idx) const
{
    const Vec3f& p1 = m_vertices[m_triangles[facet_idx].verts_idxs[0]].v;
    const Vec3f& p2 = m_vertices[m_triangles[facet_idx].verts_idxs[1]].v;
    const Vec3f& p3 = m_vertices[m_triangles[facet_idx].verts_idxs[2]].v;
    return m_cursor.is_pointer_in_triangle(p1, p2, p3);
}



// Determine whether this facet is potentially visible (still can be obscured).
bool TriangleSelector::faces_camera(int facet) const
{
    assert(facet < m_orig_size_indices);
    // The normal is cached in mesh->stl, use it.
    Vec3f normal = m_mesh->stl.facet_start[facet].normal;

    if (! m_cursor.uniform_scaling) {
        // Transform the normal into world coords.
        normal = m_cursor.trafo_normal * normal;
    }
    return (normal.dot(m_cursor.dir) < 0.);
}


// How many vertices of a triangle are inside the circle?
int TriangleSelector::vertices_inside(int facet_idx) const
{
    int inside = 0;
    for (size_t i=0; i<3; ++i) {
        if (m_cursor.is_mesh_point_inside(m_vertices[m_triangles[facet_idx].verts_idxs[i]].v))
            ++inside;
    }
    return inside;
}


// Is edge inside cursor?
bool TriangleSelector::is_edge_inside_cursor(int facet_idx) const
{
    std::array<Vec3f, 3> pts;
    for (int i=0; i<3; ++i) {
        pts[i] = m_vertices[m_triangles[facet_idx].verts_idxs[i]].v;
        if (! m_cursor.uniform_scaling)
            pts[i] = m_cursor.trafo * pts[i];
    }

    const Vec3f& p = m_cursor.center;

    for (int side = 0; side < 3; ++side) {
        const Vec3f& a = pts[side];
        const Vec3f& b = pts[side<2 ? side+1 : 0];
        Vec3f s = (b-a).normalized();
        float t = (p-a).dot(s);
        Vec3f vector = a+t*s - p;

        // vector is 3D vector from center to the intersection. What we want to
        // measure is length of its projection onto plane perpendicular to dir.
        float dist_sqr = vector.squaredNorm() - std::pow(vector.dot(m_cursor.dir), 2.f);
        if (dist_sqr < m_cursor.radius_sqr && t>=0.f && t<=(b-a).norm())
            return true;
    }
    return false;
}



// Recursively remove all subtriangles.
void TriangleSelector::undivide_triangle(int facet_idx)
{
    assert(facet_idx < int(m_triangles.size()));
    Triangle& tr = m_triangles[facet_idx];

    if (tr.is_split()) {
        for (int i=0; i<=tr.number_of_split_sides(); ++i) {
            int       child    = tr.children[i];
            Triangle &child_tr = m_triangles[child];
            assert(child_tr.valid());
            undivide_triangle(child);
            for (int i = 0; i < 3; ++ i) {
                int     iv = child_tr.verts_idxs[i];
                Vertex &v  = m_vertices[iv];
                assert(v.ref_cnt > 0);
                if (-- v.ref_cnt == 0) {
                    // Release this vertex.
                    // Chain released vertices into a linked list through ref_cnt.
                    assert(m_free_vertices_head >= -1 && m_free_vertices_head < int(m_vertices.size()));
                    memcpy(&m_vertices[iv].v[0], &m_free_vertices_head, sizeof(m_free_vertices_head));
                    m_free_vertices_head = iv;
                    assert(m_free_vertices_head >= -1 && m_free_vertices_head < int(m_vertices.size()));
                }
            }
            // Chain released triangles into a linked list through children[0].
            assert(child_tr.valid());
            child_tr.m_valid = false;
            assert(m_free_triangles_head >= -1 && m_free_triangles_head < int(m_triangles.size()));
            assert(m_free_triangles_head == -1 || ! m_triangles[m_free_triangles_head].valid());
            child_tr.children[0] = m_free_triangles_head;
            m_free_triangles_head = child;
            assert(m_free_triangles_head >= -1 && m_free_triangles_head < int(m_triangles.size()));
            ++m_invalid_triangles;
        }
        tr.set_division(0, 0); // not split
    }
}


void TriangleSelector::remove_useless_children(int facet_idx)
{
    // Check that all children are leafs of the same type. If not, try to
    // make them (recursive call). Remove them if sucessful.

    assert(facet_idx < int(m_triangles.size()) && m_triangles[facet_idx].valid());
    Triangle& tr = m_triangles[facet_idx];

    if (! tr.is_split()) {
        // This is a leaf, there nothing to do. This can happen during the
        // first (non-recursive call). Shouldn't otherwise.
        return;
    }

    // Call this for all non-leaf children.
    for (int child_idx=0; child_idx<=tr.number_of_split_sides(); ++child_idx) {
        assert(child_idx < int(m_triangles.size()) && m_triangles[child_idx].valid());
        if (m_triangles[tr.children[child_idx]].is_split())
            remove_useless_children(tr.children[child_idx]);
    }


    // Return if a child is not leaf or two children differ in type.
    EnforcerBlockerType first_child_type = EnforcerBlockerType::NONE;
    for (int child_idx=0; child_idx<=tr.number_of_split_sides(); ++child_idx) {
        if (m_triangles[tr.children[child_idx]].is_split())
            return;
        if (child_idx == 0)
            first_child_type = m_triangles[tr.children[0]].get_state();
        else if (m_triangles[tr.children[child_idx]].get_state() != first_child_type)
            return;
    }

    // If we got here, the children can be removed.
    undivide_triangle(facet_idx);
    tr.set_state(first_child_type);
}



void TriangleSelector::garbage_collect()
{
    // First make a map from old to new triangle indices.
    int new_idx = m_orig_size_indices;
    std::vector<int> new_triangle_indices(m_triangles.size(), -1);
    for (int i = m_orig_size_indices; i<int(m_triangles.size()); ++i)
        if (m_triangles[i].valid())
            new_triangle_indices[i] = new_idx ++;

    // Now we know which vertices are not referenced anymore. Make a map
    // from old idxs to new ones, like we did for triangles.
    new_idx = m_orig_size_vertices;
    std::vector<int> new_vertices_indices(m_vertices.size(), -1);
    for (int i=m_orig_size_vertices; i<int(m_vertices.size()); ++i) {
        assert(m_vertices[i].ref_cnt >= 0);
        if (m_vertices[i].ref_cnt != 0)
            new_vertices_indices[i] = new_idx ++;
    }

    // We can remove all invalid triangles and vertices that are no longer referenced.
    m_triangles.erase(std::remove_if(m_triangles.begin()+m_orig_size_indices, m_triangles.end(),
                          [](const Triangle& tr) { return ! tr.valid(); }),
                      m_triangles.end());
    m_vertices.erase(std::remove_if(m_vertices.begin()+m_orig_size_vertices, m_vertices.end(),
                          [](const Vertex& vert) { return vert.ref_cnt == 0; }),
                      m_vertices.end());

    // Now go through all remaining triangles and update changed indices.
    for (Triangle& tr : m_triangles) {
        assert(tr.valid());

        if (tr.is_split()) {
            // There are children. Update their indices.
            for (int j=0; j<=tr.number_of_split_sides(); ++j) {
                assert(new_triangle_indices[tr.children[j]] != -1);
                tr.children[j] = new_triangle_indices[tr.children[j]];
            }
        }

        // Update indices into m_vertices. The original vertices are never
        // touched and need not be reindexed.
        for (int& idx : tr.verts_idxs) {
            if (idx >= m_orig_size_vertices) {
                assert(new_vertices_indices[idx] != -1);
                idx = new_vertices_indices[idx];
            }
        }
    }

    m_invalid_triangles = 0;
    m_free_triangles_head = -1;
    m_free_vertices_head = -1;
}

TriangleSelector::TriangleSelector(const TriangleMesh& mesh)
    : m_mesh{&mesh}
{
    reset();
}


void TriangleSelector::reset()
{
    m_vertices.clear();
    m_triangles.clear();
    m_invalid_triangles = 0;
    m_free_triangles_head = -1;
    m_free_vertices_head = -1;
    m_vertices.reserve(m_mesh->its.vertices.size());
    for (const stl_vertex& vert : m_mesh->its.vertices)
        m_vertices.emplace_back(vert);
    m_triangles.reserve(m_mesh->its.indices.size());
    for (size_t i=0; i<m_mesh->its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& ind = m_mesh->its.indices[i];
        push_triangle(ind[0], ind[1], ind[2], i);
    }
    m_orig_size_vertices = m_vertices.size();
    m_orig_size_indices  = m_triangles.size();
}





void TriangleSelector::set_edge_limit(float edge_limit)
{
    m_edge_limit_sqr = std::pow(edge_limit, 2.f);
}



int TriangleSelector::push_triangle(int a, int b, int c, int source_triangle, const EnforcerBlockerType state)
{
    for (int i : {a, b, c}) {
        assert(i >= 0 && i < int(m_vertices.size()));
        ++m_vertices[i].ref_cnt;
    }
    int idx;
    if (m_free_triangles_head == -1) {
        // Allocate a new triangle.
        assert(m_invalid_triangles == 0);
        idx = int(m_triangles.size());
        m_triangles.emplace_back(a, b, c, source_triangle, state);
    } else {
        // Reuse triangle from the free list.
        assert(m_free_triangles_head >= -1 && m_free_triangles_head < int(m_triangles.size()));
        assert(! m_triangles[m_free_triangles_head].valid());
        assert(m_invalid_triangles > 0);
        idx = m_free_triangles_head;
        m_free_triangles_head = m_triangles[idx].children[0];
        -- m_invalid_triangles;
        assert(m_free_triangles_head >= -1 && m_free_triangles_head < int(m_triangles.size()));
        assert(m_free_triangles_head == -1 || ! m_triangles[m_free_triangles_head].valid());
        assert(m_invalid_triangles >= 0);
        assert((m_invalid_triangles == 0) == (m_free_triangles_head == -1));
        m_triangles[idx] = {a, b, c, source_triangle, state};
    }
    assert(m_triangles[idx].valid());
    return idx;
}

// called by deserialize() and select_patch()->select_triangle()->...select_triangle()->split_triangle()
// Split a triangle based on Triangle::number_of_split_sides() and Triangle::special_side()
// by allocating child triangles and midpoint vertices.
// Midpoint vertices are possibly reused by traversing children of neighbor triangles.
void TriangleSelector::perform_split(int facet_idx, const Vec3i &neighbors, EnforcerBlockerType old_state)
{
    // Reserve space for the new triangles upfront, so that the reference to this triangle will not change.
    m_triangles.reserve(m_triangles.size() + m_triangles[facet_idx].number_of_split_sides() + 1);

    Triangle &tr = m_triangles[facet_idx];
    assert(tr.is_split());

    // indices of triangle vertices
#ifdef NDEBUG
    boost::container::small_vector<int, 6> verts_idxs;
#else // NDEBUG
    // For easier debugging.
    std::vector<int> verts_idxs;
    verts_idxs.reserve(6);
#endif // NDEBUG
    for (int j=0, idx = tr.special_side(); j<3; ++j, idx = next_idx_modulo(idx, 3))
        verts_idxs.push_back(tr.verts_idxs[idx]);

    auto get_alloc_vertex = [this, &neighbors, &verts_idxs](int edge, int i1, int i2) -> int {
        return this->triangle_midpoint_or_allocate(neighbors(edge), verts_idxs[i1], verts_idxs[i2]);
    };

    int ichild = 0;
    switch (tr.number_of_split_sides()) {
    case 1:
        verts_idxs.insert(verts_idxs.begin()+2, get_alloc_vertex(next_idx_modulo(tr.special_side(), 3), 2, 1));
        tr.children[ichild ++] = push_triangle(verts_idxs[0], verts_idxs[1], verts_idxs[2], tr.source_triangle, old_state);
        tr.children[ichild   ] = push_triangle(verts_idxs[2], verts_idxs[3], verts_idxs[0], tr.source_triangle, old_state);
        break;

    case 2:
        verts_idxs.insert(verts_idxs.begin()+1, get_alloc_vertex(tr.special_side(), 1, 0));
        verts_idxs.insert(verts_idxs.begin()+4, get_alloc_vertex(prev_idx_modulo(tr.special_side(), 3), 0, 3));
        tr.children[ichild ++] = push_triangle(verts_idxs[0], verts_idxs[1], verts_idxs[4], tr.source_triangle, old_state);
        tr.children[ichild ++] = push_triangle(verts_idxs[1], verts_idxs[2], verts_idxs[4], tr.source_triangle, old_state);
        tr.children[ichild   ] = push_triangle(verts_idxs[2], verts_idxs[3], verts_idxs[4], tr.source_triangle, old_state);
        break;

    case 3:
        assert(tr.special_side() == 0);
        verts_idxs.insert(verts_idxs.begin()+1, get_alloc_vertex(0, 1, 0));
        verts_idxs.insert(verts_idxs.begin()+3, get_alloc_vertex(1, 3, 2));
        verts_idxs.insert(verts_idxs.begin()+5, get_alloc_vertex(2, 0, 4));
        tr.children[ichild ++] = push_triangle(verts_idxs[0], verts_idxs[1], verts_idxs[5], tr.source_triangle, old_state);
        tr.children[ichild ++] = push_triangle(verts_idxs[1], verts_idxs[2], verts_idxs[3], tr.source_triangle, old_state);
        tr.children[ichild ++] = push_triangle(verts_idxs[3], verts_idxs[4], verts_idxs[5], tr.source_triangle, old_state);
        tr.children[ichild   ] = push_triangle(verts_idxs[1], verts_idxs[3], verts_idxs[5], tr.source_triangle, old_state);
        break;

    default:
        break;
    }

#ifndef NDEBUG
    assert(this->verify_triangle_neighbors(tr, neighbors));
    for (int i = 0; i <= tr.number_of_split_sides(); ++i) {
        Vec3i n = this->child_neighbors(tr, neighbors, i);
        assert(this->verify_triangle_neighbors(m_triangles[tr.children[i]], n));
    }
#endif // NDEBUG
}

bool TriangleSelector::has_facets(EnforcerBlockerType state) const
{
    for (const Triangle& tr : m_triangles)
        if (tr.valid() && ! tr.is_split() && tr.get_state() == state)
            return true;
    return false;
}

int TriangleSelector::num_facets(EnforcerBlockerType state) const
{
    int cnt = 0;
    for (const Triangle& tr : m_triangles)
        if (tr.valid() && ! tr.is_split() && tr.get_state() == state)
            ++ cnt;
    return cnt;
}

indexed_triangle_set TriangleSelector::get_facets(EnforcerBlockerType state) const
{
    indexed_triangle_set out;
    std::vector<int> vertex_map(m_vertices.size(), -1);
    for (const Triangle& tr : m_triangles) {
        if (tr.valid() && ! tr.is_split() && tr.get_state() == state) {
            stl_triangle_vertex_indices indices;
            for (int i=0; i<3; ++i) {
                int j = tr.verts_idxs[i];
                if (vertex_map[j] == -1) {
                    vertex_map[j] = int(out.vertices.size());
                    out.vertices.emplace_back(m_vertices[j].v);
                }
                indices[i] = vertex_map[j];
            }
            out.indices.emplace_back(indices);
        }
    }
    return out;
}

indexed_triangle_set TriangleSelector::get_facets_strict(EnforcerBlockerType state) const
{
    indexed_triangle_set out;

    size_t num_vertices = 0;
    for (const Vertex &v : m_vertices)
        if (v.ref_cnt > 0)
            ++ num_vertices;
    out.vertices.reserve(num_vertices);
    std::vector<int> vertex_map(m_vertices.size(), -1);
    for (int i = 0; i < m_vertices.size(); ++ i)
        if (const Vertex &v = m_vertices[i]; v.ref_cnt > 0) {
            vertex_map[i] = int(out.vertices.size());
            out.vertices.emplace_back(v.v);
        }

    for (int itriangle = 0; itriangle < m_orig_size_indices; ++ itriangle)
        this->get_facets_strict_recursive(m_triangles[itriangle], root_neighbors(*m_mesh, itriangle), state, out.indices);

    for (auto &triangle : out.indices)
        for (int i = 0; i < 3; ++ i)
            triangle(i) = vertex_map[triangle(i)];

    return out;
}

void TriangleSelector::get_facets_strict_recursive(
    const Triangle                              &tr,
    const Vec3i                                 &neighbors,
    EnforcerBlockerType                          state,
    std::vector<stl_triangle_vertex_indices>    &out_triangles) const
{
    if (tr.is_split()) {
        for (int i = 0; i <= tr.number_of_split_sides(); ++ i)
            this->get_facets_strict_recursive(
                m_triangles[tr.children[i]],
                this->child_neighbors(tr, neighbors, i),
                state, out_triangles);
    } else if (tr.get_state() == state)
        this->get_facets_split_by_tjoints({tr.verts_idxs[0], tr.verts_idxs[1], tr.verts_idxs[2]}, neighbors, out_triangles);
}

void TriangleSelector::get_facets_split_by_tjoints(const Vec3i vertices, const Vec3i neighbors, std::vector<stl_triangle_vertex_indices> &out_triangles) const
{
// Export this triangle, but first collect the T-joint vertices along its edges.
    Vec3i midpoints(
        this->triangle_midpoint(neighbors(0), vertices(1), vertices(0)),
        this->triangle_midpoint(neighbors(1), vertices(2), vertices(1)),
        this->triangle_midpoint(neighbors(2), vertices(0), vertices(2)));
    int splits = (midpoints(0) != -1) + (midpoints(1) != -1) + (midpoints(2) != -1);
    if (splits == 0) {
        // Just emit this triangle.
        out_triangles.emplace_back(vertices(0), midpoints(0), midpoints(2));
    } else if (splits == 1) {
        // Split to two triangles
        int i = midpoints(0) != -1 ? 2 : midpoints(1) != -1 ? 0 : 1;
        int j = next_idx_modulo(i, 3);
        int k = next_idx_modulo(j, 3);
        this->get_facets_split_by_tjoints(
            { vertices(i), vertices(j), midpoints(j) },
            { neighbors(i),
              this->neighbor_child(neighbors(j), vertices(j), vertices(k), Partition::Second),
              -1 },
              out_triangles);
        this->get_facets_split_by_tjoints(
            { midpoints(j), vertices(j), vertices(k) },
            { this->neighbor_child(neighbors(j), vertices(j), vertices(k), Partition::First),
              neighbors(k),
              -1 },
              out_triangles);
    } else if (splits == 2) {
        // Split to three triangles.
        int i = midpoints(0) == -1 ? 2 : midpoints(1) == -1 ? 0 : 1;
        int j = next_idx_modulo(i, 3);
        int k = next_idx_modulo(j, 3);
        this->get_facets_split_by_tjoints(
            { vertices(i), midpoints(i), midpoints(k) },
            { this->neighbor_child(neighbors(i), vertices(j), vertices(i), Partition::Second),
              -1,
              this->neighbor_child(neighbors(k), vertices(i), vertices(k), Partition::First) },
              out_triangles);
        this->get_facets_split_by_tjoints(
            { midpoints(i), vertices(j), midpoints(k) },
            { this->neighbor_child(neighbors(i), vertices(j), vertices(i), Partition::First),
              -1, -1 },
              out_triangles);
        this->get_facets_split_by_tjoints(
            { vertices(j), vertices(k), midpoints(k) },
            { neighbors(j),
              this->neighbor_child(neighbors(k), vertices(i), vertices(k), Partition::Second),
              -1 },
              out_triangles);
    } else if (splits == 4) {
        // Split to 4 triangles.
        this->get_facets_split_by_tjoints(
            { vertices(0), midpoints(0), midpoints(2) },
            { this->neighbor_child(neighbors(0), vertices(1), vertices(0), Partition::Second),
              -1, 
              this->neighbor_child(neighbors(2), vertices(0), vertices(2), Partition::First) },
              out_triangles);
        this->get_facets_split_by_tjoints(
            { midpoints(0), vertices(1), midpoints(1) },
            { this->neighbor_child(neighbors(0), vertices(1), vertices(0), Partition::First),
              this->neighbor_child(neighbors(1), vertices(2), vertices(1), Partition::Second),
              -1 },
              out_triangles);
        this->get_facets_split_by_tjoints(
            { midpoints(1), vertices(2), midpoints(2) },
            { this->neighbor_child(neighbors(1), vertices(2), vertices(1), Partition::First),
              this->neighbor_child(neighbors(2), vertices(0), vertices(2), Partition::Second),
              -1 },
              out_triangles);
        out_triangles.emplace_back(midpoints);
    }
}

std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> TriangleSelector::serialize() const
{
    // Each original triangle of the mesh is assigned a number encoding its state
    // or how it is split. Each triangle is encoded by 4 bits (xxyy) or 8 bits (zzzzxxyy):
    // leaf triangle: xx = EnforcerBlockerType (Only values 0, 1, and 2. Value 3 is used as an indicator for additional 4 bits.), yy = 0
    // leaf triangle: xx = 0b11, yy = 0b00, zzzz = EnforcerBlockerType (subtracted by 3)
    // non-leaf:      xx = special side, yy = number of split sides
    // These are bitwise appended and formed into one 64-bit integer.

    // The function returns a map from original triangle indices to
    // stream of bits encoding state and offsprings.

    // Using an explicit function object to support recursive call of Serializer::serialize().
    // This is cheaper than the previous implementation using a recursive call of type erased std::function.
    // (std::function calls using a pointer, while this implementation calls directly).
    struct Serializer {
        const TriangleSelector* triangle_selector;
        std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> data;

        void serialize(int facet_idx) {
            const Triangle& tr = triangle_selector->m_triangles[facet_idx];

            // Always save number of split sides. It is zero for unsplit triangles.
            int split_sides = tr.number_of_split_sides();
            assert(split_sides >= 0 && split_sides <= 3);

            data.second.push_back(split_sides & 0b01);
            data.second.push_back(split_sides & 0b10);

            if (split_sides) {
                // If this triangle is split, save which side is split (in case
                // of one split) or kept (in case of two splits). The value will
                // be ignored for 3-side split.
                assert(tr.is_split() && split_sides > 0);
                assert(tr.special_side() >= 0 && tr.special_side() <= 3);
                data.second.push_back(tr.special_side() & 0b01);
                data.second.push_back(tr.special_side() & 0b10);
                // Now save all children.
                // Serialized in reverse order for compatibility with PrusaSlicer 2.3.1.
                for (int child_idx = split_sides; child_idx >= 0; -- child_idx)
                    this->serialize(tr.children[child_idx]);
            } else {
                // In case this is leaf, we better save information about its state.
                int n = int(tr.get_state());
                if (n >= 3) {
                    assert(n <= 16);
                    if (n <= 16) {
                        // Store "11" plus 4 bits of (n-3).
                        data.second.insert(data.second.end(), { true, true });
                        n -= 3;
                        for (size_t bit_idx = 0; bit_idx < 4; ++bit_idx)
                            data.second.push_back(n & (uint64_t(0b0001) << bit_idx));
                    }
                } else {
                    // Simple case, compatible with PrusaSlicer 2.3.1 and older for storing paint on supports and seams.
                    // Store 2 bits of n.
                    data.second.push_back(n & 0b01);
                    data.second.push_back(n & 0b10);
                }
            }
        }
    } out { this };

    out.data.first.reserve(m_orig_size_indices);
    for (int i=0; i<m_orig_size_indices; ++i)
        if (const Triangle& tr = m_triangles[i]; tr.is_split() || tr.get_state() != EnforcerBlockerType::NONE) {
            // Store index of the first bit assigned to ith triangle.
            out.data.first.emplace_back(i, int(out.data.second.size()));
            // out the triangle bits.
            out.serialize(i);
        }

    // May be stored onto Undo / Redo stack, thus conserve memory.
    out.data.first.shrink_to_fit();
    out.data.second.shrink_to_fit();
    return out.data;
}

void TriangleSelector::deserialize(const std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> &data)
{
    reset(); // dump any current state

    // Vector to store all parents that have offsprings.
    struct ProcessingInfo {
        int facet_id = 0;
        Vec3i neighbors { -1, -1, -1 };
        int processed_children = 0;
        int total_children = 0;
    };
    // Depth-first queue of a source mesh triangle and its childern.
    // kept outside of the loop to avoid re-allocating inside the loop.
    std::vector<ProcessingInfo> parents;

    for (auto [triangle_id, ibit] : data.first) {
        assert(triangle_id < int(m_triangles.size()));
        assert(ibit < data.second.size());
        auto next_nibble = [&data, &ibit = ibit]() {
            int n = 0;
            for (int i = 0; i < 4; ++ i)
                n |= data.second[ibit ++] << i;
            return n;
        };

        parents.clear();
        while (true) {
            // Read next triangle info.
            int code = next_nibble();
            int num_of_split_sides = code & 0b11;
            int num_of_children = num_of_split_sides == 0 ? 0 : num_of_split_sides + 1;
            bool is_split = num_of_children != 0;
            // Only valid if not is_split. Value of the second nibble was subtracted by 3, so it is added back.
            auto state = is_split ? EnforcerBlockerType::NONE : EnforcerBlockerType((code & 0b1100) == 0b1100 ? next_nibble() + 3 : code >> 2);
            // Only valid if is_split.
            int special_side = code >> 2;

            // Take care of the first iteration separately, so handling of the others is simpler.
            if (parents.empty()) {
                if (is_split) {
                    // root is split, add it into list of parents and split it.
                    // then go to the next.
                    Vec3i neighbors = root_neighbors(*m_mesh, triangle_id);
                    parents.push_back({triangle_id, neighbors, 0, num_of_children});
                    m_triangles[triangle_id].set_division(num_of_split_sides, special_side);
                    perform_split(triangle_id, neighbors, EnforcerBlockerType::NONE);
                    continue;
                } else {
                    // root is not split. just set the state and that's it.
                    m_triangles[triangle_id].set_state(state);
                    break;
                }
            }

            // This is not the first iteration. This triangle is a child of last seen parent.
            assert(! parents.empty());
            assert(parents.back().processed_children < parents.back().total_children);

            if (ProcessingInfo& last = parents.back();  is_split) {
                // split the triangle and save it as parent of the next ones.
                const Triangle &tr = m_triangles[last.facet_id];
                int   child_idx = last.total_children - last.processed_children - 1;
                Vec3i neighbors = this->child_neighbors(tr, last.neighbors, child_idx);
                int this_idx = tr.children[child_idx];
                m_triangles[this_idx].set_division(num_of_split_sides, special_side);
                perform_split(this_idx, neighbors, EnforcerBlockerType::NONE);
                parents.push_back({this_idx, neighbors, 0, num_of_children});
            } else {
                // this triangle belongs to last split one
                int child_idx = last.total_children - last.processed_children - 1;
                m_triangles[m_triangles[last.facet_id].children[child_idx]].set_state(state);
                ++last.processed_children;
            }

            // If all children of the past parent triangle are claimed, move to grandparent.
            while (parents.back().processed_children == parents.back().total_children) {
                parents.pop_back();

                if (parents.empty())
                    break;

                // And increment the grandparent children counter, because
                // we have just finished that branch and got back here.
                ++parents.back().processed_children;
            }

            // In case we popped back the root, we should be done.
            if (parents.empty())
                break;
        }
    }
}

// Lightweight variant of deserialization, which only tests whether a face of test_state exists.
bool TriangleSelector::has_facets(const std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> &data, const EnforcerBlockerType test_state)
{
    // Depth-first queue of a number of unvisited children.
    // Kept outside of the loop to avoid re-allocating inside the loop.
    std::vector<int> parents_children;
    parents_children.reserve(64);

    for (auto [triangle_id, ibit] : data.first) {
        assert(ibit < data.second.size());
        auto next_nibble = [&data, &ibit = ibit]() {
            int n = 0;
            for (int i = 0; i < 4; ++ i)
                n |= data.second[ibit ++] << i;
            return n;
        };
        // < 0 -> negative of a number of children
        // >= 0 -> state
        auto num_children_or_state = [&next_nibble]() -> int {
            int code               = next_nibble();
            int num_of_split_sides = code & 0b11;
            return num_of_split_sides == 0 ?
                ((code & 0b1100) == 0b1100 ? next_nibble() + 3 : code >> 2) :
                - num_of_split_sides - 1;
        };

        int state = num_children_or_state();
        if (state < 0) {
            // Root is split.
            parents_children.clear();
            parents_children.emplace_back(- state);
            do {
                if (-- parents_children.back() >= 0) {
                    int state = num_children_or_state();
                    if (state < 0)
                        // Child is split.
                        parents_children.emplace_back(- state);
                    else if (state == int(test_state))
                        // Child is not split and a face of test_state was found.
                        return true;
                } else
                    parents_children.pop_back();
            } while (! parents_children.empty());
        } else if (state == int(test_state))
            // Root is not split and a face of test_state was found.
            return true;
    }

    return false;
}

void TriangleSelector::seed_fill_unselect_all_triangles()
{
    for (Triangle &triangle : m_triangles)
        if (!triangle.is_split())
            triangle.unselect_by_seed_fill();
}

void TriangleSelector::seed_fill_apply_on_triangles(EnforcerBlockerType new_state)
{
    for (Triangle &triangle : m_triangles)
        if (!triangle.is_split() && triangle.is_selected_by_seed_fill())
            triangle.set_state(new_state);

    for (Triangle &triangle : m_triangles)
        if (triangle.is_split() && triangle.valid()) {
            size_t facet_idx = &triangle - &m_triangles.front();
            remove_useless_children(facet_idx);
        }
}

TriangleSelector::Cursor::Cursor(
        const Vec3f& center_, const Vec3f& source_, float radius_world,
        CursorType type_, const Transform3d& trafo_)
    : center{center_},
      source{source_},
      type{type_},
      trafo{trafo_.cast<float>()}
{
    Vec3d sf = Geometry::Transformation(trafo_).get_scaling_factor();
    if (is_approx(sf(0), sf(1)) && is_approx(sf(1), sf(2))) {
        radius_sqr = std::pow(radius_world / sf(0), 2);
        uniform_scaling = true;
    }
    else {
        // In case that the transformation is non-uniform, all checks whether
        // something is inside the cursor should be done in world coords.
        // First transform center, source and dir in world coords and remember
        // that we did this.
        center = trafo * center;
        source = trafo * source;
        uniform_scaling = false;
        radius_sqr = radius_world * radius_world;
        trafo_normal = trafo.linear().inverse().transpose();
    }

    // Calculate dir, in whatever coords is appropriate.
    dir = (center - source).normalized();
}


// Is a point (in mesh coords) inside a cursor?
bool TriangleSelector::Cursor::is_mesh_point_inside(Vec3f point) const
{
    if (! uniform_scaling)
        point = trafo * point;

     Vec3f diff = center - point;
     return (type == CIRCLE ?
                (diff - diff.dot(dir) * dir).squaredNorm() :
                 diff.squaredNorm())
            < radius_sqr;
}



// p1, p2, p3 are in mesh coords!
bool TriangleSelector::Cursor::is_pointer_in_triangle(const Vec3f& p1_,
                                                      const Vec3f& p2_,
                                                      const Vec3f& p3_) const
{
    const Vec3f& q1 = center + dir;
    const Vec3f& q2 = center - dir;

    auto signed_volume_sign = [](const Vec3f& a, const Vec3f& b,
                                 const Vec3f& c, const Vec3f& d) -> bool {
        return ((b-a).cross(c-a)).dot(d-a) > 0.;
    };

    // In case the object is non-uniformly scaled, do the check in world coords.
    const Vec3f& p1 = uniform_scaling ? p1_ : Vec3f(trafo * p1_);
    const Vec3f& p2 = uniform_scaling ? p2_ : Vec3f(trafo * p2_);
    const Vec3f& p3 = uniform_scaling ? p3_ : Vec3f(trafo * p3_);

    if (signed_volume_sign(q1,p1,p2,p3) == signed_volume_sign(q2,p1,p2,p3))
        return false;

    bool pos = signed_volume_sign(q1,q2,p1,p2);
    return signed_volume_sign(q1,q2,p2,p3) == pos && signed_volume_sign(q1,q2,p3,p1) == pos;
}

} // namespace Slic3r
