#ifndef SPATINDEX_HPP
#define SPATINDEX_HPP

#include <memory>
#include <utility>
#include <vector>

#include <Eigen/Geometry>

namespace Slic3r {
namespace sla {

typedef Eigen::Matrix<double,   3, 1, Eigen::DontAlign> Vec3d;
using SpatElement = std::pair<Vec3d, unsigned>;

class SpatIndex {
    class Impl;

    // We use Pimpl because it takes a long time to compile boost headers which
    // is the engine of this class. We include it only in the cpp file.
    std::unique_ptr<Impl> m_impl;
public:

    SpatIndex();
    ~SpatIndex();

    SpatIndex(const SpatIndex&);
    SpatIndex(SpatIndex&&);
    SpatIndex& operator=(const SpatIndex&);
    SpatIndex& operator=(SpatIndex&&);

    void insert(const SpatElement&);
    bool remove(const SpatElement&);

    inline void insert(const Vec3d& v, unsigned idx)
    {
        insert(std::make_pair(v, unsigned(idx)));
    }

    std::vector<SpatElement> query(std::function<bool(const SpatElement&)>);
    std::vector<SpatElement> nearest(const Vec3d&, unsigned k);

    // For testing
    size_t size() const;
    bool empty() const { return size() == 0; }
};

}
}

#endif // SPATINDEX_HPP
