#include "SLACommon.hpp"
#include <libslic3r/Format/objparser.hpp>

namespace Slic3r { namespace sla {

Contour3D::Contour3D(const TriangleMesh &trmesh)
{
    points.reserve(trmesh.its.vertices.size());
    faces3.reserve(trmesh.its.indices.size());
    
    for (auto &v : trmesh.its.vertices)
        points.emplace_back(v.cast<double>());
    
    std::copy(trmesh.its.indices.begin(), trmesh.its.indices.end(),
              std::back_inserter(faces3));
}

Contour3D::Contour3D(TriangleMesh &&trmesh)
{
    points.reserve(trmesh.its.vertices.size());
    
    for (auto &v : trmesh.its.vertices)
        points.emplace_back(v.cast<double>());
    
    faces3.swap(trmesh.its.indices);
}

Contour3D::Contour3D(const EigenMesh3D &emesh) {
    points.reserve(size_t(emesh.V().rows()));
    faces3.reserve(size_t(emesh.F().rows()));
    
    for (int r = 0; r < emesh.V().rows(); r++)
        points.emplace_back(emesh.V().row(r).cast<double>());
    
    for (int i = 0; i < emesh.F().rows(); i++)
        faces3.emplace_back(emesh.F().row(i));
}

Contour3D &Contour3D::merge(const Contour3D &ctr)
{
    auto N = coord_t(points.size());
    auto N_f3 = faces3.size();
    auto N_f4 = faces4.size();
    
    points.insert(points.end(), ctr.points.begin(), ctr.points.end());
    faces3.insert(faces3.end(), ctr.faces3.begin(), ctr.faces3.end());
    faces4.insert(faces4.end(), ctr.faces4.begin(), ctr.faces4.end());
    
    for(size_t n = N_f3; n < faces3.size(); n++) {
        auto& idx = faces3[n]; idx.x() += N; idx.y() += N; idx.z() += N;
    }
    
    for(size_t n = N_f4; n < faces4.size(); n++) {
        auto& idx = faces4[n]; for (int k = 0; k < 4; k++) idx(k) += N;
    }        
    
    return *this;
}

Contour3D &Contour3D::merge(const Pointf3s &triangles)
{
    const size_t offs = points.size();
    points.insert(points.end(), triangles.begin(), triangles.end());
    faces3.reserve(faces3.size() + points.size() / 3);
    
    for(int i = int(offs); i < int(points.size()); i += 3)
        faces3.emplace_back(i, i + 1, i + 2);
    
    return *this;
}

void Contour3D::to_obj(std::ostream &stream)
{
    for(auto& p : points)
        stream << "v " << p.transpose() << "\n";
    
    for(auto& f : faces3) 
        stream << "f " << (f + Vec3i(1, 1, 1)).transpose() << "\n";
    
    for(auto& f : faces4)
        stream << "f " << (f + Vec4i(1, 1, 1, 1)).transpose() << "\n";
}

void Contour3D::from_obj(std::istream &stream)
{
    ObjParser::ObjData data;
    ObjParser::objparse(stream, data);
    
    points.reserve(data.coordinates.size() / 4 + 1);
    auto &coords = data.coordinates;
    for (size_t i = 0; i < coords.size(); i += 4)
        points.emplace_back(coords[i], coords[i + 1], coords[i + 2]);
    
    Vec3i triangle;
    Vec4i quad;
    size_t v = 0;
    while(v < data.vertices.size()) {
        size_t N = 0;
        size_t i = v;
        while (data.vertices[v++].coordIdx != -1) ++N;
        
        std::function<void(int, int)> setfn;
        if (N < 3 || N > 4) continue;
        else if (N == 3) setfn = [&triangle](int k, int f) { triangle(k) = f; };
        else setfn = [&quad](int k, int f) { quad(k) = f; };
        
        for (size_t j = 0; j < N; ++j)
            setfn(int(j), data.vertices[i + j].coordIdx);
    }
}

TriangleMesh to_triangle_mesh(const Contour3D &ctour) {
    if (ctour.faces4.empty()) return {ctour.points, ctour.faces3};
    
    std::vector<Vec3i> triangles;
    
    triangles.reserve(ctour.faces3.size() + 2 * ctour.faces4.size());
    std::copy(ctour.faces3.begin(), ctour.faces3.end(),
              std::back_inserter(triangles));
    
    for (auto &quad : ctour.faces4) {
        triangles.emplace_back(quad(0), quad(1), quad(2));
        triangles.emplace_back(quad(2), quad(3), quad(0));
    }
    
    return {ctour.points, std::move(triangles)};
}

TriangleMesh to_triangle_mesh(Contour3D &&ctour) {
    if (ctour.faces4.empty())
        return {std::move(ctour.points), std::move(ctour.faces3)};
    
    std::vector<Vec3i> triangles;
    
    triangles.reserve(ctour.faces3.size() + 2 * ctour.faces4.size());
    std::copy(ctour.faces3.begin(), ctour.faces3.end(),
              std::back_inserter(triangles));
    
    for (auto &quad : ctour.faces4) {
        triangles.emplace_back(quad(0), quad(1), quad(2));
        triangles.emplace_back(quad(2), quad(3), quad(0));
    }
    
    return {std::move(ctour.points), std::move(triangles)};
}

}} // namespace Slic3r::sla
