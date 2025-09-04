#ifndef MARCHINGSQUARES_HPP
#define MARCHINGSQUARES_HPP

#include <type_traits>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>

namespace marchsq {

// Marks a square in the grid
struct Coord {
    long r = 0, c = 0;

    Coord() = default;
    explicit Coord(long s) : r(s), c(s) {}
    Coord(long _r, long _c): r(_r), c(_c) {}

    size_t seq(const Coord &res) const { return r * res.c + c; }
    Coord& operator+=(const Coord& b) { r += b.r; c += b.c; return *this; }
    Coord operator+(const Coord& b) const { Coord a = *this; a += b; return a; }
};

// Closed ring of cell coordinates
using Ring = std::vector<Coord>;

// Specialize this struct to register a raster type for the Marching squares alg
template<class T, class Enable = void> struct _RasterTraits {

    // The type of pixel cell in the raster
    using ValueType = typename T::ValueType;

    // Value at a given position
    static ValueType get(const T &raster, size_t row, size_t col);

    // Number of rows and cols of the raster
    static size_t rows(const T &raster);
    static size_t cols(const T &raster);
};

// Specialize this to use parellel loops within the algorithm
template<class ExecutionPolicy, class Enable = void> struct _Loop {
    template<class It, class Fn> static void for_each(It from, It to, Fn &&fn)
    {
        for (auto it = from; it < to; ++it) fn(*it, size_t(it - from));
    }
};

namespace __impl {

template<class T> using RasterTraits = _RasterTraits<std::decay_t<T>>;
template<class T> using TRasterValue = typename RasterTraits<T>::ValueType;

template<class T> size_t rows(const T &raster)
{
    return RasterTraits<T>::rows(raster);
}

template<class T> size_t cols(const T &raster)
{
    return RasterTraits<T>::cols(raster);
}

template<class T> TRasterValue<T> isoval(const T &rst, const Coord &crd)
{
    return RasterTraits<T>::get(rst, crd.r, crd.c);
}

template<class ExecutionPolicy, class It, class Fn>
void for_each(ExecutionPolicy&& policy, It from, It to, Fn &&fn)
{
    _Loop<ExecutionPolicy>::for_each(from, to, fn);
}

// Type of squares (tiles) depending on which vertices are inside an ROI
// The vertices would be marked a, b, c, d in counter clockwise order from the
// bottom left vertex of a square.
// d --- c
// |     |
// |     |
// a --- b
enum class SquareTag : uint8_t {
//     0, 1, 2,  3, 4,  5,  6,   7, 8,  9, 10,  11, 12,  13,  14,  15
    none, a, b, ab, c, ac, bc, abc, d, ad, bd, abd, cd, acd, bcd, full
};

template<class E> constexpr std::underlying_type_t<E> _t(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

enum class Dir: uint8_t {
    none      = 0b0000,
    left      = 0b0001,
    down      = 0b0010,
    right     = 0b0100,
    leftright = left | right,
    up        = 0b1000,
    updown    = up | down,
    all       = 0b1111
};

constexpr bool operator!(const Dir& a) { return !_t(a); }
constexpr Dir operator~(const Dir& a) { return static_cast<Dir>(~_t(a)); }
constexpr Dir operator&(const Dir& a, const Dir& b) { return static_cast<Dir>(_t(a) & _t(b)); }
constexpr Dir operator|(const Dir& a, const Dir& b) { return static_cast<Dir>(_t(a) | _t(b)); }

// This maps SquareTag values to possible next directions.
static const constexpr Dir NEXT_CCW[] = {
    /* 00 */ Dir::none,
    /* 01 */ Dir::left,
    /* 02 */ Dir::down,
    /* 03 */ Dir::left,
    /* 04 */ Dir::right,
    /* 05 */ Dir::leftright,
    /* 06 */ Dir::down,
    /* 07 */ Dir::left,
    /* 08 */ Dir::up,
    /* 09 */ Dir::up,
    /* 10 */ Dir::updown,
    /* 11 */ Dir::up,
    /* 12 */ Dir::right,
    /* 13 */ Dir::right,
    /* 14 */ Dir::down,
    /* 15 */ Dir::none
};

// Step a point in a direction.
inline Coord step(const Coord &crd, Dir d)
{
  switch (d) {
      case Dir::left: return {crd.r, crd.c - 1};
      case Dir::down: return {crd.r + 1, crd.c};
      case Dir::right: return {crd.r, crd.c + 1};
      case Dir::up: return {crd.r - 1, crd.c};
      default: return crd;
  }
}


template<class Rst> class Grid {
    const Rst *            m_rst = nullptr;
    Coord                  m_window, m_gridsize;
    std::vector<uint8_t>   m_tags;     // squaretags and unvisited flags for each square.

    // Convert a grid coordinate point into raster coordinates.
    Coord rastercoord(const Coord &crd) const
    {
        return {(crd.r - 1) * m_window.r, (crd.c - 1) * m_window.c};
    }

    // Get the 4 corners of a grid coordinate cell in raster coordinates.
    Coord bl(const Coord &crd) const { return tl(crd) + Coord{m_window.r, 0}; }
    Coord br(const Coord &crd) const { return tl(crd) + Coord{m_window.r, m_window.c}; }
    Coord tr(const Coord &crd) const { return tl(crd) + Coord{0, m_window.c}; }
    Coord tl(const Coord &crd) const { return rastercoord(crd); }

    // Test if a raster coordinate point is within the raster area.
    bool is_within(const Coord &crd)
    {
        long R = rows(*m_rst), C = cols(*m_rst);
        return crd.r >= 0 && crd.r < R && crd.c >= 0 && crd.c < C;
    };

    // Calculate the tag for a cell (or square). The cell coordinates mark the
    // top left vertex of a square in the raster. v is the isovalue
    uint8_t get_tag_for_cell(const Coord &cell, TRasterValue<Rst> v)
    {
        Coord sqr[] = {bl(cell), br(cell), tr(cell), tl(cell)};

        uint8_t t = ((is_within(sqr[0]) && isoval(*m_rst, sqr[0]) >= v)) +
                    ((is_within(sqr[1]) && isoval(*m_rst, sqr[1]) >= v) << 1) +
                    ((is_within(sqr[2]) && isoval(*m_rst, sqr[2]) >= v) << 2) +
                    ((is_within(sqr[3]) && isoval(*m_rst, sqr[3]) >= v) << 3);
        assert(t < 16);
        // Set the unvisited flags with the possible next directions set.
        t = (t << 4) | _t(NEXT_CCW[t]);
        return t;
    }

    void set_visited(size_t idx, Dir d = Dir::none)
    {
        // Clear the corresponding unvisited flag.
        m_tags[idx] &= _t(~d);
    }

    // Get the square tag from the cell m_tag value.
    inline SquareTag squaretag(size_t idx) const {
        return SquareTag(m_tags[idx] >> 4);
    }

    // Get the selected unvisited flags from the cell m_tag value.
    inline Dir unvisited(size_t idx, Dir d=Dir::all) const {
        return Dir(m_tags[idx] & _t(d));
    }

    // Get a cell coordinate from a sequential index
    Coord coord(size_t i) const
    {
        return {long(i) / m_gridsize.c, long(i) % m_gridsize.c};
    }

    // Get a sequential index from a cell coordinate.
    size_t seq(const Coord &crd) const {
        return crd.seq(m_gridsize);
    }

    // Step a sequential index in a direction.
    size_t stepidx(const size_t idx, const Dir d) const
    {
        switch (d) {
            case Dir::left: return idx - 1;
            case Dir::down: return idx + m_gridsize.c;
            case Dir::right: return idx + 1;
            case Dir::up: return idx - m_gridsize.c;
            default: return idx;
      }
    }

    // Search for a new starting square
    size_t search_start_cell(size_t i = 0) const
    {
        // Skip cells without any unvisited edges.
        while (i < m_tags.size() && !unvisited(i)) ++i;
        return i;
    }

    // Get the next direction for a cell index after the prev direction.
    Dir next_dir(size_t idx, Dir prev = Dir::all) const
    {
        Dir next = unvisited(idx);

        // Treat ambiguous cases as two separate regions in one square. If
        // there are two possible next directions, pick based on the prev
        // direction. If prev=all we are starting a new line so pick the one
        // that leads "forwards" (right or down) in the search.
        switch (next) {
            case Dir::leftright:
            // We must be coming from up, down, or starting a new line.
            assert(prev == Dir::up || prev == Dir::down || prev == Dir::all);
            return (prev == Dir::up) ? Dir::left : Dir::right;
            case Dir::updown:
            // We must be coming from left, right, or starting a new line.
            assert(prev == Dir::left || prev == Dir::right || prev == Dir::all);
            return (prev == Dir::right) ? Dir::up : Dir::down;
        default:
            // Next must be a single direction or none to stop.
            assert(next == Dir::none || next == Dir::left || next == Dir::down ||
                   next == Dir::right || next == Dir::up);
            return next;
        }
    }

    struct CellIt {
        Coord crd; Dir dir= Dir::none; const Rst *grid = nullptr;

        TRasterValue<Rst> operator*() const { return isoval(*grid, crd); }
        CellIt& operator++() { crd = step(crd, dir); return *this; }
        CellIt operator++(int) { CellIt it = *this; ++(*this); return it; }
        bool operator!=(const CellIt &it) { return crd.r != it.crd.r || crd.c != it.crd.c; }

        using value_type        = TRasterValue<Rst>;
        using pointer           = TRasterValue<Rst> *;
        using reference         = TRasterValue<Rst> &;
        using difference_type   = long;
        using iterator_category = std::forward_iterator_tag;
    };

    // Two cell iterators representing an edge of a square. This is then
    // used for binary search for the first active pixel on the edge.
    struct Edge { CellIt from, to; };

    Edge _edge(const Coord &ringvertex) const
    {
        size_t idx = ringvertex.r;
        Coord cell = coord(idx);
        Dir d = Dir(ringvertex.c);

        switch (d) {
            case Dir::left: return {{tl(cell), Dir::down,  m_rst}, {bl(cell)}};
            case Dir::down:  return {{bl(cell), Dir::right, m_rst}, {br(cell)}};
            case Dir::right: return {{br(cell), Dir::up,    m_rst}, {tr(cell)}};
            case Dir::up:    return {{tr(cell), Dir::left,  m_rst}, {tl(cell)}};
            default: assert(false);
        }
        return {};
    }

    Edge edge(const Coord &ringvertex) const
    {
        const long R = rows(*m_rst), C = cols(*m_rst);

        Edge e = _edge(ringvertex);
        e.to.dir = e.from.dir;
        ++e.to;

        e.from.crd.r = std::clamp(e.from.crd.r, 0l, R-1);
        e.from.crd.c = std::clamp(e.from.crd.c, 0l, C-1);
        e.to.crd.r = std::clamp(e.to.crd.r, 0l, R);
        e.to.crd.c = std::clamp(e.to.crd.c, 0l, C);
        return e;
    }

public:
    explicit Grid(const Rst &rst, const Coord &window)
        : m_rst{&rst}
        , m_window{window}
        , m_gridsize{2 + long(rows(rst)) / m_window.r,
                     2 + long(cols(rst)) / m_window.c}
        , m_tags(m_gridsize.r * m_gridsize.c, 0)
    {}

    // Go through the cells and mark them with the appropriate tag.
    template<class ExecutionPolicy>
    void tag_grid(ExecutionPolicy &&policy, TRasterValue<Rst> isoval)
    {
        // parallel for r
        for_each (std::forward<ExecutionPolicy>(policy),
                 m_tags.begin(), m_tags.end(),
                 [this, isoval](uint8_t& tag, size_t idx) {
            tag = get_tag_for_cell(coord(idx), isoval);
        });
    }

    // Scan for the rings on the tagged grid. Each ring vertex stores the
    // sequential index of the cell and the next direction (Dir).
    // This info can be used later to calculate the exact raster coordinate.
    std::vector<Ring> scan_rings()
    {
        std::vector<Ring> rings;
        size_t startidx = 0;
        while ((startidx = search_start_cell(startidx)) < m_tags.size()) {
            Ring ring;

            size_t idx = startidx;
            Dir next = next_dir(idx);
            while (next != Dir::none) {
                Coord ringvertex{long(idx), long(next)};
                ring.emplace_back(ringvertex);
                set_visited(idx, next);
                idx = stepidx(idx, next);
                next = next_dir(idx, next);
            }
            if (ring.size() > 1) {
                rings.emplace_back(ring);
            }
        }
        return rings;
    }

    // Calculate the exact raster position from the cells which store the
    // sequantial index of the square and the next direction
    template<class ExecutionPolicy>
    void interpolate_rings(ExecutionPolicy && policy,
                           std::vector<Ring> &rings,
                           TRasterValue<Rst>  isov)
    {
        for_each(std::forward<ExecutionPolicy>(policy),
                 rings.begin(), rings.end(), [this, isov] (Ring &ring, size_t)
        {
            for (Coord &ringvertex : ring) {
                Edge e = edge(ringvertex);

                CellIt found = std::lower_bound(e.from, e.to, isov);
                ringvertex = found.crd;
            }
        });
    }
};

template<class Raster, class ExecutionPolicy>
std::vector<marchsq::Ring> execute_with_policy(ExecutionPolicy &&   policy,
                                               const Raster &       raster,
                                               TRasterValue<Raster> isoval,
                                               Coord windowsize = {})
{
    if (!rows(raster) || !cols(raster)) return {};

    size_t ratio = cols(raster) / rows(raster);

    if (!windowsize.r) windowsize.r = 2;
    if (!windowsize.c)
        windowsize.c = std::max(2l, long(windowsize.r * ratio));

    Grid<Raster> grid{raster, windowsize};

    grid.tag_grid(std::forward<ExecutionPolicy>(policy), isoval);
    std::vector<marchsq::Ring> rings = grid.scan_rings();
    grid.interpolate_rings(std::forward<ExecutionPolicy>(policy), rings, isoval);

    return rings;
}

template<class Raster>
std::vector<marchsq::Ring> execute(const Raster &raster,
                                   TRasterValue<Raster> isoval,
                                   Coord                windowsize = {})
{
    return execute_with_policy(nullptr, raster, isoval, windowsize);
}

} // namespace __impl

using __impl::execute_with_policy;
using __impl::execute;

} // namespace marchsq

#endif // MARCHINGSQUARES_HPP
