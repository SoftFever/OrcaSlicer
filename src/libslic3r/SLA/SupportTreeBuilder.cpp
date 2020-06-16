#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/SupportTreeBuildsteps.hpp>
#include <libslic3r/SLA/SupportTreeMesher.hpp>
#include <libslic3r/SLA/Contour3D.hpp>

namespace Slic3r {
namespace sla {

Head::Head(double       r_big_mm,
           double       r_small_mm,
           double       length_mm,
           double       penetration,
           const Vec3d &direction,
           const Vec3d &offset)
    : dir(direction)
    , pos(offset)
    , r_back_mm(r_big_mm)
    , r_pin_mm(r_small_mm)
    , width_mm(length_mm)
    , penetration_mm(penetration)
{
//    mesh = pinhead(r_pin_mm, r_back_mm, width_mm, steps);
    
    // To simplify further processing, we translate the mesh so that the
    // last vertex of the pointing sphere (the pinpoint) will be at (0,0,0)
//    for(auto& p : mesh.points) p.z() -= (fullwidth() - r_back_mm);
}

//Pillar::Pillar(const Vec3d &endp, double h, double radius, size_t st):
//    height{h}, r(radius), steps(st), endpt(endp), starts_from_head(false)
//{
//    assert(steps > 0);

//    if(height > EPSILON) { // Endpoint is below the starting point
        
//        // We just create a bridge geometry with the pillar parameters and
//        // move the data.
//        Contour3D body = cylinder(radius, height, st, endp);
//        mesh.points.swap(body.points);
//        mesh.faces3.swap(body.faces3);
//    }
//}

//Pillar &Pillar::add_base(double baseheight, double radius)
//{
//    if(baseheight <= 0) return *this;
//    if(baseheight > height) baseheight = height;
    
//    assert(steps >= 0);
//    auto last = int(steps - 1);
    
//    if(radius < r ) radius = r;
    
//    double a = 2*PI/steps;
//    double z = endpt(Z) + baseheight;
    
//    for(size_t i = 0; i < steps; ++i) {
//        double phi = i*a;
//        double x = endpt(X) + r*std::cos(phi);
//        double y = endpt(Y) + r*std::sin(phi);
//        base.points.emplace_back(x, y, z);
//    }
    
//    for(size_t i = 0; i < steps; ++i) {
//        double phi = i*a;
//        double x = endpt(X) + radius*std::cos(phi);
//        double y = endpt(Y) + radius*std::sin(phi);
//        base.points.emplace_back(x, y, z - baseheight);
//    }
    
//    auto ep = endpt; ep(Z) += baseheight;
//    base.points.emplace_back(endpt);
//    base.points.emplace_back(ep);
    
//    auto& indices = base.faces3;
//    auto hcenter = int(base.points.size() - 1);
//    auto lcenter = int(base.points.size() - 2);
//    auto offs = int(steps);
//    for(int i = 0; i < last; ++i) {
//        indices.emplace_back(i, i + offs, offs + i + 1);
//        indices.emplace_back(i, offs + i + 1, i + 1);
//        indices.emplace_back(i, i + 1, hcenter);
//        indices.emplace_back(lcenter, offs + i + 1, offs + i);
//    }
    
//    indices.emplace_back(0, last, offs);
//    indices.emplace_back(last, offs + last, offs);
//    indices.emplace_back(hcenter, last, 0);
//    indices.emplace_back(offs, offs + last, lcenter);
//    return *this;
//}

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
    m_pad = o.m_pad;
    m_meshcache = o.m_meshcache;
    m_meshcache_valid = o.m_meshcache_valid;
    m_model_height = o.m_model_height;
    ground_level = o.ground_level;
    return *this;
}

const TriangleMesh &SupportTreeBuilder::merged_mesh(size_t steps) const
{
    if (m_meshcache_valid) return m_meshcache;
    
    Contour3D merged;
    
    for (auto &head : m_heads) {
        if (ctl().stopcondition()) break;
        if (head.is_valid()) merged.merge(get_mesh(head, steps));
    }
    
    for (auto &pill : m_pillars) {
        if (ctl().stopcondition()) break;
        merged.merge(get_mesh(pill, steps));
    }

    for (auto &pedest : m_pedestals) {
        merged.merge(get_mesh(pedest, steps));
    }
    
    for (auto &j : m_junctions) {
        if (ctl().stopcondition()) break;
        merged.merge(get_mesh(j, steps));
    }

    for (auto &bs : m_bridges) {
        if (ctl().stopcondition()) break;
        merged.merge(get_mesh(bs, steps));
    }
    
    for (auto &bs : m_crossbridges) {
        if (ctl().stopcondition()) break;
        merged.merge(get_mesh(bs, steps));
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

}} // namespace Slic3r::sla
