#include "CurveAnalyzer.hpp"

#include <cmath>
#include <cassert>

static const int curvatures_sampling_number = 6;
static const double curvatures_densify_width = 1;           // mm
static const double curvatures_sampling_width = 6;         // mm
static const double curvatures_angle_best = PI/6;
static const double curvatures_angle_worst = 5*PI/6;

static const double curvatures_best = (curvatures_angle_best * 1000 / curvatures_sampling_width);
static const double curvatures_worst = (curvatures_angle_worst * 1000 / curvatures_sampling_width);

namespace Slic3r {

// This function is used to calculate curvature for paths.
// Paths must be generated from a closed polygon.
// Data in paths may be modify, and paths will be spilited and regenerated
// arrording to different curve degree.
void CurveAnalyzer::calculate_curvatures(ExtrusionPaths& paths, ECurveAnalyseMode mode)
{
    Polygon polygon;
    std::vector<float> paths_length(paths.size(), 0.0);
    for (size_t i = 0; i < paths.size(); i++) {
        if (i == 0) {
            paths_length[i] = paths[i].polyline.length();
        }
        else {
            paths_length[i] = paths_length[i - 1] + paths[i].polyline.length();
        }
        polygon.points.insert(polygon.points.end(), paths[i].polyline.points.begin(), paths[i].polyline.points.end() - 1);
    }
    // 1 generate point series which is on the line of polygon, point distance along the polygon is smaller than 1mm
    polygon.densify(scale_(curvatures_densify_width));
    std::vector<float> polygon_length = polygon.parameter_by_length();

    // 2 calculate angle of every segment
    size_t point_num = polygon.points.size();
    std::vector<float> angles(point_num, 0.f);
    for (size_t i = 0; i < point_num; i++) {
        size_t curr = i;
        size_t prev = (curr == 0) ? point_num - 1 : curr - 1;
        size_t next = (curr == point_num - 1) ? 0 : curr + 1;
        const Point  v1 = polygon.points[curr] - polygon.points[prev];
        const Point  v2 = polygon.points[next] - polygon.points[curr];
        int64_t dot = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
        int64_t cross = int64_t(v1(0)) * int64_t(v2(1)) - int64_t(v1(1)) * int64_t(v2(0));
        if (mode == ECurveAnalyseMode::RelativeMode)
            cross = abs(cross);
        angles[curr] = float(atan2(double(cross), double(dot)));
    }

    // 3 generate sum of angle and length of the adjacent segment for eveny point, range is approximately curvatures_sampling_width.
    //   And then calculate the curvature
    std::vector<float> sum_angles(point_num, 0.f);
    std::vector<double> average_curvatures(point_num, 0.f);
    if (paths_length.back() < scale_(curvatures_sampling_width)) {
        // loop is too short, so the curvatures is max
        double temp = 1000.0 * 2.0 * PI / ((double)(paths_length.back()) * SCALING_FACTOR);
        for (size_t i = 0; i < point_num; i++) {
            average_curvatures[i] = temp;
        }
    }
    else {
        for (size_t i = 0; i < point_num; i++) {
            // right segment
            size_t j = i;
            float right_length = 0;
            while (right_length < scale_(curvatures_sampling_width / 2)) {
                int next_j = (j + 1 >= point_num) ? 0 : j + 1;
                sum_angles[i] += angles[j];
                right_length += (polygon.points[next_j] - polygon.points[j]).cast<float>().norm();
                j = next_j;
            }
            // left segment
            size_t k = i;
            float left_length = 0;
            while (left_length < scale_(curvatures_sampling_width / 2)) {
                size_t next_k = (k < 1) ? point_num - 1 : k - 1;
                sum_angles[i] += angles[k];
                left_length += (polygon.points[k] - polygon.points[next_k]).cast<float>().norm();
                k = next_k;
            }
            sum_angles[i] = sum_angles[i] - angles[i];
            average_curvatures[i] = (1000.0 * (double)abs(sum_angles[i]) / (double)curvatures_sampling_width);
        }
    }

    // 4 calculate the degree of curve
    //   For angle >= curvatures_angle_worst, we think it's enough to be worst. Should make the speed to be slowest.
    //   For angle <= curvatures_angle_best, we thins it's enough to be best. Should make the speed to be fastest.
    //   Use several steps [0 1 2...curvatures_sampling_number - 1] to describe the degree of curve. 0 is the flatest. curvatures_sampling_number - 1 is the sharpest
    std::vector<int> curvatures_norm(point_num, 0.f);
    std::vector<int> sampling_step(curvatures_sampling_number - 1, 0);
    for (size_t i = 0; i < curvatures_sampling_number - 1; i++) {
        sampling_step[i] = (2 * i + 1) * 50 / (curvatures_sampling_number - 1);
    }
    sampling_step[0] = 0;
    sampling_step[curvatures_sampling_number - 2] = 100;
    for (size_t i = 0; i < point_num; i++) {
        curvatures_norm[i] = (int)(100 * (average_curvatures[i] - curvatures_best) / (curvatures_worst - curvatures_best));
        if (curvatures_norm[i] >= 100)
            curvatures_norm[i] = curvatures_sampling_number - 1;
        else
            for (size_t j = 0; j < curvatures_sampling_number - 1; j++) {
                if (curvatures_norm[i] < sampling_step[j]) {
                    curvatures_norm[i] = j;
                    break;
                }
            }
    }
    std::vector<std::pair<std::pair<Point, int>, int>> curvature_list;   // point, index, curve_degree
    int last_curvature_norm = -1;
    for (int i = 0; i < point_num; i++) {
        if (curvatures_norm[i] != last_curvature_norm) {
            last_curvature_norm = curvatures_norm[i];
            curvature_list.push_back(std::pair<std::pair<Point, int>, int>(std::pair<Point, int>(polygon.points[i], i), last_curvature_norm));
        }
    }
    curvature_list.push_back(std::pair<std::pair<Point, int>, int>(std::pair<Point, int>(polygon.points[0], point_num), curvatures_norm[0])); // the last point should be the first point

    //5 split and modify the path according to the degree of curve
    if (curvature_list.size() == 2) {   // all paths has same curva_degree
        for (size_t i = 0; i < paths.size(); i++) {
            paths[i].set_curve_degree(curvature_list[0].second);
        }
    }
    else {
        ExtrusionPaths out;
        out.reserve(paths.size() + curvature_list.size() - 1);
        size_t j = 1;
        int current_curva_norm = curvature_list[0].second;
        for (size_t i = 0; i < paths.size() && j < curvature_list.size(); i++) {
            if (paths[i].last_point() == curvature_list[j].first.first) {
                paths[i].set_curve_degree(current_curva_norm);
                out.push_back(paths[i]);
                current_curva_norm = curvature_list[j].second;
                j++;
                continue;
            }
            else if (paths[i].first_point() == curvature_list[j].first.first) {
                if (paths[i].polyline.points.front() == paths[i].polyline.points.back()) {
                    paths[i].set_curve_degree(current_curva_norm);
                    out.push_back(paths[i]);
                    current_curva_norm = curvature_list[j].second;
                    j++;
                    continue;
                }
                else {
                    // should never happen
                    assert(0);
                }
            }

            if (paths_length[i] <= polygon_length[curvature_list[j].first.second] ||
                paths[i].last_point() == curvature_list[j].first.first) {
                // save paths[i] directly
                paths[i].set_curve_degree(current_curva_norm);
                out.push_back(paths[i]);
                if (paths[i].last_point() == curvature_list[j].first.first) {
                    current_curva_norm = curvature_list[j].second;
                    j++;
                }
            }
            else {
                //split paths[i]
                ExtrusionPath current_path = paths[i];
                while (j < curvature_list.size()) {
                    Polyline left, right;
                    current_path.polyline.split_at(curvature_list[j].first.first, &left, &right);
                    ExtrusionPath left_path(left, current_path);
                    left_path.set_curve_degree(current_curva_norm);
                    out.push_back(left_path);
                    ExtrusionPath right_path(right, current_path);
                    current_path = right_path;

                    current_curva_norm = curvature_list[j].second;
                    j++;
                    if (j < curvature_list.size() &&
                        (paths_length[i] <= polygon_length[curvature_list[j].first.second] ||
                            paths[i].last_point() == curvature_list[j].first.first)) {
                        current_path.set_curve_degree(current_curva_norm);
                        out.push_back(current_path);
                        if (current_path.last_point() == curvature_list[j].first.first) {
                            current_curva_norm = curvature_list[j].second;
                            j++;
                        }
                        break;
                    }
                }
            }
        }

        paths.clear();
        paths.reserve(out.size());
        for (int i = 0; i < out.size(); i++) {
            paths.push_back(out[i]);
        }
    }
}

}