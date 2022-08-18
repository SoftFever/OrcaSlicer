#ifndef SRC_LIBSLIC3R_AABBTREELINES_HPP_
#define SRC_LIBSLIC3R_AABBTREELINES_HPP_

#include "libslic3r/Point.hpp"
#include "libslic3r/EdgeGrid.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/Line.hpp"

namespace Slic3r {

namespace AABBTreeLines {

namespace detail {

template<typename ALineType, typename ATreeType, typename AVectorType>
struct IndexedLinesDistancer {
    using LineType = ALineType;
    using TreeType = ATreeType;
    using VectorType = AVectorType;
    using ScalarType = typename VectorType::Scalar;

    const std::vector<LineType> &lines;
    const TreeType &tree;

    const VectorType origin;

    inline VectorType closest_point_to_origin(size_t primitive_index,
            ScalarType &squared_distance) {
        VectorType nearest_point;
        const LineType &line = lines[primitive_index];
        squared_distance = line_alg::distance_to_squared(line, origin, &nearest_point);
        return nearest_point;
    }
};

}

// Build a balanced AABB Tree over a vector of float lines, balancing the tree
// on centroids of the lines.
// Epsilon is applied to the bounding boxes of the AABB Tree to cope with numeric inaccuracies
// during tree traversal.
template<typename LineType>
inline AABBTreeIndirect::Tree<2, typename LineType::Scalar> build_aabb_tree_over_indexed_lines(
        const std::vector<LineType> &lines,
        //FIXME do we want to apply an epsilon?
        const float eps = 0)
        {
    using TreeType = AABBTreeIndirect::Tree<2, typename LineType::Scalar>;
//    using              CoordType      = typename TreeType::CoordType;
    using VectorType = typename TreeType::VectorType;
    using BoundingBox = typename TreeType::BoundingBox;

    struct InputType {
        size_t idx() const {
            return m_idx;
        }
        const BoundingBox& bbox() const {
            return m_bbox;
        }
        const VectorType& centroid() const {
            return m_centroid;
        }

        size_t m_idx;
        BoundingBox m_bbox;
        VectorType m_centroid;
    };

    std::vector<InputType> input;
    input.reserve(lines.size());
    const VectorType veps(eps, eps);
    for (size_t i = 0; i < lines.size(); ++i) {
        const LineType &line = lines[i];
        InputType n;
        n.m_idx = i;
        n.m_centroid = (line.a + line.b) * 0.5;
        n.m_bbox = BoundingBox(line.a, line.a);
        n.m_bbox.extend(line.b);
        n.m_bbox.min() -= veps;
        n.m_bbox.max() += veps;
        input.emplace_back(n);
    }

    TreeType out;
    out.build(std::move(input));
    return out;
}

// Finding a closest line, its closest point and squared distance to the closest point
// Returns squared distance to the closest point or -1 if the input is empty.
template<typename LineType, typename TreeType, typename VectorType>
inline typename VectorType::Scalar squared_distance_to_indexed_lines(
        const std::vector<LineType> &lines,
        const TreeType &tree,
        const VectorType &point,
        size_t &hit_idx_out,
        Eigen::PlainObjectBase<VectorType> &hit_point_out)
        {
    using Scalar = typename VectorType::Scalar;
    auto distancer = detail::IndexedLinesDistancer<LineType, TreeType, VectorType>
            { lines, tree, point };
    return tree.empty() ?
                          Scalar(-1) :
                          AABBTreeIndirect::detail::squared_distance_to_indexed_primitives_recursive(distancer, size_t(0), Scalar(0),
                                  std::numeric_limits<Scalar>::infinity(), hit_idx_out, hit_point_out);
}

}

}

#endif /* SRC_LIBSLIC3R_AABBTREELINES_HPP_ */
