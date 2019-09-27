#ifndef slic3r_MutablePriorityQueue_hpp_
#define slic3r_MutablePriorityQueue_hpp_

#include <assert.h>

template<typename T, typename IndexSetter, typename LessPredicate>
class MutablePriorityQueue
{
public:
	MutablePriorityQueue(IndexSetter &&index_setter, LessPredicate &&less_predicate) :
		m_index_setter(std::forward<IndexSetter>(index_setter)), 
		m_less_predicate(std::forward<LessPredicate>(less_predicate)) 
		{}
	~MutablePriorityQueue()	{ clear(); }

	void		clear();
	void		reserve(size_t cnt) 				{ m_heap.reserve(cnt); }
	void		push(const T &item);
	void		push(T &&item);
	void		pop();
	T&			top()								{ return m_heap.front(); }
	void		remove(size_t idx);
	void		update(size_t idx) 					{ T item = m_heap[idx]; remove(idx); push(item); }

	size_t		size() const						{ return m_heap.size(); }
	bool		empty() const						{ return m_heap.empty(); }

	using iterator		 = typename std::vector<T>::iterator;
	using const_iterator = typename std::vector<T>::const_iterator;
	iterator 		begin() 		{ return m_heap.begin(); }
	iterator 		end() 			{ return m_heap.end(); }
	const_iterator 	cbegin() const	{ return m_heap.cbegin(); }
	const_iterator 	cend() const	{ return m_heap.cend(); }

protected:
	void		update_heap_up(size_t top, size_t bottom);
	void		update_heap_down(size_t top, size_t bottom);

private:
	std::vector<T>	m_heap;
	IndexSetter		m_index_setter;
	LessPredicate	m_less_predicate;
};

template<typename T, typename IndexSetter, typename LessPredicate>
MutablePriorityQueue<T, IndexSetter, LessPredicate> make_mutable_priority_queue(IndexSetter &&index_setter, LessPredicate &&less_predicate)
{
    return MutablePriorityQueue<T, IndexSetter, LessPredicate>(
    	std::forward<IndexSetter>(index_setter), std::forward<LessPredicate>(less_predicate));
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::clear()
{ 
#ifndef NDEBUG
	for (size_t idx = 0; idx < m_heap.size(); ++ idx)
		// Mark as removed from the queue.
		m_index_setter(m_heap[idx], std::numeric_limits<size_t>::max());
#endif /* NDEBUG */
	m_heap.clear();
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::push(const T &item)
{
	size_t idx = m_heap.size();
	m_heap.emplace_back(item);
	m_index_setter(m_heap.back(), idx);
	update_heap_up(0, idx);
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::push(T &&item)
{
	size_t idx = m_heap.size();
	m_heap.emplace_back(std::move(item));
	m_index_setter(m_heap.back(), idx);
	update_heap_up(0, idx);
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::pop()
{
	assert(! m_heap.empty());
#ifndef NDEBUG
	// Mark as removed from the queue.
	m_index_setter(m_heap.front(), std::numeric_limits<size_t>::max());
#endif /* NDEBUG */
	if (m_heap.size() > 1) {
		m_heap.front() = m_heap.back();
		m_heap.pop_back();
		m_index_setter(m_heap.front(), 0);
		update_heap_down(0, m_heap.size() - 1);
	} else
		m_heap.clear();
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::remove(size_t idx)
{
	assert(idx < m_heap.size());
#ifndef NDEBUG
	// Mark as removed from the queue.
	m_index_setter(m_heap[idx], std::numeric_limits<size_t>::max());
#endif /* NDEBUG */
	if (idx + 1 == m_heap.size()) {
		m_heap.pop_back();
		return;
	}
	m_heap[idx] = m_heap.back();
	m_index_setter(m_heap[idx], idx);
	m_heap.pop_back();
	update_heap_down(idx, m_heap.size() - 1);
	update_heap_up(0, idx);
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::update_heap_up(size_t top, size_t bottom)
{
	size_t childIdx = bottom;
	T *child = &m_heap[childIdx];
	for (;;) {
		size_t parentIdx = (childIdx - 1) >> 1;
		if (childIdx == 0 || parentIdx < top)
			break;
		T *parent = &m_heap[parentIdx];
		// switch nodes
		if (! m_less_predicate(*parent, *child)) {
			T tmp = *parent;
			m_index_setter(*parent, childIdx);
			m_index_setter(*child, parentIdx);
			m_heap[parentIdx] = *child;
			m_heap[childIdx] = tmp;
		}
		// shift up
		childIdx = parentIdx;
		child = parent;
	}
}

template<class T, class LessPredicate, class IndexSetter>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter>::update_heap_down(size_t top, size_t bottom)
{
	size_t parentIdx = top;
	T *parent = &m_heap[parentIdx];
	for (;;) {
		size_t childIdx = (parentIdx << 1) + 1;
		if (childIdx > bottom)
			break;
		T *child = &m_heap[childIdx];
		size_t child2Idx = childIdx + 1;
		if (child2Idx <= bottom) {
			T *child2 = &m_heap[child2Idx];
			if (! m_less_predicate(*child, *child2)) {
				child = child2;
				childIdx = child2Idx;
			}
		}
		if (m_less_predicate(*parent, *child))
			return;
		// switch nodes
		m_index_setter(*parent, childIdx);
		m_index_setter(*child, parentIdx);
		T tmp = *parent;
		m_heap[parentIdx] = *child;
		m_heap[childIdx] = tmp;
		// shift down
		parentIdx = childIdx;
		parent = child;
	}
}

#endif /* slic3r_MutablePriorityQueue_hpp_ */
