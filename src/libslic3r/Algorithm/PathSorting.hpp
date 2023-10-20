///|/ Copyright (c) Prusa Research 2023 Pavel Mikuš @Godrak
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SRC_LIBSLIC3R_PATH_SORTING_HPP_
#define SRC_LIBSLIC3R_PATH_SORTING_HPP_

#include "AABBTreeLines.hpp"
#include "BoundingBox.hpp"
#include "Line.hpp"
#include "ankerl/unordered_dense.h"
#include <algorithm>
#include <iterator>
#include <libslic3r/Point.hpp>
#include <libslic3r/Polygon.hpp>
#include <libslic3r/ExPolygon.hpp>
#include <limits>
#include <type_traits>
#include <unordered_set>

namespace Slic3r {
namespace Algorithm {

//Sorts the paths such that all paths between begin and last_seed are printed first, in some order. The rest of the paths is sorted
// such that the paths that are touching some of the already printed are printed first, sorted secondary by the distance to the last point of the last 
// printed path.
// begin, end, and last_seed are random access iterators. touch_limit_distance is used to check if the paths are touching - if any part of the path gets this close
// to the second, then they touch.
// convert_to_lines is a lambda that should accept the path as argument and return it as Lines vector, in correct order.
template<typename RandomAccessIterator, typename ToLines>
void sort_paths(RandomAccessIterator begin, RandomAccessIterator end, Point start, double touch_limit_distance, ToLines convert_to_lines)
{
    size_t paths_count = std::distance(begin, end);
    if (paths_count <= 1)
        return;

    auto paths_touch = [touch_limit_distance](const AABBTreeLines::LinesDistancer<Line> &left,
                                              const AABBTreeLines::LinesDistancer<Line> &right) {
        for (const Line &l : left.get_lines()) {
            if (right.distance_from_lines<false>(l.a) < touch_limit_distance) {
                return true;
            }
        }
        if (right.distance_from_lines<false>(left.get_lines().back().b) < touch_limit_distance) {
            return true;
        }

        for (const Line &l : right.get_lines()) {
            if (left.distance_from_lines<false>(l.a) < touch_limit_distance) {
                return true;
            }
        }
        if (left.distance_from_lines<false>(right.get_lines().back().b) < touch_limit_distance) {
            return true;
        }
        return false;
    };

    std::vector<AABBTreeLines::LinesDistancer<Line>> distancers(paths_count);
    for (size_t path_idx = 0; path_idx < paths_count; path_idx++) {
        distancers[path_idx] = AABBTreeLines::LinesDistancer<Line>{convert_to_lines(*std::next(begin, path_idx))};
    }

    std::vector<std::unordered_set<size_t>> dependencies(paths_count);
    for (size_t path_idx = 0; path_idx < paths_count; path_idx++) {
        for (size_t next_path_idx = path_idx + 1; next_path_idx < paths_count; next_path_idx++) {
            if (paths_touch(distancers[path_idx], distancers[next_path_idx])) {
                dependencies[next_path_idx].insert(path_idx);
            }
        }
    }

    Point current_point = start;

    std::vector<std::pair<size_t, bool>> correct_order_and_direction(paths_count);
    size_t                               unsorted_idx = 0;
    size_t                               null_idx     = size_t(-1);
    size_t                               next_idx     = null_idx;
    bool                                 reverse      = false;
    while (unsorted_idx < paths_count) {
        next_idx          = null_idx;
        double lines_dist = std::numeric_limits<double>::max();
        for (size_t path_idx = 0; path_idx < paths_count; path_idx++) {
            if (!dependencies[path_idx].empty())
                continue;

            double ldist = distancers[path_idx].distance_from_lines<false>(current_point);
            if (ldist < lines_dist) {
                const auto &lines  = distancers[path_idx].get_lines();
                double      dist_a = (lines.front().a - current_point).cast<double>().squaredNorm();
                double      dist_b = (lines.back().b - current_point).cast<double>().squaredNorm();
                next_idx           = path_idx;
                reverse            = dist_b < dist_a;
                lines_dist         = ldist;
            }
        }

        // we have valid next_idx, sort it, update dependencies, update current point
        correct_order_and_direction[next_idx] = {unsorted_idx, reverse};
        unsorted_idx++;
        current_point = reverse ? distancers[next_idx].get_lines().front().a : distancers[next_idx].get_lines().back().b;

        dependencies[next_idx].insert(null_idx); // prevent it from being selected again
        for (size_t path_idx = 0; path_idx < paths_count; path_idx++) {
            dependencies[path_idx].erase(next_idx);
        }
    }

    for (size_t path_idx = 0; path_idx < paths_count; path_idx++) {
        if (correct_order_and_direction[path_idx].second) {
            std::next(begin, path_idx)->reverse();
        }
    }

    for (size_t i = 0; i < correct_order_and_direction.size() - 1; i++) {
        bool swapped = false;
        for (size_t j = 0; j < correct_order_and_direction.size() - i - 1; j++) {
            if (correct_order_and_direction[j].first > correct_order_and_direction[j + 1].first) {
                std::swap(correct_order_and_direction[j], correct_order_and_direction[j + 1]);
                std::iter_swap(std::next(begin, j), std::next(begin, j + 1));
                swapped = true;
            }
        }
        if (swapped == false) {
            break;
        }
    }
}

}} // namespace Slic3r::Algorithm

#endif /*SRC_LIBSLIC3R_PATH_SORTING_HPP_*/