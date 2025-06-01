#include "../ClipperUtils.hpp"
#include "FillTpmsD.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <tbb/parallel_for.h>
// From Creality Print
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

void drawContour(double                                            contourValue,
                 int                                               gridSize_w,
                 int                                               gridSize_h,
                 vector<vector<double>>&                           data,
                 std::vector<std::vector<MarchingSquares::Point>>& posxy,
                 Polylines&                                        repls)
{
    vector<Point>         contourPoints;
    int                   total_size = (gridSize_h - 1) * (gridSize_w - 1);
    vector<vector<Point>> contourPointss;
    contourPointss.resize(total_size);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, total_size),
                      [&contourValue, &posxy, &contourPointss, &data, &gridSize_w](const tbb::blocked_range<size_t>& range) {
                          for (size_t k = range.begin(); k < range.end(); ++k) {
                              int i = k / (gridSize_w - 1); //
                              int j = k % (gridSize_w - 1); //
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
        repltmp.simplify(scale_(0.05f));
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

static float get_sin(float angle)
{
    angle     = angle * PIratio;
    int index = static_cast<int>(std::fmod(angle, 360) + 360) % 360;
    return sin_table[index];
}

static float get_cos(float angle)
{
    angle     = angle * PIratio;
    int index = static_cast<int>(std::fmod(angle, 360) + 360) % 360;
    return cos_table[index];
}

FillTpmsD::FillTpmsD()
{
    if (!g_is_init) {
        initialize_lookup_tables();
        g_is_init = true;
    }
}
void FillTpmsD::_fill_surface_single(const FillParams&              params,
                                     unsigned int                   thickness_layers,
                                     const std::pair<float, Point>& direction,
                                     ExPolygon                      expolygon,
                                     Polylines&                     polylines_out)
{
    auto infill_angle = float(this->angle - (CorrectionAngle * 2 * M_PI) / 360.);
    if (std::abs(infill_angle) >= EPSILON)
        expolygon.rotate(-infill_angle);

    float vari_T = 2.98 * spacing / params.density; // Infill density adjustment factor for TPMS-D

    BoundingBox bb      = expolygon.contour.bounding_box();
    auto        cenpos  = unscale(bb.center());
    auto        boxsize = unscale(bb.size());
    float       xlen    = boxsize.x();
    float       ylen    = boxsize.y();

    float delta    = 0.25f;
    float myperiod = 2 * PI / vari_T;
    float c_z      = myperiod * this->z;
    float cos_z    = get_cos(c_z);
    float sin_z    = get_sin(c_z);

    auto scalar_field = [&](float x, float y) {
        // TPMS-D
        float a_x = myperiod * x;
        float b_y = myperiod * y;
        float r   = get_cos(a_x) * get_cos(b_y) * cos_z - get_sin(a_x) * get_sin(b_y) * sin_z;
        return r;
    };

    std::vector<std::vector<MarchingSquares::Point>> posxy;
    int                                              i = 0, j = 0;
    std::vector<MarchingSquares::Point>              allptpos;
    for (float y = -(ylen) / 2.0f - 2; y < (ylen) / 2.0f + 2; y = y + delta, i++) {
        j = 0;
        std::vector<MarchingSquares::Point> colposxy;
        for (float x = -(xlen) / 2.0f - 2; x < (xlen) / 2.0f + 2; x = x + delta, j++) {
            MarchingSquares::Point pt;
            pt.x = cenpos.x() + x;
            pt.y = cenpos.y() + y;
            colposxy.push_back(pt);
        }
        posxy.push_back(colposxy);
    }

    std::vector<std::vector<double>> data(posxy.size(), std::vector<double>(posxy[0].size()));

    int width      = posxy[0].size();
    int height     = posxy.size();
    int total_size = (height) * (width);
    tbb::parallel_for(tbb::blocked_range<size_t>(0, total_size),
                      [&width, &scalar_field, &data, &posxy](const tbb::blocked_range<size_t>& range) {
                          for (size_t k = range.begin(); k < range.end(); ++k) {
                              int i      = k / (width);
                              int j      = k % (width);
                              data[i][j] = scalar_field(posxy[i][j].x, posxy[i][j].y);
                          }
                      });

    Polylines polylines;
    MarchingSquares::drawContour(0, j, i, data, posxy, polylines);

    polylines = intersection_pl(polylines, expolygon);

    if (!polylines.empty()) {
        // connect lines
        size_t polylines_out_first_idx = polylines_out.size();
        if (params.dont_connect())
            append(polylines_out, chain_polylines(polylines));
        else
            this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);
        // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
            for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it)
                it->rotate(infill_angle);
        }
    }
}

} // namespace Slic3r