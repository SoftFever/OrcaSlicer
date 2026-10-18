#include "Thumbnails.hpp"

#include "Session.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Slic3r { namespace Mobile {

namespace {

struct ProjectedVertex
{
    float x  = 0;
    float y  = 0;
    float z  = 0; // depth in world mm; larger is closer to the camera (top-down).
};

// Barycentric scanline rasterizer with a Z-buffer. Fills the triangle
// (a, b, c) in image space and writes an RGBA colour with shade already
// baked in when the interpolated depth beats the buffer.
void rasterize(std::vector<unsigned char>& pixels,
               std::vector<float>&         depth,
               unsigned int                width,
               unsigned int                height,
               ProjectedVertex             a,
               ProjectedVertex             b,
               ProjectedVertex             c,
               std::array<unsigned char, 4> color)
{
    const float min_x = std::max(0.f, std::floor(std::min({ a.x, b.x, c.x })));
    const float max_x = std::min(float(width) - 1.f, std::ceil(std::max({ a.x, b.x, c.x })));
    const float min_y = std::max(0.f, std::floor(std::min({ a.y, b.y, c.y })));
    const float max_y = std::min(float(height) - 1.f, std::ceil(std::max({ a.y, b.y, c.y })));

    const float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(denom) < 1e-6f) return;

    const int    x0 = int(min_x), x1 = int(max_x);
    const int    y0 = int(min_y), y1 = int(max_y);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const float px = float(x) + 0.5f;
            const float py = float(y) + 0.5f;
            const float w0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / denom;
            const float w1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / denom;
            const float w2 = 1.f - w0 - w1;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
            const float z = w0 * a.z + w1 * b.z + w2 * c.z;
            const size_t idx = size_t(y) * width + size_t(x);
            if (z <= depth[idx]) continue;
            depth[idx] = z;
            const size_t p = idx * 4;
            pixels[p + 0] = color[0];
            pixels[p + 1] = color[1];
            pixels[p + 2] = color[2];
            pixels[p + 3] = color[3];
        }
    }
}

// Fill the axis-aligned rectangle covering the printable area with a soft
// neutral so the object stands out even for tiny thumbnails where the plate
// polygon is only a few pixels tall.
void fill_bed_backdrop(std::vector<unsigned char>& pixels,
                       std::vector<float>&         depth,
                       unsigned int                width,
                       unsigned int                height,
                       float                       min_px_x,
                       float                       min_px_y,
                       float                       max_px_x,
                       float                       max_px_y)
{
    const int x0 = std::max(0, int(std::floor(min_px_x)));
    const int y0 = std::max(0, int(std::floor(min_px_y)));
    const int x1 = std::min(int(width)  - 1, int(std::ceil(max_px_x)));
    const int y1 = std::min(int(height) - 1, int(std::ceil(max_px_y)));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const size_t idx = size_t(y) * width + size_t(x);
            const size_t p = idx * 4;
            pixels[p + 0] = 240;
            pixels[p + 1] = 240;
            pixels[p + 2] = 240;
            pixels[p + 3] = 255;
            depth[idx] = 0.f;
        }
    }
}

} // namespace

Slic3r::ThumbnailData render_plate_thumbnail(const Slic3r::Model& model,
                                             const BedInfo&       bed,
                                             unsigned int         width,
                                             unsigned int         height)
{
    Slic3r::ThumbnailData out;
    out.set(width, height);
    out.pixels.assign(size_t(width) * size_t(height) * 4, 0);
    std::vector<float> depth(size_t(width) * size_t(height), -std::numeric_limits<float>::infinity());

    // Bed bounds in world mm. Fall back to a 200 mm square so a session
    // without a printer preset still renders something.
    double bed_min_x = 1e30, bed_min_y = 1e30, bed_max_x = -1e30, bed_max_y = -1e30;
    for (const auto& p : bed.shape) {
        bed_min_x = std::min(bed_min_x, p[0]);
        bed_min_y = std::min(bed_min_y, p[1]);
        bed_max_x = std::max(bed_max_x, p[0]);
        bed_max_y = std::max(bed_max_y, p[1]);
    }
    if (bed.shape.empty()) {
        bed_min_x = 0; bed_min_y = 0; bed_max_x = 200; bed_max_y = 200;
    }
    const double bed_w = std::max(1.0, bed_max_x - bed_min_x);
    const double bed_h = std::max(1.0, bed_max_y - bed_min_y);

    // Fit the plate inside the image, keep aspect ratio, small margin.
    const double margin = 0.06 * std::min(double(width), double(height));
    const double scale_x = (double(width)  - 2.0 * margin) / bed_w;
    const double scale_y = (double(height) - 2.0 * margin) / bed_h;
    const double scale   = std::min(scale_x, scale_y);
    const double render_w = bed_w * scale;
    const double render_h = bed_h * scale;
    const double origin_px_x = (double(width)  - render_w) * 0.5;
    const double origin_px_y = (double(height) - render_h) * 0.5;

    fill_bed_backdrop(out.pixels, depth, width, height,
                      float(origin_px_x), float(origin_px_y),
                      float(origin_px_x + render_w), float(origin_px_y + render_h));

    auto to_pixel = [&](double wx, double wy, double wz) {
        ProjectedVertex v;
        v.x = float(origin_px_x + (wx - bed_min_x) * scale);
        // Image y grows down; world y grows up — flip.
        v.y = float(double(height) - origin_px_y - (wy - bed_min_y) * scale);
        v.z = float(wz);
        return v;
    };

    // Soft top-down light, mostly overhead with a little forward bias so
    // vertical faces still catch some shade.
    const std::array<double, 3> sun{ 0.30, -0.20, 0.90 };
    const double sun_len = std::sqrt(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);

    // Object body colour matches the viewport's default (a muted sage).
    const std::array<unsigned char, 3> body{ 140, 184, 158 };

    for (const ModelObject* object : model.objects) {
        if (object == nullptr) continue;
        const TriangleMesh raw = object->raw_mesh();
        if (raw.its.indices.empty()) continue;
        for (const ModelInstance* instance : object->instances) {
            if (instance == nullptr || ! instance->printable) continue;
            const Transform3d trafo = instance->get_matrix();
            for (const stl_triangle_vertex_indices& face : raw.its.indices) {
                const Vec3d a = trafo * raw.its.vertices[face(0)].cast<double>();
                const Vec3d b = trafo * raw.its.vertices[face(1)].cast<double>();
                const Vec3d c = trafo * raw.its.vertices[face(2)].cast<double>();
                const Vec3d ab = b - a;
                const Vec3d ac = c - a;
                Vec3d       n  = ab.cross(ac);
                const double nlen = n.norm();
                if (nlen < 1e-9) continue;
                n /= nlen;
                double dot = (n.x() * sun[0] + n.y() * sun[1] + n.z() * sun[2]) / sun_len;
                if (dot < 0) dot = 0;
                const double shade = 0.35 + 0.65 * dot;
                std::array<unsigned char, 4> color{
                    (unsigned char) std::min(255.0, body[0] * shade),
                    (unsigned char) std::min(255.0, body[1] * shade),
                    (unsigned char) std::min(255.0, body[2] * shade),
                    255,
                };
                rasterize(out.pixels, depth, width, height,
                          to_pixel(a.x(), a.y(), a.z()),
                          to_pixel(b.x(), b.y(), b.z()),
                          to_pixel(c.x(), c.y(), c.z()),
                          color);
            }
        }
    }
    return out;
}

}} // namespace Slic3r::Mobile
