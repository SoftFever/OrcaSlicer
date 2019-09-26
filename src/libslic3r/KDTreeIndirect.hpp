// KD tree built upon external data set, referencing the external data by integer indices.

#ifndef slic3r_KDTreeIndirect_hpp_
#define slic3r_KDTreeIndirect_hpp_

#include <algorithm>
#include <limits>
#include <vector>

#include "Utils.hpp" // for next_highest_power_of_2()

namespace Slic3r {

// KD tree for N-dimensional closest point search.
template<size_t ANumDimensions, typename ACoordType, typename ACoordinateFn>
class KDTreeIndirect
{
public:
	static constexpr size_t NumDimensions = ANumDimensions;
	using					CoordinateFn  = ACoordinateFn;
	using					CoordType     = ACoordType;
	static constexpr size_t npos		  = size_t(-1);

	KDTreeIndirect(CoordinateFn coordinate) : coordinate(coordinate) {}
	KDTreeIndirect(CoordinateFn coordinate, std::vector<size_t>   indices) : coordinate(coordinate) { this->build(std::move(indices)); }
	KDTreeIndirect(CoordinateFn coordinate, std::vector<size_t> &&indices) : coordinate(coordinate) { this->build(std::move(indices)); }
	KDTreeIndirect(CoordinateFn coordinate, size_t num_indices) : coordinate(coordinate) { this->build(num_indices); }
	KDTreeIndirect(KDTreeIndirect &&rhs) : m_nodes(std::move(rhs.m_nodes)), coordinate(std::move(rhs.coordinate)) {}
	KDTreeIndirect& operator=(KDTreeIndirect &&rhs) { m_nodes = std::move(rhs.m_nodes); coordinate = std::move(rhs.coordinate); return *this; }
	void clear() { m_nodes.clear(); }

	void build(size_t num_indices)
	{
		std::vector<size_t> indices;
		indices.reserve(num_indices);
		for (size_t i = 0; i < num_indices; ++ i)
			indices.emplace_back(i);
		this->build(std::move(indices));
	}

	void build(std::vector<size_t> &&indices)
	{
		if (indices.empty())
			clear();
		else {
			// Allocate a next highest power of 2 nodes, because the incomplete binary tree will not have the leaves filled strictly from the left.
			m_nodes.assign(next_highest_power_of_2(indices.size() + 1), npos);
			build_recursive(indices, 0, 0, 0, (int)(indices.size() - 1));
		}
		indices.clear();
	}

	enum class VisitorReturnMask : unsigned int
	{
		CONTINUE_LEFT   = 1,
		CONTINUE_RIGHT  = 2,
		STOP 			= 4,
	};
	template<typename CoordType> 
	unsigned int descent_mask(const CoordType &point_coord, const CoordType &search_radius, size_t idx, size_t dimension) const
	{
		CoordType dist = point_coord - this->coordinate(idx, dimension);
		return (dist * dist < search_radius + CoordType(EPSILON)) ?
			((unsigned int)(VisitorReturnMask::CONTINUE_LEFT) | (unsigned int)(VisitorReturnMask::CONTINUE_RIGHT)) :
			(dist < CoordType(0)) ? (unsigned int)(VisitorReturnMask::CONTINUE_RIGHT) : (unsigned int)(VisitorReturnMask::CONTINUE_LEFT);
	}

	// Visitor is supposed to return a bit mask of VisitorReturnMask.
	template<typename Visitor>
	void visit(Visitor &visitor) const
	{
		return m_nodes.empty() ? npos : visit_recursive(0, 0, visitor);
	}

	CoordinateFn coordinate;

private:
	// Build a balanced tree by splitting the input sequence by an axis aligned plane at a dimension.
	void build_recursive(std::vector<size_t> &input, size_t node, int dimension, int left, int right)
	{
		if (left > right)
			return;

		assert(node < m_nodes.size());

		if (left == right) {
			// Insert a node into the balanced tree.
			m_nodes[node] = input[left];
			return;
		}

		// Partition the input sequence to two equal halves.
		int center = (left + right) >> 1;
		partition_input(input, dimension, left, right, center);
		// Insert a node into the tree.
		m_nodes[node] = input[center];
		// Partition the left and right subtrees.
		size_t next_dimension = (++ dimension == NumDimensions) ? 0 : dimension;
		build_recursive(input, (node << 1) + 1, next_dimension, left,	    center - 1);
		build_recursive(input, (node << 1) + 2, next_dimension, center + 1, right);
	}

	// Partition the input m_nodes <left, right> at k using QuickSelect method.
	// https://en.wikipedia.org/wiki/Quickselect
	void partition_input(std::vector<size_t> &input, int dimension, int left, int right, int k) const
	{
		while (left < right) {
			// Guess the k'th element.
			// Pick the pivot as a median of first, center and last value.
			// Sort first, center and last values.
			int  center       = (left + right) >> 1;
			auto left_value   = this->coordinate(input[left],   dimension);
			auto center_value = this->coordinate(input[center], dimension);
			auto right_value  = this->coordinate(input[right],  dimension);
			if (center_value < left_value) {
				std::swap(input[left], input[center]);
				std::swap(left_value,  center_value);
			}
			if (right_value < left_value) {
				std::swap(input[left], input[right]);
				std::swap(left_value,  right_value);
			}
			if (right_value < center_value) {
				std::swap(input[center], input[right]);
				// No need to do that, result is not used.
				// std::swap(center_value,  right_value);
			}
			// Only two or three values are left and those are sorted already.
			if (left + 3 > right)
				break;
			// left and right items are already at their correct positions.
			// input[left].point[dimension] <= input[center].point[dimension] <= input[right].point[dimension]
			// Move the pivot to the (right - 1) position.
			std::swap(input[center], input[right - 1]);
			// Pivot value.
			double pivot = this->coordinate(input[right - 1],  dimension);
			// Partition the set based on the pivot.
			int i = left;
			int j = right - 1;
			for (;;) {
				// Skip left points that are already at correct positions.
				// Search will certainly stop at position (right - 1), which stores the pivot.
				while (this->coordinate(input[++ i], dimension) < pivot) ;
				// Skip right points that are already at correct positions.
				while (this->coordinate(input[-- j], dimension) > pivot && i < j) ;
				if (i >= j)
					break;
				std::swap(input[i], input[j]);
			}
			// Restore pivot to the center of the sequence.
			std::swap(input[i], input[right]);
			// Which side the kth element is in?
			if (k < i)
				right = i - 1;
			else if (k == i)
				// Sequence is partitioned, kth element is at its place.
				break;
			else
				left = i + 1;
		}
	}

	template<typename Visitor>
	void visit_recursive(size_t node, size_t dimension, Visitor &visitor) const
	{
		assert(! m_nodes.empty());
		if (node >= m_nodes.size() || m_nodes[node] == npos)
			return;

		// Left / right child node index.
		size_t left  = (node << 1) + 1;
		size_t right = left + 1;
		unsigned int mask = visitor(m_nodes[node], dimension);
		if ((mask & (unsigned int)VisitorReturnMask::STOP) == 0) {
			size_t next_dimension = (++ dimension == NumDimensions) ? 0 : dimension;
			if (mask & (unsigned int)VisitorReturnMask::CONTINUE_LEFT)
				visit_recursive(left,  next_dimension, visitor);
			if (mask & (unsigned int)VisitorReturnMask::CONTINUE_RIGHT)
				visit_recursive(right, next_dimension, visitor);
		}
	}

	std::vector<size_t> m_nodes;
};

// Find a closest point using Euclidian metrics.
// Returns npos if not found.
template<typename KDTreeIndirectType, typename PointType, typename FilterFn>
size_t find_closest_point(const KDTreeIndirectType &kdtree, const PointType &point, FilterFn filter)
{
	struct Visitor {
		using CoordType = typename KDTreeIndirectType::CoordType;
		const KDTreeIndirectType   &kdtree;
		const PointType    		   &point;
		const FilterFn				filter;
		size_t 						min_idx  = KDTreeIndirectType::npos;
		CoordType					min_dist = std::numeric_limits<CoordType>::max();

		Visitor(const KDTreeIndirectType &kdtree, const PointType &point, FilterFn filter) : kdtree(kdtree), point(point), filter(filter) {}
		unsigned int operator()(size_t idx, size_t dimension) {
			if (this->filter(idx)) {
				auto dist = CoordType(0);
				for (size_t i = 0; i < KDTreeIndirectType::NumDimensions; ++ i) {
					CoordType d = point[i] - kdtree.coordinate(idx, i);
					dist += d * d;
				}
				if (dist < min_dist) {
					min_dist = dist;
					min_idx  = idx;
				}
			}
			return kdtree.descent_mask(point[dimension], min_dist, idx, dimension);
		}
	} visitor(kdtree, point, filter);

	kdtree.visit(visitor);
	return visitor.min_idx;
}

template<typename KDTreeIndirectType, typename PointType>
size_t find_closest_point(const KDTreeIndirectType& kdtree, const PointType& point)
{
	return find_closest_point(kdtree, point, [](size_t) { return true; });
}

} // namespace Slic3r

#endif /* slic3r_KDTreeIndirect_hpp_ */
