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

void FillTpmsFK::_fill_surface_single(const FillParams& params,
                                     unsigned int thickness_layers,
                                     const std::pair<float, Point>& direction,
                                     ExPolygon expolygon,
                                     Polylines& polylines_out)
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
    float vari_T =  4.18 * spacing * params.multiline / density_factor; 
    
    BoundingBox bb = expolygon.contour.bounding_box();
    auto cenpos = unscale(bb.center());
    auto boxsize = unscale(bb.size());
    float xlen = boxsize.x();
    float ylen = boxsize.y();

  
    float delta = 0.1f; // Paso de la malla (ajustar para calidad/rendimiento)
    float margin = 3.0f; 
    
   
    float myperiod = 2 * PI / vari_T;
    float c_z = myperiod * this->z; //altura en z
    float cos_z = get_cos(c_z);
    float sin_z = get_sin(c_z);

    
    auto scalar_field = [&](float x, float y) {
        float a_x = myperiod * x;
        float b_y = myperiod * y;
       
        
        float cos2ax = get_cos(2*a_x);
        float sinby = get_sin(b_y);
        float cosax = get_cos(a_x);
        float sinax = get_sin(a_x);
        float cosby = get_cos(b_y);
        float cos2by = get_cos(2*b_y);
        float cos2cz = get_cos(2*c_z);
        
        
        // cos(2x)sin(y)cos(z) + cos(2y)sin(z)cos(x) + cos(2z)sin(x)cos(y)
        return cos2ax * sinby * cos_z 
             + cos2by * sin_z * cosax
             + cos2cz * sinax * cosby;
    };

    
    std::vector<std::vector<MarchingSquares::Point>> posxy;
    int i = 0, j = 0;
    for (float y = -(ylen)/2.0f - margin; y < (ylen)/2.0f + margin; y += delta, i++) {
        j = 0;
        std::vector<MarchingSquares::Point> colposxy;
        for (float x = -(xlen)/2.0f - margin; x < (xlen)/2.0f + margin; x += delta, j++) {
            MarchingSquares::Point pt;
            pt.x = cenpos.x() + x;
            pt.y = cenpos.y() + y;
            colposxy.push_back(pt);
        }
        posxy.push_back(colposxy);
    }

    
    std::vector<std::vector<double>> data(posxy.size(), std::vector<double>(posxy[0].size()));
    int width = posxy[0].size();
    int height = posxy.size();
    
    tbb::parallel_for(tbb::blocked_range<size_t>(0, height),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                for (size_t j = 0; j < width; ++j) {
                    data[i][j] = scalar_field(posxy[i][j].x, posxy[i][j].y);
                }
            }
        });

    
    double min_val = *std::min_element(data.begin()->begin(), data.begin()->end());
    double max_val = *std::max_element(data.begin()->begin(), data.begin()->end());
    
    tbb::parallel_for(tbb::blocked_range<size_t>(0, height),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i < range.end(); ++i) {
                for (size_t j = 0; j < width; ++j) {
                    data[i][j] = (data[i][j] - min_val) / (max_val - min_val) - 0.5; 
                }
            }
        });

   
    Polylines polylines;
    double contour_value = 0.0; 
    MarchingSquares::drawContour(contour_value, width-1, height-1, data, posxy, polylines);
    
   
    if (!polylines.empty()) {
        
        multiline_fill(polylines, params, spacing);
        
       
        polylines = intersection_pl(polylines, expolygon);

        if (!polylines.empty()) {
            size_t polylines_out_first_idx = polylines_out.size();
            if (params.dont_connect()) {
                append(polylines_out, chain_polylines(polylines));
            } else {
                this->connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);
            }
            
            
            if (std::abs(infill_angle) >= EPSILON) {
                for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++it) {
                    it->rotate(infill_angle);
                }
            }
        }
    }
}

} // namespace Slic3r