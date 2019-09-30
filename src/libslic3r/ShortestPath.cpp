#if 0
	#pragma optimize("", off)
	#undef NDEBUG
	#undef assert
#endif

#include "clipper.hpp"
#include "ShortestPath.hpp"
#include "KDTreeIndirect.hpp"
#include "MutablePriorityQueue.hpp"
#include "Print.hpp"

#include <cmath>
#include <cassert>

namespace Slic3r {

// Naive implementation of the Traveling Salesman Problem, it works by always taking the next closest neighbor.
// This implementation will always produce valid result even if some segments cannot reverse.
template<typename EndPointType, typename KDTreeType, typename CouldReverseFunc>
std::vector<std::pair<size_t, bool>> chain_segments_closest_point(std::vector<EndPointType> &end_points, KDTreeType &kdtree, CouldReverseFunc &could_reverse_func, EndPointType &first_point)
{
	assert((end_points.size() & 1) == 0);
	size_t num_segments = end_points.size() / 2;
	assert(num_segments >= 2);
	for (EndPointType &ep : end_points)
		ep.chain_id = 0;
	std::vector<std::pair<size_t, bool>> out;
	out.reserve(num_segments);
	size_t first_point_idx = &first_point - end_points.data();
	out.emplace_back(first_point_idx / 2, (first_point_idx & 1) != 0);
	first_point.chain_id = 1;
	size_t this_idx = first_point_idx ^ 1;
	for (int iter = (int)num_segments - 2; iter >= 0; -- iter) {
		EndPointType &this_point = end_points[this_idx];
    	this_point.chain_id = 1;
    	// Find the closest point to this end_point, which lies on a different extrusion path (filtered by the lambda).
    	// Ignore the starting point as the starting point is considered to be occupied, no end point coud connect to it.
		size_t next_idx = find_closest_point(kdtree, this_point.pos,
			[this_idx, &end_points, &could_reverse_func](size_t idx) {
				return (idx ^ this_idx) > 1 && end_points[idx].chain_id == 0 && ((idx ^ 1) == 0 || could_reverse_func(idx >> 1));
		});
		assert(next_idx < end_points.size());
		EndPointType &end_point = end_points[next_idx];
		end_point.chain_id = 1;
		this_idx = next_idx ^ 1;
	}
#ifndef NDEBUG
	assert(end_points[this_idx].chain_id == 0);
	for (EndPointType &ep : end_points)
		assert(&ep == &end_points[this_idx] || ep.chain_id == 1);
#endif /* NDEBUG */
	return out;
}

// Chain perimeters (always closed) and thin fills (closed or open) using a greedy algorithm.
// Solving a Traveling Salesman Problem (TSP) with the modification, that the sites are not always points, but points and segments.
// Solving using a greedy algorithm, where a shortest edge is added to the solution if it does not produce a bifurcation or a cycle.
// Return index and "reversed" flag.
// https://en.wikipedia.org/wiki/Multi-fragment_algorithm
// The algorithm builds a tour for the traveling salesman one edge at a time and thus maintains multiple tour fragments, each of which 
// is a simple path in the complete graph of cities. At each stage, the algorithm selects the edge of minimal cost that either creates 
// a new fragment, extends one of the existing paths or creates a cycle of length equal to the number of cities.
template<typename PointType, typename SegmentEndPointFunc, bool REVERSE_COULD_FAIL, typename CouldReverseFunc>
std::vector<std::pair<size_t, bool>> chain_segments_greedy_constrained_reversals_(SegmentEndPointFunc end_point_func, CouldReverseFunc could_reverse_func, size_t num_segments, const PointType *start_near)
{
	std::vector<std::pair<size_t, bool>> out;

	if (num_segments == 0) {
		// Nothing to do.
	} 
	else if (num_segments == 1)
	{
		// Just sort the end points so that the first point visited is closest to start_near.
		out.emplace_back(0, start_near != nullptr && 
            (end_point_func(0, true) - *start_near).template cast<double>().squaredNorm() < (end_point_func(0, false) - *start_near).template cast<double>().squaredNorm());
	} 
	else
	{
		// End points of segments for the KD tree closest point search.
		// A single end point is inserted into the search structure for loops, two end points are entered for open paths.
		struct EndPoint {
			EndPoint(const Vec2d &pos) : pos(pos) {}
			Vec2d     pos;
			// Identifier of the chain, to which this end point belongs. Zero means unassigned.
			size_t    chain_id = 0;
			// Link to the closest currently valid end point.
			EndPoint *edge_out = nullptr;
			// Distance to the next end point following the link.
			// Zero value -> start of the final path.
			double    distance_out = std::numeric_limits<double>::max();
			size_t    heap_idx = std::numeric_limits<size_t>::max();
		};
	    std::vector<EndPoint> end_points;
	    end_points.reserve(num_segments * 2);
	    for (size_t i = 0; i < num_segments; ++ i) {
            end_points.emplace_back(end_point_func(i, true ).template cast<double>());
            end_points.emplace_back(end_point_func(i, false).template cast<double>());
	    }

	    // Construct the closest point KD tree over end points of segments.
		auto coordinate_fn = [&end_points](size_t idx, size_t dimension) -> double { return end_points[idx].pos[dimension]; };
		KDTreeIndirect<2, double, decltype(coordinate_fn)> kdtree(coordinate_fn, end_points.size());

		// Helper to detect loops in already connected paths.
		// Unique chain IDs are assigned to paths. If paths are connected, end points will not have their chain IDs updated, but the chain IDs
		// will remember an "equivalent" chain ID, which is the lowest ID of all the IDs in the path, and the lowest ID is equivalent to itself.
		class EquivalentChains {
		public:
			// Zero'th chain ID is invalid.
			EquivalentChains(size_t reserve) { m_equivalent_with.reserve(reserve); m_equivalent_with.emplace_back(0); }
			// Generate next equivalence class.
			size_t 				next() {
				m_equivalent_with.emplace_back(++ m_last_chain_id);
				return m_last_chain_id;
			}
			// Get equivalence class for chain ID.
			size_t 				operator()(size_t chain_id) {
				if (chain_id != 0) {
					for (size_t last = chain_id;;) {
						size_t lower = m_equivalent_with[last];
						if (lower == last) {
							m_equivalent_with[chain_id] = lower;
							chain_id = lower;
							break;
						}
						last = lower;
					}
				}
				return chain_id;
			}
			size_t 			  	merge(size_t chain_id1, size_t chain_id2) {
				size_t chain_id = std::min((*this)(chain_id1), (*this)(chain_id2));
				m_equivalent_with[chain_id1] = chain_id;
				m_equivalent_with[chain_id2] = chain_id;
				return chain_id;
			}

#ifndef NDEBUG
			bool				validate()
			{
				assert(m_last_chain_id >= 0);
				assert(m_last_chain_id + 1 == m_equivalent_with.size());
				for (size_t i = 0; i < m_equivalent_with.size(); ++ i) {
					for (size_t last = i;;) {
						size_t lower = m_equivalent_with[last];
						assert(lower <= last);
						if (lower == last)
							break;
						last = lower;
					}
				}
				return true;
			}
#endif /* NDEBUG */

		private:
			// Unique chain ID assigned to chains of end points of segments.
			size_t              m_last_chain_id = 0;
			std::vector<size_t> m_equivalent_with;
		} equivalent_chain(num_segments);

		// Find the first end point closest to start_near.
		EndPoint *first_point = nullptr;
		size_t    first_point_idx = std::numeric_limits<size_t>::max();
		if (start_near != nullptr) {
            size_t idx = find_closest_point(kdtree, start_near->template cast<double>());
			assert(idx < end_points.size());
			first_point = &end_points[idx];
			first_point->distance_out = 0.;
			first_point->chain_id = equivalent_chain.next();
			first_point_idx = idx;
		}
		EndPoint *initial_point = first_point;
		EndPoint *last_point = nullptr;

		// Assign the closest point and distance to the end points.
		for (EndPoint &end_point : end_points) {
	    	assert(end_point.edge_out == nullptr);
	    	if (&end_point != first_point) {
		    	size_t this_idx = &end_point - &end_points.front();
		    	// Find the closest point to this end_point, which lies on a different extrusion path (filtered by the lambda).
		    	// Ignore the starting point as the starting point is considered to be occupied, no end point coud connect to it.
				size_t next_idx = find_closest_point(kdtree, end_point.pos, 
					[this_idx, first_point_idx](size_t idx){ return idx != first_point_idx && (idx ^ this_idx) > 1; });
				assert(next_idx < end_points.size());
				EndPoint &end_point2 = end_points[next_idx];
				end_point.edge_out = &end_point2;
				end_point.distance_out = (end_point2.pos - end_point.pos).squaredNorm();
			}
		}

	    // Initialize a heap of end points sorted by the lowest distance to the next valid point of a path.
	    auto queue = make_mutable_priority_queue<EndPoint*>(
			[](EndPoint *ep, size_t idx){ ep->heap_idx = idx; }, 
	    	[](EndPoint *l, EndPoint *r){ return l->distance_out < r->distance_out; });
		queue.reserve(end_points.size() * 2 - 1);
	    for (EndPoint &ep : end_points)
	    	if (first_point != &ep)
				queue.push(&ep);

#ifndef NDEBUG
		auto validate_graph_and_queue = [&equivalent_chain, &end_points, &queue, first_point]() -> bool {
			assert(equivalent_chain.validate());
			for (EndPoint &ep : end_points) {
				if (ep.heap_idx < queue.size()) {
					// End point is on the heap.
					assert(*(queue.cbegin() + ep.heap_idx) == &ep);
					assert(ep.chain_id == 0);
				} else {
					// End point is NOT on the heap, therefore it is part of the output path.
					assert(ep.heap_idx == std::numeric_limits<size_t>::max());
					assert(ep.chain_id != 0);
					if (&ep == first_point) {
						assert(ep.edge_out == nullptr);
					} else {
						assert(ep.edge_out != nullptr);
						// Detect loops.
						for (EndPoint *pt = &ep; pt != nullptr;) {
							// Out of queue. It is a final point.
							assert(pt->heap_idx == std::numeric_limits<size_t>::max());
							EndPoint *pt_other = &end_points[(pt - &end_points.front()) ^ 1];
							if (pt_other->heap_idx < queue.size())
								// The other side of this segment is undecided yet.
								break;
							pt = pt_other->edge_out;
						}
					}
				}
			}
			for (EndPoint *ep : queue)
				// Points in the queue are not connected yet.
				assert(ep->chain_id == 0);
			return true;
		};
#endif /* NDEBUG */

	    // Chain the end points: find (num_segments - 1) shortest links not forming bifurcations or loops.
		assert(num_segments >= 2);
		for (int iter = int(num_segments) - 2;; -- iter) {
			assert(validate_graph_and_queue());
	    	// Take the first end point, for which the link points to the currently closest valid neighbor.
	    	EndPoint &end_point1 = *queue.top();
	    	assert(end_point1.edge_out != nullptr);
	    	// No point on the queue may be connected yet.
	    	assert(end_point1.chain_id == 0);
	    	// Take the closest end point to the first end point,
	    	EndPoint &end_point2 = *end_point1.edge_out;
	    	bool valid = true;
	    	size_t end_point1_other_chain_id = 0;
	    	size_t end_point2_other_chain_id = 0;
	    	if (end_point2.chain_id > 0) {
	    		// The other side is part of the output path. Don't connect to end_point2, update end_point1 and try another one.
	    		valid = false;
	    	} else {
				// End points of the opposite ends of the segments.
				end_point1_other_chain_id = equivalent_chain(end_points[(&end_point1 - &end_points.front()) ^ 1].chain_id);
				end_point2_other_chain_id = equivalent_chain(end_points[(&end_point2 - &end_points.front()) ^ 1].chain_id);
				if (end_point1_other_chain_id == end_point2_other_chain_id && end_point1_other_chain_id != 0)
					// This edge forms a loop. Update end_point1 and try another one.
					valid = false;
	    	}
	    	if (valid) {
		    	// Remove the first and second point from the queue.
				queue.pop();
		    	queue.remove(end_point2.heap_idx);
		    	assert(end_point1.edge_out = &end_point2);
		    	end_point2.edge_out = &end_point1;
		    	end_point2.distance_out = end_point1.distance_out;
		    	// Assign chain IDs to the newly connected end points, set equivalent_chain if two chains were merged.
		    	size_t chain_id =
					(end_point1_other_chain_id == 0) ?
						((end_point2_other_chain_id == 0) ? equivalent_chain.next() : end_point2_other_chain_id) :
						((end_point2_other_chain_id == 0) ? end_point1_other_chain_id : 
							(end_point1_other_chain_id == end_point2_other_chain_id) ? 
								end_point1_other_chain_id :
								equivalent_chain.merge(end_point1_other_chain_id, end_point2_other_chain_id));
				end_point1.chain_id = chain_id;
				end_point2.chain_id = chain_id;
				assert(validate_graph_and_queue());
				if (iter == 0) {
					// Last iteration. There shall be exactly one or two end points waiting to be connected.
					assert(queue.size() == ((first_point == nullptr) ? 2 : 1));
					if (first_point == nullptr) {
						first_point = queue.top();
						queue.pop();
						first_point->edge_out = nullptr;
					}
					last_point = queue.top();
					last_point->edge_out = nullptr;
					queue.pop();
					assert(queue.empty());
					break;
				}
	    	} else {
				// This edge forms a loop. Update end_point1 and try another one.
				++ iter;
				end_point1.edge_out = nullptr;
		    	// Update edge_out and distance.
		    	size_t this_idx = &end_point1 - &end_points.front();
		    	// Find the closest point to this end_point, which lies on a different extrusion path (filtered by the filter lambda).
				size_t next_idx = find_closest_point(kdtree, end_point1.pos, [&end_points, &equivalent_chain, this_idx](size_t idx) { 
			    	assert(end_points[this_idx].edge_out == nullptr);
			    	assert(end_points[this_idx].chain_id == 0);
					if ((idx ^ this_idx) <= 1 || end_points[idx].chain_id != 0)
						// Points of the same segment shall not be connected,
						// cannot connect to an already connected point (ideally those would be removed from the KD tree, but the update is difficult).
						return false;
			    	size_t chain1 = equivalent_chain(end_points[this_idx ^ 1].chain_id);
			    	size_t chain2 = equivalent_chain(end_points[idx      ^ 1].chain_id);
			    	return chain1 != chain2 || chain1 == 0;
				});
				assert(next_idx < end_points.size());
				end_point1.edge_out = &end_points[next_idx];
				end_point1.distance_out = (end_points[next_idx].pos - end_point1.pos).squaredNorm();
				// Update position of this end point in the queue based on the distance calculated at the line above.
				queue.update(end_point1.heap_idx);
		    	//FIXME Remove the other end point from the KD tree.
		    	// As the KD tree update is expensive, do it only after some larger number of points is removed from the queue.
				assert(validate_graph_and_queue());
	    	}
		}
		assert(queue.empty());

		// Now interconnect pairs of segments into a chain.
		assert(first_point != nullptr);
		out.reserve(num_segments);
		bool      failed = false;
		do {
			assert(out.size() < num_segments);
			size_t    		 first_point_id = first_point - &end_points.front();
			size_t           segment_id 	= first_point_id >> 1;
			bool             reverse        = (first_point_id & 1) != 0;
			EndPoint 		*second_point   = &end_points[first_point_id ^ 1];
			if (REVERSE_COULD_FAIL) {
				if (reverse && ! could_reverse_func(segment_id)) {
					failed = true;
					break;
				}
			} else {
				assert(! reverse || could_reverse_func(segment_id));
			}
			out.emplace_back(segment_id, reverse);
			first_point = second_point->edge_out;
		} while (first_point != nullptr);
		if (REVERSE_COULD_FAIL) {
			if (failed) {
				if (start_near == nullptr) {
					// We may try the reverse order.
					out.clear();
					first_point = last_point;
					failed = false;
					do {
						assert(out.size() < num_segments);
						size_t    		 first_point_id = first_point - &end_points.front();
						size_t           segment_id 	= first_point_id >> 1;
						bool             reverse        = (first_point_id & 1) != 0;
						EndPoint 		*second_point   = &end_points[first_point_id ^ 1];
						if (reverse && ! could_reverse_func(segment_id)) {
							failed = true;
							break;
						}
						out.emplace_back(segment_id, reverse);
						first_point = second_point->edge_out;
					} while (first_point != nullptr);
				}
			}
			if (failed)
				// As a last resort, try a dumb algorithm, which is not sensitive to edge reversal constraints.
				out = chain_segments_closest_point<EndPoint, decltype(kdtree), CouldReverseFunc>(end_points, kdtree, could_reverse_func, (initial_point != nullptr) ? *initial_point : end_points.front());
		} else {
			assert(! failed);
		}
	}

	assert(out.size() == num_segments);
	return out;
}

template<typename PointType, typename SegmentEndPointFunc, typename CouldReverseFunc>
std::vector<std::pair<size_t, bool>> chain_segments_greedy_constrained_reversals(SegmentEndPointFunc end_point_func, CouldReverseFunc could_reverse_func, size_t num_segments, const PointType *start_near)
{
	return chain_segments_greedy_constrained_reversals_<PointType, SegmentEndPointFunc, true, CouldReverseFunc>(end_point_func, could_reverse_func, num_segments, start_near);
}

template<typename PointType, typename SegmentEndPointFunc>
std::vector<std::pair<size_t, bool>> chain_segments_greedy(SegmentEndPointFunc end_point_func, size_t num_segments, const PointType *start_near)
{
	auto could_reverse_func = [](size_t /* idx */) -> bool { return true; };
	return chain_segments_greedy_constrained_reversals_<PointType, SegmentEndPointFunc, false, decltype(could_reverse_func)>(end_point_func, could_reverse_func, num_segments, start_near);
}

std::vector<std::pair<size_t, bool>> chain_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near)
{
	auto segment_end_point = [&entities](size_t idx, bool first_point) -> const Point& { return first_point ? entities[idx]->first_point() : entities[idx]->last_point(); };
	auto could_reverse = [&entities](size_t idx) { const ExtrusionEntity *ee = entities[idx]; return ee->is_loop() || ee->can_reverse(); };
	std::vector<std::pair<size_t, bool>> out = chain_segments_greedy_constrained_reversals<Point, decltype(segment_end_point), decltype(could_reverse)>(segment_end_point, could_reverse, entities.size(), start_near);
	for (size_t i = 0; i < entities.size(); ++ i) {
		ExtrusionEntity *ee = entities[i];
		if (ee->is_loop())
			// Ignore reversals for loops, as the start point equals the end point.
			out[i].second = false;
		// Is can_reverse() respected by the reversals?
		assert(entities[i]->can_reverse() || ! out[i].second);
	}
	return out;
}

void reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const std::vector<std::pair<size_t, bool>> &chain)
{
	assert(entities.size() == chain.size());
	std::vector<ExtrusionEntity*> out;
	out.reserve(entities.size());
    for (const std::pair<size_t, bool> &idx : chain) {
		assert(entities[idx.first] != nullptr);
        out.emplace_back(entities[idx.first]);
        if (idx.second)
			out.back()->reverse();
    }
    entities.swap(out);
}

void chain_and_reorder_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near)
{
	reorder_extrusion_entities(entities, chain_extrusion_entities(entities, start_near));
}

std::vector<std::pair<size_t, bool>> chain_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near)
{
	auto segment_end_point = [&extrusion_paths](size_t idx, bool first_point) -> const Point& { return first_point ? extrusion_paths[idx].first_point() : extrusion_paths[idx].last_point(); };
	return chain_segments_greedy<Point, decltype(segment_end_point)>(segment_end_point, extrusion_paths.size(), start_near);
}

void reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const std::vector<std::pair<size_t, bool>> &chain)
{
	assert(extrusion_paths.size() == chain.size());
	std::vector<ExtrusionPath> out;
	out.reserve(extrusion_paths.size());
    for (const std::pair<size_t, bool> &idx : chain) {
        out.emplace_back(std::move(extrusion_paths[idx.first]));
        if (idx.second)
			out.back().reverse();
    }
    extrusion_paths.swap(out);
}

void chain_and_reorder_extrusion_paths(std::vector<ExtrusionPath> &extrusion_paths, const Point *start_near)
{
	reorder_extrusion_paths(extrusion_paths, chain_extrusion_paths(extrusion_paths, start_near));
}

std::vector<size_t> chain_points(const Points &points, Point *start_near)
{
	auto segment_end_point = [&points](size_t idx, bool /* first_point */) -> const Point& { return points[idx]; };
	std::vector<std::pair<size_t, bool>> ordered = chain_segments_greedy<Point, decltype(segment_end_point)>(segment_end_point, points.size(), start_near);
	std::vector<size_t> out;
	out.reserve(ordered.size());
	for (auto &segment_and_reversal : ordered)
		out.emplace_back(segment_and_reversal.first);
	return out;
}

Polylines chain_polylines(Polylines &&polylines, const Point *start_near)
{
	auto segment_end_point = [&polylines](size_t idx, bool first_point) -> const Point& { return first_point ? polylines[idx].first_point() : polylines[idx].last_point(); };
	std::vector<std::pair<size_t, bool>> ordered = chain_segments_greedy<Point, decltype(segment_end_point)>(segment_end_point, polylines.size(), start_near);
	Polylines out;
	out.reserve(polylines.size()); 
	for (auto &segment_and_reversal : ordered) {
		out.emplace_back(std::move(polylines[segment_and_reversal.first]));
		if (segment_and_reversal.second)
			out.back().reverse();
	}
	return out;	
}

template<class T> static inline T chain_path_items(const Points &points, const T &items)
{
	auto segment_end_point = [&points](size_t idx, bool /* first_point */) -> const Point& { return points[idx]; };
	std::vector<std::pair<size_t, bool>> ordered = chain_segments_greedy<Point, decltype(segment_end_point)>(segment_end_point, points.size(), nullptr);
	T out;
	out.reserve(items.size());
	for (auto &segment_and_reversal : ordered)
		out.emplace_back(items[segment_and_reversal.first]);
	return out;
}

ClipperLib::PolyNodes chain_clipper_polynodes(const Points &points, const ClipperLib::PolyNodes &items)
{
	return chain_path_items(points, items);
}

std::vector<std::pair<size_t, size_t>> chain_print_object_instances(const Print &print)
{
    // Order objects using a nearest neighbor search.
    Points object_reference_points;
    std::vector<std::pair<size_t, size_t>> instances;
    for (size_t i = 0; i < print.objects().size(); ++ i) {
    	const PrintObject &object = *print.objects()[i];
    	for (size_t j = 0; j < object.copies().size(); ++ j) {
        	object_reference_points.emplace_back(object.copy_center(j));
        	instances.emplace_back(i, j);
        }
    }
	auto segment_end_point = [&object_reference_points](size_t idx, bool /* first_point */) -> const Point& { return object_reference_points[idx]; };
	std::vector<std::pair<size_t, bool>> ordered = chain_segments_greedy<Point, decltype(segment_end_point)>(segment_end_point, instances.size(), nullptr);
    std::vector<std::pair<size_t, size_t>> out;
	out.reserve(instances.size());
	for (auto &segment_and_reversal : ordered)
		out.emplace_back(instances[segment_and_reversal.first]);
	return out;
}

} // namespace Slic3r
