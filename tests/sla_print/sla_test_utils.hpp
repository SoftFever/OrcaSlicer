#ifndef SLA_TEST_UTILS_HPP
#define SLA_TEST_UTILS_HPP

#include <catch2/catch.hpp>
#include <test_utils.hpp>

// Debug
#include <fstream>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"
#include "libslic3r/SLA/SupportTreeBuildsteps.hpp"
#include "libslic3r/SLA/SupportPointGenerator.hpp"
#include "libslic3r/SLA/Raster.hpp"
#include "libslic3r/SLA/ConcaveHull.hpp"
#include "libslic3r/MTUtils.hpp"

#include "libslic3r/SVG.hpp"
#include "libslic3r/Format/OBJ.hpp"

using namespace Slic3r;

enum e_validity {
    ASSUME_NO_EMPTY = 1,
    ASSUME_MANIFOLD = 2,
    ASSUME_NO_REPAIR = 4
};

void check_validity(const TriangleMesh &input_mesh,
                    int flags = ASSUME_NO_EMPTY | ASSUME_MANIFOLD |
                                ASSUME_NO_REPAIR);

struct PadByproducts
{
    ExPolygons   model_contours;
    ExPolygons   support_contours;
    TriangleMesh mesh;
};

void test_concave_hull(const ExPolygons &polys);

void test_pad(const std::string &   obj_filename,
              const sla::PadConfig &padcfg,
              PadByproducts &       out);

inline void test_pad(const std::string &   obj_filename,
              const sla::PadConfig &padcfg = {})
{
    PadByproducts byproducts;
    test_pad(obj_filename, padcfg, byproducts);
}

struct SupportByproducts
{
    std::string             obj_fname;
    std::vector<float>      slicegrid;
    std::vector<ExPolygons> model_slices;
    sla::SupportTreeBuilder supporttree;
    TriangleMesh            input_mesh;
};

const constexpr float CLOSING_RADIUS = 0.005f;

void check_support_tree_integrity(const sla::SupportTreeBuilder &stree,
                                  const sla::SupportConfig &cfg);

void test_supports(const std::string          &obj_filename,
                   const sla::SupportConfig   &supportcfg,
                   const sla::HollowingConfig &hollowingcfg,
                   const sla::DrainHoles      &drainholes,
                   SupportByproducts          &out);

inline void test_supports(const std::string &obj_filename,
                   const sla::SupportConfig &supportcfg,
                   SupportByproducts        &out) 
{
    sla::HollowingConfig hcfg;
    hcfg.enabled = false;
    test_supports(obj_filename, supportcfg, hcfg, {}, out);    
}

inline void test_supports(const std::string &obj_filename,
                   const sla::SupportConfig &supportcfg = {})
{
    SupportByproducts byproducts;
    test_supports(obj_filename, supportcfg, byproducts);
}

void export_failed_case(const std::vector<ExPolygons> &support_slices,
                        const SupportByproducts &byproducts);


void test_support_model_collision(
    const std::string          &obj_filename,
    const sla::SupportConfig   &input_supportcfg,
    const sla::HollowingConfig &hollowingcfg,
    const sla::DrainHoles      &drainholes);

inline void test_support_model_collision(
    const std::string        &obj_filename,
    const sla::SupportConfig &input_supportcfg = {}) 
{
    sla::HollowingConfig hcfg;
    hcfg.enabled = false;
    test_support_model_collision(obj_filename, input_supportcfg, hcfg, {});
}

#endif // SLA_TEST_UTILS_HPP
