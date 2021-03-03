#ifndef slic3r_MutablePolygon_hpp_
#define slic3r_MutablePolygon_hpp_

#include "Point.hpp"
#include "Polygon.hpp"

namespace Slic3r {

class MutablePolygon
{
public:
    using IndexType = int32_t;
    using PointType = Point;
    class const_iterator {
    public:
        bool             operator==(const const_iterator &rhs) const { assert(m_data == rhs.m_data); assert(this->valid()); return m_idx == rhs.m_idx; }
        bool             operator!=(const const_iterator &rhs) const { return ! (*this == rhs); }
        const_iterator&  operator--()    { assert(this->valid()); m_idx = m_data->at(m_idx).prev; return *this; }
        const_iterator   operator--(int) { const_iterator result(*this); --(*this); return result; }
        const_iterator&  operator++()    { assert(this->valid()); m_idx = m_data->at(m_idx).next; return *this; }
        const_iterator   operator++(int) { const_iterator result(*this); ++(*this); return result; }
        const_iterator   prev()    const { assert(this->valid()); return { m_data, m_data->at(m_idx).prev }; }
        const_iterator   next()    const { assert(this->valid()); return { m_data, m_data->at(m_idx).next }; }
        bool             valid()   const { return m_idx >= 0; }
        const PointType& operator*() const { return m_data->at(m_idx).point; }
        const PointType* operator->() const { return &m_data->at(m_idx).point; }
        const MutablePolygon& polygon() const { assert(this->valid()); return *m_data; }
        IndexType        size()    const { assert(this->valid()); return m_data->size(); }
    private:
        const_iterator(const MutablePolygon *data, IndexType idx) : m_data(data), m_idx(idx) {}
        friend class MutablePolygon;
        const MutablePolygon  *m_data;
        IndexType              m_idx;
    };

    class iterator {
    public:
        bool            operator==(const iterator &rhs) const { assert(m_data == rhs.m_data); assert(this->valid()); return m_idx == rhs.m_idx; }
        bool            operator!=(const iterator &rhs) const { return !(*this == rhs); }
        iterator&       operator--()    { assert(this->valid()); m_idx = m_data->at(m_idx).prev; return *this; }
        iterator        operator--(int) { iterator result(*this); --(*this); return result; }
        iterator&       operator++()    { assert(this->valid()); m_idx = m_data->at(m_idx).next; return *this; }
        iterator        operator++(int) { iterator result(*this); ++(*this); return result; }
        iterator        prev()    const { assert(this->valid()); return { m_data, m_data->at(m_idx).prev }; }
        iterator        next()    const { assert(this->valid()); return { m_data, m_data->at(m_idx).next }; }
        bool            valid()   const { return m_idx >= 0; }
        PointType&      operator*() const { return m_data->at(m_idx).point; }
        PointType*      operator->() const { return &m_data->at(m_idx).point; }
        MutablePolygon& polygon() const { assert(this->valid()); return *m_data; }
        IndexType       size()    const { assert(this->valid()); return m_data->size(); }
        iterator&       remove()        { this->m_idx = m_data->remove(*this).m_idx; return *this; }
        iterator        insert(const PointType pt) const { return m_data->insert(*this, pt); }
    private:
        iterator(MutablePolygon *data, IndexType idx) : m_data(data), m_idx(idx) {}
        friend class MutablePolygon;
        MutablePolygon  *m_data;
        IndexType        m_idx;
    };

    MutablePolygon() = default;
    MutablePolygon(const Polygon &rhs, size_t reserve = 0) : MutablePolygon(rhs.points.begin(), rhs.points.end(), reserve) {}
    MutablePolygon(std::initializer_list<Point> rhs, size_t reserve = 0) : MutablePolygon(rhs.begin(), rhs.end(), reserve) {}

    template<typename IT>
    MutablePolygon(IT begin, IT end, size_t reserve = 0) {
        m_size = IndexType(end - begin);
        if (m_size > 0) {
            m_head = 0;
            m_data.reserve(std::max<size_t>(m_size, reserve));
            auto i = IndexType(-1);
            auto j = IndexType(1);
            for (auto it = begin; it != end; ++ it)
                m_data.push_back({ *it, i ++, j ++ });
            m_data.front().prev = m_size - 1;
            m_data.back ().next = 0;
        }
    };

    Polygon polygon() const {
        Polygon out;
        if (this->valid()) {
            out.points.reserve(this->size());
            for (auto it = this->cbegin(); it != this->cend(); ++ it)
                out.points.emplace_back(*it);
        }
        return out;
    };

    bool            empty()  const { return this->m_size == 0; }
    size_t          size()   const { return this->m_size; }
    size_t          capacity() const { return this->m_data.capacity(); }
    bool            valid()  const { return this->m_size >= 3; }

    iterator        begin()        { return { this, m_head }; }
    const_iterator  cbegin() const { return { this, m_head }; }
    const_iterator  begin()  const { return this->cbegin(); }
    // End points to the last item before roll over. This is different from the usual end() concept!
    iterator        end()          { return { this, this->empty() ? -1 : this->at(m_head).prev }; }
    const_iterator  cend()   const { return { this, this->empty() ? -1 : this->at(m_head).prev }; }
    const_iterator  end()    const { return this->cend(); }

    // Returns iterator following the removed element. Returned iterator will become invalid if last point is removed.
    // If begin() is removed, then the next element will become the new begin().
    iterator        remove(const iterator it) { assert(it.m_data == this); return { this, this->remove(it.m_idx) }; }
    // Insert a new point before it. Returns iterator to the newly inserted point.
    // begin() will not change, end() may point to the newly inserted point.
    iterator        insert(const iterator it, const PointType pt) { assert(it.m_data == this); return { this, this->insert(it.m_idx, pt) }; }

private:
    struct LinkedPoint {
        PointType point;
        IndexType prev;
        IndexType next;
    };
    std::vector<LinkedPoint>    m_data;
    // Number of points in the linked list.
    IndexType                   m_size { 0 };
    IndexType                   m_head { IndexType(-1) };
    // Head of the free list.
    IndexType                   m_head_free { IndexType(-1) };

    LinkedPoint&          at(IndexType i)       { return m_data[i]; }
    const LinkedPoint&    at(IndexType i) const { return m_data[i]; }

    IndexType remove(const IndexType i) {
        assert(i >= 0);
        assert(m_size > 0);
        assert(m_head != -1);
        LinkedPoint &lp = this->at(i);
        IndexType prev = lp.prev;
        IndexType next = lp.next;
        lp.next = m_head_free;
        m_head_free = i;
        if (-- m_size == 0)
            m_head = -1;
        else if (m_head == i)
            m_head = next;
        assert(! this->empty() || (prev == i && next == i));
        if (this->empty())
            return IndexType(-1);
        this->at(prev).next = next;
        this->at(next).prev = prev;
        return next;
    }

    IndexType insert(const IndexType i, const Point pt) {
        assert(i >= 0);
        IndexType n;
        IndexType j = this->at(i).prev;
        if (m_head_free == -1) {
            // Allocate a new item.
            n = IndexType(m_data.size());
            m_data.push_back({ pt, j, i });
        } else {
            n = m_head_free;
            LinkedPoint &nlp = this->at(n);
            m_head_free = nlp.next;
            nlp = { pt, j, i };
        }
        this->at(j).next = n;
        this->at(i).prev = n;
        ++ m_size;
        return n;
    }

    /*
    IndexType insert(const IndexType i, const Point pt) {
        assert(i >= 0);
        if (this->at(i).point == pt)
            return i;
        IndexType j = this->at(i).next;
        if (this->at(j).point == pt)
            return i;
        IndexType n;
        if (m_head_free == -1) {
            // Allocate a new item.
            n = IndexType(m_data.size());
            m_data.push_back({ pt, i, j });
        } else {
            LinkedPoint &nlp = this->at(m_head_free);
            m_head_free = nlp.next;
            nlp = { pt, i, j };
        }
        this->at(i).next = n;
        this->at(j).prev = n;
        ++ m_size;
        return n;
    }
    */
};

inline bool operator==(const MutablePolygon &p1, const MutablePolygon &p2) 
{ 
    if (p1.size() != p2.size())
        return false;
    if (p1.empty())
        return true;
    auto begin = p1.cbegin();
    auto it  = begin;
    auto it2 = p2.cbegin();
    for (;;) {
        if (! (*it == *it2))
            return false;
        if (++ it == begin)
            return true;
        ++ it2;
    }
}

inline bool operator!=(const MutablePolygon &p1, const MutablePolygon &p2) { return ! (p1 == p2); }

// Remove exact duplicate points. May reduce the polygon down to empty polygon.
void remove_duplicates(MutablePolygon &polygon);
void remove_duplicates(MutablePolygon &polygon, double eps);

void smooth_outward(MutablePolygon &polygon, double shortcut_length);

inline Polygon smooth_outward(const Polygon &polygon, double shortcut_length) 
{ 
    MutablePolygon mp(polygon, polygon.size() * 2);
    smooth_outward(mp, shortcut_length);
    return mp.polygon();
}

}

#endif // slic3r_MutablePolygon_hpp_
