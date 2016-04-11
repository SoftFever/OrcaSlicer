#include "../ClipperUtils.hpp"
#include "../PolylineCollection.hpp"
#include "../Surface.hpp"

#include "FillHoneycomb.hpp"

namespace Slic3r {

Polylines FillHoneycomb::fill_surface(const Surface *surface, const FillParams &params)
{
    std::pair<float, Point> rotate_vector = this->infill_direction(surface);
    
    // cache hexagons math
    CacheID cache_id(params.density, this->spacing);
    Cache::iterator it_m = this->cache.find(cache_id);
    if (it_m == this->cache.end()) {
#if SLIC3R_CPPVER > 11
        it_m = this->cache.emplace_hint(it_m);
#else
        it_m = this->cache.insert(it_m, std::pair<CacheID, CacheData>(cache_id, CacheData()));
#endif
        CacheData &m = it_m->second;
        coord_t min_spacing = scale_(this->spacing);
        m.distance = min_spacing / params.density;
        m.hex_side = m.distance / (sqrt(3)/2);
        m.hex_width = m.distance * 2; // $m->{hex_width} == $m->{hex_side} * sqrt(3);
        coord_t hex_height = m.hex_side * 2;
        m.pattern_height = hex_height + m.hex_side;
        m.y_short = m.distance * sqrt(3)/3;
        m.x_offset = min_spacing / 2;
        m.y_offset = m.x_offset * sqrt(3)/3;
        m.hex_center = Point(m.hex_width/2, m.hex_side);
    }
    CacheData &m = it_m->second;

    Polygons polygons;
    {
        // adjust actual bounding box to the nearest multiple of our hex pattern
        // and align it so that it matches across layers
        
        BoundingBox bounding_box = surface->expolygon.contour.bounding_box();
        {
            // rotate bounding box according to infill direction
            Polygon bb_polygon = bounding_box.polygon();
            bb_polygon.rotate(rotate_vector.first, m.hex_center);
            bounding_box = bb_polygon.bounding_box();
            
            // extend bounding box so that our pattern will be aligned with other layers
            // $bounding_box->[X1] and [Y1] represent the displacement between new bounding box offset and old one
            bounding_box.merge(Point(
                bounding_box.min.x - (bounding_box.min.x % m.hex_width),
                bounding_box.min.y - (bounding_box.min.y % m.pattern_height)));
        }
        
        coord_t x = bounding_box.min.x;
        while (x <= bounding_box.max.x) {
            Polygon p;
            coord_t ax[2] = { x + m.x_offset, x + m.distance - m.x_offset };
            for (size_t i = 0; i < 2; ++ i) {
                std::reverse(p.points.begin(), p.points.end()); // turn first half upside down
                for (coord_t y = bounding_box.min.y; y <= bounding_box.max.y; y += m.y_short + m.hex_side + m.y_short + m.hex_side) {
                    p.points.push_back(Point(ax[1], y + m.y_offset));
                    p.points.push_back(Point(ax[0], y + m.y_short - m.y_offset));
                    p.points.push_back(Point(ax[0], y + m.y_short + m.hex_side + m.y_offset));
                    p.points.push_back(Point(ax[1], y + m.y_short + m.hex_side + m.y_short - m.y_offset));
                    p.points.push_back(Point(ax[1], y + m.y_short + m.hex_side + m.y_short + m.hex_side + m.y_offset));
                }
                ax[0] = ax[0] + m.distance;
                ax[1] = ax[1] + m.distance;
                std::swap(ax[0], ax[1]); // draw symmetrical pattern
                x += m.distance;
            }
            p.rotate(-rotate_vector.first, m.hex_center);
            polygons.push_back(p);
        }
    }
    
    Polylines paths;
    if (params.complete || true) {
        // we were requested to complete each loop;
        // in this case we don't try to make more continuous paths
        Polygons polygons_trimmed = intersection((Polygons)*surface, polygons);
        for (Polygons::iterator it = polygons_trimmed.begin(); it != polygons_trimmed.end(); ++ it)
            paths.push_back(it->split_at_first_point());
    } else {
        // consider polygons as polylines without re-appending the initial point:
        // this cuts the last segment on purpose, so that the jump to the next 
        // path is more straight
        {
            Polylines p;
            for (Polygons::iterator it = polygons.begin(); it != polygons.end(); ++ it)
                p.push_back((Polyline)(*it));
            paths = intersection(p, (Polygons)*surface);
        }

        // connect paths
        if (! paths.empty()) { // prevent calling leftmost_point() on empty collections
            Polylines chained = PolylineCollection::chained_path_from(
#if SLIC3R_CPPVER >= 11
                std::move(paths), 
#else
                paths,
#endif
                PolylineCollection::leftmost_point(paths), false);
            assert(paths.empty());
            paths.clear();
            for (Polylines::iterator it_path = chained.begin(); it_path != chained.end(); ++ it_path) {
                if (! paths.empty()) {
                    // distance between first point of this path and last point of last path
                    double distance = paths.back().last_point().distance_to(it_path->first_point());
                    if (distance <= m.hex_width) {
                        paths.back().points.insert(paths.back().points.end(), it_path->points.begin(), it_path->points.end());
                        continue;
                    }
                }
                // Don't connect the paths.
                paths.push_back(*it_path);
            }
        }
        
        // clip paths again to prevent connection segments from crossing the expolygon boundaries
        Polylines paths_trimmed = intersection(paths, to_polygons(offset_ex(surface->expolygon, SCALED_EPSILON)));
#if SLIC3R_CPPVER >= 11
        paths = std::move(paths_trimmed);
#else
        std::swap(paths, paths_trimmed);
#endif
    }
    
    return paths;
}

} // namespace Slic3r
