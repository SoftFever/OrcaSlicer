#include <unordered_set>
#include <unordered_map>
#include <random>

// Debug
#include <fstream>

#include <gtest/gtest.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Format/OBJ.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/SLA/SLAPad.hpp"
#include "libslic3r/SLA/SLASupportTreeBuilder.hpp"
#include "libslic3r/SLA/SLASupportTreeBuildsteps.hpp"
#include "libslic3r/SLA/SLAAutoSupports.hpp"
#include "libslic3r/SLA/SLARaster.hpp"
#include "libslic3r/MTUtils.hpp"

#include "libslic3r/SVG.hpp"
#include "libslic3r/Format/OBJ.hpp"

#if defined(WIN32) || defined(_WIN32)
#define PATH_SEPARATOR R"(\)"
#else
#define PATH_SEPARATOR R"(/)"
#endif

namespace  {
using namespace Slic3r;

TriangleMesh load_model(const std::string &obj_filename)
{
    TriangleMesh mesh;
    auto fpath = TEST_DATA_DIR PATH_SEPARATOR + obj_filename;
    load_obj(fpath.c_str(), &mesh);
    return mesh;
}

enum e_validity {
    ASSUME_NO_EMPTY = 1,
    ASSUME_MANIFOLD = 2,
    ASSUME_NO_REPAIR = 4
};

void check_validity(const TriangleMesh &input_mesh,
                    int flags = ASSUME_NO_EMPTY | ASSUME_MANIFOLD |
                                ASSUME_NO_REPAIR)
{
    TriangleMesh mesh{input_mesh};

    if (flags & ASSUME_NO_EMPTY) {
        ASSERT_FALSE(mesh.empty());
    } else if (mesh.empty())
        return; // If it can be empty and it is, there is nothing left to do.

    ASSERT_TRUE(stl_validate(&mesh.stl));

    bool do_update_shared_vertices = false;
    mesh.repair(do_update_shared_vertices);

    if (flags & ASSUME_NO_REPAIR) {
        ASSERT_FALSE(mesh.needed_repair());
    }

    if (flags & ASSUME_MANIFOLD) {
        mesh.require_shared_vertices();
        if (!mesh.is_manifold()) mesh.WriteOBJFile("non_manifold.obj");
        ASSERT_TRUE(mesh.is_manifold());
    }
}

struct PadByproducts
{
    ExPolygons   model_contours;
    ExPolygons   support_contours;
    TriangleMesh mesh;
};

void test_pad(const std::string &   obj_filename,
              const sla::PadConfig &padcfg,
              PadByproducts &       out)
{
    ASSERT_TRUE(padcfg.validate().empty());

    TriangleMesh mesh = load_model(obj_filename);

    ASSERT_FALSE(mesh.empty());

    // Create pad skeleton only from the model
    Slic3r::sla::pad_blueprint(mesh, out.model_contours);

    ASSERT_FALSE(out.model_contours.empty());

    // Create the pad geometry for the model contours only
    Slic3r::sla::create_pad({}, out.model_contours, out.mesh, padcfg);

    check_validity(out.mesh);

    auto bb = out.mesh.bounding_box();
    ASSERT_DOUBLE_EQ(bb.max.z() - bb.min.z(),
                     padcfg.full_height());
}

void test_pad(const std::string &   obj_filename,
              const sla::PadConfig &padcfg = {})
{
    PadByproducts byproducts;
    test_pad(obj_filename, padcfg, byproducts);
}

struct SupportByproducts
{
    std::vector<float>      slicegrid;
    std::vector<ExPolygons> model_slices;
    sla::SupportTreeBuilder supporttree;
};

const constexpr float CLOSING_RADIUS = 0.005f;

void check_support_tree_integrity(const sla::SupportTreeBuilder &stree,
                                  const sla::SupportConfig &cfg)
{
    double gnd  = stree.ground_level;
    double H1   = cfg.max_solo_pillar_height_mm;
    double H2   = cfg.max_dual_pillar_height_mm;
    
    for (const sla::Head &head : stree.heads()) {
        ASSERT_TRUE(!head.is_valid() || 
                     head.pillar_id != sla::ID_UNSET || 
                     head.bridge_id != sla::ID_UNSET);
    }

    for (const sla::Pillar &pillar : stree.pillars()) {
        if (std::abs(pillar.endpoint().z() - gnd) < EPSILON) {
            double h = pillar.height;

            if (h > H1) ASSERT_GE(pillar.links, 1);
            else if(h > H2) { ASSERT_GE(pillar.links, 2); }
        }

        ASSERT_LE(pillar.links, cfg.pillar_cascade_neighbors);
        ASSERT_LE(pillar.bridges, cfg.max_bridges_on_pillar);
    }

    double max_bridgelen = 0.;
    auto chck_bridge = [&cfg](const sla::Bridge &bridge, double &max_brlen) {
        Vec3d n = bridge.endp - bridge.startp;
        double d = sla::distance(n);
        max_brlen = std::max(d, max_brlen);

        double z     = n.z();
        double polar = std::acos(z / d);
        double slope = -polar + PI / 2.;
        ASSERT_GE(std::abs(slope), cfg.bridge_slope - EPSILON);
    };

    for (auto &bridge : stree.bridges()) chck_bridge(bridge, max_bridgelen);
    ASSERT_LE(max_bridgelen, cfg.max_bridge_length_mm);

    max_bridgelen = 0;
    for (auto &bridge : stree.crossbridges()) chck_bridge(bridge, max_bridgelen);

    double md = cfg.max_pillar_link_distance_mm / std::cos(-cfg.bridge_slope);
    ASSERT_LE(max_bridgelen, md);
}

void test_supports(const std::string &       obj_filename,
                   const sla::SupportConfig &supportcfg,
                   SupportByproducts &       out)
{
    using namespace Slic3r;
    TriangleMesh mesh = load_model(obj_filename);

    ASSERT_FALSE(mesh.empty());

    TriangleMeshSlicer slicer{&mesh};

    auto   bb      = mesh.bounding_box();
    double zmin    = bb.min.z();
    double zmax    = bb.max.z();
    double gnd     = zmin - supportcfg.object_elevation_mm;
    auto   layer_h = 0.05f;

    out.slicegrid = grid(float(gnd), float(zmax), layer_h);
    slicer.slice(out.slicegrid , CLOSING_RADIUS, &out.model_slices, []{});

    // Create the special index-triangle mesh with spatial indexing which
    // is the input of the support point and support mesh generators
    sla::EigenMesh3D emesh{mesh};

    // Create the support point generator
    sla::SLAAutoSupports::Config autogencfg;
    autogencfg.head_diameter = float(2 * supportcfg.head_front_radius_mm);
    sla::SLAAutoSupports point_gen{emesh, out.model_slices, out.slicegrid,
                                   autogencfg, [] {}, [](int) {}};

    // Get the calculated support points.
    std::vector<sla::SupportPoint> support_points = point_gen.output();

    int validityflags = ASSUME_NO_REPAIR;

    // If there is no elevation, support points shall be removed from the
    // bottom of the object.
    if (std::abs(supportcfg.object_elevation_mm) < EPSILON) {
        sla::remove_bottom_points(support_points, zmin,
                                  supportcfg.base_height_mm);
    } else {
        // Should be support points at least on the bottom of the model
        ASSERT_FALSE(support_points.empty());

        // Also the support mesh should not be empty.
        validityflags |= ASSUME_NO_EMPTY;
    }

    // Generate the actual support tree
    sla::SupportTreeBuilder treebuilder;
    treebuilder.build(sla::SupportableMesh{emesh, support_points, supportcfg});

    check_support_tree_integrity(treebuilder, supportcfg);

    const TriangleMesh &output_mesh = treebuilder.retrieve_mesh();

    check_validity(output_mesh, validityflags);

    // Quick check if the dimensions and placement of supports are correct
    auto obb = output_mesh.bounding_box();
    
    double allowed_zmin = zmin - supportcfg.object_elevation_mm;
    
    if (std::abs(supportcfg.object_elevation_mm) < EPSILON)
        allowed_zmin = zmin - 2 * supportcfg.head_back_radius_mm;

    if (std::abs(obb.min.z() - allowed_zmin) > EPSILON)
        output_mesh.WriteOBJFile("outmesh_supports.obj");
    
    ASSERT_GE(obb.min.z(), allowed_zmin);
    ASSERT_LE(obb.max.z(), zmax);

    // Move out the support tree into the byproducts, we can examine it further
    // in various tests.
    out.supporttree = std::move(treebuilder);
}

void test_supports(const std::string &       obj_filename,
                   const sla::SupportConfig &supportcfg = {})
{
    SupportByproducts byproducts;
    test_supports(obj_filename, supportcfg, byproducts);
}

void test_support_model_collision(
    const std::string &       obj_filename,
    const sla::SupportConfig &input_supportcfg = {})
{
    SupportByproducts byproducts;

    sla::SupportConfig supportcfg = input_supportcfg;

    // Set head penetration to a small negative value which should ensure that
    // the supports will not touch the model body.
    supportcfg.head_penetration_mm = -input_supportcfg.head_front_radius_mm;

    test_supports(obj_filename, supportcfg, byproducts);

    // Slice the support mesh given the slice grid of the model.
    std::vector<ExPolygons> support_slices =
        byproducts.supporttree.slice(byproducts.slicegrid, CLOSING_RADIUS);

    // The slices originate from the same slice grid so the numbers must match
    ASSERT_EQ(support_slices.size(), byproducts.model_slices.size());

    bool notouch = true;
    for (size_t n = 0; notouch && n < support_slices.size(); ++n) {
        const ExPolygons &sup_slice = support_slices[n];
        const ExPolygons &mod_slice = byproducts.model_slices[n];

        Polygons intersections = intersection(sup_slice, mod_slice);
        notouch = notouch && intersections.empty();
    }

    ASSERT_TRUE(notouch);
}

const char * const BELOW_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
};

const char * const AROUND_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
    "frog_legs.obj",
    "cube_with_concave_hole_enlarged.obj",
};

const char *const SUPPORT_TEST_MODELS[] = {
    "cube_with_concave_hole_enlarged_standing.obj",
    "A_upsidedown.obj"
};

} // namespace

// Test pair hash for 'nums' random number pairs.
template <class I, class II> void test_pairhash()
{
    const constexpr size_t nums = 1000;
    I A[nums] = {0}, B[nums] = {0};
    std::unordered_set<I> CH;
    std::unordered_map<II, std::pair<I, I>> ints;

    std::random_device rd;
    std::mt19937 gen(rd());

    const I Ibits = int(sizeof(I) * CHAR_BIT);
    const II IIbits = int(sizeof(II) * CHAR_BIT);
    const int bits = IIbits / 2 < Ibits ? Ibits / 2 : Ibits;

    const I Imax = I(std::pow(2., bits) - 1);
    std::uniform_int_distribution<I> dis(0, Imax);

    for (size_t i = 0; i < nums;) {
        I a = dis(gen);
        if (CH.find(a) == CH.end()) { CH.insert(a); A[i] = a; ++i; }
    }

    for (size_t i = 0; i < nums;) {
        I b = dis(gen);
        if (CH.find(b) == CH.end()) { CH.insert(b); B[i] = b; ++i; }
    }

    for (size_t i = 0; i < nums; ++i) {
        I a = A[i], b = B[i];

        ASSERT_TRUE(a != b);

        II hash_ab = sla::pairhash<I, II>(a, b);
        II hash_ba = sla::pairhash<I, II>(b, a);
        ASSERT_EQ(hash_ab, hash_ba);

        auto it = ints.find(hash_ab);

        if (it != ints.end()) {
            ASSERT_TRUE(
                (it->second.first == a && it->second.second == b) ||
                (it->second.first == b && it->second.second == a));
        } else
            ints[hash_ab] = std::make_pair(a, b);
    }
}

TEST(SLASupportGeneration, PillarPairHashShouldBeUnique) {
    test_pairhash<int, long>();
    test_pairhash<unsigned, unsigned>();
    test_pairhash<unsigned, unsigned long>();
}

TEST(SLASupportGeneration, FlatPadGeometryIsValid) {
    sla::PadConfig padcfg;

    // Disable wings
    padcfg.wall_height_mm = .0;

    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST(SLASupportGeneration, WingedPadGeometryIsValid) {
    sla::PadConfig padcfg;

    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;

    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST(SLASupportGeneration, FlatPadAroundObjectIsValid) {
    sla::PadConfig padcfg;

    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 0.;
    // padcfg.embed_object.stick_stride_mm = 0.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;

    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST(SLASupportGeneration, WingedPadAroundObjectIsValid) {
    sla::PadConfig padcfg;

    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;

    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST(SLASupportGeneration, ElevatedSupportGeometryIsValid) {
    sla::SupportConfig supportcfg;
    supportcfg.object_elevation_mm = 5.;

    for (auto fname : SUPPORT_TEST_MODELS) test_supports(fname);
}

TEST(SLASupportGeneration, FloorSupportGeometryIsValid) {
    sla::SupportConfig supportcfg;
    supportcfg.object_elevation_mm = 0;

    for (auto &fname: SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
}

TEST(SLASupportGeneration, SupportsDoNotPierceModel) {

    sla::SupportConfig supportcfg;

    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST(SLARasterOutput, DefaultRasterShouldBeEmpty) {
    sla::Raster raster;
    ASSERT_TRUE(raster.empty());
}

TEST(SLARasterOutput, InitializedRasterShouldBeNONEmpty) {
    // Default Prusa SL1 display parameters
    sla::Raster::Resolution res{2560, 1440};
    sla::Raster::PixelDim   pixdim{120. / res.width_px, 68. / res.height_px};

    sla::Raster raster;
    raster.reset(res, pixdim);
    ASSERT_FALSE(raster.empty());
    ASSERT_EQ(raster.resolution().width_px, res.width_px);
    ASSERT_EQ(raster.resolution().height_px, res.height_px);
    ASSERT_DOUBLE_EQ(raster.pixel_dimensions().w_mm, pixdim.w_mm);
    ASSERT_DOUBLE_EQ(raster.pixel_dimensions().h_mm, pixdim.h_mm);
}

using TPixel = uint8_t;
static constexpr const TPixel FullWhite = 255;
static constexpr const TPixel FullBlack = 0;

template <class A, int N> constexpr int arraysize(const A (&)[N]) { return N; }

static void check_raster_transformations(sla::Raster::Orientation o,
                                         sla::Raster::TMirroring  mirroring)
{
    double disp_w = 120., disp_h = 68.;
    sla::Raster::Resolution res{2560, 1440};
    sla::Raster::PixelDim pixdim{disp_w / res.width_px, disp_h / res.height_px};

    auto bb = BoundingBox({0, 0}, {scaled(disp_w), scaled(disp_h)});
    sla::Raster::Trafo trafo{o, mirroring};
    trafo.origin_x = bb.center().x();
    trafo.origin_y = bb.center().y();

    sla::Raster raster{res, pixdim, trafo};

    // create box of size 32x32 pixels (not 1x1 to avoid antialiasing errors)
    coord_t pw = 32 * coord_t(std::ceil(scaled<double>(pixdim.w_mm)));
    coord_t ph = 32 * coord_t(std::ceil(scaled<double>(pixdim.h_mm)));
    ExPolygon box;
    box.contour.points = {{-pw, -ph}, {pw, -ph}, {pw, ph}, {-pw, ph}};

    double tr_x = scaled<double>(20.), tr_y = tr_x;

    box.translate(tr_x, tr_y);
    ExPolygon expected_box = box;

    // Now calculate the position of the translated box according to output
    // trafo.
    if (o == sla::Raster::Orientation::roPortrait) expected_box.rotate(PI / 2.);

    if (mirroring[X])
        for (auto &p : expected_box.contour.points) p.x() = -p.x();

    if (mirroring[Y])
        for (auto &p : expected_box.contour.points) p.y() = -p.y();

    raster.draw(box);

    Point expected_coords = expected_box.contour.bounding_box().center();
    double rx = unscaled(expected_coords.x() + bb.center().x()) / pixdim.w_mm;
    double ry = unscaled(expected_coords.y() + bb.center().y()) / pixdim.h_mm;
    auto w = size_t(std::floor(rx));
    auto h = res.height_px - size_t(std::floor(ry));

    ASSERT_TRUE(w < res.width_px && h < res.height_px);

    auto px = raster.read_pixel(w, h);

    if (px != FullWhite) {
        sla::PNGImage img;
        std::fstream outf("out.png", std::ios::out);

        outf << img.serialize(raster);
    }

    ASSERT_EQ(px, FullWhite);
}

TEST(SLARasterOutput, MirroringShouldBeCorrect) {
    sla::Raster::TMirroring mirrorings[] = {sla::Raster::NoMirror,
                                            sla::Raster::MirrorX,
                                            sla::Raster::MirrorY,
                                            sla::Raster::MirrorXY};

    sla::Raster::Orientation orientations[] = {sla::Raster::roLandscape,
                                               sla::Raster::roPortrait};
    for (auto orientation : orientations)
        for (auto &mirror : mirrorings)
            check_raster_transformations(orientation, mirror);
}

static ExPolygon square_with_hole(double v)
{
    ExPolygon poly;
    coord_t V = scaled(v / 2.);

    poly.contour.points = {{-V, -V}, {V, -V}, {V, V}, {-V, V}};
    poly.holes.emplace_back();
    V = V / 2;
    poly.holes.front().points = {{-V, V}, {V, V}, {V, -V}, {-V, -V}};
    return poly;
}

static double pixel_area(TPixel px, const sla::Raster::PixelDim &pxdim)
{
    return (pxdim.h_mm * pxdim.w_mm) * px * 1. / (FullWhite - FullBlack);
}

static double raster_white_area(const sla::Raster &raster)
{
    if (raster.empty()) return std::nan("");

    auto res = raster.resolution();
    double a = 0;

    for (size_t x = 0; x < res.width_px; ++x)
        for (size_t y = 0; y < res.height_px; ++y) {
            auto px = raster.read_pixel(x, y);
            a += pixel_area(px, raster.pixel_dimensions());
        }

    return a;
}

static double predict_error(const ExPolygon &p, const sla::Raster::PixelDim &pd)
{
    auto lines = p.lines();
    double pix_err = pixel_area(FullWhite, pd)  / 2.;

    // Worst case is when a line is parallel to the shorter axis of one pixel,
    // when the line will be composed of the max number of pixels
    double pix_l = std::min(pd.h_mm, pd.w_mm);

    double error = 0.;
    for (auto &l : lines)
        error += (unscaled(l.length()) / pix_l) * pix_err;

    return error;
}

TEST(SLARasterOutput, RasterizedPolygonAreaShouldMatch) {
    double disp_w = 120., disp_h = 68.;
    sla::Raster::Resolution res{2560, 1440};
    sla::Raster::PixelDim pixdim{disp_w / res.width_px, disp_h / res.height_px};

    sla::Raster raster{res, pixdim};
    auto bb = BoundingBox({0, 0}, {scaled(disp_w), scaled(disp_h)});

    ExPolygon poly = square_with_hole(10.);
    poly.translate(bb.center().x(), bb.center().y());
    raster.draw(poly);

    double a = poly.area() / (scaled<double>(1.) * scaled(1.));
    double ra = raster_white_area(raster);
    double diff = std::abs(a - ra);

    ASSERT_LE(diff, predict_error(poly, pixdim));

    raster.clear();
    poly = square_with_hole(60.);
    poly.translate(bb.center().x(), bb.center().y());
    raster.draw(poly);

    a = poly.area() / (scaled<double>(1.) * scaled(1.));
    ra = raster_white_area(raster);
    diff = std::abs(a - ra);

    ASSERT_LE(diff, predict_error(poly, pixdim));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
