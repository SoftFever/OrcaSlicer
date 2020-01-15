#include <unordered_set>
#include <unordered_map>
#include <random>

#include "sla_test_utils.hpp"

namespace {

const char *const BELOW_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
};

const char *const AROUND_PAD_TEST_OBJECTS[] = {
    "20mm_cube.obj",
    "V.obj",
    "frog_legs.obj",
    "cube_with_concave_hole_enlarged.obj",
};

const char *const SUPPORT_TEST_MODELS[] = {
    "cube_with_concave_hole_enlarged_standing.obj",
    "A_upsidedown.obj",
    "extruder_idler.obj"
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
    
    int bits = IIbits / 2 < Ibits ? Ibits / 2 : Ibits;
    if (std::is_signed<I>::value) bits -= 1;
    const I Imin = 0;
    const I Imax = I(std::pow(2., bits) - 1);
    
    std::uniform_int_distribution<I> dis(Imin, Imax);
    
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
        
        REQUIRE(a != b);
        
        II hash_ab = sla::pairhash<I, II>(a, b);
        II hash_ba = sla::pairhash<I, II>(b, a);
        REQUIRE(hash_ab == hash_ba);
        
        auto it = ints.find(hash_ab);
        
        if (it != ints.end()) {
            REQUIRE((
                (it->second.first == a && it->second.second == b) ||
                (it->second.first == b && it->second.second == a)
                ));
        } else
            ints[hash_ab] = std::make_pair(a, b);
    }
}

TEST_CASE("Pillar pairhash should be unique", "[SLASupportGeneration]") {
    test_pairhash<int, int>();
    test_pairhash<int, long>();
    test_pairhash<unsigned, unsigned>();
    test_pairhash<unsigned, unsigned long>();
}

TEST_CASE("Support point generator should be deterministic if seeded", 
          "[SLASupportGeneration], [SLAPointGen]") {
    TriangleMesh mesh = load_model("A_upsidedown.obj");
    
    sla::EigenMesh3D emesh{mesh};
    
    sla::SupportConfig supportcfg;
    sla::SupportPointGenerator::Config autogencfg;
    autogencfg.head_diameter = float(2 * supportcfg.head_front_radius_mm);
    sla::SupportPointGenerator point_gen{emesh, autogencfg, [] {}, [](int) {}};
    
    TriangleMeshSlicer slicer{&mesh};
    
    auto   bb      = mesh.bounding_box();
    double zmin    = bb.min.z();
    double zmax    = bb.max.z();
    double gnd     = zmin - supportcfg.object_elevation_mm;
    auto   layer_h = 0.05f;
    
    auto slicegrid = grid(float(gnd), float(zmax), layer_h);
    std::vector<ExPolygons> slices;
    slicer.slice(slicegrid, CLOSING_RADIUS, &slices, []{});
    
    point_gen.seed(0);
    point_gen.execute(slices, slicegrid);
    
    auto get_chksum = [](const std::vector<sla::SupportPoint> &pts){
        long long chksum = 0;
        for (auto &pt : pts) {
            auto p = scaled(pt.pos);
            chksum += p.x() + p.y() + p.z();
        }
        
        return chksum;
    };
    
    long long checksum = get_chksum(point_gen.output());
    size_t ptnum = point_gen.output().size();
    REQUIRE(point_gen.output().size() > 0);
    
    for (int i = 0; i < 20; ++i) {
        point_gen.output().clear();
        point_gen.execute(slices, slicegrid);
        REQUIRE(point_gen.output().size() == ptnum);
        REQUIRE(checksum == get_chksum(point_gen.output()));
    }
}

TEST_CASE("Flat pad geometry is valid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Disable wings
    padcfg.wall_height_mm = .0;
    
    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("WingedPadGeometryIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;
    
    for (auto &fname : BELOW_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("FlatPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 0.;
    // padcfg.embed_object.stick_stride_mm = 0.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;
    
    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("WingedPadAroundObjectIsValid", "[SLASupportGeneration]") {
    sla::PadConfig padcfg;
    
    // Add some wings to the pad to test the cavity
    padcfg.wall_height_mm = 1.;
    padcfg.embed_object.enabled = true;
    padcfg.embed_object.everywhere = true;
    
    for (auto &fname : AROUND_PAD_TEST_OBJECTS) test_pad(fname, padcfg);
}

TEST_CASE("ElevatedSupportGeometryIsValid", "[SLASupportGeneration]") {
    sla::SupportConfig supportcfg;
    supportcfg.object_elevation_mm = 5.;
    
    for (auto fname : SUPPORT_TEST_MODELS) test_supports(fname);
}

TEST_CASE("FloorSupportGeometryIsValid", "[SLASupportGeneration]") {
    sla::SupportConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    
    for (auto &fname: SUPPORT_TEST_MODELS) test_supports(fname, supportcfg);
}

TEST_CASE("ElevatedSupportsDoNotPierceModel", "[SLASupportGeneration]") {
    
    sla::SupportConfig supportcfg;
    
    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("FloorSupportsDoNotPierceModel", "[SLASupportGeneration]") {
    
    sla::SupportConfig supportcfg;
    supportcfg.object_elevation_mm = 0;
    
    for (auto fname : SUPPORT_TEST_MODELS)
        test_support_model_collision(fname, supportcfg);
}

TEST_CASE("DefaultRasterShouldBeEmpty", "[SLARasterOutput]") {
    sla::Raster raster;
    REQUIRE(raster.empty());
}

TEST_CASE("InitializedRasterShouldBeNONEmpty", "[SLARasterOutput]") {
    // Default Prusa SL1 display parameters
    sla::Raster::Resolution res{2560, 1440};
    sla::Raster::PixelDim   pixdim{120. / res.width_px, 68. / res.height_px};
    
    sla::Raster raster;
    raster.reset(res, pixdim);
    REQUIRE_FALSE(raster.empty());
    REQUIRE(raster.resolution().width_px == res.width_px);
    REQUIRE(raster.resolution().height_px == res.height_px);
    REQUIRE(raster.pixel_dimensions().w_mm == Approx(pixdim.w_mm));
    REQUIRE(raster.pixel_dimensions().h_mm == Approx(pixdim.h_mm));
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
    
    REQUIRE((w < res.width_px && h < res.height_px));
    
    auto px = raster.read_pixel(w, h);
    
    if (px != FullWhite) {
        sla::PNGImage img;
        std::fstream outf("out.png", std::ios::out);
        
        outf << img.serialize(raster);
    }
    
    REQUIRE(px == FullWhite);
}

TEST_CASE("MirroringShouldBeCorrect", "[SLARasterOutput]") {
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

TEST_CASE("RasterizedPolygonAreaShouldMatch", "[SLARasterOutput]") {
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
    
    REQUIRE(diff <= predict_error(poly, pixdim));
    
    raster.clear();
    poly = square_with_hole(60.);
    poly.translate(bb.center().x(), bb.center().y());
    raster.draw(poly);
    
    a = poly.area() / (scaled<double>(1.) * scaled(1.));
    ra = raster_white_area(raster);
    diff = std::abs(a - ra);
    
    REQUIRE(diff <= predict_error(poly, pixdim));
}

TEST_CASE("Triangle mesh conversions should be correct", "[SLAConversions]")
{
    sla::Contour3D cntr;
    
    {
        std::fstream infile{"extruder_idler_quads.obj", std::ios::in};
        cntr.from_obj(infile);
    }
    
    
    
    
}
