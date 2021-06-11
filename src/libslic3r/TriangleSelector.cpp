#include "TriangleSelector.hpp"
#include "Model.hpp"

#include <boost/container/small_vector.hpp>

namespace Slic3r {



// sides_to_split==-1 : just restore previous split
void TriangleSelector::Triangle::set_division(int sides_to_split, int special_side_idx)
{
    assert(sides_to_split >=-1 && sides_to_split <= 3);
    assert(special_side_idx >=-1 && special_side_idx < 3);

    // If splitting one or two sides, second argument must be provided.
    assert(sides_to_split != 1 || special_side_idx != -1);
    assert(sides_to_split != 2 || special_side_idx != -1);

    if (sides_to_split == -1) {
        assert(old_number_of_splits != 0);
        this->number_of_splits = old_number_of_splits;
        // indices of children should still be there.
    } else {
        this->number_of_splits = sides_to_split;
        if (sides_to_split != 0) {
            assert(old_number_of_splits == 0);
            this->special_side_idx = special_side_idx;
            this->old_number_of_splits = sides_to_split;
        }
    }
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
            if (select_triangle(facet, new_state, false, triangle_splitting)) {
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
                    assert(neighbor_idx >= 0);
                    if (neighbor_idx >= 0 && !visited[neighbor_idx]) {
                        // Check if neighbour_facet_idx is satisfies angle in seed_fill_angle and append it to facet_queue if it do.
                        double dot_product = m_triangles[neighbor_idx].normal.dot(m_triangles[current_facet].normal);
                        if (std::clamp(dot_product, 0., 1.) >= facet_angle_limit)
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
bool TriangleSelector::select_triangle(int facet_idx, EnforcerBlockerType type, bool recursive_call, bool triangle_splitting)
{
    assert(facet_idx < int(m_triangles.size()));

    Triangle* tr = &m_triangles[facet_idx];
    if (! tr->valid())
        return false;

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
            split_triangle(facet_idx);
        else if (!m_triangles[facet_idx].is_split())
            m_triangles[facet_idx].set_state(type);
        tr = &m_triangles[facet_idx]; // might have been invalidated by split_triangle().


        int num_of_children = tr->number_of_split_sides() + 1;
        if (num_of_children != 1) {
            for (int i=0; i<num_of_children; ++i) {
                assert(i < int(tr->children.size()));
                assert(tr->children[i] < int(m_triangles.size()));

                select_triangle(tr->children[i], type, true, triangle_splitting);
                tr = &m_triangles[facet_idx]; // might have been invalidated
            }
        }
    }

    if (! recursive_call) {
        // In case that all children are leafs and have the same state now,
        // they may be removed and substituted by the parent triangle.
        remove_useless_children(facet_idx);

        // Make sure that we did not lose track of invalid triangles.
        assert(m_invalid_triangles == std::count_if(m_triangles.begin(), m_triangles.end(),
                   [](const Triangle& tr) { return ! tr.valid(); }));

        // Do garbage collection maybe?
        if (2*m_invalid_triangles > int(m_triangles.size()))
            garbage_collect();
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

void TriangleSelector::split_triangle(int facet_idx)
{
    if (m_triangles[facet_idx].is_split()) {
        // The triangle is divided already.
        return;
    }

    Triangle* tr = &m_triangles[facet_idx];

    EnforcerBlockerType old_type = tr->get_state();

    if (tr->was_split_before() != 0) {
        // This triangle is not split at the moment, but was at one point
        // in history. We can just restore it and resurrect its children.
        tr->set_division(-1);
        for (int i=0; i<=tr->number_of_split_sides(); ++i) {
            m_triangles[tr->children[i]].set_state(old_type);
            m_triangles[tr->children[i]].m_valid = true;
            --m_invalid_triangles;
        }
        return;
    }

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
        tr->set_division(0);
        return;
    }

    // Save how the triangle will be split. Second argument makes sense only for one
    // or two split sides, otherwise the value is ignored.
    tr->set_division(sides_to_split.size(),
        sides_to_split.size() == 2 ? side_to_keep : sides_to_split[0]);

    perform_split(facet_idx, old_type);
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
            undivide_triangle(tr.children[i]);
            m_triangles[tr.children[i]].m_valid = false;
            ++m_invalid_triangles;
        }
        tr.set_division(0); // not split
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
    for (int i = m_orig_size_indices; i<int(m_triangles.size()); ++i) {
        if (m_triangles[i].valid()) {
            new_triangle_indices[i] = new_idx ++;
        } else {
            // Decrement reference counter for the vertices.
            for (int j=0; j<3; ++j)
                --m_vertices[m_triangles[i].verts_idxs[j]].ref_cnt;
        }
    }

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

        // If this triangle was split before, forget it.
        // Children referenced in the cache are dead by now.
        tr.forget_history();
    }

    m_invalid_triangles = 0;
}

TriangleSelector::TriangleSelector(const TriangleMesh& mesh)
    : m_mesh{&mesh}
{
    reset();
}


void TriangleSelector::reset(const EnforcerBlockerType reset_state)
{
    m_vertices.clear();
    m_triangles.clear();
    m_vertices.reserve(m_mesh->its.vertices.size());
    for (const stl_vertex& vert : m_mesh->its.vertices)
        m_vertices.emplace_back(vert);
    m_triangles.reserve(m_mesh->its.indices.size());
    for (size_t i=0; i<m_mesh->its.indices.size(); ++i) {
        const stl_triangle_vertex_indices& ind = m_mesh->its.indices[i];
        const Vec3f& normal = m_mesh->stl.facet_start[i].normal;
        push_triangle(ind[0], ind[1], ind[2], normal, reset_state);
    }
    m_orig_size_vertices = m_vertices.size();
    m_orig_size_indices = m_triangles.size();
    m_invalid_triangles = 0;
}





void TriangleSelector::set_edge_limit(float edge_limit)
{
    float new_limit_sqr = std::pow(edge_limit, 2.f);

    if (new_limit_sqr != m_edge_limit_sqr) {
        m_edge_limit_sqr = new_limit_sqr;

        // The way how triangles split may be different now, forget
        // all cached splits.
        for (Triangle& tr : m_triangles)
            tr.forget_history();
    }
}



void TriangleSelector::push_triangle(int a, int b, int c, const Vec3f& normal, const EnforcerBlockerType state)
{
    for (int i : {a, b, c}) {
        assert(i >= 0 && i < int(m_vertices.size()));
        ++m_vertices[i].ref_cnt;
    }
    m_triangles.emplace_back(a, b, c, normal, state);
}


void TriangleSelector::perform_split(int facet_idx, EnforcerBlockerType old_state)
{
    Triangle* tr = &m_triangles[facet_idx];
    const Vec3f normal = tr->normal;

    assert(tr->is_split());

    // Read info about how to split this triangle.
    int sides_to_split = tr->number_of_split_sides();

    // indices of triangle vertices
    std::vector<int> verts_idxs;
    int idx = tr->special_side();
    for (int j=0; j<3; ++j) {
        verts_idxs.push_back(tr->verts_idxs[idx++]);
        if (idx == 3)
            idx = 0;
    }

    switch (sides_to_split) {
    case 1:
        m_vertices.emplace_back((m_vertices[verts_idxs[1]].v + m_vertices[verts_idxs[2]].v)/2.);
        verts_idxs.insert(verts_idxs.begin()+2, m_vertices.size() - 1);

        push_triangle(verts_idxs[0], verts_idxs[1], verts_idxs[2], normal);
        push_triangle(verts_idxs[2], verts_idxs[3], verts_idxs[0], normal);
        break;

    case 2:
        m_vertices.emplace_back((m_vertices[verts_idxs[0]].v + m_vertices[verts_idxs[1]].v)/2.);
        verts_idxs.insert(verts_idxs.begin()+1, m_vertices.size() - 1);

        m_vertices.emplace_back((m_vertices[verts_idxs[0]].v + m_vertices[verts_idxs[3]].v)/2.);
        verts_idxs.insert(verts_idxs.begin()+4, m_vertices.size() - 1);

        push_triangle(verts_idxs[0], verts_idxs[1], verts_idxs[4], normal);
        push_triangle(verts_idxs[1], verts_idxs[2], verts_idxs[4], normal);
        push_triangle(verts_idxs[2], verts_idxs[3], verts_idxs[4], normal);
        break;

    case 3:
        m_vertices.emplace_back((m_vertices[verts_idxs[0]].v + m_vertices[verts_idxs[1]].v)/2.);
        verts_idxs.insert(verts_idxs.begin()+1, m_vertices.size() - 1);
        m_vertices.emplace_back((m_vertices[verts_idxs[2]].v + m_vertices[verts_idxs[3]].v)/2.);
        verts_idxs.insert(verts_idxs.begin()+3, m_vertices.size() - 1);
        m_vertices.emplace_back((m_vertices[verts_idxs[4]].v + m_vertices[verts_idxs[0]].v)/2.);
        verts_idxs.insert(verts_idxs.begin()+5, m_vertices.size() - 1);

        push_triangle(verts_idxs[0], verts_idxs[1], verts_idxs[5], normal);
        push_triangle(verts_idxs[1], verts_idxs[2], verts_idxs[3], normal);
        push_triangle(verts_idxs[3], verts_idxs[4], verts_idxs[5], normal);
        push_triangle(verts_idxs[1], verts_idxs[3], verts_idxs[5], normal);
        break;

    default:
        break;
    }

    // tr may have been invalidated due to reallocation of m_triangles.
    tr = &m_triangles[facet_idx];

    // And save the children. All children should start with the same state as the triangle we just split.
    assert(sides_to_split <= 3);
    for (int i=0; i<=sides_to_split; ++i) {
        tr->children[i] = m_triangles.size()-1-i;
        m_triangles[tr->children[i]].set_state(old_state);
    }
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
                for (int child_idx = 0; child_idx <= split_sides; ++child_idx)
                    this->serialize(tr.children[child_idx]);
            } else {
                // In case this is leaf, we better save information about its state.
                int n = int(tr.get_state());
                if (n >= 3) {
                    assert(n <= 15);
                    if (n <= 15) {
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

void TriangleSelector::deserialize(const std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> &data, const EnforcerBlockerType init_state)
{
    reset(init_state); // dump any current state

    // Vector to store all parents that have offsprings.
    struct ProcessingInfo {
        int facet_id = 0;
        int processed_children = 0;
        int total_children = 0;
    };
    // kept outside of the loop to avoid re-allocating inside the loop.
    std::vector<ProcessingInfo> parents;

    for (auto [triangle_id, ibit] : data.first) {
        assert(triangle_id < int(m_triangles.size()));
        assert(ibit < data.second.size());
        auto next_nibble = [&data, &ibit]() {
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
                    parents.push_back({triangle_id, 0, num_of_children});
                    m_triangles[triangle_id].set_division(num_of_split_sides, special_side);
                    perform_split(triangle_id, EnforcerBlockerType::NONE);
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

            if (ProcessingInfo& last = parents.back(); is_split) {
                // split the triangle and save it as parent of the next ones.
                int this_idx = m_triangles[last.facet_id].children[last.processed_children];
                m_triangles[this_idx].set_division(num_of_split_sides, special_side);
                perform_split(this_idx, EnforcerBlockerType::NONE);
                parents.push_back({this_idx, 0, num_of_children});
            } else {
                // this triangle belongs to last split one
                m_triangles[m_triangles[last.facet_id].children[last.processed_children]].set_state(state);
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
