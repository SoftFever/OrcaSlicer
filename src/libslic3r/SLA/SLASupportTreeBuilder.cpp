#include "SLASupportTreeBuilder.hpp"
#include "SLASupportTreeBuildsteps.hpp"

namespace Slic3r {
namespace sla {

Contour3D sphere(double rho, Portion portion, double fa) {
    
    Contour3D ret;
    
    // prohibit close to zero radius
    if(rho <= 1e-6 && rho >= -1e-6) return ret;
    
    auto& vertices = ret.points;
    auto& facets = ret.faces3;
    
    // Algorithm:
    // Add points one-by-one to the sphere grid and form facets using relative
    // coordinates. Sphere is composed effectively of a mesh of stacked circles.
    
    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2*PI / floor(2*PI / fa));
    
    // Ring to be scaled to generate the steps of the sphere
    std::vector<double> ring;
    
    for (double i = 0; i < 2*PI; i+=angle) ring.emplace_back(i);
    
    const auto sbegin = size_t(2*std::get<0>(portion)/angle);
    const auto send = size_t(2*std::get<1>(portion)/angle);
    
    const size_t steps = ring.size();
    const double increment = 1.0 / double(steps);
    
    // special case: first ring connects to 0,0,0
    // insert and form facets.
    if(sbegin == 0)
        vertices.emplace_back(Vec3d(0.0, 0.0, -rho + increment*sbegin*2.0*rho));
    
    auto id = coord_t(vertices.size());
    for (size_t i = 0; i < ring.size(); i++) {
        // Fixed scaling
        const double z = -rho + increment*rho*2.0 * (sbegin + 1.0);
        // radius of the circle for this step.
        const double r = std::sqrt(std::abs(rho*rho - z*z));
        Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
        vertices.emplace_back(Vec3d(b(0), b(1), z));
        
        if (sbegin == 0)
            facets.emplace_back((i == 0) ?
                                    Vec3crd(coord_t(ring.size()), 0, 1) :
                                    Vec3crd(id - 1, 0, id));
        ++id;
    }
    
    // General case: insert and form facets for each step,
    // joining it to the ring below it.
    for (size_t s = sbegin + 2; s < send - 1; s++) {
        const double z = -rho + increment*double(s*2.0*rho);
        const double r = std::sqrt(std::abs(rho*rho - z*z));
        
        for (size_t i = 0; i < ring.size(); i++) {
            Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
            vertices.emplace_back(Vec3d(b(0), b(1), z));
            auto id_ringsize = coord_t(id - int(ring.size()));
            if (i == 0) {
                // wrap around
                facets.emplace_back(Vec3crd(id - 1, id,
                                            id + coord_t(ring.size() - 1)));
                facets.emplace_back(Vec3crd(id - 1, id_ringsize, id));
            } else {
                facets.emplace_back(Vec3crd(id_ringsize - 1, id_ringsize, id));
                facets.emplace_back(Vec3crd(id - 1, id_ringsize - 1, id));
            }
            id++;
        }
    }
    
    // special case: last ring connects to 0,0,rho*2.0
    // only form facets.
    if(send >= size_t(2*PI / angle)) {
        vertices.emplace_back(Vec3d(0.0, 0.0, -rho + increment*send*2.0*rho));
        for (size_t i = 0; i < ring.size(); i++) {
            auto id_ringsize = coord_t(id - int(ring.size()));
            if (i == 0) {
                // third vertex is on the other side of the ring.
                facets.emplace_back(Vec3crd(id - 1, id_ringsize, id));
            } else {
                auto ci = coord_t(id_ringsize + coord_t(i));
                facets.emplace_back(Vec3crd(ci - 1, ci, id));
            }
        }
    }
    id++;
    
    return ret;
}

Contour3D cylinder(double r, double h, size_t ssteps, const Vec3d &sp)
{
    Contour3D ret;
    
    auto steps = int(ssteps);
    auto& points = ret.points;
    auto& indices = ret.faces3;
    points.reserve(2*ssteps);
    double a = 2*PI/steps;
    
    Vec3d jp = sp;
    Vec3d endp = {sp(X), sp(Y), sp(Z) + h};
    
    // Upper circle points
    for(int i = 0; i < steps; ++i) {
        double phi = i*a;
        double ex = endp(X) + r*std::cos(phi);
        double ey = endp(Y) + r*std::sin(phi);
        points.emplace_back(ex, ey, endp(Z));
    }
    
    // Lower circle points
    for(int i = 0; i < steps; ++i) {
        double phi = i*a;
        double x = jp(X) + r*std::cos(phi);
        double y = jp(Y) + r*std::sin(phi);
        points.emplace_back(x, y, jp(Z));
    }
    
    // Now create long triangles connecting upper and lower circles
    indices.reserve(2*ssteps);
    auto offs = steps;
    for(int i = 0; i < steps - 1; ++i) {
        indices.emplace_back(i, i + offs, offs + i + 1);
        indices.emplace_back(i, offs + i + 1, i + 1);
    }
    
    // Last triangle connecting the first and last vertices
    auto last = steps - 1;
    indices.emplace_back(0, last, offs);
    indices.emplace_back(last, offs + last, offs);
    
    // According to the slicing algorithms, we need to aid them with generating
    // a watertight body. So we create a triangle fan for the upper and lower
    // ending of the cylinder to close the geometry.
    points.emplace_back(jp); int ci = int(points.size() - 1);
    for(int i = 0; i < steps - 1; ++i)
        indices.emplace_back(i + offs + 1, i + offs, ci);
    
    indices.emplace_back(offs, steps + offs - 1, ci);
    
    points.emplace_back(endp); ci = int(points.size() - 1);
    for(int i = 0; i < steps - 1; ++i)
        indices.emplace_back(ci, i, i + 1);
    
    indices.emplace_back(steps - 1, 0, ci);
    
    return ret;
}

Head::Head(double       r_big_mm,
           double       r_small_mm,
           double       length_mm,
           double       penetration,
           const Vec3d &direction,
           const Vec3d &offset,
           const size_t circlesteps)
    : steps(circlesteps)
    , dir(direction)
    , tr(offset)
    , r_back_mm(r_big_mm)
    , r_pin_mm(r_small_mm)
    , width_mm(length_mm)
    , penetration_mm(penetration)
{
    assert(width_mm > 0.);
    assert(r_back_mm > 0.);
    assert(r_pin_mm > 0.);
    
    // We create two spheres which will be connected with a robe that fits
    // both circles perfectly.
    
    // Set up the model detail level
    const double detail = 2*PI/steps;
    
    // We don't generate whole circles. Instead, we generate only the
    // portions which are visible (not covered by the robe) To know the
    // exact portion of the bottom and top circles we need to use some
    // rules of tangent circles from which we can derive (using simple
    // triangles the following relations:
    
    // The height of the whole mesh
    const double h = r_big_mm + r_small_mm + width_mm;
    double phi = PI/2 - std::acos( (r_big_mm - r_small_mm) / h );
    
    // To generate a whole circle we would pass a portion of (0, Pi)
    // To generate only a half horizontal circle we can pass (0, Pi/2)
    // The calculated phi is an offset to the half circles needed to smooth
    // the transition from the circle to the robe geometry
    
    auto&& s1 = sphere(r_big_mm, make_portion(0, PI/2 + phi), detail);
    auto&& s2 = sphere(r_small_mm, make_portion(PI/2 + phi, PI), detail);
    
    for(auto& p : s2.points) p.z() += h;
    
    mesh.merge(s1);
    mesh.merge(s2);
    
    for(size_t idx1 = s1.points.size() - steps, idx2 = s1.points.size();
        idx1 < s1.points.size() - 1;
        idx1++, idx2++)
    {
        coord_t i1s1 = coord_t(idx1), i1s2 = coord_t(idx2);
        coord_t i2s1 = i1s1 + 1, i2s2 = i1s2 + 1;
        
        mesh.faces3.emplace_back(i1s1, i2s1, i2s2);
        mesh.faces3.emplace_back(i1s1, i2s2, i1s2);
    }
    
    auto i1s1 = coord_t(s1.points.size()) - coord_t(steps);
    auto i2s1 = coord_t(s1.points.size()) - 1;
    auto i1s2 = coord_t(s1.points.size());
    auto i2s2 = coord_t(s1.points.size()) + coord_t(steps) - 1;
    
    mesh.faces3.emplace_back(i2s2, i2s1, i1s1);
    mesh.faces3.emplace_back(i1s2, i2s2, i1s1);
    
    // To simplify further processing, we translate the mesh so that the
    // last vertex of the pointing sphere (the pinpoint) will be at (0,0,0)
    for(auto& p : mesh.points) p.z() -= (h + r_small_mm - penetration_mm);
}

Pillar::Pillar(const Vec3d &jp, const Vec3d &endp, double radius, size_t st):
    r(radius), steps(st), endpt(endp), starts_from_head(false)
{
    assert(steps > 0);
    
    height = jp(Z) - endp(Z);
    if(height > EPSILON) { // Endpoint is below the starting point
        
        // We just create a bridge geometry with the pillar parameters and
        // move the data.
        Contour3D body = cylinder(radius, height, st, endp);
        mesh.points.swap(body.points);
        mesh.faces3.swap(body.faces3);
    }
}

Pillar &Pillar::add_base(double baseheight, double radius)
{
    if(baseheight <= 0) return *this;
    if(baseheight > height) baseheight = height;
    
    assert(steps >= 0);
    auto last = int(steps - 1);
    
    if(radius < r ) radius = r;
    
    double a = 2*PI/steps;
    double z = endpt(Z) + baseheight;
    
    for(size_t i = 0; i < steps; ++i) {
        double phi = i*a;
        double x = endpt(X) + r*std::cos(phi);
        double y = endpt(Y) + r*std::sin(phi);
        base.points.emplace_back(x, y, z);
    }
    
    for(size_t i = 0; i < steps; ++i) {
        double phi = i*a;
        double x = endpt(X) + radius*std::cos(phi);
        double y = endpt(Y) + radius*std::sin(phi);
        base.points.emplace_back(x, y, z - baseheight);
    }
    
    auto ep = endpt; ep(Z) += baseheight;
    base.points.emplace_back(endpt);
    base.points.emplace_back(ep);
    
    auto& indices = base.faces3;
    auto hcenter = int(base.points.size() - 1);
    auto lcenter = int(base.points.size() - 2);
    auto offs = int(steps);
    for(int i = 0; i < last; ++i) {
        indices.emplace_back(i, i + offs, offs + i + 1);
        indices.emplace_back(i, offs + i + 1, i + 1);
        indices.emplace_back(i, i + 1, hcenter);
        indices.emplace_back(lcenter, offs + i + 1, offs + i);
    }
    
    indices.emplace_back(0, last, offs);
    indices.emplace_back(last, offs + last, offs);
    indices.emplace_back(hcenter, last, 0);
    indices.emplace_back(offs, offs + last, lcenter);
    return *this;
}

Bridge::Bridge(const Vec3d &j1, const Vec3d &j2, double r_mm, size_t steps):
    r(r_mm), startp(j1), endp(j2)
{
    using Quaternion = Eigen::Quaternion<double>;
    Vec3d dir = (j2 - j1).normalized();
    double d = distance(j2, j1);
    
    mesh = cylinder(r, d, steps);
    
    auto quater = Quaternion::FromTwoVectors(Vec3d{0,0,1}, dir);
    for(auto& p : mesh.points) p = quater * p + j1;
}

CompactBridge::CompactBridge(const Vec3d &sp,
                             const Vec3d &ep,
                             const Vec3d &n,
                             double       r,
                             bool         endball,
                             size_t       steps)
{
    Vec3d startp = sp + r * n;
    Vec3d dir = (ep - startp).normalized();
    Vec3d endp = ep - r * dir;
    
    Bridge br(startp, endp, r, steps);
    mesh.merge(br.mesh);
    
    // now add the pins
    double fa = 2*PI/steps;
    auto upperball = sphere(r, Portion{PI / 2 - fa, PI}, fa);
    for(auto& p : upperball.points) p += startp;
    
    if(endball) {
        auto lowerball = sphere(r, Portion{0, PI/2 + 2*fa}, fa);
        for(auto& p : lowerball.points) p += endp;
        mesh.merge(lowerball);
    }
    
    mesh.merge(upperball);
}

Pad::Pad(const TriangleMesh &support_mesh,
         const ExPolygons &  model_contours,
         double              ground_level,
         const PadConfig &   pcfg,
         ThrowOnCancel       thr)
    : cfg(pcfg)
    , zlevel(ground_level + pcfg.full_height() - pcfg.required_elevation())
{
    thr();
    
    ExPolygons sup_contours;
    
    float zstart = float(zlevel);
    float zend   = zstart + float(pcfg.full_height() + EPSILON);
    
    pad_blueprint(support_mesh, sup_contours, grid(zstart, zend, 0.1f), thr);
    create_pad(sup_contours, model_contours, tmesh, pcfg);
    
    tmesh.translate(0, 0, float(zlevel));
    if (!tmesh.empty()) tmesh.require_shared_vertices();
}

const TriangleMesh &SupportTreeBuilder::add_pad(const ExPolygons &modelbase,
                                                const PadConfig & cfg)
{
    m_pad = Pad{merged_mesh(), modelbase, ground_level, cfg, ctl().cancelfn};
    return m_pad.tmesh;
}

SupportTreeBuilder::SupportTreeBuilder(SupportTreeBuilder &&o)
    : m_heads(std::move(o.m_heads))
    , m_head_indices{std::move(o.m_head_indices)}
    , m_pillars{std::move(o.m_pillars)}
    , m_bridges{std::move(o.m_bridges)}
    , m_crossbridges{std::move(o.m_crossbridges)}
    , m_compact_bridges{std::move(o.m_compact_bridges)}
    , m_pad{std::move(o.m_pad)}
    , m_meshcache{std::move(o.m_meshcache)}
    , m_meshcache_valid{o.m_meshcache_valid}
    , m_model_height{o.m_model_height}
    , ground_level{o.ground_level}
{}

SupportTreeBuilder::SupportTreeBuilder(const SupportTreeBuilder &o)
    : m_heads(o.m_heads)
    , m_head_indices{o.m_head_indices}
    , m_pillars{o.m_pillars}
    , m_bridges{o.m_bridges}
    , m_crossbridges{o.m_crossbridges}
    , m_compact_bridges{o.m_compact_bridges}
    , m_pad{o.m_pad}
    , m_meshcache{o.m_meshcache}
    , m_meshcache_valid{o.m_meshcache_valid}
    , m_model_height{o.m_model_height}
    , ground_level{o.ground_level}
{}

SupportTreeBuilder &SupportTreeBuilder::operator=(SupportTreeBuilder &&o)
{
    m_heads = std::move(o.m_heads);
    m_head_indices = std::move(o.m_head_indices);
    m_pillars = std::move(o.m_pillars);
    m_bridges = std::move(o.m_bridges);
    m_crossbridges = std::move(o.m_crossbridges);
    m_compact_bridges = std::move(o.m_compact_bridges);
    m_pad = std::move(o.m_pad);
    m_meshcache = std::move(o.m_meshcache);
    m_meshcache_valid = o.m_meshcache_valid;
    m_model_height = o.m_model_height;
    ground_level = o.ground_level;
    return *this;
}

SupportTreeBuilder &SupportTreeBuilder::operator=(const SupportTreeBuilder &o)
{
    m_heads = o.m_heads;
    m_head_indices = o.m_head_indices;
    m_pillars = o.m_pillars;
    m_bridges = o.m_bridges;
    m_crossbridges = o.m_crossbridges;
    m_compact_bridges = o.m_compact_bridges;
    m_pad = o.m_pad;
    m_meshcache = o.m_meshcache;
    m_meshcache_valid = o.m_meshcache_valid;
    m_model_height = o.m_model_height;
    ground_level = o.ground_level;
    return *this;
}

const TriangleMesh &SupportTreeBuilder::merged_mesh() const
{
    if (m_meshcache_valid) return m_meshcache;
    
    Contour3D merged;
    
    for (auto &head : m_heads) {
        if (ctl().stopcondition()) break;
        if (head.is_valid()) merged.merge(head.mesh);
    }
    
    for (auto &stick : m_pillars) {
        if (ctl().stopcondition()) break;
        merged.merge(stick.mesh);
        merged.merge(stick.base);
    }
    
    for (auto &j : m_junctions) {
        if (ctl().stopcondition()) break;
        merged.merge(j.mesh);
    }
    
    for (auto &cb : m_compact_bridges) {
        if (ctl().stopcondition()) break;
        merged.merge(cb.mesh);
    }
    
    for (auto &bs : m_bridges) {
        if (ctl().stopcondition()) break;
        merged.merge(bs.mesh);
    }
    
    for (auto &bs : m_crossbridges) {
        if (ctl().stopcondition()) break;
        merged.merge(bs.mesh);
    }
    
    if (ctl().stopcondition()) {
        // In case of failure we have to return an empty mesh
        m_meshcache = TriangleMesh();
        return m_meshcache;
    }
    
    m_meshcache = to_triangle_mesh(merged);
    
    // The mesh will be passed by const-pointer to TriangleMeshSlicer,
    // which will need this.
    if (!m_meshcache.empty()) m_meshcache.require_shared_vertices();
    
    BoundingBoxf3 &&bb = m_meshcache.bounding_box();
    m_model_height       = bb.max(Z) - bb.min(Z);
    
    m_meshcache_valid = true;
    return m_meshcache;
}

double SupportTreeBuilder::full_height() const
{
    if (merged_mesh().empty() && !pad().empty())
        return pad().cfg.full_height();
    
    double h = mesh_height();
    if (!pad().empty()) h += pad().cfg.required_elevation();
    return h;
}

const TriangleMesh &SupportTreeBuilder::merge_and_cleanup()
{
    // in case the mesh is not generated, it should be...
    auto &ret = merged_mesh(); 
    
    // Doing clear() does not garantee to release the memory.
    m_heads = {};
    m_head_indices = {};
    m_pillars = {};
    m_junctions = {};
    m_bridges = {};
    m_compact_bridges = {};
    
    return ret;
}

const TriangleMesh &SupportTreeBuilder::retrieve_mesh(MeshType meshtype) const
{
    switch(meshtype) {
    case MeshType::Support: return merged_mesh();
    case MeshType::Pad:     return pad().tmesh;
    }
    
    return m_meshcache;
}

bool SupportTreeBuilder::build(const SupportableMesh &sm)
{
    ground_level = sm.emesh.ground_level() - sm.cfg.object_elevation_mm;
    return SupportTreeBuildsteps::execute(*this, sm);
}

}
}
