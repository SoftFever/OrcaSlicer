#ifndef slic3r_3DScene_hpp_
#define slic3r_3DScene_hpp_

#include <myinit.h>
#include "../Point.hpp"
#include "../Line.hpp"
#include "../TriangleMesh.hpp"

namespace Slic3r {

class GLVertexArray {
    public:
    std::vector<float> verts, norms;
    
    void reserve(size_t len) {
        this->verts.reserve(len);
        this->norms.reserve(len);
    };
    void reserve_more(size_t len) {
        len += this->verts.size();
        this->reserve(len);
    };
    void push_vert(const Pointf3 &point) {
        this->verts.push_back(point.x);
        this->verts.push_back(point.y);
        this->verts.push_back(point.z);
    };
    void push_vert(float x, float y, float z) {
        this->verts.push_back(x);
        this->verts.push_back(y);
        this->verts.push_back(z);
    };
    void push_norm(const Pointf3 &point) {
        this->norms.push_back(point.x);
        this->norms.push_back(point.y);
        this->norms.push_back(point.z);
    };
    void push_norm(float x, float y, float z) {
        this->norms.push_back(x);
        this->norms.push_back(y);
        this->norms.push_back(z);
    };
    void load_mesh(const TriangleMesh &mesh);
};

class _3DScene
{
    public:
    static void _extrusionentity_to_verts_do(const Lines &lines, const std::vector<double> &widths,
        const std::vector<double> &heights, bool closed, double top_z, const Point &copy,
        GLVertexArray* qverts, GLVertexArray* tverts);
};

}

#endif
