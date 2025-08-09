#include "../ClipperUtils.hpp"
#include "FillTpmsFK.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <tbb/parallel_for.h>

namespace Slic3r {

using namespace std;
struct myPoint
{
    coord_t x, y;
};
class LineSegmentMerger
{
public:
    void mergeSegments(const vector<pair<myPoint, myPoint>>& segments, vector<vector<myPoint>>& polylines2)
    {
        std::unordered_map<int, myPoint> point_id_xy;
        std::set<std::pair<int, int>>    segment_ids;
        std::unordered_map<int64_t, int> map_keyxy_pointid;

        auto get_itr = [&](coord_t x, coord_t y) {
            for (auto i : {0}) //,-2,2
            {
                for (auto j : {0}) //,-2,2
                {
                    int64_t combined_key1 = static_cast<int64_t>(x + i) << 32 | static_cast<uint32_t>(y + j);
                    auto    itr1          = map_keyxy_pointid.find(combined_key1);
                    if (itr1 != map_keyxy_pointid.end()) {
                        return itr1;
                    }
                }
            }
            return map_keyxy_pointid.end();
        };

        int pointid = 0;
        for (const auto& segment : segments) {
            coord_t x          = segment.first.x;
            coord_t y          = segment.first.y;
            auto    itr        = get_itr(x, y);
            int     segmentid0 = -1;
            if (itr == map_keyxy_pointid.end()) {
                int64_t combined_key            = static_cast<int64_t>(x) << 32 | static_cast<uint32_t>(y);
                segmentid0                      = pointid;
                point_id_xy[pointid]            = segment.first;
                map_keyxy_pointid[combined_key] = pointid++;
            } else {
                segmentid0 = itr->second;
            }
            int segmentid1 = -1;
            x              = segment.second.x;
            y              = segment.second.y;
            itr            = get_itr(x, y);
            if (itr == map_keyxy_pointid.end()) {
                int64_t combined_key            = static_cast<int64_t>(x) << 32 | static_cast<uint32_t>(y);
                segmentid1                      = pointid;
                point_id_xy[pointid]            = segment.second;
                map_keyxy_pointid[combined_key] = pointid++;
            } else {
                segmentid1 = itr->second;
            }

            if (segmentid0 != segmentid1) {
                segment_ids.insert(segmentid0 < segmentid1 ? std::make_pair(segmentid0, segmentid1) :
                                                             std::make_pair(segmentid1, segmentid0));
            }
        }

        unordered_map<int, vector<int>> graph;
        unordered_set<int>              visited;
        vector<vector<int>>             polylines;

        // Build the graph
        for (const auto& segment : segment_ids) {
            graph[segment.first].push_back(segment.second);
            graph[segment.second].push_back(segment.first);
        }

        vector<int> startnodes;
        for (const auto& node : graph) {
            if (node.second.size() == 1) {
                startnodes.push_back(node.first);
            }
        }

        // Find all connected components
        for (const auto& point_first : startnodes) {
            if (visited.find(point_first) == visited.end()) {
                vector<int> polyline;
                dfs(point_first, graph, visited, polyline);
                polylines.push_back(std::move(polyline));
            }
        }

        for (const auto& point : graph) {
            if (visited.find(point.first) == visited.end()) {
                vector<int> polyline;
                dfs(point.first, graph, visited, polyline);
                polylines.push_back(std::move(polyline));
            }
        }

        for (auto& pl : polylines) {
            vector<myPoint> tmpps;
            for (auto& pid : pl) {
                tmpps.push_back(point_id_xy[pid]);
            }
            polylines2.push_back(tmpps);
        }
    }

private:
    void dfs(const int&                                 start_node,
             std::unordered_map<int, std::vector<int>>& graph,
             std::unordered_set<int>&                   visited,
             std::vector<int>&                          polyline)
    {
        std::vector<int> stack;
        stack.reserve(graph.size());
        stack.push_back(start_node);
        while (!stack.empty()) {
            int node = stack.back();
            stack.pop_back();
            if (!visited.insert(node).second) {
                continue;
            }
            polyline.push_back(node);
            auto& neighbors = graph[node];
            for (const auto& neighbor : neighbors) {
                if (visited.find(neighbor) == visited.end()) {
                    stack.push_back(neighbor);
                }
            }
        }
    }
};

namespace MarchingSquares {
struct Point
{
    double x, y;
};

vector<double> getGridValues(int i, int j, vector<vector<double>>& data)
{
    vector<double> values;
    values.push_back(data[i][j + 1]);
    values.push_back(data[i + 1][j + 1]);
    values.push_back(data[i + 1][j]);
    values.push_back(data[i][j]);
    return values;
}
bool  needContour(double value, double contourValue) { return value >= contourValue; }
Point interpolate(std::vector<std::vector<MarchingSquares::Point>>& posxy,
                  std::vector<int>                                  p1ij,
                  std::vector<int>                                  p2ij,
                  double                                            v1,
                  double                                            v2,
                  double                                            contourValue)
{
    Point p1;
    p1.x = posxy[p1ij[0]][p1ij[1]].x;
    p1.y = posxy[p1ij[0]][p1ij[1]].y;
    Point p2;
    p2.x = posxy[p2ij[0]][p2ij[1]].x;
    p2.y = posxy[p2ij[0]][p2ij[1]].y;

    double mu = (contourValue - v1) / (v2 - v1);
    Point  p;
    p.x = p1.x + mu * (p2.x - p1.x);
    p.y = p1.y + mu * (p2.y - p1.y);
    return p;
}

void process_block(int                                               i,
                   int                                               j,
                   vector<vector<double>>&                           data,
                   double                                            contourValue,
                   std::vector<std::vector<MarchingSquares::Point>>& posxy,
                   vector<Point>&                                    contourPoints)
{
    vector<double> values = getGridValues(i, j, data);
    vector<bool>   isNeedContour;
    for (double value : values) {
        isNeedContour.push_back(needContour(value, contourValue));
    }
    int index = 0;
    if (isNeedContour[0])
        index |= 1;
    if (isNeedContour[1])
        index |= 2;
    if (isNeedContour[2])
        index |= 4;
    if (isNeedContour[3])
        index |= 8;
    vector<Point> points;
    switch (index) {
    case 0:
    case 15: break;

    case 1:
        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));

        break;
    case 14:
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));
        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));
        break;

    case 2:
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));
        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));

        break;
    case 13:
        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));
        break;
    case 3:
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));

        break;
    case 12:
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));

        break;
    case 4:
        points.push_back(interpolate(posxy, {i + 1, j}, {i, j}, values[2], values[3], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));

        break;
    case 11:
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j}, {i, j}, values[2], values[3], contourValue));
        break;
    case 5:
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));
        points.push_back(interpolate(posxy, {i, j}, {i + 1, j}, values[3], values[2], contourValue));

        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));
        break;
    case 6:
        points.push_back(interpolate(posxy, {i + 1, j}, {i, j}, values[2], values[3], contourValue));
        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));

        break;
    case 9:
        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j}, {i, j}, values[2], values[3], contourValue));
        break;
    case 7:
        points.push_back(interpolate(posxy, {i + 1, j}, {i, j}, values[2], values[3], contourValue));
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));

        break;
    case 8:
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j}, {i, j}, values[2], values[3], contourValue));
        break;
    case 10:
        points.push_back(interpolate(posxy, {i, j}, {i, j + 1}, values[3], values[0], contourValue));
        points.push_back(interpolate(posxy, {i, j}, {i + 1, j}, values[3], values[2], contourValue));

        points.push_back(interpolate(posxy, {i, j + 1}, {i + 1, j + 1}, values[0], values[1], contourValue));
        points.push_back(interpolate(posxy, {i + 1, j + 1}, {i + 1, j}, values[1], values[2], contourValue));
        break;
    }
    for (Point& p : points) {
        contourPoints.push_back(p);
    }
}

// --- Chaikin Smooth ---

static Polyline chaikin_smooth(Polyline poly, int iterations , double weight )
{
    if (poly.points.size() < 3) return poly;

    const double w1 = 1.0 - weight;
    decltype(poly.points) buffer;
    buffer.reserve(poly.points.size() * 2);

    for (int it = 0; it < iterations; ++it) {
        buffer.clear();
        buffer.push_back(poly.points.front());

        for (size_t i = 0; i < poly.points.size() - 1; ++i) {
            const auto &p0 = poly.points[i];
            const auto &p1 = poly.points[i + 1];

            buffer.emplace_back(
                p0.x() * w1 + p1.x() * weight,
                p0.y() * w1 + p1.y() * weight
            );
            buffer.emplace_back(
                p0.x() * weight + p1.x() * w1,
                p0.y() * weight + p1.y() * w1
            );
        }

        buffer.push_back(poly.points.back());
        poly.points.swap(buffer); 
    }

    return poly; 
}


void drawContour(double                                            contourValue,
                 int                                               gridSize_w,
                 int                                               gridSize_h,
                 vector<vector<double>>&                           data,
                 std::vector<std::vector<MarchingSquares::Point>>& posxy,
                 Polylines&                                        repls,
                 const FillParams&                                 params)
{
    vector<Point>         contourPoints;
    int                   total_size = (gridSize_h - 1) * (gridSize_w - 1);
    vector<vector<Point>> contourPointss;
    contourPointss.resize(total_size);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, total_size),
                      [&contourValue, &posxy, &contourPointss, &data, &gridSize_w](const tbb::blocked_range<size_t>& range) {
                          for (size_t k = range.begin(); k < range.end(); ++k) {
                              int i = k / (gridSize_w - 1);
                              int j = k % (gridSize_w - 1);
                              process_block(i, j, data, contourValue, posxy, contourPointss[k]);
                          }
                      });

    vector<pair<myPoint, myPoint>> segments2;
    myPoint                        p1, p2;
    for (int k = 0; k < total_size; k++) {
        for (int i = 0; i < contourPointss[k].size() / 2; i++) {
            p1.x = scale_(contourPointss[k][i * 2].x);
            p1.y = scale_(contourPointss[k][i * 2].y);
            p2.x = scale_(contourPointss[k][i * 2 + 1].x);
            p2.y = scale_(contourPointss[k][i * 2 + 1].y);
            segments2.push_back({p1, p2});
        }
    }

    LineSegmentMerger       merger;
    vector<vector<myPoint>> result;
    merger.mergeSegments(segments2, result);

    for (vector<myPoint>& p : result) {
        Polyline repltmp;
        for (myPoint& pt : p) {
            repltmp.points.push_back(Slic3r::Point(pt.x, pt.y));
        }
        // symplify tolerance based on density
        const float min_tolerance      = 0.005f;
        const float max_tolerance      = 0.2f;
        float       simplify_tolerance = (0.005f / params.density);
        simplify_tolerance             = std::clamp(simplify_tolerance, min_tolerance, max_tolerance);
        repltmp.simplify(scale_(simplify_tolerance));
        repltmp = chaikin_smooth(repltmp, 2, 0.25);
        repls.push_back(repltmp);
    }
}
} // namespace MarchingSquares

static float sin_table[360];
static float cos_table[360];
static bool  g_is_init = false;

#define PIratio 57.29577951308232 // 180/PI

static void initialize_lookup_tables()
{
    for (int i = 0; i < 360; ++i) {
        float angle  = i * (M_PI / 180.0);
        sin_table[i] = std::sin(angle);
        cos_table[i] = std::cos(angle);
    }
}

inline static float get_sin(float angle)
{
    angle     = angle * PIratio;
    int index = static_cast<int>(std::fmod(angle, 360) + 360) % 360;
    return sin_table[index];
}

inline static float get_cos(float angle)
{
    angle     = angle * PIratio;
    int index = static_cast<int>(std::fmod(angle, 360) + 360) % 360;
    return cos_table[index];
}

void FillTpmsFK::_fill_surface_single(const FillParams&              params,
                                      unsigned int                   thickness_layers,
                                      const std::pair<float, Point>& direction,
                                      ExPolygon                      expolygon,
                                      Polylines&                     polylines_out)
{
    if (!g_is_init) {
        initialize_lookup_tables();
        g_is_init = true;
    }

    auto infill_angle = float(this->angle - (CorrectionAngle * 2 * M_PI) / 360.);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    float density_factor = std::min(0.9f, params.density);
    // Density adjusted to have a good %of weight.
    const float vari_T = 4.18f * spacing * params.multiline / density_factor;

    BoundingBox bb      = expolygon.contour.bounding_box();
    auto        cenpos  = unscale(bb.center());
    auto        boxsize = unscale(bb.size());
    float       xlen    = boxsize.x();
    float       ylen    = boxsize.y();

    const float delta = 0.5f; // mesh step (adjust for quality/performance)

    float myperiod = 2 * PI / vari_T;
    float c_z      = myperiod * this->z; // z height

    // scalar field Fischer-Koch
    auto scalar_field = [&](float xlen, float ylen, float cen_x, float cen_y, float delta, float myperiod, float c_z,
                            std::vector<std::vector<double>>& data) {
        int width  = static_cast<int>(std::ceil(xlen / delta));
        int height = static_cast<int>(std::ceil(ylen / delta));

        std::vector<float> cos_ax(width), sin_ax(width), cos2ax(width);
        std::vector<float> cos_by(height), sin_by(height), cos2by(height);

        for (int j = 0; j < width; ++j) {
            float x   = -(xlen) / 2.0f + j * delta;
            float a_x = myperiod * (cen_x + x);
            cos_ax[j] = get_cos(a_x);
            sin_ax[j] = get_sin(a_x);
            cos2ax[j] = get_cos(2 * a_x);
        }
        for (int i = 0; i < height; ++i) {
            float y   = -(ylen) / 2.0f + i * delta;
            float b_y = myperiod * (cen_y + y);
            cos_by[i] = get_cos(b_y);
            sin_by[i] = get_sin(b_y);
            cos2by[i] = get_cos(2 * b_y);
        }

        const float coscz  = get_cos(c_z);
        const float sincz  = get_sin(c_z);
        const float cos2cz = get_cos(2 * c_z);
        // Fischer -Koch S ecuation:
        // cos(2x)sin(y)cos(z) + cos(2y)sin(z)cos(x) + cos(2z)sin(x)cos(y) = 0
        data.resize(height, std::vector<double>(width));
        tbb::parallel_for(tbb::blocked_range<size_t>(0, height), [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                for (size_t j = 0; j < width; ++j) {
                    data[i][j] = cos2ax[j] * sin_by[i] * coscz + cos2by[i] * sincz * cos_ax[j] + cos2cz * sin_ax[j] * cos_by[i];
                }
            }
        });

        return std::pair<int, int>{width, height};
    };

    // Mesh generation
    std::vector<std::vector<MarchingSquares::Point>> posxy;
    int                                              i = 0, j = 0;
    for (float y = -(ylen) / 2.0f; y < (ylen) / 2.0f; y += delta, i++) {
        j = 0;
        std::vector<MarchingSquares::Point> colposxy;
        for (float x = -(xlen) / 2.0f; x < (xlen) / 2.0f; x += delta, j++) {
            MarchingSquares::Point pt;
            pt.x = cenpos.x() + x;
            pt.y = cenpos.y() + y;
            colposxy.push_back(pt);
        }
        posxy.push_back(colposxy);
    }

    std::vector<std::vector<double>> data(posxy.size(), std::vector<double>(posxy[0].size()));
    int                              width  = posxy[0].size();
    int                              height = posxy.size();

tbb::parallel_for(tbb::blocked_range<size_t>(0, height),
    [&](const tbb::blocked_range<size_t>& range) {
       
       
        static thread_local std::vector<float> cos_ax, sin_ax, cos2ax;
        static thread_local std::vector<float> cos_by, sin_by, cos2by;
        static thread_local float last_myperiod_x = -1.0f;
        static thread_local float last_myperiod_y = -1.0f;

        
        if (cos_ax.size() != static_cast<size_t>(width) || last_myperiod_x != myperiod) {
            cos_ax.resize(width);
            sin_ax.resize(width);
            cos2ax.resize(width);
            for (size_t jj = 0; jj < static_cast<size_t>(width); ++jj) {
                float local_x = static_cast<float>(posxy[0][jj].x - cenpos.x());
                float a_x = myperiod * local_x;
                cos_ax[jj]  = get_cos(a_x);
                sin_ax[jj]  = get_sin(a_x);
                cos2ax[jj]  = get_cos(2 * a_x);
            }
            last_myperiod_x = myperiod;
        }

        
        if (cos_by.size() != static_cast<size_t>(height) || last_myperiod_y != myperiod) {
            cos_by.resize(height);
            sin_by.resize(height);
            cos2by.resize(height);
            for (size_t ii = 0; ii < static_cast<size_t>(height); ++ii) {
                float local_y = static_cast<float>(posxy[ii][0].y - cenpos.y());
                float b_y = myperiod * local_y;
                cos_by[ii]  = get_cos(b_y);
                sin_by[ii]  = get_sin(b_y);
                cos2by[ii]  = get_cos(2 * b_y);
            }
            last_myperiod_y = myperiod;
        }

        const float c_z    = myperiod * this->z;
        const float coscz  = get_cos(c_z);
        const float sincz  = get_sin(c_z);
        const float cos2cz = get_cos(2 * c_z);

        for (size_t ii = range.begin(); ii < range.end(); ++ii) {
            for (size_t jj = 0; jj < static_cast<size_t>(width); ++jj) {
                data[ii][jj] =
                    cos2ax[jj] * sin_by[ii] * coscz +
                    cos2by[ii] * sincz     * cos_ax[jj] +
                    cos2cz    * sin_ax[jj] * cos_by[ii];
            }
        }
    });


    Polylines polylines;
    const double contour_value = 0.075; // offset from zero to avoid numerical issues
    MarchingSquares::drawContour(contour_value, width - 1, height - 1, data, posxy, polylines, params);

    if (!polylines.empty()) {
        // Apply multiline offset if needed
        multiline_fill(polylines, params, spacing);

        polylines = intersection_pl(polylines, expolygon);

        if (!polylines.empty()) {
            // connect lines
            size_t polylines_out_first_idx = polylines_out.size();
            if (params.dont_connect()) {
                append(polylines_out, chain_polylines(polylines));
            } else {
                this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);
            }

            // new paths must be rotated back
            if (std::abs(infill_angle) >= EPSILON) {
                for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it) {
                    it->rotate(infill_angle);
                }
            }
        }
    }
}

} // namespace Slic3r