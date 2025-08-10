#include "../ClipperUtils.hpp"
#include "FillTpmsFK.hpp"
#include <cmath>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <tbb/parallel_for.h>
#include <mutex> 

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

    double denom = v2 - v1;
    double mu;
    if (std::abs(denom) < 1e-12) {
        // avoid division by zero
        mu = 0.5;
    } else {
        mu = (contourValue - v1) / denom;
    }
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
   
    if (data.empty() || data[0].empty()) {
        
        return;
    }
    gridSize_h = static_cast<int>(data.size());
    gridSize_w = static_cast<int>(data[0].size());

    
    if (static_cast<int>(posxy.size()) != gridSize_h || static_cast<int>(posxy[0].size()) != gridSize_w) {
       
        return;
    }

    int total_size = (gridSize_h - 1) * (gridSize_w - 1);
    vector<vector<MarchingSquares::Point>> contourPointss(total_size);

    tbb::parallel_for(tbb::blocked_range<size_t>(0, total_size),
                      [&contourValue, &posxy, &contourPointss, &data, gridSize_w](const tbb::blocked_range<size_t>& range) {
                          for (size_t k = range.begin(); k < range.end(); ++k) {
                              int i = static_cast<int>(k) / (gridSize_w - 1);
                              int j = static_cast<int>(k) % (gridSize_w - 1);
                              
                              if (i + 1 < static_cast<int>(data.size()) && j + 1 < static_cast<int>(data[0].size())) {
                                  process_block(i, j, data, contourValue, posxy, contourPointss[k]);
                              }
                          }
                      });

    vector<pair<myPoint, myPoint>> segments2;
    myPoint                        p1, p2;
    for (int k = 0; k < total_size; k++) {
        for (int i = 0; i < static_cast<int>(contourPointss[k].size()) / 2; i++) {
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
static std::once_flag trig_tables_once_flag; 

#define PIratio 57.29577951308232 // 180/PI

static void initialize_lookup_tables()
{
    for (int i = 0; i < 360; ++i) {
        float angle  = i * (M_PI / 180.0);
        sin_table[i] = std::sin(angle);
        cos_table[i] = std::cos(angle);
    }
}


inline static void ensure_trig_tables_initialized()
{
    std::call_once(trig_tables_once_flag, initialize_lookup_tables);
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
    ensure_trig_tables_initialized(); 

    auto infill_angle = float(this->angle + (CorrectionAngle * 2 * M_PI) / 360.);
    if(std::abs(infill_angle) >= EPSILON)
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
    auto scalar_field = [&](float x, float y) {
        float a_x = myperiod * x;
        float b_y = myperiod * y;
       
        // Fischer - Koch S equation:
        // cos(2x)sin(y)cos(z) + cos(2y)sin(z)cos(x) + cos(2z)sin(x)cos(y) = 0
        const float cos2ax = get_cos(2*a_x);
        const float cos2by = get_cos(2*b_y);
        const float cos2cz = get_cos(2*c_z);
        const float sinby = get_sin(b_y);
        const float cosax = get_cos(a_x);
        const float sinax = get_sin(a_x);
        const float cosby = get_cos(b_y);
        const float sincz = get_sin(c_z);
        const float coscz = get_cos(c_z);

        return cos2ax * sinby * coscz
             + cos2by * sincz * cosax
             + cos2cz * sinax * cosby;
    };

    // Mesh generation
    std::vector<std::vector<MarchingSquares::Point>> posxy;
    int                                              i = 0, j = 0;
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
    const double contour_value = 0.075; // offset from zero to avoid numerical issues
    MarchingSquares::drawContour(contour_value, width , height , data, posxy, polylines, params);

    if (!polylines.empty()) {
        // Apply multiline offset if needed
        multiline_fill(polylines, params, spacing);


        polylines = intersection_pl(polylines, expolygon);

        // Remove very small bits, but be careful to not remove infill lines connecting thin walls!
        // The infill perimeter lines should be separated by around a single infill line width.
        const double minlength = scale_(0.8 * this->spacing);
		polylines.erase(
			std::remove_if(polylines.begin(), polylines.end(), [minlength](const Polyline &pl) { return pl.length() < minlength; }),
			polylines.end());

		// connect lines
		size_t polylines_out_first_idx = polylines_out.size();
        chain_or_connect_infill(std::move(polylines), expolygon, polylines_out, this->spacing, params);

	    // new paths must be rotated back
        if (std::abs(infill_angle) >= EPSILON) {
	        for (auto it = polylines_out.begin() + polylines_out_first_idx; it != polylines_out.end(); ++ it)
	        	it->rotate(infill_angle);
	    }
        
    }
}

} // namespace Slic3r
