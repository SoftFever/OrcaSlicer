/**
 * In this file we will implement the automatic SLA support tree generation.
 *
 */

#include <numeric>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/Common.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>

#include <libslic3r/MTUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Model.hpp>

#include <libnest2d/optimizers/nlopt/genetic.hpp>
#include <libnest2d/optimizers/nlopt/subplex.hpp>
#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>
#include <tbb/mutex.h>
#include <tbb/spin_mutex.h>
#include <libslic3r/I18N.hpp>

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {
namespace sla {

// Compile time configuration value definitions:

// The max Z angle for a normal at which it will get completely ignored.
const double SupportConfig::normal_cutoff_angle = 150.0 * M_PI / 180.0;

// The shortest distance of any support structure from the model surface
const double SupportConfig::safety_distance_mm = 0.5;

const double SupportConfig::max_solo_pillar_height_mm = 15.0;
const double SupportConfig::max_dual_pillar_height_mm = 35.0;
const double   SupportConfig::optimizer_rel_score_diff = 1e-6;
const unsigned SupportConfig::optimizer_max_iterations = 1000;
const unsigned SupportConfig::pillar_cascade_neighbors = 3;
const unsigned SupportConfig::max_bridges_on_pillar = 3;

void SupportTree::retrieve_full_mesh(TriangleMesh &outmesh) const {
    outmesh.merge(retrieve_mesh(MeshType::Support));
    outmesh.merge(retrieve_mesh(MeshType::Pad));
}

std::vector<ExPolygons> SupportTree::slice(
    const std::vector<float> &grid, float cr) const
{
    const TriangleMesh &sup_mesh = retrieve_mesh(MeshType::Support);
    const TriangleMesh &pad_mesh = retrieve_mesh(MeshType::Pad);

    using Slices = std::vector<ExPolygons>;
    auto slices = reserve_vector<Slices>(2);

    if (!sup_mesh.empty()) {
        slices.emplace_back();

        TriangleMeshSlicer sup_slicer(&sup_mesh);
        sup_slicer.slice(grid, cr, &slices.back(), ctl().cancelfn);
    }

    if (!pad_mesh.empty()) {
        slices.emplace_back();

        auto bb = pad_mesh.bounding_box();
        auto maxzit = std::upper_bound(grid.begin(), grid.end(), bb.max.z());
        
        auto cap = grid.end() - maxzit;
        auto padgrid = reserve_vector<float>(size_t(cap > 0 ? cap : 0));
        std::copy(grid.begin(), maxzit, std::back_inserter(padgrid));

        TriangleMeshSlicer pad_slicer(&pad_mesh);
        pad_slicer.slice(padgrid, cr, &slices.back(), ctl().cancelfn);
    }

    size_t len = grid.size();
    for (const Slices &slv : slices) { len = std::min(len, slv.size()); }

    // Either the support or the pad or both has to be non empty
    if (slices.empty()) return {};

    Slices &mrg = slices.front();

    for (auto it = std::next(slices.begin()); it != slices.end(); ++it) {
        for (size_t i = 0; i < len; ++i) {
            Slices &slv = *it;
            std::copy(slv[i].begin(), slv[i].end(), std::back_inserter(mrg[i]));
            slv[i] = {}; // clear and delete
        }
    }

    return mrg;
}

SupportTree::UPtr SupportTree::create(const SupportableMesh &sm,
                                      const JobController &  ctl)
{
    auto builder = make_unique<SupportTreeBuilder>();
    builder->m_ctl = ctl;
    
    if (sm.cfg.enabled) {
        builder->build(sm);
        builder->merge_and_cleanup();   // clean metadata, leave only the meshes.
    } else {
        builder->ground_level = sm.emesh.ground_level();
    }
    
    return std::move(builder);
}

}} // namespace Slic3r::sla
