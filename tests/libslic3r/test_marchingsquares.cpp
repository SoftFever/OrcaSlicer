#include <catch2/catch.hpp>
#include <test_utils.hpp>

#include <fstream>

#include <libslic3r/MarchingSquares.hpp>
#include <libslic3r/SLA/RasterToPolygons.hpp>
#include <libslic3r/SLA/AGGRaster.hpp>
#include <libslic3r/MTUtils.hpp>
#include <libslic3r/SVG.hpp>
#include <libslic3r/ClipperUtils.hpp>

#include <libslic3r/TriangulateWall.hpp>
#include <libslic3r/Tesselate.hpp>
#include <libslic3r/SlicesToTriangleMesh.hpp>
#include <libslic3r/SLA/Contour3D.hpp>

using namespace Slic3r;

static Slic3r::sla::RasterGrayscaleAA create_raster(
    const sla::RasterBase::Resolution &res,
    double                             disp_w = 100.,
    double                             disp_h = 100.)
{
    sla::RasterBase::PixelDim pixdim{disp_w / res.width_px, disp_h / res.height_px};
    
    auto bb = BoundingBox({0, 0}, {scaled(disp_w), scaled(disp_h)});
    sla::RasterBase::Trafo trafo;
//    trafo.center_x = bb.center().x();
//    trafo.center_y = bb.center().y();
//    trafo.center_x = scaled(pixdim.w_mm);
//    trafo.center_y = scaled(pixdim.h_mm);

    return sla::RasterGrayscaleAA{res, pixdim, trafo, agg::gamma_threshold(.5)};
}

static ExPolygon square(double a, Point center = {0, 0})
{
    ExPolygon poly;
    coord_t V = scaled(a / 2.);
    
    poly.contour.points = {{-V, -V}, {V, -V}, {V, V}, {-V, V}};
    poly.translate(center.x(), center.y());
    
    return poly;
}

static ExPolygon square_with_hole(double a, Point center = {0, 0})
{
    ExPolygon poly = square(a);
    
    poly.holes.emplace_back();
    coord_t V = scaled(a / 4.);
    poly.holes.front().points = {{-V, V}, {V, V}, {V, -V}, {-V, -V}};
    
    poly.translate(center.x(), center.y());

    return poly;
}

static ExPolygons circle_with_hole(double r, Point center = {0, 0}) {
    
    ExPolygon poly;
    
    std::vector<double> pis = linspace_vector(0., 2 * PI, 100);
    
    coord_t rs = scaled(r);
    for (double phi : pis) {
        poly.contour.points.emplace_back(rs * std::cos(phi), rs * std::sin(phi));
    }
    
    poly.holes.emplace_back(poly.contour);
    poly.holes.front().reverse();
    for (auto &p : poly.holes.front().points) p /= 2;
    
    poly.translate(center.x(), center.y());
    
    return {poly};
}

template<class Rst>
static void test_expolys(Rst &&             rst,
                         const ExPolygons & ref,
                         float accuracy,
                         const std::string &name = "test")
{
    for (const ExPolygon &expoly : ref) rst.draw(expoly);
    
    std::fstream out(name + ".png", std::ios::out);
    out << rst.encode(sla::PNGRasterEncoder{});
    out.close();
    
    ExPolygons extracted = sla::raster_to_polygons(rst, accuracy);
    
    SVG svg(name + ".svg");
    svg.draw(extracted);
    svg.Close();
    
    REQUIRE(extracted.size() == ref.size());
    for (size_t i = 0; i < ref.size(); ++i) {
        REQUIRE(extracted[i].contour.is_counter_clockwise());
        REQUIRE(extracted[i].holes.size() == ref[i].holes.size());
        
        for (auto &h : extracted[i].holes) REQUIRE(h.is_clockwise());
        
        double refa = ref[i].area();
        REQUIRE(std::abs(extracted[i].area() - refa) < 0.1 * refa);
    }
}

TEST_CASE("Empty raster should result in empty polygons", "[MarchingSquares]") {
    sla::RasterGrayscaleAAGammaPower rst{{}, {}, {}};
    ExPolygons extracted = sla::raster_to_polygons(rst);
    REQUIRE(extracted.size() == 0);
}

TEST_CASE("Marching squares directions", "[MarchingSquares]") {
    marchsq::Coord crd{1, 1};
    
    REQUIRE(step(crd, marchsq::__impl::Dir::left).r == 1);
    REQUIRE(step(crd, marchsq::__impl::Dir::left).c == 0);
                 
    REQUIRE(step(crd, marchsq::__impl::Dir::down).r == 2);
    REQUIRE(step(crd, marchsq::__impl::Dir::down).c == 1);
                 
    REQUIRE(step(crd, marchsq::__impl::Dir::right).r == 1);
    REQUIRE(step(crd, marchsq::__impl::Dir::right).c == 2);
                 
    REQUIRE(step(crd, marchsq::__impl::Dir::up).r == 0);
    REQUIRE(step(crd, marchsq::__impl::Dir::up).c == 1);
}

TEST_CASE("4x4 raster with one ring", "[MarchingSquares]") {
    
    sla::RasterBase::PixelDim pixdim{1, 1};
    
    // We need one additional row and column to detect edges
    sla::RasterGrayscaleAA rst{{5, 5}, pixdim, {}, agg::gamma_threshold(.5)};
    
    // Draw a triangle from individual pixels
    rst.draw(square(1., {1500000, 1500000}));
    rst.draw(square(1., {2500000, 1500000}));
    rst.draw(square(1., {3500000, 1500000}));
    
    rst.draw(square(1., {2500000, 2500000}));
    rst.draw(square(1., {3500000, 2500000}));
    
    rst.draw(square(1., {3500000, 3500000}));
    
    std::fstream out("4x4.png", std::ios::out);
    out << rst.encode(sla::PNGRasterEncoder{});
    out.close();
    
    ExPolygons extracted = sla::raster_to_polygons(rst);
    
    SVG svg("4x4.svg");
    svg.draw(extracted);
    svg.Close();
    
    REQUIRE(extracted.size() == 1);
}

TEST_CASE("4x4 raster with two rings", "[MarchingSquares]") {
    
    sla::RasterBase::PixelDim pixdim{1, 1};
    
    // We need one additional row and column to detect edges
    sla::RasterGrayscaleAA rst{{5, 5}, pixdim, {}, agg::gamma_threshold(.5)};
    
    SECTION("Ambiguous case with 'ac' square") {
        
        // Draw a triangle from individual pixels
        rst.draw(square(1., {3500000, 2500000}));
        rst.draw(square(1., {3500000, 3500000}));
        rst.draw(square(1., {2500000, 3500000}));
        
        rst.draw(square(1., {2500000, 1500000}));
        rst.draw(square(1., {1500000, 1500000}));
        rst.draw(square(1., {1500000, 2500000}));
        
        std::fstream out("4x4_ac.png", std::ios::out);
        out << rst.encode(sla::PNGRasterEncoder{});
        out.close();
        
        ExPolygons extracted = sla::raster_to_polygons(rst);
        
        SVG svg("4x4_ac.svg");
        svg.draw(extracted);
        svg.Close();
        
        REQUIRE(extracted.size() == 2);
    }
    
    SECTION("Ambiguous case with 'bd' square") {
        
        // Draw a triangle from individual pixels
        rst.draw(square(1., {3500000, 1500000}));
        rst.draw(square(1., {3500000, 2500000}));
        rst.draw(square(1., {2500000, 1500000}));
        
        rst.draw(square(1., {1500000, 2500000}));
        rst.draw(square(1., {1500000, 3500000}));
        rst.draw(square(1., {2500000, 3500000}));
        
        std::fstream out("4x4_bd.png", std::ios::out);
        out << rst.encode(sla::PNGRasterEncoder{});
        out.close();
        
        ExPolygons extracted = sla::raster_to_polygons(rst);
        
        SVG svg("4x4_bd.svg");
        svg.draw(extracted);
        svg.Close();
        
        REQUIRE(extracted.size() == 2);
    }
}

TEST_CASE("Square with hole in the middle", "[MarchingSquares]") {
    using namespace Slic3r;
    
    ExPolygons inp = {square_with_hole(50.)};
    
    SECTION("Proportional raster, 1x1 mm pixel size, full accuracy") {
        test_expolys(create_raster({100, 100}, 100., 100.), inp, 1.f, "square_with_hole_proportional_1x1_mm_px_full");
    }
    
    SECTION("Proportional raster, 1x1 mm pixel size, half accuracy") {
        test_expolys(create_raster({100, 100}, 100., 100.), inp, .5f, "square_with_hole_proportional_1x1_mm_px_half");
    }
    
    SECTION("Landscape raster, 1x1 mm pixel size, full accuracy") {
        test_expolys(create_raster({150, 100}, 150., 100.), inp, 1.f, "square_with_hole_landsc_1x1_mm_px_full");
    }
    
    SECTION("Landscape raster, 1x1 mm pixel size, half accuracy") {
        test_expolys(create_raster({150, 100}, 150., 100.), inp, .5f, "square_with_hole_landsc_1x1_mm_px_half");
    }
    
    SECTION("Portrait raster, 1x1 mm pixel size, full accuracy") {
        test_expolys(create_raster({100, 150}, 100., 150.), inp, 1.f, "square_with_hole_portrait_1x1_mm_px_full");
    }
    
    SECTION("Portrait raster, 1x1 mm pixel size, half accuracy") {
        test_expolys(create_raster({100, 150}, 100., 150.), inp, .5f, "square_with_hole_portrait_1x1_mm_px_half");
    }
    
    SECTION("Proportional raster, 2x2 mm pixel size, full accuracy") {
        test_expolys(create_raster({200, 200}, 100., 100.), inp, 1.f, "square_with_hole_proportional_2x2_mm_px_full");
    }
    
    SECTION("Proportional raster, 2x2 mm pixel size, half accuracy") {
        test_expolys(create_raster({200, 200}, 100., 100.), inp, .5f, "square_with_hole_proportional_2x2_mm_px_half");
    }
    
    SECTION("Proportional raster, 0.5x0.5 mm pixel size, full accuracy") {
        test_expolys(create_raster({50, 50}, 100., 100.), inp, 1.f, "square_with_hole_proportional_0.5x0.5_mm_px_full");
    }
    
    SECTION("Proportional raster, 0.5x0.5 mm pixel size, half accuracy") {
        test_expolys(create_raster({50, 50}, 100., 100.), inp, .5f, "square_with_hole_proportional_0.5x0.5_mm_px_half");
    }
}

TEST_CASE("Circle with hole in the middle", "[MarchingSquares]") {
    using namespace Slic3r;
    
    test_expolys(create_raster({100, 100}), circle_with_hole(25.), 1.f, "circle_with_hole");   
}

static void recreate_object_from_slices(const std::string &objname, float lh) {
    TriangleMesh mesh = load_model(objname);
    mesh.require_shared_vertices();
    
    auto bb = mesh.bounding_box();
    std::vector<ExPolygons> layers;
    slice_mesh(mesh, grid(float(bb.min.z()), float(bb.max.z()), lh), layers, 0.f, []{});
    
    TriangleMesh out = slices_to_triangle_mesh(layers, bb.min.z(), double(lh), double(lh));
    
    out.require_shared_vertices();
    out.WriteOBJFile("out_from_slices.obj");
}

static void recreate_object_from_rasters(const std::string &objname, float lh) {
    TriangleMesh mesh = load_model(objname);
    
    auto bb = mesh.bounding_box();
//    Vec3f tr = -bb.center().cast<float>();
//    mesh.translate(tr.x(), tr.y(), tr.z());
//    bb = mesh.bounding_box();
    
    std::vector<ExPolygons> layers;
    slice_mesh(mesh, grid(float(bb.min.z()) + lh, float(bb.max.z()), lh), layers, 0.f, []{});
    
    sla::RasterBase::Resolution res{2560, 1440};
    double                      disp_w = 120.96;
    double                      disp_h = 68.04;
    
    size_t cntr = 0;
    for (ExPolygons &layer : layers) {
        auto rst = create_raster(res, disp_w, disp_h);
        
        for (ExPolygon &island : layer) {
            rst.draw(island);
        }
        
        std::fstream out(objname + std::to_string(cntr) + ".png", std::ios::out);
        out << rst.encode(sla::PNGRasterEncoder{});
        out.close();
        
        ExPolygons layer_ = sla::raster_to_polygons(rst);
//        float delta = scaled(std::min(rst.pixel_dimensions().h_mm,
//                                      rst.pixel_dimensions().w_mm));
        
//        layer_ = expolygons_simplify(layer_, delta);
        
        SVG svg(objname +  std::to_string(cntr) + ".svg", BoundingBox(Point{0, 0}, Point{scaled(disp_w), scaled(disp_h)}));
        svg.draw(layer_);
        svg.draw(layer, "green");
        svg.Close();
        
        double layera = 0., layera_ = 0.;
        for (auto &p : layer) layera += p.area();
        for (auto &p : layer_) layera_ += p.area();
        
        std::cout << cntr++ << std::endl;
        double diff = std::abs(layera_ - layera);
        REQUIRE((diff <= 0.1 * layera || diff < scaled<double>(1.) * scaled<double>(1.)));
        
        layer = std::move(layer_);
    }
    
    TriangleMesh out = slices_to_triangle_mesh(layers, bb.min.z(), double(lh), double(lh));
    
    out.require_shared_vertices();
    out.WriteOBJFile("out_from_rasters.obj");
}

TEST_CASE("Recreate object from rasters", "[SL1Import]") {
    recreate_object_from_rasters("triang.obj", 0.05f);
}
