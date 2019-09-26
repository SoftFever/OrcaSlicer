#include "ShortestPath.hpp"
#include "KDTreeIndirect.hpp"
#include "MutablePriorityQueue.hpp"

#if 0
	#undef NDEBUG
	#undef assert
#endif

#include <cmath>
#include <cassert>

namespace Slic3r {

// Chain perimeters (always closed) and thin fills (closed or open) using a greedy algorithm.
// Solving a Traveling Salesman Problem (TSP) with the modification, that the sites are not always points, but points and segments.
// Solving using a greedy algorithm, where a shortest edge is added to the solution if it does not produce a bifurcation or a cycle.
// Return index and "reversed" flag.
// https://en.wikipedia.org/wiki/Multi-fragment_algorithm
// The algorithm builds a tour for the traveling salesman one edge at a time and thus maintains multiple tour fragments, each of which 
// is a simple path in the complete graph of cities. At each stage, the algorithm selects the edge of minimal cost that either creates 
// a new fragment, extends one of the existing paths or creates a cycle of length equal to the number of cities.
std::vector<std::pair<size_t, bool>> chain_extrusion_entities(std::vector<ExtrusionEntity*> &entities, const Point *start_near)
{
	std::vector<std::pair<size_t, bool>> out;

	if (entities.empty()) {
		// Nothing to do.
	} 
	else if (entities.size() == 1)
	{
		// Just sort the end points so that the first point visited is closest to start_near.
		ExtrusionEntity *extrusion_entity = entities.front();
		out.emplace_back(0, extrusion_entity->can_reverse() && start_near != nullptr && 
			(extrusion_entity->last_point() - *start_near).cast<double>().squaredNorm() < (extrusion_entity->first_point() - *start_near).cast<double>().squaredNorm());
	} 
	else
	{
		// End points of entities for the KD tree closest point search.
		// A single end point is inserted into the search structure for loops, two end points are entered for open paths.
		struct EndPoint {
			EndPoint(const Vec2d &pos) : pos(pos) {}

			Vec2d     pos;

			// Identifier of the chain, to which this end point belongs. Zero means unassigned.
			size_t    chain_id = 0;
			// Link to the closest currently valid end point.
			EndPoint *edge_out = nullptr;
			// Reverse of edge_out. As there may be multiple end points with the same edge_out,
			// these other edge_in points are chained using the on_circle_prev / on_circle_next cyclic loop.
			EndPoint *edge_in  = nullptr;
			EndPoint* on_circle_prev = nullptr;
			EndPoint* on_circle_next = nullptr;
			void 	  on_circle_merge(EndPoint *other)
			{
				EndPoint *a = this;
				EndPoint *b = other;
				assert(a->validate());
				assert(b->validate());
				if (a->on_circle_next == nullptr)
					std::swap(a, b);
				if (a->on_circle_next == nullptr) {
					a->on_circle_next = a->on_circle_prev = b;
					b->on_circle_next = b->on_circle_prev = a;
				} else if (b->on_circle_next == nullptr) {
					b->on_circle_next = a;
					b->on_circle_prev = a->on_circle_prev;
					a->on_circle_prev = b;
					b->on_circle_prev->on_circle_next = b;
				} else {
					EndPoint *next = a->on_circle_next;
					EndPoint *prev = b->on_circle_prev;
					a->on_circle_next = b;
					b->on_circle_prev = a;
					prev->on_circle_next = next;
					next->on_circle_prev = prev;
				}
				assert(this->validate());
			}
			void 	  on_circle_detach() 
			{
				if (this->on_circle_next) {
					EndPoint *next = this->on_circle_next;
					EndPoint *prev = this->on_circle_prev;
					if (prev == next) {
						next->on_circle_next = nullptr;
						next->on_circle_prev = nullptr;
					} else {
						prev->on_circle_next = next;
						next->on_circle_prev = prev;
					}
					assert(prev->validate());
					assert(next->validate());
					this->on_circle_next = this->on_circle_prev = nullptr;
				}
				assert(this->validate());
			}
			bool 	  on_circle_empty() const
			{
				assert((this->on_circle_prev == nullptr) == (this->on_circle_next == nullptr));
				assert(this->on_circle_prev == nullptr || (this->on_circle_prev != this && this->on_circle_next != this));
				return this->on_circle_next == nullptr;
			}

#ifndef NDEBUG
			bool	  validate() 
			{
				assert((this->on_circle_prev == nullptr) == (this->on_circle_next == nullptr));
				assert(this->on_circle_prev == nullptr || (this->on_circle_prev != this && this->on_circle_next != this));
				assert(this->edge_out == nullptr || edge_out->edge_in != nullptr);
				assert(this->distance_out >= 0.);
				assert(this->edge_in == nullptr || this->edge_in->edge_out == this);
				// Point which is a member of path (chain_id > 0) must not be in circle of some edge_in.
				assert(this->chain_id == 0 || this->on_circle_empty());
				if (! this->on_circle_empty()) {
					// Iterate over the cycle and validate the loop.
					std::set<const EndPoint*> visited;
					const EndPoint *ep = this;
					bool edge_in_found = false;
					do {
						// This end point is visited for the first time.
						assert(visited.insert(ep).second);
						assert(ep->on_circle_next != ep);
						assert(ep->on_circle_prev != ep);
						assert(ep->on_circle_next->on_circle_prev == ep);
						assert(ep->on_circle_prev->on_circle_next == ep);
						assert(ep->edge_out != nullptr && ep->edge_out == this->edge_out);
						if (ep->edge_out->edge_in == ep)
							edge_in_found = true;
						ep = ep->on_circle_next;
					} while (ep != this);
					assert(edge_in_found);
				}
				return true;
			}
#endif /* NDEBUG */

			// Distance to the next end point following the link.
			// Zero value -> start of the final path.
			double    distance_out = std::numeric_limits<double>::max();
			size_t    heap_idx = std::numeric_limits<size_t>::max();
		};
	    std::vector<EndPoint> end_points;
	    end_points.reserve(entities.size() * 2);
	    for (const ExtrusionEntity* const &entity : entities) {
	    	end_points.emplace_back(entity->first_point().cast<double>());
	    	end_points.emplace_back(entity->last_point().cast<double>());
	    }

	    // Construct the closest point KD tree over end points of extrusion entities.
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
				assert(m_last_chain_id > 0);
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
			// Unique chain ID assigned to chains of end points of entities.
			size_t              m_last_chain_id = 0;
			std::vector<size_t> m_equivalent_with;
		} equivalent_chain(entities.size());

		// Find the first end point closest to start_near.
		EndPoint *first_point = nullptr;
		size_t    first_point_idx = std::numeric_limits<size_t>::max();
		if (start_near != nullptr) {
			size_t idx = find_closest_point(kdtree, start_near->cast<double>());
			assert(idx != kdtree.npos);
			assert(idx < end_points.size());
			first_point = &end_points[idx];
			first_point->distance_out = 0.;
			first_point->chain_id = equivalent_chain.next();
			first_point_idx = idx;
		}

#ifndef NDEBUG
		auto validate_graph = [&end_points, &equivalent_chain]() -> bool {
			for (EndPoint& ep : end_points)
				ep.validate();
			assert(equivalent_chain.validate());
			return true;
		};
#endif /* NDEBUG */

		// Assign the closest point and distance to the end points.
		assert(validate_graph());
		for (EndPoint &end_point : end_points) {
	    	assert(end_point.edge_out == nullptr);
	    	if (&end_point != first_point) {
		    	size_t this_idx = &end_point - &end_points.front();
		    	// Find the closest point to this end_point, which lies on a different extrusion path (filtered by the lambda).
		    	// Ignore the starting point as the starting point is considered to be occupied, no end point coud connect to it.
				size_t next_idx = find_closest_point(kdtree, end_point.pos, 
					[this_idx, first_point_idx](size_t idx){ return idx != first_point_idx && (idx ^ this_idx) > 1; });
				assert(next_idx != kdtree.npos);
				assert(next_idx < end_points.size());
				EndPoint &end_point2 = end_points[next_idx];
				end_point.edge_out = &end_point2;
				if (end_point2.edge_in == nullptr)
					end_point2.edge_in = &end_point;
				else {
					assert(end_point.on_circle_empty());
					assert(end_point2.edge_in->edge_out == &end_point2);
					end_point.on_circle_merge(end_point2.edge_in);
				}
				end_point.distance_out = (end_point2.pos - end_point.pos).squaredNorm();
			}
			assert(validate_graph());
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
		auto validate_graph_and_queue = [&validate_graph, &end_points, &queue, first_point]() -> bool {
			assert(validate_graph());
			for (EndPoint &ep : end_points) {
				if (ep.heap_idx < queue.size()) {
					// End point is on the heap.
					assert(*(queue.cbegin() + ep.heap_idx) == &ep);
					assert(ep.chain_id == 0);
					// Point on the heap may only points to other points on the heap.
					assert(ep.edge_in  == nullptr || ep.edge_in ->heap_idx < queue.size());
					assert(ep.edge_out == nullptr || ep.edge_out->heap_idx < queue.size());
				} else {
					// End point is NOT on the heap, therefore it is part of the output path.
					assert(ep.heap_idx == std::numeric_limits<size_t>::max());
					assert(ep.chain_id != 0);
					assert(ep.on_circle_empty());
					if (&ep == first_point) {
						assert(ep.edge_in  == nullptr);
						assert(ep.edge_out == nullptr);
					} else {
						assert(ep.edge_in  != nullptr);
						assert(ep.edge_out != nullptr);
						assert(ep.edge_in != &ep);
						assert(ep.edge_in == ep.edge_out);
						assert(ep.edge_in->edge_out == &ep);
						assert(ep.edge_out->edge_in == &ep);
						assert(ep.edge_in->heap_idx == std::numeric_limits<size_t>::max());
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

	    // Chain the end points: find (entities.size() - 1) shortest links not forming bifurcations or loops.
	    std::vector<EndPoint*> end_points_update;
	    end_points_update.reserve(16);
		assert(entities.size() >= 2);
		for (int iter = int(entities.size()) - 2;; -- iter) {
			assert(validate_graph_and_queue());
	    	// Take the first end point, for which the link points to the currently closest valid neighbor.
	    	EndPoint &end_point1 = *queue.top();
	    	assert(end_point1.edge_out != nullptr);
	    	// No point on the queue may be connected yet.
	    	assert(end_point1.chain_id == 0);
	    	// Take the closest end point to the first end point,
	    	EndPoint &end_point2 = *end_point1.edge_out;
	    	// The closest point must not be connected yet.
	    	assert(end_point2.chain_id == 0);
			// If end_point1.edge_out == end_point2, then end_point2.edge_in == &end_point1, or end_point2.edge_in points to some point on loop of end_point1.
			assert(end_point2.edge_in != nullptr);
			// End points of the opposite ends of the segments.
			size_t end_point1_other_chain_id = equivalent_chain(end_points[(&end_point1 - &end_points.front()) ^ 1].chain_id);
			size_t end_point2_other_chain_id = equivalent_chain(end_points[(&end_point2 - &end_points.front()) ^ 1].chain_id);
			if (end_point1_other_chain_id == end_point2_other_chain_id && end_point1_other_chain_id != 0) {
				// This edge forms a loop. Update end_point1 and try another one.
				++ iter;
				assert(end_point1.edge_out != nullptr);
				assert(end_point1.edge_out->edge_in != nullptr);
				assert(! end_point1.on_circle_empty() || end_point1.edge_out->edge_in == &end_point1);
				end_point1.edge_out->edge_in = end_point1.on_circle_empty() ? nullptr : end_point1.on_circle_next;
				end_point1.edge_out = nullptr;
				if (! end_point1.on_circle_empty())
					end_point1.on_circle_detach();
				assert(validate_graph_and_queue());
				end_points_update.emplace_back(&end_point1);
			} else {
		    	// Remove the first and second point from the queue.
				queue.pop();
		    	queue.remove(end_point2.heap_idx);
	#ifndef NDEBUG
				// Mark them as removed from the queue.
				end_point1.heap_idx = std::numeric_limits<size_t>::max();
				end_point2.heap_idx = std::numeric_limits<size_t>::max();
	#endif /* NDEBUG */
				// Collect the other end points pointing to this one, detach them from the on_circle linked list.
				for (EndPoint *pt_first : { end_point1.edge_in, end_point2.edge_in })
					if (pt_first != nullptr) {
						EndPoint *pt = pt_first;
						do {
							if (pt != &end_point1 && pt != &end_point2) {
								// Point is in the queue.
								assert(pt->heap_idx < queue.size());
								// Point is not connected yet.
								assert(pt->chain_id == 0);
								end_points_update.emplace_back(pt);
								pt->edge_out = nullptr;
							}
							EndPoint *next = pt->on_circle_next;
							pt->on_circle_prev = nullptr;
							pt->on_circle_next = nullptr;
							pt = next;
						} while (pt != nullptr && pt != pt_first);
					}
				// If end_point1 was on a circle, the circle belonged to end_point2.edge_in, which was broken in the loop above.
				assert(end_point1.on_circle_empty());
				// If end_point2 pointed to end_point1, then end_point2 was on a circle that belonged to end_point1.edge_in, which was broken in the loop above.
				//assert(end_point2.on_circle_empty() == (end_point2.edge_out == &end_point1));
				assert(end_point2.on_circle_empty() || end_point2.edge_out != nullptr);
				end_point2.edge_out->edge_in = end_point2.on_circle_empty() ? nullptr : end_point2.on_circle_next;
		    	// The end_point2.link may not necessarily point back to end_point1 due to numeric issues and points on circles.
		    	// Update the link back.
		    	end_point1.edge_out = &end_point2;
		    	end_point1.edge_in  = &end_point2;
		    	end_point2.edge_out = &end_point1;
		    	end_point2.edge_in  = &end_point1;
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
				if (! end_point2.on_circle_empty())
					end_point2.on_circle_detach();
				assert(validate_graph_and_queue());
			}
#ifndef NDEBUG
			for (EndPoint *end_point : end_points_update) {
				assert(end_point->edge_out == nullptr);
				// Point is in the queue.
				assert(end_point->heap_idx < queue.size());
				// Point is not connected yet.
				assert(end_point->chain_id == 0);
			}
#endif /* NDEBUG */
			if (iter == 0) {
				// Last iteration. There shall be exactly one or two end points waiting to be connected.
				if (first_point == nullptr) {
					// Two unconnected points are the end points of the constructed path.
					assert(end_points_update.size() == 2);
					first_point = end_points_update.front();
				} else
					assert(end_points_update.size() == 1);
				// Mark both points as ends of the path.
				for (EndPoint *end_point : end_points_update)
					end_point->edge_in = end_point->edge_out = nullptr;
				break;
			}
	    	// Update links, distances and queue positions of all points that used to point to end_point1 or end_point2.
	    	for (EndPoint *end_point : end_points_update) {
		    	size_t this_idx = end_point - &end_points.front();
		    	// Find the closest point to this end_point, which lies on a different extrusion path (filtered by the filter lambda).
				size_t next_idx = find_closest_point(kdtree, end_point->pos, [&end_points, &equivalent_chain, this_idx](size_t idx) { 
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
				assert(next_idx != kdtree.npos);
				assert(next_idx < end_points.size());
				EndPoint &end_point2 = end_points[next_idx];
				end_point->edge_out = &end_point2;
				if (end_point2.edge_in == nullptr)
					end_point2.edge_in = end_point;
				else {
					assert(end_point->on_circle_empty());
					assert(end_point2.edge_in->edge_out == &end_point2);
					end_point->on_circle_merge(end_point2.edge_in);
				}
				end_point->distance_out = (end_points[next_idx].pos - end_point->pos).squaredNorm();
				// Update position of this end point in the queue based on the distance calculated at the line above.
				queue.update(end_point->heap_idx);
		    	//FIXME Remove the other end point from the KD tree.
		    	// As the KD tree update is expensive, do it only after some larger number of points is removed from the queue.
				assert(validate_graph_and_queue());
	    	}
			end_points_update.clear();
		}
		assert(queue.size() == (first_point == nullptr) ? 1 : 2);

		// Now interconnect pairs of segments into a chain.
		assert(first_point != nullptr);
		do {
			size_t    		 first_point_id      = first_point - &end_points.front();
			size_t           extrusion_entity_id = first_point_id >> 1;
			EndPoint 		*second_point        = &end_points[first_point_id ^ 1];
			ExtrusionEntity *extrusion_entity    = entities[extrusion_entity_id];
			out.emplace_back(extrusion_entity_id, extrusion_entity->can_reverse() && (first_point_id & 1));
			first_point = second_point->edge_out;
		} while (first_point != nullptr);
	}

	assert(out.size() == entities.size());
	return out;
}

} // namespace Slic3r
