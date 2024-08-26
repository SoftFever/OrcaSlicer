#include <math.h>

#include "MinimumSpanningTree.hpp"
#include "TreeSupport.hpp"
#include "Print.hpp"
#include "Layer.hpp"
#include "Fill/FillBase.hpp"
#include "Fill/FillConcentric.hpp"
#include "CurveAnalyzer.hpp"
#include "SVG.hpp"
#include "ShortestPath.hpp"
#include "I18N.hpp"
#include <libnest2d/backends/libslic3r/geometries.hpp>

#include <boost/log/trivial.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#define _L(s) Slic3r::I18N::translate(s)

#define USE_PLAN_LAYER_HEIGHTS 1

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#ifndef SIGN
#define SIGN(x) (x>=0?1:-1)
#endif
#define TAU (2.0 * M_PI)
#define NO_INDEX (std::numeric_limits<unsigned int>::max())

// #define SUPPORT_TREE_DEBUG_TO_SVG

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
#include "nlohmann/json.hpp"
#endif
namespace Slic3r
{
#define unscale_(val) ((val) * SCALING_FACTOR)

inline unsigned int round_divide(unsigned int dividend, unsigned int divisor) //!< Return dividend divided by divisor rounded to the nearest integer
{
    return (dividend + divisor / 2) / divisor;
}
inline unsigned int round_up_divide(unsigned int dividend, unsigned int divisor) //!< Return dividend divided by divisor rounded to the nearest integer
{
    return (dividend + divisor - 1) / divisor;
}

inline double dot_with_unscale(const Point a, const Point b)
{
    return unscale_(a(0)) * unscale_(b(0)) + unscale_(a(1)) * unscale_(b(1));
}

inline double vsize2_with_unscale(const Point pt)
{
    return dot_with_unscale(pt, pt);
}

inline Point turn90_ccw(const Point pt)
{
    Point ret;

    ret(0) = -pt(1);
    ret(1) = pt(0);
    return ret;
}

inline Point normal(Point pt, double scale)
{
    double length = scale_(sqrt(vsize2_with_unscale(pt)));

    return pt * (scale / length);
}

enum TreeSupportStage {
    STAGE_DETECT_OVERHANGS,
    STAGE_GENERATE_CONTACT_NODES,
    STAGE_DROP_DOWN_NODES,
    STAGE_DRAW_CIRCLES,
    STAGE_GENERATE_TOOLPATHS,
    STAGE_MinimumSpanningTree,
    STAGE_GET_AVOIDANCE,
    STAGE_projection_onto_ex,
    STAGE_get_collision,
    STAGE_intersection_ln,
    STAGE_total,
    NUM_STAGES
};

class TreeSupportProfiler
{
public:
    uint32_t stage_durations[NUM_STAGES];
    uint32_t stage_index = 0;
    boost::posix_time::ptime tic_time;
    boost::posix_time::ptime toc_time;

    TreeSupportProfiler()
    {
        for (uint32_t& item : stage_durations) {
            item = 0;
        }
    }

    void stage_start(TreeSupportStage stage)
    {
        if (stage > NUM_STAGES)
            return;

        m_stage_start_times[stage] = boost::posix_time::microsec_clock::local_time();
    }

    void stage_finish(TreeSupportStage stage)
    {
        if (stage > NUM_STAGES)
            return;

        boost::posix_time::ptime time = boost::posix_time::microsec_clock::local_time();
        stage_durations[stage] = (time - m_stage_start_times[stage]).total_milliseconds();
    }

    void tic() { tic_time = boost::posix_time::microsec_clock::local_time(); }
    void toc() { toc_time = boost::posix_time::microsec_clock::local_time(); }
    void stage_add(TreeSupportStage stage, bool do_toc = false)
    {
        if (stage > NUM_STAGES)
            return;
        if(do_toc)
            toc_time = boost::posix_time::microsec_clock::local_time();
        stage_durations[stage] += (toc_time - tic_time).total_milliseconds();
    }

    std::string report()
    {
        std::stringstream ss;
        ss << "total overhange cost: " << stage_durations[STAGE_total]
            << "; STAGE_DETECT_OVERHANGS: " << stage_durations[STAGE_DETECT_OVERHANGS]
            << "; STAGE_GENERATE_CONTACT_NODES: " << stage_durations[STAGE_GENERATE_CONTACT_NODES]
            << "; STAGE_DROP_DOWN_NODES: " << stage_durations[STAGE_DROP_DOWN_NODES]
            << "; STAGE_DRAW_CIRCLES: " << stage_durations[STAGE_DRAW_CIRCLES]
            << "; STAGE_GENERATE_TOOLPATHS: " << stage_durations[STAGE_GENERATE_TOOLPATHS]
            << "; STAGE_MinimumSpanningTree: " << stage_durations[STAGE_MinimumSpanningTree]
            << "; STAGE_GET_AVOIDANCE: " << stage_durations[STAGE_GET_AVOIDANCE]
            << "; STAGE_projection_onto_ex: " << stage_durations[STAGE_projection_onto_ex]
            << "; STAGE_get_collision: " << stage_durations[STAGE_get_collision]
            << "; STAGE_intersection_ln: " << stage_durations[STAGE_intersection_ln];

        return ss.str();
    }
private:
    boost::posix_time::ptime m_stage_start_times[NUM_STAGES];
};
TreeSupportProfiler profiler;

Lines spanning_tree_to_lines(const std::vector<MinimumSpanningTree>& spanning_trees)
{
    Lines polylines;
    for (const MinimumSpanningTree& mst : spanning_trees) {
        std::vector<Point> points = mst.vertices();
        std::unordered_set<Point, PointHash> to_ignore;
        for (Point pt1 : points) {
            if (to_ignore.find(pt1) != to_ignore.end())
                continue;

            const std::vector<Point>& neighbours = mst.adjacent_nodes(pt1);
            if (neighbours.empty())
                continue;

            for (Point pt2 : neighbours) {
                if (to_ignore.find(pt2) != to_ignore.end())
                    continue;

                Line line(pt1, pt2);
                polylines.push_back(line);
            }

            to_ignore.insert(pt1);
        }
    }
    return polylines;
}


#ifdef SUPPORT_TREE_DEBUG_TO_SVG
static  std::string get_svg_filename(std::string layer_nr_or_z, std::string tag = "bbl_ts")
{
    static bool rand_init = false;

    if (!rand_init) {
        srand(time(NULL));
        rand_init = true;
    }

    int rand_num = rand() % 1000000;
    //makedir("./SVG");
    std::string prefix = "./SVG/";
    std::string suffix = ".svg";
    return prefix + tag + "_" + layer_nr_or_z /*+ "_" + std::to_string(rand_num)*/ + suffix;
}

static void draw_contours_and_nodes_to_svg
(
    std::string layer_nr_or_z,
    const ExPolygons &overhangs,
    const ExPolygons &overhangs_after_offset,
    const ExPolygons &outlines_below,
    const std::vector<TreeSupport::Node*> &layer_nodes,
    const std::vector<TreeSupport::Node*> &lower_layer_nodes,
    std::string name_prefix,
    std::vector<std::string> legends = { "overhang","avoid","outlines" }, std::vector<std::string> colors = { "blue","red","yellow" }
)
{
    BoundingBox bbox = get_extents(overhangs);
    bbox.merge(get_extents(overhangs_after_offset));
    bbox.merge(get_extents(outlines_below));
    Points layer_pts;
    for (TreeSupport::Node* node : layer_nodes) {
        layer_pts.push_back(node->position);
    }
    bbox.merge(get_extents(layer_pts));
    bbox.inflated(scale_(1));
    bbox.max.x() = std::max(bbox.max.x(), (coord_t)scale_(10));
    bbox.max.y() = std::max(bbox.max.y(), (coord_t)scale_(10));

    SVG svg;
    if(!layer_nr_or_z.empty())
        svg.open(get_svg_filename(layer_nr_or_z, name_prefix), bbox);
    else
        svg.open(name_prefix, bbox);
    if (!svg.is_opened())        return;

    // draw grid
    svg.draw_grid(bbox, "gray", coord_t(scale_(0.05)));

    // draw overhang areas
    svg.draw_outline(union_ex(overhangs), colors[0]);
    svg.draw_outline(union_ex(overhangs_after_offset), colors[1]);
    svg.draw_outline(outlines_below, colors[2]);

    // draw legend
    if (!lower_layer_nodes.empty()) {
        svg.draw_text(bbox.min + Point(scale_(0), scale_(0)), format("nPoints: %1%->%2%",layer_nodes.size(), lower_layer_nodes.size()).c_str(), "green", 2);
    }
    else {
        svg.draw_text(bbox.min + Point(scale_(0), scale_(0)), ("nPoints: " + std::to_string(layer_nodes.size())).c_str(), "green", 2);
    }
    svg.draw_text(bbox.min + Point(scale_(0), scale_(2)), legends[0].c_str(), colors[0].c_str(), 2);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(4)), legends[1].c_str(), colors[1].c_str(), 2);
    svg.draw_text(bbox.min + Point(scale_(0), scale_(6)), legends[2].c_str(), colors[2].c_str(), 2);

    // draw layer nodes
    svg.draw(layer_pts, "green", coord_t(scale_(0.1)));
#if 0
    // lower layer points
    layer_pts.clear();
    for (TreeSupport::Node *node : lower_layer_nodes) {
        layer_pts.push_back(node->position);
    }
    svg.draw(layer_pts, "black", coord_t(scale_(0.1)));

    // higher layer points
    layer_pts.clear();
    for (TreeSupport::Node* node : layer_nodes) {
        if(node->parent)
            layer_pts.push_back(node->parent->position);
    }
    svg.draw(layer_pts, "blue", coord_t(scale_(0.1)));
#endif
}

static void draw_layer_mst
(const std::string &layer_nr_or_z,
    const std::vector<MinimumSpanningTree> &spanning_trees,
    const ExPolygons& outline
)
{
    auto lines = spanning_tree_to_lines(spanning_trees);
    BoundingBox bbox = get_extents(lines);
    for (auto& poly : outline)
    {
        BoundingBox bb = poly.contour.bounding_box();
        bbox.merge(bb);
    }

    SVG svg(get_svg_filename(layer_nr_or_z, "mstree").c_str(), bbox);
    if (!svg.is_opened())        return;

    svg.draw(lines, "blue", coord_t(scale_(0.05)));
    svg.draw_outline(outline, "yellow");
    for (auto &spanning_tree : spanning_trees)
        svg.draw(spanning_tree.vertices(), "black", coord_t(scale_(0.1)));
}

static void draw_two_overhangs_to_svg(SupportLayer* ts_layer, const ExPolygons& overhangs1, const ExPolygons& overhangs2)
{
    if (overhangs1.empty() && overhangs2.empty())
        return;
    BoundingBox bbox1 = get_extents(overhangs1);
    BoundingBox bbox2 = get_extents(overhangs2);
    bbox1.merge(bbox2);

    SVG svg(get_svg_filename(std::to_string(ts_layer->print_z), "two_overhangs"), bbox1);
    if (!svg.is_opened())        return;

    svg.draw(union_ex(overhangs1), "blue");
    svg.draw(union_ex(overhangs2), "red");
}

static void draw_polylines(SupportLayer* ts_layer, Polylines& polylines)
{
    if (polylines.empty())
        return;
    BoundingBox bbox = get_extents(polylines);

    SVG svg(get_svg_filename(std::to_string(ts_layer->print_z), "lightnings"), bbox);
    if (!svg.is_opened())        return;

    int id = 0;
    for (Polyline& pline : polylines)
    {
        int i1, i2;
        for (size_t i = 0; i < pline.size() - 1; i++)
        {
            i1 = i;
            i2 = i + 1;
            svg.draw(Line(pline.points[i1], pline.points[i2]), "blue");
            svg.draw(pline.points[i1], "red");
            id++;
            svg.draw_text(pline.points[i1], std::to_string(id).c_str(), "black", 1);
        }
        svg.draw(pline.points[i2], "red");
        id++;
        svg.draw_text(pline.points[i2], std::to_string(id).c_str(), "black", 1);
    }
}
#endif

// Move point from inside polygon if distance>0, outside if distance<0.
// Special case: distance=0 means find the nearest point of from on the polygon contour.
// The max move distance should not excceed max_move_distance.
static unsigned int move_inside_expoly(const ExPolygon &polygon, Point& from, double distance = 0, double max_move_distance = std::numeric_limits<double>::max())
{
    //TODO: This is copied from the moveInside of Polygons.
    /*
    We'd like to use this function as subroutine in moveInside(Polygons...), but
    then we'd need to recompute the distance of the point to the polygon, which
    is expensive. Or we need to return the distance. We need the distance there
    to compare with the distance to other polygons.
    */
    Point ret = from;
    double bestDist2 = std::numeric_limits<double>::max();
    bool is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
    const Polygon &contour = polygon.contour;

    if (contour.points.size() < 2)
    {
        return 0;
    }
    Point p0 = contour.points[polygon.contour.size() - 2];
    Point p1 = contour.points.back();
    // because we compare with vsize2_with_unscale here (no division by zero), we also need to compare by vsize2_with_unscale inside the loop
    // to avoid integer rounding edge cases
    bool projected_p_beyond_prev_segment = dot_with_unscale(p1 - p0, from - p0) >= vsize2_with_unscale(p1 - p0);
    for(const Point& p2 : polygon.contour.points)
    {
        // X = A + Normal(B-A) * (((B-A) dot_with_unscale (P-A)) / VSize(B-A));
        //   = A +       (B-A) *  ((B-A) dot_with_unscale (P-A)) / VSize2(B-A);
        // X = P projected on AB
        const Point& a = p1;
        const Point& b = p2;
        const Point& p = from;
        Point ab = b - a;
        Point ap = p - a;
        double ab_length2 = vsize2_with_unscale(ab);
        if(ab_length2 <= 0) //A = B, i.e. the input polygon had two adjacent points on top of each other.
        {
            p1 = p2; //Skip only one of the points.
            continue;
        }
        double dot_prod = dot_with_unscale(ab, ap);
        if (dot_prod <= 0) // x is projected to before ab
        {
            if (projected_p_beyond_prev_segment)
            { //  case which looks like:   > .
                projected_p_beyond_prev_segment = false;
                Point& x = p1;

                double dist2 = vsize2_with_unscale(x - p);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    if (distance == 0)
                    {
                        ret = x;
                    }
                    else
                    {
                        // TODO: check whether it needs scale_()
                        Point inward_dir = turn90_ccw(normal(ab, 10.0) + normal(p1 - p0, 10.0)); // inward direction irrespective of sign of [distance]
                        // MM2INT(10.0) to retain precision for the eventual normalization
                        ret = x + normal(inward_dir, scale_(distance));
                        is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) * distance >= 0;
                    }
                }
            }
            else
            {
                projected_p_beyond_prev_segment = false;
                p0 = p1;
                p1 = p2;
                continue;
            }
        }
        else if (dot_prod >= ab_length2) // x is projected to beyond ab
        {
            projected_p_beyond_prev_segment = true;
            p0 = p1;
            p1 = p2;
            continue;
        }
        else
        { // x is projected to a point properly on the line segment (not onto a vertex). The case which looks like | .
            projected_p_beyond_prev_segment = false;
            Point x = a + ab * (dot_prod / ab_length2);

            double dist2 = vsize2_with_unscale(p - x);
            if (dist2 < bestDist2)
            {
                bestDist2 = dist2;
                if (distance == 0)
                {
                    ret = x;
                }
                else
                {
                    Point inward_dir = turn90_ccw(normal(ab, scale_(distance))); // inward or outward depending on the sign of [distance]
                    ret = x + inward_dir;
                    is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) >= 0;
                }
            }
        }

        p0 = p1;
        p1 = p2;
    }

    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        // BBS. Remove this condition.
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
    }
    else if (bestDist2 < max_move_distance * max_move_distance)
    {
        from = ret;
    }
    return 0;
}

/*
 * Implementation assumes moving inside, but moving outside should just as well be possible.
 */
static bool move_inside_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    Point from0 = from;
    Point ret = from;
    std::vector<Point> valid_pts;
    double bestDist2 = std::numeric_limits<double>::max();
    unsigned int bestPoly = NO_INDEX;
    bool is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
    Point inward_dir;
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        const ExPolygon poly = polygons[poly_idx];
        if (poly.contour.size() < 2)
            continue;
        Point p0 = poly.contour[poly.contour.size()-2];
        Point p1 = poly.contour.points.back();
        // because we compare with vsize2_with_unscale here (no division by zero), we also need to compare by vsize2_with_unscale inside the loop
        // to avoid integer rounding edge cases
        bool projected_p_beyond_prev_segment = dot_with_unscale(p1 - p0, from - p0) >= vsize2_with_unscale(p1 - p0);
        for(const Point& p2 : poly.contour.points)
        {
            // X = A + Normal(B-A) * (((B-A) dot_with_unscale (P-A)) / VSize(B-A));
            //   = A +       (B-A) *  ((B-A) dot_with_unscale (P-A)) / VSize2(B-A);
            // X = P projected on AB
            Point a = p1;
            Point b = p2;
            Point p = from;
            Point ab = b - a;
            Point ap = p - a;
            double ab_length2 = vsize2_with_unscale(ab);
            if(ab_length2 <= 0) //A = B, i.e. the input polygon had two adjacent points on top of each other.
            {
                p1 = p2; //Skip only one of the points.
                continue;
            }
            double dot_prod = dot_with_unscale(ab, ap);
            if (dot_prod <= 0) // x is projected to before ab
            {
                if (projected_p_beyond_prev_segment)
                { //  case which looks like:   > .
                    projected_p_beyond_prev_segment = false;
                    Point& x = p1;

                    double dist2 = vsize2_with_unscale(x - p);
                    if (dist2 < bestDist2)
                    {
                        bestDist2 = dist2;
                        bestPoly = poly_idx;
                        if (distance == 0) { ret = x; }
                        else
                        {
                            inward_dir = turn90_ccw(normal(ab, 10.0) + normal(p1 - p0, 10.0)); // inward direction irrespective of sign of [distance]
                            // MM2INT(10.0) to retain precision for the eventual normalization
                            ret = x + normal(inward_dir, scale_(distance));
                            is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) * distance >= 0;
                            if (is_already_on_correct_side_of_boundary && dist2 < distance * distance)
                                valid_pts.push_back(ret-from0);
                        }
                    }
                }
                else
                {
                    projected_p_beyond_prev_segment = false;
                    p0 = p1;
                    p1 = p2;
                    continue;
                }
            }
            else if (dot_prod >= ab_length2) // x is projected to beyond ab
            {
                projected_p_beyond_prev_segment = true;
                p0 = p1;
                p1 = p2;
                continue;
            }
            else
            { // x is projected to a point properly on the line segment (not onto a vertex). The case which looks like | .
                projected_p_beyond_prev_segment = false;
                Point x = a + ab * (dot_prod / ab_length2);

                double dist2 = vsize2_with_unscale(p - x);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestPoly = poly_idx;
                    if (distance == 0) { ret = x; }
                    else
                    {
                        inward_dir = turn90_ccw(normal(ab, scale_(distance))); // inward or outward depending on the sign of [distance]
                        ret = x + inward_dir;
                        is_already_on_correct_side_of_boundary = dot_with_unscale(inward_dir, p - x) >= 0;
                        if (is_already_on_correct_side_of_boundary && dist2<distance*distance)
                            valid_pts.push_back(ret-from0);
                    }
                }
            }
            p0 = p1;
            p1 = p2;
        }
    }

    //if (valid_pts.size() > 1) {
    //    std::sort(valid_pts.begin(), valid_pts.end());
    //    Point v_combine = valid_pts[0] + valid_pts[1];
    //    if(vsize2_with_unscale(v_combine)<distance*distance)
    //        v_combine = normal(v_combine, scale_(distance));
    //    ret = v_combine + from0;
    //}

    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        return true;
    }
    else if (bestDist2 < max_move_distance * max_move_distance)
    {
        from = ret;
        return true;
    }
    return false;
}

static Point find_closest_ex(Point from, const ExPolygons& polygons)
{
    Point closest_pt;
    double min_dist2 = std::numeric_limits<double>::max();

    for (const ExPolygon &poly : polygons) {
        for (int i = 0; i < poly.num_contours(); i++) {
            const Point* candidate = poly.contour_or_hole(i).closest_point(from);
            double dist2 = vsize2_with_unscale(*candidate - from);
            if (dist2 < min_dist2) {
                closest_pt = *candidate;
                min_dist2 = dist2;
            }
        }
    }

    return closest_pt;
}

static bool move_outside_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    return move_inside_expolys(polygons, from, -distance, -max_move_distance);
}

static bool is_inside_ex(const ExPolygon &polygon, const Point &pt)
{
    if (!get_extents(polygon).contains(pt))
        return false;

    return polygon.contains(pt);
}

static bool is_inside_ex(const ExPolygons &polygons, const Point &pt)
{
    for (const ExPolygon &poly : polygons) {
        if (is_inside_ex(poly, pt))
            return true;
    }

    return false;
}

static bool move_out_expolys(const ExPolygons& polygons, Point& from, double distance, double max_move_distance)
{
    Point from0 = from;
    ExPolygons polys_dilated = union_ex(offset_ex(polygons, scale_(distance)));
    Point pt = projection_onto(polys_dilated, from);// find_closest_ex(from, polys_dilated);
    Point outward_dir = pt - from;
    Point pt_max = from + normal(outward_dir, scale_(max_move_distance));
    double dist2 = vsize2_with_unscale(outward_dir);
    if (dist2 > SQ(max_move_distance))
        pt = pt_max;
    // case 5: already outside and far enough, no need to move
    if (!is_inside_ex(polys_dilated, from))
        return true;
    else if (!is_inside_ex(polygons, from)) {
        // case 4: already outside but not far enough
        from = pt;
        return true;
    }
    else {
        bool pt_max_in_poly = is_inside_ex(polygons, pt_max);
        if (!pt_max_in_poly) {
            from = pt_max;
            return true;
        }
        else {
            return false;
        }
    }
}

static Point bounding_box_middle(const BoundingBox &bbox)
{
    return (bbox.max + bbox.min) / 2;
}

TreeSupport::TreeSupport(PrintObject& object, const SlicingParameters &slicing_params)
    : m_object(&object), m_slicing_params(slicing_params), m_object_config(&object.config())
{
    m_raft_layers = slicing_params.base_raft_layers + slicing_params.interface_raft_layers;
    support_type = m_object_config->support_type;
    support_style = m_object_config->support_style;
    if (support_style == smsDefault)
        support_style = smsTreeHybrid;
    SupportMaterialPattern support_pattern  = m_object_config->support_base_pattern;
    if (support_style == smsTreeHybrid && support_pattern == smpDefault) support_pattern = smpRectilinear;
    m_support_params.base_fill_pattern      = 
        support_pattern == smpLightning ? ipLightning :
        support_pattern == smpHoneycomb ? ipHoneycomb :
                                              m_support_params.support_density > 0.95 || m_support_params.with_sheath ? ipRectilinear :
                                                                                                                        ipSupportBase;

    m_support_params.interface_fill_pattern = (m_support_params.interface_density > 0.95 ? ipRectilinear : ipSupportBase);
    if (m_object_config->support_interface_pattern == smipGrid)
        m_support_params.contact_fill_pattern = ipGrid;
    else if (m_object_config->support_interface_pattern == smipRectilinearInterlaced)
        m_support_params.contact_fill_pattern = ipRectilinear;
    else
        m_support_params.contact_fill_pattern = (m_object_config->support_interface_pattern == smipAuto && m_slicing_params.soluble_interface) ||
        m_object_config->support_interface_pattern == smipConcentric ?
        ipConcentric :
        (m_support_params.interface_density > 0.95 ? ipRectilinear : ipSupportBase);

    const auto nozzle_diameter = object.print()->config().nozzle_diameter.get_at(object.config().support_interface_filament-1);
    const coordf_t extrusion_width = m_object_config->line_width.get_abs_value(nozzle_diameter);
    const coordf_t support_extrusion_width = m_object_config->support_line_width.get_abs_value(nozzle_diameter);

    m_support_params.support_extrusion_width = support_extrusion_width > 0 ? support_extrusion_width : extrusion_width;
    is_slim                                  = is_tree_slim(support_type, support_style);
    is_strong = is_tree(support_type) && support_style == smsTreeStrong;
    MAX_BRANCH_RADIUS                        = 10.0;
    tree_support_branch_diameter_angle       = 5.0;//is_slim ? 10.0 : 5.0;
    // by default tree support needs no infill, unless it's tree hybrid which contains normal nodes.
    with_infill                              = support_pattern != smpNone && support_pattern != smpDefault;
    const PrintConfig& print_config = m_object->print()->config();
    m_machine_border.contour = get_bed_shape_with_excluded_area(print_config);
    Vec3d plate_offset       = m_object->print()->get_plate_origin();
    // align with the centered object in current plate (may not be the 1st plate, so need to add the plate offset)
    m_machine_border.translate(Point(scale_(plate_offset(0)), scale_(plate_offset(1))) - m_object->instances().front().shift);
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    SVG svg("SVG/machine_boarder.svg", m_object->bounding_box());
    if (svg.is_opened()) svg.draw(m_machine_border, "yellow");
#endif
}


#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.
void TreeSupport::detect_overhangs(bool detect_first_sharp_tail_only)
{
    // overhangs are already detected
    if (m_object->support_layer_count() >= m_object->layer_count())
        return;

    // Clear and create Tree Support Layers
    m_object->clear_support_layers();
    m_object->clear_tree_support_preview_cache();
    create_tree_support_layers();

    const PrintObjectConfig& config = m_object->config();
    SupportType stype = support_type;
    const coordf_t radius_sample_resolution = g_config_tree_support_collision_resolution;
    const double nozzle_diameter = m_object->print()->config().nozzle_diameter.get_at(0);
    const coordf_t extrusion_width = config.get_abs_value("line_width", nozzle_diameter);
    const coordf_t extrusion_width_scaled = scale_(extrusion_width);
    const coordf_t max_bridge_length = scale_(config.max_bridge_length.value);
    const bool bridge_no_support = max_bridge_length > 0;
    const bool support_critical_regions_only = config.support_critical_regions_only.value;
    const bool config_remove_small_overhangs = config.support_remove_small_overhang.value;
    const int enforce_support_layers = config.enforce_support_layers.value;
    const double area_thresh_well_supported = SQ(scale_(6));
    const double length_thresh_well_supported = scale_(6);
    static const double sharp_tail_max_support_height = 16.f;
    // a region is considered well supported if the number of layers below it exceeds this threshold
    const int thresh_layers_below = 10 / config.layer_height;
    double obj_height = m_object->size().z();
    // +1 makes the threshold inclusive
    double thresh_angle = config.support_threshold_angle.value > EPSILON ? config.support_threshold_angle.value + 1 : 30;
    thresh_angle = std::min(thresh_angle, 89.); // should be smaller than 90
    const double threshold_rad = Geometry::deg2rad(thresh_angle);

    // for small overhang removal
    struct OverhangCluster {
        std::map<int, const ExPolygon*> layer_overhangs;
        ExPolygons merged_poly;
        BoundingBox merged_bbox;
        int min_layer = 1e7;
        int max_layer = 0;
        coordf_t offset = 0;
        bool is_cantilever = false;
        bool is_sharp_tail = false;
        bool is_small_overhang = false;
        OverhangCluster(const ExPolygon* expoly, int layer_nr) {
            push_back(expoly, layer_nr);
        }
        void push_back(const ExPolygon* expoly, int layer_nr) {
            layer_overhangs.emplace(layer_nr, expoly);
            auto dilate1 = offset_ex(*expoly, offset);
            if (!dilate1.empty())
                merged_poly = union_ex(merged_poly, dilate1);
            min_layer = std::min(min_layer, layer_nr);
            max_layer = std::max(max_layer, layer_nr);
            merged_bbox.merge(get_extents(dilate1));
        }
        int height() {
            return max_layer - min_layer + 1;
        }
        bool intersects(const ExPolygon& region, int layer_nr, coordf_t offset) {
            if (layer_nr < 1) return false;
            auto it = layer_overhangs.find(layer_nr - 1);
            if (it == layer_overhangs.end()) return false;
            const ExPolygon* overhang = it->second;

            this->offset = offset;
            auto dilate1 = offset_ex(region, offset);
            BoundingBox bbox = get_extents(dilate1);
            if (!merged_bbox.overlap(bbox))
                return false;
            return overlaps({ *overhang }, dilate1);
        }
        // it's basically the combination of push_back and intersects, but saves an offset_ex
        bool push_back_if_intersects(const ExPolygon& region, int layer_nr, coordf_t offset) {
            bool is_intersect = false;
            ExPolygons dilate1;
            BoundingBox bbox;
            do {
                if (layer_nr < 1) break;
                auto it = layer_overhangs.find(layer_nr - 1);
                if (it == layer_overhangs.end()) break;
                const ExPolygon* overhang = it->second;

                this->offset = offset;
                dilate1 = offset_ex(region, offset);
                if (dilate1.empty()) break;
                bbox = get_extents(dilate1);
                if (!merged_bbox.overlap(bbox))
                    break;
                is_intersect = overlaps({ *overhang }, dilate1);
            } while (0);
            if (is_intersect) {
                layer_overhangs.emplace(layer_nr, &region);
                merged_poly = union_ex(merged_poly, dilate1);
                min_layer = std::min(min_layer, layer_nr);
                max_layer = std::max(max_layer, layer_nr);
                merged_bbox.merge(bbox);
            }
            return is_intersect;
        }
    };
    std::vector<OverhangCluster> overhangClusters;

    auto find_and_insert_cluster = [](auto &regionClusters, const ExPolygon &region, int layer_nr, coordf_t offset) {
        OverhangCluster *cluster = nullptr;
        for (int i = 0; i < regionClusters.size(); i++) {
            auto cluster_i = &regionClusters[i];
            if (cluster_i->push_back_if_intersects(region, layer_nr, offset)) {
                cluster = cluster_i;
                break;
            }
        }
        if (!cluster) {
            cluster = &regionClusters.emplace_back(&region, layer_nr);
        }
        return cluster;
    };

    if (!is_tree(stype)) return;   

    max_cantilever_dist = 0;

    // main part of overhang detection can be parallel
    tbb::parallel_for(tbb::blocked_range<size_t>(0, m_object->layer_count()),
        [&](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
                if (m_object->print()->canceled())
                    break;

                if (!is_auto(stype) && layer_nr > enforce_support_layers)
                    continue;

                Layer* layer = m_object->get_layer(layer_nr);

                if (layer->lower_layer == nullptr) {
                    for (auto& slice : layer->lslices) {
                        auto bbox_size = get_extents(slice).size();
                        if (!((bbox_size.x() > length_thresh_well_supported && bbox_size.y() > length_thresh_well_supported))
                            && g_config_support_sharp_tails) {
                            layer->sharp_tails.push_back(slice);
                            layer->sharp_tails_height.insert({ &slice, layer->height });
                        }
                    }
                    continue;
                }

                Layer* lower_layer = layer->lower_layer;
                coordf_t lower_layer_offset = layer_nr < enforce_support_layers ? -0.15 * extrusion_width : (float)lower_layer->height / tan(threshold_rad);
                coordf_t support_offset_scaled = scale_(lower_layer_offset);
                // Filter out areas whose diameter that is smaller than extrusion_width. Do not use offset2() for this purpose!
                ExPolygons lower_polys;
                for (const ExPolygon& expoly : lower_layer->lslices) {
                    if (!offset_ex(expoly, -extrusion_width_scaled / 2).empty()) {
                        lower_polys.emplace_back(expoly);
                    }
                }
                ExPolygons curr_polys;
                std::vector<const ExPolygon*> curr_poly_ptrs;
                for (const ExPolygon& expoly : layer->lslices) {
                    if (!offset_ex(expoly, -extrusion_width_scaled / 2).empty()) {
                        curr_polys.emplace_back(expoly);
                        curr_poly_ptrs.emplace_back(&expoly);
                    }
                }

                // normal overhang
                ExPolygons lower_layer_offseted = offset_ex(lower_polys, support_offset_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS);
                ExPolygons overhang_areas = diff_ex(curr_polys, lower_layer_offseted);

                overhang_areas.erase(std::remove_if(overhang_areas.begin(), overhang_areas.end(),
                    [extrusion_width_scaled](ExPolygon& area) { return offset_ex(area, -0.1 * extrusion_width_scaled).empty(); }),
                    overhang_areas.end());


                if (is_auto(stype) && g_config_support_sharp_tails)
                {
                    // BBS detect sharp tail
                    for (const ExPolygon* expoly : curr_poly_ptrs) {
                        bool  is_sharp_tail = false;
                        // 1. nothing below
                        // this is a sharp tail region if it's small but non-ignorable
                        if (!overlaps(offset_ex(*expoly, 0.5 * extrusion_width_scaled), lower_polys)) {
                            is_sharp_tail = expoly->area() < area_thresh_well_supported && !offset_ex(*expoly, -0.1 * extrusion_width_scaled).empty();
                        }

                        if (is_sharp_tail) {
                            ExPolygons overhang = diff_ex({ *expoly }, lower_polys);
                            layer->sharp_tails.push_back(*expoly);
                            layer->sharp_tails_height.insert({ expoly, layer->height });
                            append(overhang_areas, overhang);

                            if (!overhang.empty()) {
                                has_sharp_tails = true;
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                                SVG svg(format("SVG/sharp_tail_orig_%.02f.svg", layer->print_z), m_object->bounding_box());
                                if (svg.is_opened()) svg.draw(overhang, "red");
#endif
                            }
                        }                        
                    }
                }

                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                for (ExPolygon& poly : overhang_areas) {
                    if (offset_ex(poly, -0.1 * extrusion_width_scaled).empty()) continue;
                    ts_layer->overhang_areas.emplace_back(poly);

                    // check cantilever
                    {
                        auto cluster_boundary_ex = intersection_ex(poly, offset_ex(lower_layer->lslices, scale_(0.5)));
                        Polygons cluster_boundary = to_polygons(cluster_boundary_ex);
                        if (cluster_boundary.empty()) continue;
                        double dist_max = 0;
                        for (auto& pt : poly.contour.points) {
                            double dist_pt = std::numeric_limits<double>::max();
                            for (auto& ply : cluster_boundary) {
                                double d = ply.distance_to(pt);
                                dist_pt = std::min(dist_pt, d);
                            }
                            dist_max = std::max(dist_max, dist_pt);
                        }
                        if (dist_max > scale_(3)) {  // is cantilever if the farmost point is larger than 3mm away from base                            
                            max_cantilever_dist = std::max(max_cantilever_dist, dist_max);
                            layer->cantilevers.emplace_back(poly);
                            BOOST_LOG_TRIVIAL(debug) << "found a cantilever cluster. layer_nr=" << layer_nr << dist_max;
                            has_cantilever = true;
                        }
                    }
                }
            }
        }
    ); // end tbb::parallel_for

    BOOST_LOG_TRIVIAL(info) << "max_cantilever_dist=" << max_cantilever_dist;

    // check if the sharp tails should be extended higher
    if (is_auto(stype) && g_config_support_sharp_tails && !detect_first_sharp_tail_only) {
        for (size_t layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
            if (m_object->print()->canceled())
                break;

            Layer* layer = m_object->get_layer(layer_nr);
            SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
            Layer* lower_layer = layer->lower_layer;
            if (!lower_layer)
                continue;

            // BBS detect sharp tail
            const ExPolygons& lower_layer_sharptails = lower_layer->sharp_tails;
            const auto& lower_layer_sharptails_height = lower_layer->sharp_tails_height;
            for (ExPolygon& expoly : layer->lslices) {
                bool  is_sharp_tail = false;
                float accum_height = layer->height;
                do {
                    // 2. something below
                    // check whether this is above a sharp tail region.

                    // 2.1 If no sharp tail below, this is considered as common region.
                    ExPolygons supported_by_lower = intersection_ex({ expoly }, lower_layer_sharptails);
                    if (supported_by_lower.empty()) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.2 If sharp tail below, check whether it support this region enough.
#if 0
                    // judge by area isn't reliable, failure cases include 45 degree rotated cube
                    float       supported_area = area(supported_by_lower);
                    if (supported_area > area_thresh_well_supported) {
                        is_sharp_tail = false;
                        break;
                    }
#endif
                    BoundingBox bbox = get_extents(supported_by_lower);
                    if (bbox.size().x() > length_thresh_well_supported && bbox.size().y() > length_thresh_well_supported) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.3 check whether sharp tail exceed the max height
                    for (const auto& lower_sharp_tail_height : lower_layer_sharptails_height) {
                        if (lower_sharp_tail_height.first->overlaps(expoly)) {
                            accum_height += lower_sharp_tail_height.second;
                            break;
                        }
                    }
                    if (accum_height > sharp_tail_max_support_height) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.4 if the area grows fast than threshold, it get connected to other part or
                    // it has a sharp slop and will be auto supported.
                    ExPolygons new_overhang_expolys = diff_ex({ expoly }, lower_layer_sharptails);
                    if ((get_extents(new_overhang_expolys).size() - get_extents(lower_layer_sharptails).size()).both_comp(Point(scale_(5), scale_(5)), ">") || !offset_ex(new_overhang_expolys, -5.0 * extrusion_width_scaled).empty()) {
                        is_sharp_tail = false;
                        break;
                    }

                    // 2.5 mark the expoly as sharptail
                    is_sharp_tail = true;
                } while (0);

                if (is_sharp_tail) {
                    ExPolygons overhang = diff_ex({ expoly }, lower_layer->lslices);
                    layer->sharp_tails.push_back(expoly);
                    layer->sharp_tails_height.insert({ &expoly, accum_height });
                    append(ts_layer->overhang_areas, overhang);

                    if (!overhang.empty())
                        has_sharp_tails = true;
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                    SVG svg(format("SVG/sharp_tail_%.02f.svg",layer->print_z), m_object->bounding_box());
                    if (svg.is_opened()) svg.draw(overhang, "red");
#endif
                }

            }          
        }
    }
    
    // group overhang clusters
    for (size_t layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
        if (m_object->print()->canceled())
            break;
        SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        Layer* layer = m_object->get_layer(layer_nr);
        for (auto& overhang : ts_layer->overhang_areas) {
            OverhangCluster* cluster = find_and_insert_cluster(overhangClusters, overhang, layer_nr, extrusion_width_scaled);
            if (overlaps({ overhang },layer->cantilevers))
                cluster->is_cantilever = true;
        }
    }

    auto enforcers = m_object->slice_support_enforcers();
    auto blockers  = m_object->slice_support_blockers();
    m_object->project_and_append_custom_facets(false, EnforcerBlockerType::ENFORCER, enforcers);
    m_object->project_and_append_custom_facets(false, EnforcerBlockerType::BLOCKER, blockers);
    if (is_auto(stype) && config_remove_small_overhangs) {
        if (blockers.size() < m_object->layer_count())
            blockers.resize(m_object->layer_count());
        for (auto& cluster : overhangClusters) {
            // 3. check whether the small overhang is sharp tail
            cluster.is_sharp_tail = false;
            for (size_t layer_id = cluster.min_layer; layer_id <= cluster.max_layer; layer_id++) {
                Layer* layer = m_object->get_layer(layer_id);
                if (overlaps(layer->sharp_tails, cluster.merged_poly)) {
                    cluster.is_sharp_tail = true;
                    break;
                }
            }

            if (!cluster.is_sharp_tail && !cluster.is_cantilever) {
                // 2. check overhang cluster size is smaller than 3.0 * fw_scaled
                auto erode1 = offset_ex(cluster.merged_poly, -1 * extrusion_width_scaled);
                Point bbox_sz = get_extents(erode1).size();
                if (bbox_sz.x() < 2 * extrusion_width_scaled || bbox_sz.y() < 2 * extrusion_width_scaled) {
                    cluster.is_small_overhang = true;
                }
            }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
            const Layer* layer1 = m_object->get_layer(cluster.min_layer);
            BoundingBox bbox = cluster.merged_bbox;
            bbox.merge(get_extents(layer1->lslices));
            SVG svg(format("SVG/overhangCluster_%s-%s_%s-%s_tail=%s_cantilever=%s_small=%s.svg",
                cluster.min_layer, cluster.max_layer, layer1->print_z, m_object->get_layer(cluster.max_layer)->print_z,
                cluster.is_sharp_tail, cluster.is_cantilever, cluster.is_small_overhang), bbox);
            if (svg.is_opened()) {
                svg.draw(layer1->lslices, "red");
                svg.draw(cluster.merged_poly, "blue");
                svg.draw_text(bbox.min + Point(scale_(0), scale_(2)), "lslices", "red", 2);
                svg.draw_text(bbox.min + Point(scale_(0), scale_(2)), "overhang", "blue", 2);
            }
#endif

            if (!cluster.is_small_overhang)
                continue;

            for (auto it = cluster.layer_overhangs.begin(); it != cluster.layer_overhangs.end(); it++) {
                int  layer_nr   = it->first;
                auto p_overhang = it->second;
                blockers[layer_nr].push_back(p_overhang->contour);
            }
        }
    }

    has_overhangs = false;
    for (int layer_nr = 0; layer_nr < m_object->layer_count(); layer_nr++) {
        if (m_object->print()->canceled())
            break;

        SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        auto layer = m_object->get_layer(layer_nr);
        auto lower_layer = layer->lower_layer;
        if (support_critical_regions_only && is_auto(stype)) {
            ts_layer->overhang_areas.clear();
            if (lower_layer == nullptr)
                ts_layer->overhang_areas = layer->sharp_tails;
            else
                ts_layer->overhang_areas = diff_ex(layer->sharp_tails, lower_layer->lslices);

            append(ts_layer->overhang_areas, layer->cantilevers);
        }

        if (layer_nr < blockers.size()) {
            Polygons& blocker = blockers[layer_nr];
            // Arthur: union_ is a must because after mirroring, the blocker polygons are in left-hand coordinates, ie clockwise, 
            // which are not valid polygons, and will be removed by offset_ex. union_ can make these polygons right.
            ts_layer->overhang_areas = diff_ex(ts_layer->overhang_areas, offset_ex(union_(blocker), scale_(radius_sample_resolution)));
        }

        if (max_bridge_length > 0 && ts_layer->overhang_areas.size() > 0 && lower_layer) {
            // do not break bridge for normal part in TreeHybrid
            bool break_bridge = !(support_style == smsTreeHybrid && area(ts_layer->overhang_areas) > m_support_params.thresh_big_overhang);
            m_object->remove_bridges_from_contacts(lower_layer, layer, extrusion_width_scaled, &ts_layer->overhang_areas, max_bridge_length, break_bridge);
        }

        for (auto &area : ts_layer->overhang_areas) {
            ts_layer->overhang_types.emplace(&area, SupportLayer::Detected);
        }
        // enforcers now follow same logic as normal support. See STUDIO-3692
        if (layer_nr < enforcers.size() && lower_layer) {
            float no_interface_offset = std::accumulate(layer->regions().begin(), layer->regions().end(), FLT_MAX,
                [](float acc, const LayerRegion* layerm) { return std::min(acc, float(layerm->flow(frExternalPerimeter).scaled_width())); });
            Polygons  lower_layer_polygons = (layer_nr == 0) ? Polygons() : to_polygons(lower_layer->lslices);
            Polygons& enforcer = enforcers[layer_nr];
            if (!enforcer.empty()) {
                ExPolygons enforcer_polygons = diff_ex(intersection_ex(layer->lslices, enforcer),
                    // Inflate just a tiny bit to avoid intersection of the overhang areas with the object.
                    expand(lower_layer_polygons, 0.05f * no_interface_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                append(ts_layer->overhang_areas, enforcer_polygons);
                ts_layer->overhang_types.emplace(&ts_layer->overhang_areas.back(), SupportLayer::Enforced);
            }
        }

        if (!ts_layer->overhang_areas.empty()) has_overhangs = true;
        if (!layer->cantilevers.empty()) has_cantilever = true;
    }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    for (const SupportLayer* layer : m_object->support_layers()) {
        if (layer->overhang_areas.empty() && (blockers.size()<=layer->id() || blockers[layer->id()].empty()))
            continue;

        SVG svg(format("SVG/overhang_areas_%s.svg", layer->print_z), m_object->bounding_box());
        if (svg.is_opened()) {
            svg.draw_outline(m_object->get_layer(layer->id())->lslices, "yellow");
            svg.draw(layer->overhang_areas, "orange");
            if (blockers.size() > layer->id())
                svg.draw(blockers[layer->id()], "red");
        }
        if (enforcers.size() > layer->id()) {
            SVG svg(format("SVG/enforcer_%s.svg", layer->print_z), m_object->bounding_box());
            if (svg.is_opened()) {
                svg.draw_outline(m_object->get_layer(layer->id())->lslices, "yellow");
                svg.draw(enforcers[layer->id()], "red");
            }
        }
        if (blockers.size() > layer->id()) {
            SVG svg(format("SVG/blocker_%s.svg", layer->print_z), m_object->bounding_box());
            if (svg.is_opened()) {
                svg.draw_outline(m_object->get_layer(layer->id())->lslices, "yellow");
                svg.draw(blockers[layer->id()], "red");
            }
        }
    }
#endif
}

void TreeSupport::create_tree_support_layers()
{
    int layer_id = 0;
    coordf_t raft_print_z = 0.f;
    coordf_t raft_slice_z = 0.f;
    for (; layer_id < m_slicing_params.base_raft_layers; layer_id++) {
        raft_print_z += m_slicing_params.base_raft_layer_height;
        raft_slice_z = raft_print_z - m_slicing_params.base_raft_layer_height / 2;
        m_object->add_tree_support_layer(layer_id, m_slicing_params.base_raft_layer_height, raft_print_z, raft_slice_z);
    }

    for (; layer_id < m_slicing_params.base_raft_layers + m_slicing_params.interface_raft_layers; layer_id++) {
        raft_print_z += m_slicing_params.interface_raft_layer_height;
        raft_slice_z = raft_print_z - m_slicing_params.interface_raft_layer_height / 2;
        m_object->add_tree_support_layer(layer_id, m_slicing_params.base_raft_layer_height, raft_print_z, raft_slice_z);
    }

    for (Layer *layer : m_object->layers()) {
        SupportLayer* ts_layer = m_object->add_tree_support_layer(layer->id(), layer->height, layer->print_z, layer->slice_z);
        if (ts_layer->id() > m_raft_layers) {
            SupportLayer* lower_layer = m_object->get_support_layer(ts_layer->id() - 1);
            lower_layer->upper_layer = ts_layer;
            ts_layer->lower_layer = lower_layer;
        }
    }
}

static inline BoundingBox fill_expolygon_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygon              &&expolygon,
    Fill                    *filler,
    const FillParams        &fill_params,
    ExtrusionRole            role,
    const Flow              &flow)
{
    Surface surface(stInternal, std::move(expolygon));
    Polylines polylines;
    try {
        polylines = filler->fill_surface(&surface, fill_params);
    } catch (InfillFailedException &) {
    }

    BoundingBox fill_bbox;
    if (!polylines.empty()) {
        fill_bbox = polylines[0].bounding_box();
        for (auto& polyline : polylines)
            fill_bbox.merge(polyline.bounding_box());
    }

    extrusion_entities_append_paths(dst, std::move(polylines), role, flow.mm3_per_mm(), flow.width(), flow.height());

    return fill_bbox;
}

static inline std::vector<BoundingBox> fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr   &dst,
    ExPolygons            &&expolygons,
    Fill                   *filler,
    const FillParams       &fill_params,
    ExtrusionRole           role,
    const Flow             &flow)
{
    std::vector<BoundingBox> fill_boxes;
    for (ExPolygon& expoly : expolygons) {
        auto box = fill_expolygon_generate_paths(dst, std::move(expoly), filler, fill_params, role, flow);
        fill_boxes.emplace_back(box);
    }
    return fill_boxes;
}

static void _make_loops(ExtrusionEntitiesPtr& loops_entities, ExPolygons &support_area, ExtrusionRole role, size_t wall_count, const Flow &flow)
{
    Polygons       loops;
    std::map<ExPolygon *, int> depth_per_expoly;
    std::list<ExPolygon> expoly_list;

    for (ExPolygon &expoly : support_area) {
        expoly_list.emplace_back(std::move(expoly));
        depth_per_expoly.insert({&expoly_list.back(), 0});
    }
    if (expoly_list.empty()) return;

    while (!expoly_list.empty()) {
        polygons_append(loops, to_polygons(expoly_list.front()));

        auto first_iter = expoly_list.begin();
        auto depth_iter = depth_per_expoly.find(&expoly_list.front());
        if (depth_iter->second + 1 < wall_count) {
            //ExPolygons expolys_new = offset_ex(expoly_list.front(), -float(flow.scaled_spacing()), jtSquare);
            // shrink and then dilate to prevent overlapping and overflow
            ExPolygons expolys_new = offset2_ex({expoly_list.front()}, -1.4 * float(flow.scaled_spacing()), .4 * float(flow.scaled_spacing()));

            for (ExPolygon &expoly : expolys_new) {
                auto new_iter = expoly_list.insert(expoly_list.begin(), expoly);
                depth_per_expoly.insert({&*new_iter, depth_iter->second + 1});
            }
        }
        depth_per_expoly.erase(depth_iter);
        expoly_list.erase(first_iter);
    }

    // draw connected loops
    if (/*wall_count > 1 && wall_count<5*/0) {
        // TODO this method may drop some contours
        wall_count = std::min(wall_count, loops.size());
        Polylines polylines;
        polylines.push_back(Polyline());
        Polyline& polyline = polylines.back();
        Point     end_pt;
        Point     end_dir;
        for (int wall_idx = 0; wall_idx < wall_count; wall_idx++) {
            Polygon &loop = loops[wall_idx];
            if (loop.size()<3) continue;
            // break the closed loop if this is not the last loop, so the next loop can attach to the end_pt
            //if (wall_idx != wall_count - 1 && loop.first_point() == loop.last_point())
            //    loop.points.pop_back();

            if (wall_idx == 0) {
                polyline.append(loop.points);
            } else {
                double d = loop.distance_to(end_pt);
                if (d < scale_(2)) { // if current loop is close to the previous one
                    polyline.append(end_pt);
                    ExtrusionPath expath;
                    expath.polyline.append(loop.points);
                    ExtrusionLoop extru_loop(expath);
                    extru_loop.split_at(end_pt, false);
                    polyline.append(extru_loop.as_polyline());
                }else{// create a new polylie if they are far away
                    polylines.push_back(Polyline());
                    polyline = polylines.back();
                    polyline.append(loop.points);
                }
            }
            end_pt = polyline.points.back();
            end_dir = end_pt - polyline.points[polyline.points.size() - 2];
            Point perpendicular_dir = turn90_ccw(end_dir);
            end_pt                  = end_pt + normal(perpendicular_dir, flow.scaled_spacing());
        }

        extrusion_entities_append_paths(loops_entities, polylines, role, float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
    } else {
        extrusion_entities_append_loops(loops_entities, std::move(loops), role, float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
    }
    }

static void make_perimeter_and_inner_brim(ExtrusionEntitiesPtr &dst, const ExPolygon &support_area, size_t wall_count, const Flow &flow, ExtrusionRole role)
{
    Polygons   loops;
    ExPolygons support_area_new = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);
    _make_loops(dst, support_area_new, role, wall_count, flow);
}

static void make_perimeter_and_infill(ExtrusionEntitiesPtr& dst, const Print& print, const ExPolygon& support_area, size_t wall_count, const Flow& flow, ExtrusionRole role, Fill* filler_support, double support_density, bool infill_first=true)
{
    Polygons   loops;
    ExPolygons support_area_new = offset_ex(support_area, -0.5f * float(flow.scaled_spacing()), jtSquare);

    // draw infill
    FillParams fill_params;
    fill_params.density = support_density;
    fill_params.dont_adjust = true;
    ExPolygons to_infill = support_area_new;
    std::vector<BoundingBox> fill_boxes = fill_expolygons_generate_paths(dst, std::move(to_infill), filler_support, fill_params, role, flow);

    // allow wall_count to be zero, which means only draw infill
    if (wall_count == 0) {
        for (auto fill_bbox : fill_boxes)
        {
            // extend bounding box on x-axis
            if (cos(filler_support->angle)>=sin(filler_support->angle)) {
                fill_bbox.min[0] -= scale_(10);
                fill_bbox.max[0] += scale_(10);
            }
            else {
                fill_bbox.min[1] -= scale_(10);
                fill_bbox.max[1] += scale_(10);
            }
            support_area_new = diff_ex(support_area_new, offset_ex(to_expolygons({ fill_bbox.polygon() }), 0.5*flow.scaled_width()));
        }
        // filter out small areas
        for (auto it = support_area_new.begin(); it != support_area_new.end(); ) {
            if (offset_ex(*it, -flow.scaled_width()).empty())
                it = support_area_new.erase(it);
            else
                it++;
        }
    }

    { // draw loops
        ExtrusionEntitiesPtr loops_entities;
        _make_loops(loops_entities, support_area_new, role, wall_count, flow);

        if (infill_first)
            dst.insert(dst.end(), loops_entities.begin(), loops_entities.end());
        else { // loops first            
            loops_entities.insert(loops_entities.end(), dst.begin(), dst.end());
            dst = std::move(loops_entities);
        }
    }
    if (infill_first) {
        // sort regions to reduce travel
        Points ordering_points;
        for (const auto& area : dst)
            ordering_points.push_back(area->first_point());
        std::vector<Points::size_type> order = chain_points(ordering_points);
        ExtrusionEntitiesPtr new_dst;
        new_dst.reserve(ordering_points.size());
        for (size_t i : order)
            new_dst.emplace_back(dst[i]);
        dst = new_dst;
    }
}

void TreeSupport::generate_toolpaths()
{
    const PrintConfig &print_config = m_object->print()->config();
    const PrintObjectConfig &object_config = m_object->config();
    coordf_t support_extrusion_width = m_support_params.support_extrusion_width;
    coordf_t nozzle_diameter = print_config.nozzle_diameter.get_at(object_config.support_filament - 1);
    coordf_t layer_height = object_config.layer_height.value;
    const size_t wall_count = object_config.tree_support_wall_count.value;

    // Check if set to zero, use default if so.
    if (support_extrusion_width <= 0.0)
        support_extrusion_width = Flow::auto_extrusion_width(FlowRole::frSupportMaterial, (float)nozzle_diameter);

    // coconut: use same intensity settings as SupportMaterial.cpp
    auto m_support_material_interface_flow = support_material_interface_flow(m_object, float(m_slicing_params.layer_height));
    coordf_t interface_spacing = object_config.support_interface_spacing.value + m_support_material_interface_flow.spacing();
    coordf_t bottom_interface_spacing = object_config.support_bottom_interface_spacing.value + m_support_material_interface_flow.spacing();
    coordf_t interface_density = std::min(1., m_support_material_interface_flow.spacing() / interface_spacing);
    coordf_t bottom_interface_density = std::min(1., m_support_material_interface_flow.spacing() / bottom_interface_spacing);

    const coordf_t branch_radius = object_config.tree_support_branch_diameter.value / 2;
    const coordf_t branch_radius_scaled = scale_(branch_radius);

    if (m_object->support_layers().empty())
        return;

    // calculate fill areas for raft layers
    ExPolygons raft_areas;
    if (m_object->layer_count() > 0) {
        const Layer *layer = m_object->layers().front();
        for (const ExPolygon &expoly : layer->lslices) {
            raft_areas.push_back(expoly);
        }
    }

    if (m_object->support_layer_count() > m_raft_layers) {
        const SupportLayer *ts_layer = m_object->get_support_layer(m_raft_layers);
        for (const ExPolygon& expoly : ts_layer->floor_areas)
            raft_areas.push_back(expoly);
        for (const ExPolygon& expoly : ts_layer->roof_areas)
            raft_areas.push_back(expoly);
        for (const ExPolygon& expoly : ts_layer->base_areas)
            raft_areas.push_back(expoly);
    }

    raft_areas = std::move(offset_ex(raft_areas, scale_(object_config.raft_first_layer_expansion)));

    // generate raft tool path
    if (m_raft_layers > 0)
    {
        ExtrusionRole raft_contour_er = m_slicing_params.base_raft_layers > 0 ? erSupportMaterial : erSupportMaterialInterface;
        SupportLayer *ts_layer = m_object->support_layers().front();
        Flow flow = m_object->print()->brim_flow();

        Polygons loops;
        for (const ExPolygon& expoly : raft_areas) {
            loops.push_back(expoly.contour);
            loops.insert(loops.end(), expoly.holes.begin(), expoly.holes.end());
        }
        extrusion_entities_append_loops(ts_layer->support_fills.entities, std::move(loops), raft_contour_er,
            float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));
        raft_areas = offset_ex(raft_areas, -flow.scaled_spacing() / 2.);
    }

    for (size_t layer_nr = 0; layer_nr < m_slicing_params.base_raft_layers; layer_nr++) {
        SupportLayer *ts_layer = m_object->get_support_layer(layer_nr);
        coordf_t expand_offset = (layer_nr == 0 ? 0. : -1.);

        Flow support_flow = layer_nr == 0 ? m_object->print()->brim_flow() : Flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_interface = Fill::new_from_type(ipRectilinear);
        filler_interface->angle = layer_nr == 0 ? 90 : 0;
        filler_interface->spacing = support_extrusion_width;

        FillParams fill_params;
        fill_params.density     = object_config.raft_first_layer_density * 0.01;
        fill_params.dont_adjust = true;

        fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(offset_ex(raft_areas, scale_(expand_offset))),
            filler_interface, fill_params, erSupportMaterial, support_flow);
    }

    for (size_t layer_nr = m_slicing_params.base_raft_layers;
         layer_nr < m_slicing_params.base_raft_layers + m_slicing_params.interface_raft_layers;
         layer_nr++)
    {
        SupportLayer *ts_layer = m_object->get_support_layer(layer_nr);
        coordf_t expand_offset = (layer_nr == 0 ? 0. : -1.);

        Flow support_flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
        Fill* filler_interface = Fill::new_from_type(ipRectilinear);
        filler_interface->angle = 0;
        filler_interface->spacing = support_extrusion_width;

        FillParams fill_params;
        fill_params.density = interface_density;
        fill_params.dont_adjust = true;

        fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(offset_ex(raft_areas, scale_(expand_offset))),
            filler_interface, fill_params, erSupportMaterialInterface, support_flow);
    }

    BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));

    std::shared_ptr<Fill> filler_interface = std::shared_ptr<Fill>(Fill::new_from_type(m_support_params.contact_fill_pattern));
    std::shared_ptr<Fill> filler_Roof1stLayer = std::shared_ptr<Fill>(Fill::new_from_type(ipRectilinear));
    filler_interface->set_bounding_box(bbox_object);
    filler_Roof1stLayer->set_bounding_box(bbox_object);
    filler_interface->angle = Geometry::deg2rad(object_config.support_angle.value + 90.);
    filler_Roof1stLayer->angle = Geometry::deg2rad(object_config.support_angle.value + 90.);

    // generate tree support tool paths
    tbb::parallel_for(
        tbb::blocked_range<size_t>(m_raft_layers, m_object->support_layer_count()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_id = range.begin(); layer_id < range.end(); layer_id++) {
                if (m_object->print()->canceled())
                    break;

                m_object->print()->set_status(70, (boost::format(_L("Support: generate toolpath at layer %d")) % layer_id).str());

                SupportLayer* ts_layer = m_object->get_support_layer(layer_id);
                Flow support_flow(support_extrusion_width, ts_layer->height, nozzle_diameter);
                coordf_t support_spacing         = object_config.support_base_pattern_spacing.value + support_flow.spacing();
                coordf_t support_density         = std::min(1., support_flow.spacing() / support_spacing);
                ts_layer->support_fills.no_sort = false;

                for (auto& area_group : ts_layer->area_groups) {
                    ExPolygon& poly = *area_group.area;
                    ExPolygons polys;
                    FillParams fill_params;
                    if (area_group.type != SupportLayer::BaseType) {
                        // interface
                        if (layer_id == 0) {
                            Flow flow = m_raft_layers == 0 ? m_object->print()->brim_flow() : support_flow;
                            make_perimeter_and_inner_brim(ts_layer->support_fills.entities, poly, wall_count, flow,
                                                          area_group.type == SupportLayer::RoofType ? erSupportMaterialInterface : erSupportMaterial);
                            polys = std::move(offset_ex(poly, -flow.scaled_spacing()));
                        } else if (area_group.type == SupportLayer::Roof1stLayer) {
                            polys = std::move(offset_ex(poly, 0.5*support_flow.scaled_width()));
                        }
                        else {
                            polys.push_back(poly);
                        }
                        fill_params.density = interface_density;
                        fill_params.dont_adjust = true;
                    }
                    if (area_group.type == SupportLayer::Roof1stLayer) {
                        // roof_1st_layer
                        fill_params.density = interface_density;
                        // Note: spacing means the separation between two lines as if they are tightly extruded
                        filler_Roof1stLayer->spacing = m_support_material_interface_flow.spacing();
                        // generate a perimeter first to support interface better
                        ExtrusionEntityCollection* temp_support_fills = new ExtrusionEntityCollection();
                        make_perimeter_and_infill(temp_support_fills->entities, *m_object->print(), poly, 1, m_support_material_interface_flow, erSupportMaterial,
                            filler_Roof1stLayer.get(), interface_density, false);
                        temp_support_fills->no_sort = true; // make sure loops are first
                        if (!temp_support_fills->entities.empty())
                            ts_layer->support_fills.entities.push_back(temp_support_fills);
                        else
                            delete temp_support_fills;
                    } else if (area_group.type == SupportLayer::FloorType) {
                        // floor_areas
                        fill_params.density = bottom_interface_density;
                        filler_interface->spacing = m_support_material_interface_flow.spacing();
                        fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(polys),
                            filler_interface.get(), fill_params, erSupportMaterialInterface, m_support_material_interface_flow);
                    } else if (area_group.type == SupportLayer::RoofType) {
                        // roof_areas
                        fill_params.density       = interface_density;
                        filler_interface->spacing = m_support_material_interface_flow.spacing();
                        if (m_object_config->support_interface_pattern == smipGrid) {
                            filler_interface->angle = Geometry::deg2rad(object_config.support_angle.value);
                            fill_params.dont_sort = true;
                        }
                        if (m_object_config->support_interface_pattern == smipRectilinearInterlaced)
                            filler_interface->layer_id = round(area_group.dist_to_top / ts_layer->height);
                        fill_expolygons_generate_paths(ts_layer->support_fills.entities, std::move(polys), filler_interface.get(), fill_params, erSupportMaterialInterface,
                                                       m_support_material_interface_flow);
                    }
                    else {
                        // base_areas
                        Flow flow               = (layer_id == 0 && m_raft_layers == 0) ? m_object->print()->brim_flow() : support_flow;
                        bool need_infill = with_infill;
                        if(m_object_config->support_base_pattern==smpDefault)
                            need_infill &= area_group.need_infill;
                        if (layer_id>0 && area_group.dist_to_top < 10 && !need_infill && support_style!=smsTreeHybrid) {
                            if (area_group.dist_to_top < 5)  // 1 wall at the top <5mm
                                make_perimeter_and_inner_brim(ts_layer->support_fills.entities, poly, 1, flow, erSupportMaterial);
                            else // at least 2 walls for range [5,10)
                                make_perimeter_and_inner_brim(ts_layer->support_fills.entities, poly, std::max(wall_count, size_t(2)), flow, erSupportMaterial);
                        }
                        else if (layer_id > 0 && need_infill && m_support_params.base_fill_pattern != ipLightning) {
                            std::shared_ptr<Fill> filler_support = std::shared_ptr<Fill>(Fill::new_from_type(m_support_params.base_fill_pattern));
                            filler_support->set_bounding_box(bbox_object);
                            filler_support->spacing = object_config.support_base_pattern_spacing.value * support_density;// constant spacing to align support infill lines
                            filler_support->angle = Geometry::deg2rad(object_config.support_angle.value);

                            // allow infill-only mode if support is thick enough (so min_wall_count is 0);
                            // otherwise must draw 1 wall
                            size_t min_wall_count = offset(poly, -scale_(support_spacing * 1.5)).empty() ? 1 : 0;
                            make_perimeter_and_infill(ts_layer->support_fills.entities, *m_object->print(), poly, std::max(min_wall_count, wall_count), flow,
                                erSupportMaterial, filler_support.get(), support_density);
                        }
                        else {
                            make_perimeter_and_inner_brim(ts_layer->support_fills.entities, poly,
                                layer_id > 0 ? wall_count : std::numeric_limits<size_t>::max(), flow, erSupportMaterial);
                        }
                        
                    }
                }
                if (m_support_params.base_fill_pattern == ipLightning)
                {
                    double print_z = ts_layer->print_z;
                    if (printZ_to_lightninglayer.find(print_z) == printZ_to_lightninglayer.end())
                        continue;
                    //TODO:
                    //1.the second parameter of convertToLines seems to decide how long the lightning should be trimmed from its root, so that the root wont overlap/detach the support contour.
                    // whether current value works correctly remained to be tested
                    //2.related to previous one, that lightning roots need to be trimed more when support has multiple walls
                    //3.function connect_infill() and variable 'params' helps create connection pattern along contours between two lightning roots,
                    // strengthen lightnings while it may make support harder. decide to enable it or not. if yes, proper values for params are remained to be tested
                    auto& lightning_layer = generator->getTreesForLayer(printZ_to_lightninglayer[print_z]);

                    Flow       flow  = (layer_id == 0 && m_raft_layers == 0) ? m_object->print()->brim_flow() :support_flow;
                    ExPolygons areas = offset_ex(ts_layer->base_areas, -flow.scaled_spacing());

                    for (auto& area : areas)
                    {
                        Polylines polylines = lightning_layer.convertToLines(to_polygons(area), 0);
                        for (auto itr = polylines.begin(); itr != polylines.end();)
                        {
                            if (itr->length() < scale_(1.0))
                                itr = polylines.erase(itr);
                            else
                                itr++;
                        }
                        Polylines opt_polylines;
#if 1
                        //this wont create connection patterns along contours
                        append(opt_polylines, chain_polylines(std::move(polylines)));
#else
                        //this will create connection patterns along contours
                        FillParams params;
                        params.anchor_length = float(Fill::infill_anchor * 0.01 * flow.spacing());
                        params.anchor_length_max = Fill::infill_anchor_max;
                        params.anchor_length = std::min(params.anchor_length, params.anchor_length_max);
                        Fill::connect_infill(std::move(polylines), area, opt_polylines, flow.spacing(), params);
#endif
                        extrusion_entities_append_paths(ts_layer->support_fills.entities, opt_polylines, erSupportMaterial,
                            float(flow.mm3_per_mm()), float(flow.width()), float(flow.height()));

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                        std::string name = "./SVG/trees_polyline_" + std::to_string(ts_layer->print_z) /*+ "_" + std::to_string(rand_num)*/ + ".svg";
                        BoundingBox bbox = get_extents(ts_layer->base_areas);
                        SVG svg(name, bbox);
                        if (svg.is_opened()) {
                            svg.draw(ts_layer->base_areas, "blue");
                            svg.draw(generator->Overhangs()[printZ_to_lightninglayer[print_z]], "red");
                            for (auto &line : opt_polylines) svg.draw(line, "yellow");
                        }
#endif
                    }
                }

                // sort extrusions to reduce travel, also make sure walls go before infills
                if(ts_layer->support_fills.no_sort==false)
                    chain_and_reorder_extrusion_entities(ts_layer->support_fills.entities);
            }
        }
    );
}

Polygons TreeSupport::spanning_tree_to_polygon(const std::vector<MinimumSpanningTree>& spanning_trees, Polygons layer_contours, int layer_nr)
{
    Polygons polys;
    auto& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
    for (MinimumSpanningTree mst : spanning_trees) {
        std::vector<Point> points = mst.vertices();
        if (points.size() == 0)
            continue;
        std::map<Point, bool> visited;
        for (int i=0;i<points.size();i++)
            visited.emplace(points[i],false);

        std::unordered_set<Line, LineHash> to_ignore;
        for (int i = 0; i < points.size(); i++) {
            if (visited[points[i]] == true)
                continue;

            Polygon poly;
            bool has_next = true;
            Point pt1 = points[i];
            poly.points.push_back(pt1);
            visited[pt1] = true;

            while (has_next) {
                const std::vector<Point>& neighbours = mst.adjacent_nodes(pt1);
                if (neighbours.empty())
                {
                    break;
                }

                double min_ccw = std::numeric_limits<double>::max();
                Point pt_selected = neighbours[0];
                has_next = false;
                for (Point pt2 : neighbours) {
                    if (to_ignore.find(Line(pt1, pt2)) == to_ignore.end()) {
                        auto iter = mst_line_x_layer_contour_cache.find({ pt1,pt2 });
                        if (iter != mst_line_x_layer_contour_cache.end()) {
                            if (iter->second)
                                continue;
                        }
                        else {
                            Polylines pls;
                            pls.emplace_back(pt1, pt2);
                            Polylines pls_intersect = intersection_pl(pls, layer_contours);
                            mst_line_x_layer_contour_cache.insert({ {pt1, pt2}, !pls_intersect.empty() });
                            mst_line_x_layer_contour_cache.insert({ {pt2, pt1}, !pls_intersect.empty() });
                            if (!pls_intersect.empty())
                                continue;
                        }

                        if (poly.points.size() < 2 || visited[pt2]==false)
                        {
                            pt_selected = pt2;
                            has_next = true;
                            break;
                        }
                        double curr_ccw = pt2.ccw(pt1, poly.points.back());
                        if (curr_ccw < min_ccw)
                        {
                            min_ccw = curr_ccw;
                            pt_selected = pt2;
                            has_next = true;
                        }
                    }
                }
                if (has_next) {
                    poly.points.push_back(pt_selected);
                    to_ignore.insert(Line(pt1, pt_selected));
                    visited[pt_selected] = true;
                    pt1 = pt_selected;
                }
            }
            polys.emplace_back(std::move(poly));
        }
    }
    return polys;
}

Polygons TreeSupport::contact_nodes_to_polygon(const std::vector<Node*>& contact_nodes, Polygons layer_contours, int layer_nr, std::vector<double>& radiis, std::vector<bool>& is_interface)
{
    Polygons polys;
    std::vector<MinimumSpanningTree> spanning_trees;
    std::vector<double> radiis_mtree;
    std::vector<bool> is_interface_mtree;
    // generate minimum spanning trees
    {
        std::map<Node*, bool> visited;
        for (int i = 0; i < contact_nodes.size(); i++)
            visited.emplace(contact_nodes[i], false);
        std::unordered_set<Line, LineHash> to_ignore;

        // generate minimum spaning trees
        for (int i = 0; i < contact_nodes.size(); i++) {
            Node* node = contact_nodes[i];
            if (visited[node])
                continue;

            std::vector<Point> points_to_mstree;
            double radius = 0;
            Point pt1 = node->position;
            points_to_mstree.push_back(pt1);
            visited[node] = true;
            radius += node->radius;

            for (int j = i + 1; j < contact_nodes.size(); j++) {
                Node* node2 = contact_nodes[j];
                Point pt2 = node2->position;
                // connect to this neighbor if:
                // 1) both are interface or both are not
                // 3) not readly added
                // 4) won't cross perimeters: this is not right since we need to check all possible connections
                if ((node->support_roof_layers_below > 0) == (node2->support_roof_layers_below > 0)
                    && to_ignore.find(Line(pt1, pt2)) == to_ignore.end())
                {
                    points_to_mstree.emplace_back(pt2);
                    visited[node2] = true;
                    radius += node2->radius;
                }
            }

            spanning_trees.emplace_back(points_to_mstree);
            radiis_mtree.push_back(radius / points_to_mstree.size());
            is_interface_mtree.push_back(node->support_roof_layers_below > 0);
        }
    }
    auto lines = spanning_tree_to_lines(spanning_trees);
#if 1
    // convert mtree to polygon
    for (int k = 0; k < spanning_trees.size(); k++) {
        auto& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
        MinimumSpanningTree mst = spanning_trees[k];
        std::vector<Point> points = mst.vertices();
        std::map<Point, bool> visited;
        for (int i = 0; i < points.size(); i++)
            visited.emplace(points[i], false);

        std::unordered_set<Line, LineHash> to_ignore;
        for (int i = 0; i < points.size(); i++) {
            if (visited[points[i]])
                continue;

            Polygon poly;
            Point pt1 = points[i];
            poly.points.push_back(pt1);
            visited[pt1] = true;

            bool has_next = true;
            while (has_next)
            {
                const std::vector<Point>& neighbours = mst.adjacent_nodes(pt1);
                double min_ccw = -std::numeric_limits<double>::max();
                Point pt_selected;
                has_next = false;
                for (Point pt2 : neighbours) {
                    if (to_ignore.find(Line(pt1, pt2)) == to_ignore.end()) {
                        auto iter = mst_line_x_layer_contour_cache.find({ pt1,pt2 });
                        if (iter != mst_line_x_layer_contour_cache.end()) {
                            if (iter->second)
                                continue;
                        }
                        else {
                            Polylines pls;
                            pls.emplace_back(pt1, pt2);
                            Polylines pls_intersect = intersection_pl(pls, layer_contours);
                            mst_line_x_layer_contour_cache.insert({ {pt1, pt2}, !pls_intersect.empty() });
                            mst_line_x_layer_contour_cache.insert({ {pt2, pt1}, !pls_intersect.empty() });
                            if (!pls_intersect.empty())
                                continue;
                        }
                        if (poly.points.size() < 2)
                        {
                            pt_selected = pt2;
                            has_next = true;
                            break;
                        }
                        double curr_ccw = pt2.ccw(pt1, poly.points.rbegin()[1]);
                        if (curr_ccw > min_ccw)
                        {
                            has_next = true;
                            min_ccw = curr_ccw;
                            pt_selected = pt2;
                        }
                    }
                }
                if (!has_next)
                    break;

                poly.points.push_back(pt_selected);
                to_ignore.insert(Line(pt1, pt_selected));
                visited[pt_selected] = true;
                pt1 = pt_selected;
            }
            polys.emplace_back(std::move(poly));
            radiis.push_back(radiis_mtree[k]);
            is_interface.push_back(is_interface_mtree[k]);
        }
    }
#else
    polys = spanning_tree_to_polygon(spanning_trees, layer_contours, layer_nr, radiis);
#endif
    return polys;
}


void TreeSupport::generate()
{
    bool tree_support_enable = m_object_config->enable_support.value && is_tree(m_object_config->support_type.value);
    if (!tree_support_enable)
        return;

    std::vector<std::vector<Node*>> contact_nodes(m_object->layers().size());

    profiler.stage_start(STAGE_total);

    // Generate overhang areas
    profiler.stage_start(STAGE_DETECT_OVERHANGS);
    m_object->print()->set_status(55, _L("Support: detect overhangs"));
    detect_overhangs();
    profiler.stage_finish(STAGE_DETECT_OVERHANGS);

    if (!has_overhangs) return;

    m_ts_data = m_object->alloc_tree_support_preview_cache();
    m_ts_data->is_slim = is_slim;

    // Generate contact points of tree support
    profiler.stage_start(STAGE_GENERATE_CONTACT_NODES);
    m_object->print()->set_status(56, _L("Support: generate contact points"));
    generate_contact_points(contact_nodes);
    profiler.stage_finish(STAGE_GENERATE_CONTACT_NODES);

    //Drop nodes to lower layers.
    profiler.stage_start(STAGE_DROP_DOWN_NODES);
    m_object->print()->set_status(60, _L("Support: propagate branches"));
    drop_nodes(contact_nodes);
    profiler.stage_finish(STAGE_DROP_DOWN_NODES);

    smooth_nodes(contact_nodes);

if (!m_object->config().tree_support_adaptive_layer_height)
        // Adjust support layer heights
        adjust_layer_heights(contact_nodes);


    //Generate support areas.
    profiler.stage_start(STAGE_DRAW_CIRCLES);
    m_object->print()->set_status(65, _L("Support: draw polygons"));
    draw_circles(contact_nodes);
    profiler.stage_finish(STAGE_DRAW_CIRCLES);

    for (auto& layer : contact_nodes)
    {
        for (Node* p_node : layer)
        {
            delete p_node;
        }
        layer.clear();
    }
    contact_nodes.clear();

    profiler.stage_start(STAGE_GENERATE_TOOLPATHS);
    m_object->print()->set_status(69, _L("Support: generate toolpath"));
    generate_toolpaths();
    profiler.stage_finish(STAGE_GENERATE_TOOLPATHS);

    profiler.stage_finish(STAGE_total);
    BOOST_LOG_TRIVIAL(info) << "tree support time " << profiler.report();
}

coordf_t TreeSupport::calc_branch_radius(coordf_t base_radius, size_t layers_to_top, size_t tip_layers, double diameter_angle_scale_factor)
{
    double radius;
    if (!is_slim) {
        if ((layers_to_top + 1) > tip_layers) {
            radius = base_radius + base_radius * (layers_to_top + 1) * diameter_angle_scale_factor;
        } else {
            radius = base_radius * (layers_to_top + 1) / tip_layers;
        }
    } else {
        if ((layers_to_top + 1) > tip_layers * 2) {
            radius = base_radius + base_radius * (layers_to_top + 1) * diameter_angle_scale_factor;
        } else {
            radius = base_radius * (layers_to_top + 1) / (tip_layers * 2);
        }
        radius = std::max(radius, MIN_BRANCH_RADIUS);
    }
    radius = std::min(radius, MAX_BRANCH_RADIUS);
    return radius;
}

coordf_t TreeSupport::calc_branch_radius(coordf_t base_radius, coordf_t mm_to_top,  double diameter_angle_scale_factor)
{
    double radius;
    coordf_t tip_height = base_radius;// this is a 45 degree tip
    if (mm_to_top > tip_height)
    {
        radius = base_radius + (mm_to_top-tip_height) * diameter_angle_scale_factor;
    }
    else
    {
        radius = mm_to_top;// this is a 45 degree tip
    }

    radius = std::max(radius, MIN_BRANCH_RADIUS);
    radius = std::min(radius, MAX_BRANCH_RADIUS);
    // if have interface layers, radius should be larger
    if (m_object_config->support_interface_top_layers.value > 0)
        radius = std::max(radius, base_radius);
    return radius;
}

template<typename RegionType> // RegionType could be ExPolygons or Polygons
ExPolygons avoid_object_remove_extra_small_parts(ExPolygons &expolys, const RegionType&avoid_region) {
    ExPolygons expolys_out;
    for (auto expoly : expolys) {
        auto  expolys_avoid = diff_ex(expoly, avoid_region);
        int   idx_max_area  = -1;
        float max_area      = 0;
        for (int i = 0; i < expolys_avoid.size(); ++i) {
            auto a = expolys_avoid[i].area();
            if (a > max_area) {
                max_area     = a;
                idx_max_area = i;
            }
        }
        if (idx_max_area >= 0) expolys_out.emplace_back(std::move(expolys_avoid[idx_max_area]));
    }
    return expolys_out;
}

Polygons TreeSupport::get_trim_support_regions(
    const PrintObject& object,
    SupportLayer* support_layer_ptr,
    const coordf_t       gap_extra_above,
    const coordf_t       gap_extra_below,
    const coordf_t       gap_xy)
{
    static const double sharp_tail_xy_gap = 0.2f;
    static const double no_overlap_xy_gap = 0.2f;
    double gap_xy_scaled = scale_(gap_xy);
    SupportLayer& support_layer = *support_layer_ptr;
    auto m_print_config = object.print()->config();

    size_t idx_object_layer_overlapping = size_t(-1);

    auto is_layers_overlap = [](const SupportLayer& support_layer, const Layer& object_layer, coordf_t bridging_height = 0.f) -> bool {
        if (std::abs(support_layer.print_z - object_layer.print_z) < EPSILON)
            return true;

        coordf_t object_lh = bridging_height > EPSILON ? bridging_height : object_layer.height;
        if (support_layer.print_z < object_layer.print_z && support_layer.print_z > object_layer.print_z - object_lh)
            return true;

        if (support_layer.print_z > object_layer.print_z && support_layer.bottom_z() < object_layer.print_z - EPSILON)
            return true;

        return false;
        };

    // Find the overlapping object layers including the extra above / below gap.
    coordf_t z_threshold = support_layer.bottom_z() - gap_extra_below + EPSILON;
    idx_object_layer_overlapping = Layer::idx_higher_or_equal(
        object.layers().begin(), object.layers().end(), idx_object_layer_overlapping,
        [z_threshold](const Layer* layer) { return layer->print_z >= z_threshold; });
    // Collect all the object layers intersecting with this layer.
    Polygons polygons_trimming;
    size_t i = idx_object_layer_overlapping;
    for (; i < object.layers().size(); ++i) {
        const Layer& object_layer = *object.layers()[i];
        if (object_layer.bottom_z() > support_layer.print_z + gap_extra_above - EPSILON)
            break;

        bool is_overlap = is_layers_overlap(support_layer, object_layer);
        for (const ExPolygon& expoly : object_layer.lslices) {
            // BBS
            bool is_sharptail = !intersection_ex({ expoly }, object_layer.sharp_tails).empty();
            coordf_t trimming_offset = is_sharptail ? scale_(sharp_tail_xy_gap) :
                is_overlap ? gap_xy_scaled :
                scale_(no_overlap_xy_gap);
            polygons_append(polygons_trimming, offset({ expoly }, trimming_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
            }
        }
    if (!m_slicing_params.soluble_interface && m_object_config->thick_bridges) {
        // Collect all bottom surfaces, which will be extruded with a bridging flow.
        for (; i < object.layers().size(); ++i) {
            const Layer& object_layer = *object.layers()[i];
            bool some_region_overlaps = false;
            for (LayerRegion* region : object_layer.regions()) {
                coordf_t bridging_height = region->region().bridging_height_avg(m_print_config);
                if (object_layer.print_z - bridging_height > support_layer.print_z + gap_extra_above - EPSILON)
                    break;
                some_region_overlaps = true;

                bool is_overlap = is_layers_overlap(support_layer, object_layer, bridging_height);
                coordf_t trimming_offset = is_overlap ? gap_xy_scaled : scale_(no_overlap_xy_gap);
                polygons_append(polygons_trimming,
                    offset(region->fill_surfaces.filter_by_type(stBottomBridge), trimming_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                }
            if (!some_region_overlaps)
                break;
            }
        }

    return polygons_trimming;
}

void TreeSupport::draw_circles(const std::vector<std::vector<Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object->config();
    const Print* print = m_object->print();
    bool has_brim = print->has_brim();
    int bottom_gap_layers = round(m_slicing_params.gap_object_support / m_slicing_params.layer_height);
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const coordf_t branch_radius_scaled = scale_(branch_radius);
    bool on_buildplate_only = config.support_on_build_plate_only.value;
    Polygon branch_circle; //Pre-generate a circle with correct diameter so that we don't have to recompute those (co)sines every time.

    // Use square support if there are too many nodes per layer because circle support needs much longer time to compute
    // Hower circle support can be printed faster, so we prefer circle for fewer nodes case.
    const bool SQUARE_SUPPORT = avg_node_per_layer > 200;    
    const int  CIRCLE_RESOLUTION = SQUARE_SUPPORT ? 4 : 100; // The number of vertices in each circle.


    for (int i = 0; i < CIRCLE_RESOLUTION; i++)
    {
        double angle;
        if (SQUARE_SUPPORT)
            angle = (double) i / CIRCLE_RESOLUTION * TAU + PI / 4.0 + nodes_angle;
        else
            angle = (double) i / CIRCLE_RESOLUTION * TAU;
        branch_circle.append(Point(cos(angle) * branch_radius_scaled, sin(angle) * branch_radius_scaled));
    }

    // Performance optimization. Only generate lslices for brim and skirt.
    size_t brim_skirt_layers = has_brim ? 1 : 0;
    const PrintConfig& print_config = print->config();
    for (const PrintObject* object : print->objects())
    {
        size_t skirt_layers = print->has_infinite_skirt() ? object->layer_count() : std::min(size_t(print_config.skirt_height.value), object->layer_count());
        brim_skirt_layers = std::max(brim_skirt_layers, skirt_layers);
    }

    // generate areas
    const coordf_t layer_height = config.layer_height.value;
    const size_t   top_interface_layers = config.support_interface_top_layers.value;
    const size_t   bottom_interface_layers = config.support_interface_bottom_layers.value;
    const double diameter_angle_scale_factor = tan(tree_support_branch_diameter_angle * M_PI / 180.);// * layer_height / branch_radius; //Scale factor per layer to produce the desired angle.
    const double nozzle_diameter = m_object->print()->config().nozzle_diameter.get_at(0);
    const coordf_t line_width = config.get_abs_value("support_line_width", nozzle_diameter);
    const coordf_t line_width_scaled           = scale_(line_width);

    const bool with_lightning_infill = m_support_params.base_fill_pattern == ipLightning;
    coordf_t support_extrusion_width = m_support_params.support_extrusion_width;
    const size_t wall_count = config.tree_support_wall_count.value;

    const PrintObjectConfig& object_config = m_object->config();
    BOOST_LOG_TRIVIAL(info) << "draw_circles for object: " << m_object->model_object()->name;

    // coconut: previously std::unordered_map in m_collision_cache is not multi-thread safe which may cause programs stuck, here we change to tbb::concurrent_unordered_map
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_object->layer_count()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++)
            {
                if (print->canceled())
                    break;

                const std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                assert(ts_layer != nullptr);

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (curr_layer_nodes.empty()) {
                    ts_layer->print_z = 0.0;
                    ts_layer->height = 0.0;
                    continue;
                }

                Node* first_node = curr_layer_nodes.front();
                ts_layer->print_z = first_node->print_z;
                ts_layer->height = first_node->height;
                if (ts_layer->height < EPSILON) {
                    continue;
                }

                ExPolygons& base_areas = ts_layer->base_areas;
                ExPolygons& roof_areas = ts_layer->roof_areas;
                ExPolygons& roof_1st_layer = ts_layer->roof_1st_layer;
                ExPolygons& floor_areas = ts_layer->floor_areas;
                ExPolygons& roof_gap_areas = ts_layer->roof_gap_areas;
                coordf_t         max_layers_above_base = 0;
                coordf_t         max_layers_above_roof = 0;
                coordf_t         max_layers_above_roof1 = 0;
                bool has_polygon_node = false;
                bool has_circle_node = false;

                BOOST_LOG_TRIVIAL(debug) << "circles at layer " << layer_nr << " contact nodes size=" << contact_nodes[layer_nr].size();
                //Draw the support areas and add the roofs appropriately to the support roof instead of normal areas.
                ts_layer->lslices.reserve(contact_nodes[layer_nr].size());
                for (const Node* p_node : contact_nodes[layer_nr])
                {
                    if (print->canceled())
                        break;

                    const Node& node = *p_node;
                    ExPolygons area;
                    // Generate directly from overhang polygon if one of the following is true:
                    // 1) node is a normal part of hybrid support
                    // 2) node is virtual
                    if (node.type == ePolygon || node.distance_to_top<0) {
                        if (node.overhang->contour.size() > 100 || node.overhang->holes.size()>1)
                            area.emplace_back(*node.overhang);
                        else {
                            area = offset_ex({ *node.overhang }, scale_(m_ts_data->m_xy_distance));
                        }
                        if (node.type == ePolygon)
                            has_polygon_node = true;
                    }
                    else {
                        Polygon circle;
                        size_t layers_to_top = node.distance_to_top;
                        double  scale = calc_branch_radius(branch_radius, node.dist_mm_to_top, diameter_angle_scale_factor) / branch_radius;

                        if (/*is_slim*/1) { // draw ellipse along movement direction
                            double moveX = node.movement.x() / (scale * branch_radius_scaled);
                            double moveY = node.movement.y() / (scale * branch_radius_scaled);
                            const double vsize_inv = 0.5 / (0.01 + std::sqrt(moveX * moveX + moveY * moveY));
                            double       matrix[2*2]  = {
                                scale * (1 + moveX * moveX * vsize_inv),scale * (0 + moveX * moveY * vsize_inv),
                                scale * (0 + moveX * moveY * vsize_inv),scale * (1 + moveY * moveY * vsize_inv),
                            };
                            for (auto vertex: branch_circle.points) {
                                vertex = Point(matrix[0] * vertex.x() + matrix[1] * vertex.y(), matrix[2] * vertex.x() + matrix[3] * vertex.y());
                                circle.append(node.position + vertex);
                            }
                        } else {
                            for (auto iter = branch_circle.points.begin(); iter != branch_circle.points.end(); iter++) {
                                Point corner = (*iter) * scale;
                                circle.append(node.position + corner);
                            }
                        }
                        if (layer_nr == 0 && m_raft_layers == 0) {
                            double brim_width =
                                config.tree_support_auto_brim
                                    ? layers_to_top * layer_height /
                                          (scale * branch_radius) * 0.5
                                    : config.tree_support_brim_width;
                            circle = offset(circle, scale_(brim_width))[0];
                        }
                        area.emplace_back(ExPolygon(circle));
                        // merge overhang to get a smoother interface surface
                        // Do not merge when buildplate_only is on, because some underneath nodes may have been deleted.
                        if (top_interface_layers > 0 && node.support_roof_layers_below > 0 && !on_buildplate_only) {
                            ExPolygons overhang_expanded;
                            if (node.overhang->contour.size() > 100 || node.overhang->holes.size()>1)
                                overhang_expanded.emplace_back(*node.overhang);
                            else {
                                // 对于有缺陷的模型，overhang膨胀以后可能是空的！
                                overhang_expanded = offset_ex({ *node.overhang }, scale_(m_ts_data->m_xy_distance));
                            }
                            append(area, overhang_expanded);
                        }
                        has_circle_node = true;
                    }

                    if (node.distance_to_top < 0)
                        append(roof_gap_areas, area);
                    else if (node.support_roof_layers_below == 1)
                    {
                        append(roof_1st_layer, area);
                        max_layers_above_roof1 = std::max(max_layers_above_roof1, node.dist_mm_to_top);
                    }
                    else if (node.support_roof_layers_below > 0)
                    {
                        append(roof_areas, area);
                        max_layers_above_roof = std::max(max_layers_above_roof, node.dist_mm_to_top);
                    }
                    else
                    {
                        append(base_areas, area);
                        max_layers_above_base = std::max(max_layers_above_base, node.dist_mm_to_top);
                    }

                    if (layer_nr < brim_skirt_layers)
                        append(ts_layer->lslices, area);
                }

                ts_layer->lslices = std::move(union_ex(ts_layer->lslices));

                //Must update bounding box which is used in avoid crossing perimeter
                ts_layer->lslices_bboxes.clear();
                ts_layer->lslices_bboxes.reserve(ts_layer->lslices.size());
                for (const ExPolygon &expoly : ts_layer->lslices)
                    ts_layer->lslices_bboxes.emplace_back(get_extents(expoly));
                ts_layer->backup_untyped_slices();

                m_object->print()->set_status(65, (boost::format( _L("Support: generate polygons at layer %d")) % layer_nr).str());

                // join roof segments
                double contact_dist_scaled = scale_(0.5);// scale_(m_slicing_params.gap_support_object);
                roof_areas = std::move(offset2_ex(roof_areas, contact_dist_scaled, -contact_dist_scaled));
                roof_1st_layer = std::move(offset2_ex(roof_1st_layer, contact_dist_scaled, -contact_dist_scaled));

                // avoid object
                //ExPolygons avoid_region_interface = m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr);
                Polygons avoid_region_interface = get_trim_support_regions(*m_object, ts_layer, m_slicing_params.gap_object_support, m_slicing_params.gap_support_object, m_ts_data->m_xy_distance);
                if (has_circle_node) {
                    roof_areas = avoid_object_remove_extra_small_parts(roof_areas, avoid_region_interface);
                    roof_1st_layer = avoid_object_remove_extra_small_parts(roof_1st_layer, avoid_region_interface);
                }
                else {
                    roof_areas = std::move(diff_ex(roof_areas, avoid_region_interface));
                    roof_1st_layer = std::move(diff_ex(roof_1st_layer, avoid_region_interface));
                }
                roof_areas = intersection_ex(roof_areas, m_machine_border);

                // roof_1st_layer and roof_areas may intersect, so need to subtract roof_areas from roof_1st_layer
                roof_1st_layer = std::move(diff_ex(roof_1st_layer, roof_areas));
                roof_1st_layer = intersection_ex(roof_1st_layer, m_machine_border);

                // let supports touch objects when brim is on
                auto avoid_region = m_ts_data->get_collision((layer_nr == 0 && has_brim) ? config.brim_object_gap : m_ts_data->m_xy_distance, layer_nr);
                base_areas = avoid_object_remove_extra_small_parts(base_areas, avoid_region);
                base_areas = std::move(diff_ex(base_areas, roof_areas));
                base_areas = std::move(diff_ex(base_areas, roof_1st_layer));
                base_areas = std::move(diff_ex(base_areas, roof_gap_areas));
                base_areas = intersection_ex(base_areas, m_machine_border);

                if (SQUARE_SUPPORT) {
                    // simplify support contours
                    ExPolygons base_areas_simplified;
                    for (auto &area : base_areas) { area.simplify(scale_(line_width / 2), &base_areas_simplified); }
                    base_areas = std::move(base_areas_simplified);
                }
                //Subtract support floors. We can only compute floor_areas here instead of with roof_areas,
                // or we'll get much wider floor than necessary.
                if (bottom_interface_layers + bottom_gap_layers > 0)
                {
                    if (layer_nr >= bottom_interface_layers + bottom_gap_layers)
                    {
                        for (size_t i = 0; i <= bottom_gap_layers; i++)
                        {
                            const Layer* below_layer = m_object->get_layer(layer_nr - bottom_interface_layers - i);
                            ExPolygons bottom_interface = intersection_ex(base_areas, below_layer->lslices);
                            floor_areas.insert(floor_areas.end(), bottom_interface.begin(), bottom_interface.end());
                        }
                    }
                    if (floor_areas.empty() == false) {
                        floor_areas = std::move(diff_ex(floor_areas, avoid_region_interface));
                        floor_areas = std::move(offset2_ex(floor_areas, contact_dist_scaled, -contact_dist_scaled));
                        base_areas = std::move(diff_ex(base_areas, offset_ex(floor_areas, 10)));
                    }
                }
                if (bottom_gap_layers > 0 && layer_nr > bottom_gap_layers) {
                    const Layer* below_layer = m_object->get_layer(layer_nr - bottom_gap_layers);
                    ExPolygons bottom_gap_area = intersection_ex(floor_areas, below_layer->lslices);
                    if (!bottom_gap_area.empty()) {
                        floor_areas = std::move(diff_ex(floor_areas, bottom_gap_area));
                    }
                }
                auto &area_groups = ts_layer->area_groups;
                for (auto& area : ts_layer->base_areas) {
                    area_groups.emplace_back(&area, SupportLayer::BaseType, max_layers_above_base);
                    area_groups.back().need_infill = has_polygon_node;
                }
                for (auto &area : ts_layer->roof_areas) area_groups.emplace_back(&area, SupportLayer::RoofType, max_layers_above_roof);
                for (auto &area : ts_layer->floor_areas) area_groups.emplace_back(&area, SupportLayer::FloorType, 10000);
                for (auto &area : ts_layer->roof_1st_layer) area_groups.emplace_back(&area, SupportLayer::Roof1stLayer, max_layers_above_roof1);

                for (auto &area_group : area_groups) {
                    auto& expoly = area_group.area;
                    expoly->holes.erase(std::remove_if(expoly->holes.begin(), expoly->holes.end(),
                                                       [](auto &hole) {
                                                           auto bbox_size = get_extents(hole).size();
                                                           return bbox_size[0] < scale_(2) && bbox_size[1] < scale_(2);
                                                       }),
                                        expoly->holes.end());
                }

            }
        });


        if (with_lightning_infill)
        {
            const bool global_lightning_infill = true;

            std::vector<Polygons> contours;
            std::vector<Polygons> overhangs;
            for (int layer_nr = 1; layer_nr < m_object->layer_count(); layer_nr++) {
                if (print->canceled()) break;
                const std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                assert(ts_layer != nullptr);

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (curr_layer_nodes.empty()) continue;
                if (ts_layer->height < EPSILON) continue;
                if (ts_layer->area_groups.empty()) continue;

                ExPolygons& base_areas = ts_layer->base_areas;

                int layer_nr_lower = layer_nr - 1;
                for (layer_nr_lower; layer_nr_lower >= 0; layer_nr_lower--) {
                    if (!m_object->get_support_layer(layer_nr_lower + m_raft_layers)->area_groups.empty()) break;
                }
                if (layer_nr_lower <= 0) continue;

                SupportLayer* lower_layer = m_object->get_support_layer(layer_nr_lower + m_raft_layers);
                ExPolygons& base_areas_lower = lower_layer->base_areas;

                ExPolygons overhang;
                if (global_lightning_infill)
                {
                    //search overhangs globally
                    overhang = std::move(diff_ex(offset_ex(base_areas_lower, -2.0 * scale_(support_extrusion_width)), base_areas));
                }
                else
                {
                    //search overhangs only on floating islands
                    for (auto& base_area : base_areas)
                        for (auto& hole : base_area.holes)
                        {
                            Polygon rev_hole = hole;
                            rev_hole.make_counter_clockwise();
                            ExPolygons ex_hole;
                            ex_hole.emplace_back(std::move(ExPolygon(rev_hole)));
                            for (auto& other_area : base_areas)
                                //if (&other_area != &base_area)
                                    ex_hole = std::move(diff_ex(ex_hole, other_area));
                            overhang = std::move(union_ex(overhang, ex_hole));
                        }
                    overhang = std::move(intersection_ex(overhang, offset_ex(base_areas_lower, -0.5 * scale_(support_extrusion_width))));
                }

                overhangs.emplace_back(to_polygons(overhang));
                contours.emplace_back(to_polygons(base_areas_lower));
                printZ_to_lightninglayer[lower_layer->print_z] = overhangs.size() - 1;

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                draw_two_overhangs_to_svg(m_object->get_support_layer(layer_nr_lower + m_raft_layers), base_areas_lower, to_expolygons(overhangs.back()));
#endif
            }


            auto m_support_material_flow = support_material_flow(m_object, m_slicing_params.layer_height);
            coordf_t support_spacing = object_config.support_base_pattern_spacing.value + m_support_material_flow.spacing();
            coordf_t support_density = std::min(1., m_support_material_flow.spacing() / support_spacing * 2); // for lightning infill the density is defined differently, so need to double it
            generator = std::make_unique<FillLightning::Generator>(m_object, contours, overhangs, []() {}, support_density);
        }

        else if (!with_infill) {
            // move the holes to contour so they can be well supported

            // check if poly's contour intersects with expoly's contour
            auto intersects_contour = [](Polygon poly, ExPolygon expoly, Point& pt_on_poly, Point& pt_on_expoly, Point& pt_far_on_poly, float dist_thresh = 0.01) {
                float min_dist = std::numeric_limits<float>::max();
                float max_dist = 0;
                for (auto from : poly.points) {
                    for (int i = 0; i < expoly.num_contours(); i++) {
                        const Point* candidate = expoly.contour_or_hole(i).closest_point(from);
                        double       dist2 = vsize2_with_unscale(*candidate - from);
                        if (dist2 < min_dist) {
                            min_dist = dist2;
                            pt_on_poly = from;
                            pt_on_expoly = *candidate;
                            }
                        if (dist2 > max_dist) {
                            max_dist = dist2;
                            pt_far_on_poly = from;
                            }
                        if (dist2 < dist_thresh) { return true; }
                        }
                    }
                return false;
                };

            // polygon pointer: depth, direction, farPoint
            std::map<const Polygon*, std::tuple<int, Point, Point>> holePropagationInfos;
            for (int layer_nr = m_object->layer_count() - 1; layer_nr > 0; layer_nr--) {
                if (print->canceled()) break;
                m_object->print()->set_status(66, (boost::format(_L("Support: fix holes at layer %d")) % layer_nr).str());

                const std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
                SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
                assert(ts_layer != nullptr);

                // skip if current layer has no points. This fixes potential crash in get_collision (see jira BBL001-355)
                if (curr_layer_nodes.empty()) continue;
                if (ts_layer->height < EPSILON) continue;
                if (ts_layer->area_groups.empty()) continue;

                int layer_nr_lower = layer_nr - 1;
                for (layer_nr_lower; layer_nr_lower >= 0; layer_nr_lower--) {
                    if (!m_object->get_support_layer(layer_nr_lower + m_raft_layers)->area_groups.empty()) break;
                }
                if (layer_nr_lower < 0) continue;
                auto& area_groups_lower = m_object->get_support_layer(layer_nr_lower + m_raft_layers)->area_groups;

                for (const auto& area_group : ts_layer->area_groups) {
                    if (area_group.type != SupportLayer::BaseType) continue;
                    const auto& area = area_group.area;
                    for (const auto& hole : area->holes) {
                        // auto hole_bbox = get_extents(hole).polygon();
                        for (auto& area_group_lower : area_groups_lower) {
                            if (area_group.type != SupportLayer::BaseType) continue;
                            auto& base_area_lower = *area_group_lower.area;
                            Point pt_on_poly, pt_on_expoly, pt_far_on_poly;
                            // if a hole doesn't intersect with lower layer's contours, add a hole to lower layer and move it slightly to the contour
                            if (base_area_lower.contour.contains(hole.points.front()) && !intersects_contour(hole, base_area_lower, pt_on_poly, pt_on_expoly, pt_far_on_poly)) {
                                Polygon hole_lower = hole;
                                Point   direction = normal(pt_on_expoly - pt_on_poly, line_width_scaled / 2);
                                hole_lower.translate(direction);
                                // note to expand a hole, we need to do negative offset
                                auto hole_expanded = offset(hole_lower, -line_width_scaled / 4, ClipperLib::JoinType::jtSquare);
                                if (!hole_expanded.empty()) {
                                    base_area_lower.holes.push_back(std::move(hole_expanded[0]));
                                    holePropagationInfos.insert({ &base_area_lower.holes.back(), {25, direction, pt_far_on_poly} });
                                    }
                                break;
                                }
                            else if (holePropagationInfos.find(&hole) != holePropagationInfos.end() && std::get<0>(holePropagationInfos[&hole]) > 0 &&
                                base_area_lower.contour.contains(std::get<2>(holePropagationInfos[&hole]))) {
                                Polygon hole_lower = hole;
                                auto&& direction = std::get<1>(holePropagationInfos[&hole]);
                                hole_lower.translate(direction);
                                // note to shrink a hole, we need to do positive offset
                                auto  hole_expanded = offset(hole_lower, line_width_scaled / 2, ClipperLib::JoinType::jtSquare);
                                Point farPoint = std::get<2>(holePropagationInfos[&hole]) + direction * 2;
                                if (!hole_expanded.empty()) {
                                    base_area_lower.holes.push_back(std::move(hole_expanded[0]));
                                    holePropagationInfos.insert({ &base_area_lower.holes.back(), {std::get<0>(holePropagationInfos[&hole]) - 1, direction, farPoint} });
                                    }
                                break;
                                }
                            }
                        {
                        // if roof1 interface is inside a hole, need to expand the interface
                        for (auto& roof1 : ts_layer->roof_1st_layer) {
                            //if (hole.contains(roof1.contour.points.front()) && hole.contains(roof1.contour.bounding_box().center())) 
                            bool is_inside_hole = std::all_of(roof1.contour.points.begin(), roof1.contour.points.end(), [&hole](Point& pt) { return hole.contains(pt); });
                            if (is_inside_hole) {
                                Polygon hole_reoriented = hole;
                                if (roof1.contour.is_counter_clockwise())
                                    hole_reoriented.make_counter_clockwise();
                                else if (roof1.contour.is_clockwise())
                                    hole_reoriented.make_clockwise();
                                auto tmp = union_({ roof1.contour }, { hole_reoriented });
                                if (!tmp.empty()) roof1.contour = tmp[0];

                                // make sure 1) roof1 and object 2) roof1 and roof, won't intersect
                                // Note: We can't replace roof1 directly, as we have recorded its address.
                                //       So instead we need to replace its members one by one.
                                auto tmp1 = diff_ex(roof1, m_ts_data->get_collision((layer_nr == 0 && has_brim) ? config.brim_object_gap : m_ts_data->m_xy_distance, layer_nr));
                                tmp1 = diff_ex(tmp1, ts_layer->roof_areas);
                                if (!tmp1.empty()) {
                                    roof1.contour = std::move(tmp1[0].contour);
                                    roof1.holes = std::move(tmp1[0].holes);
                                    }
                                break;
                                }
                            }
                        }
                        }
                    }
                }
            }


#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    for (int layer_nr = m_object->layer_count() - 1; layer_nr >= 0; layer_nr--) {
        SupportLayer* ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        ExPolygons& base_areas = ts_layer->base_areas;
        ExPolygons& roof_areas = ts_layer->roof_areas;
        ExPolygons& roof_1st_layer = ts_layer->roof_1st_layer;
        ExPolygons& floor_areas = ts_layer->floor_areas;
        if (base_areas.empty() && roof_areas.empty() && roof_1st_layer.empty()) continue;
        char fname[10]; sprintf(fname, "%d_%.2f", layer_nr, ts_layer->print_z);
        draw_contours_and_nodes_to_svg("", base_areas, roof_areas, roof_1st_layer, {}, {}, get_svg_filename(fname, "circles"), {"base", "roof", "roof1st"});
    }
#endif  // SUPPORT_TREE_DEBUG_TO_SVG

    SupportLayerPtrs& ts_layers = m_object->support_layers();
    auto iter = std::remove_if(ts_layers.begin(), ts_layers.end(), [](SupportLayer* ts_layer) { return ts_layer->height < EPSILON; });
    ts_layers.erase(iter, ts_layers.end());
    for (int layer_nr = 0; layer_nr < ts_layers.size(); layer_nr++) {
        ts_layers[layer_nr]->upper_layer = layer_nr != ts_layers.size() - 1 ? ts_layers[layer_nr + 1] : nullptr;
        ts_layers[layer_nr]->lower_layer = layer_nr > 0 ? ts_layers[layer_nr - 1] : nullptr;
    }
}

void TreeSupport::drop_nodes(std::vector<std::vector<Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object->config();
    // Use Minimum Spanning Tree to connect the points on each layer and move them while dropping them down.
    const coordf_t support_extrusion_width = m_support_params.support_extrusion_width;
    const coordf_t layer_height = config.layer_height.value;
    const double angle = config.tree_support_branch_angle.value * M_PI / 180.;
    const int wall_count = std::max(1, config.tree_support_wall_count.value);
    double tan_angle = tan(angle); // when nodes are thick, they can move further. this is the max angle
    const coordf_t max_move_distance = (angle < M_PI / 2) ? (coordf_t)(tan_angle * layer_height)*wall_count : std::numeric_limits<coordf_t>::max();
    const double max_move_distance2 = max_move_distance * max_move_distance;
    const coordf_t branch_radius = config.tree_support_branch_diameter.value / 2;
    const size_t tip_layers = branch_radius / layer_height; //The number of layers to be shrinking the circle to create a tip. This produces a 45 degree angle.
    const double diameter_angle_scale_factor = tan(tree_support_branch_diameter_angle * M_PI / 180.);//*layer_height / branch_radius; // Scale factor per layer to produce the desired angle.
    const coordf_t radius_sample_resolution = m_ts_data->m_radius_sample_resolution;
    const bool support_on_buildplate_only = config.support_on_build_plate_only.value;
    const size_t bottom_interface_layers = config.support_interface_bottom_layers.value;
    const size_t top_interface_layers = config.support_interface_top_layers.value;
    float        DO_NOT_MOVER_UNDER_MM       = is_slim ? 0 : 5;                     // do not move contact points under 5mm
    const auto nozzle_diameter = m_object->print()->config().nozzle_diameter.get_at(m_object->config().support_interface_filament-1);
    const auto support_line_width = config.support_line_width.get_abs_value(nozzle_diameter);

    auto get_branch_angle = [this,&config](coordf_t radius) {
        if (config.tree_support_branch_angle.value < 30.0) return config.tree_support_branch_angle.value;
        return (radius - MIN_BRANCH_RADIUS) / (MAX_BRANCH_RADIUS - MIN_BRANCH_RADIUS) * (config.tree_support_branch_angle.value - 30.0) + 30.0;
    };
    auto get_max_move_dist = [this, &config, branch_radius, tip_layers, diameter_angle_scale_factor, wall_count, support_extrusion_width, support_line_width](const Node *node, int power = 1) {
        double move_dist = node->max_move_dist;
        if (node->max_move_dist == 0) {
            if (node->radius == 0) node->radius = calc_branch_radius(branch_radius, node->dist_mm_to_top, diameter_angle_scale_factor);
            double angle = config.tree_support_branch_angle.value;
            if (angle > 30.0 && node->radius > MIN_BRANCH_RADIUS)
                angle = (node->radius - MIN_BRANCH_RADIUS) / (MAX_BRANCH_RADIUS - MIN_BRANCH_RADIUS) * (config.tree_support_branch_angle.value - 30.0) + 30.0;
            double tan_angle           = tan(angle * M_PI / 180);
            int wall_count_     = node->radius > 2 * support_line_width ? wall_count : 1;
            node->max_move_dist = (angle < 90) ? (coordf_t) (tan_angle * node->height) * wall_count_ : std::numeric_limits<coordf_t>::max();
            node->max_move_dist        = std::min(node->max_move_dist, support_extrusion_width);
            move_dist           = node->max_move_dist;
        }
        if (power == 2) move_dist = SQ(move_dist);
        return move_dist;
    };

    m_ts_data->layer_heights = plan_layer_heights(contact_nodes);
    std::vector<LayerHeightData> &layer_heights = m_ts_data->layer_heights;
    if (layer_heights.empty()) return;

    std::unordered_set<Node*> to_free_node_set;
    m_spanning_trees.resize(contact_nodes.size());
    //m_mst_line_x_layer_contour_caches.resize(contact_nodes.size());

    if (0)
    {// get outlines below and avoidance area using tbb
     // This part only takes very little time, so we disable it.
        typedef std::chrono::high_resolution_clock clock_;
        typedef std::chrono::duration<double, std::ratio<1> > second_;
        std::chrono::time_point<clock_> t0{ clock_::now() };

        // get all the possible radiis
        std::vector<std::set<coordf_t> > all_layer_radius(m_highest_overhang_layer+1);
        std::vector<std::set<coordf_t>> all_layer_node_dist(m_highest_overhang_layer + 1);
        for (size_t layer_nr = m_highest_overhang_layer; layer_nr > 0; layer_nr--)
        {
            if (layer_heights[layer_nr].height < EPSILON) continue;
            auto& layer_radius = all_layer_radius[layer_nr];
            auto& layer_node_dist = all_layer_node_dist[layer_nr];
            for (Node *p_node : contact_nodes[layer_nr]) {
                layer_node_dist.emplace(p_node->dist_mm_to_top);
            }
            size_t layer_nr_next = layer_heights[layer_nr].next_layer_nr;
            if (layer_nr_next <= m_highest_overhang_layer && layer_nr_next>0) {
                for (auto node_dist : layer_node_dist)
                    all_layer_node_dist[layer_nr_next].emplace(node_dist + layer_heights[layer_nr].height);
            }
            for (auto node_dist : layer_node_dist) {
                layer_radius.emplace(calc_branch_radius(branch_radius, node_dist, diameter_angle_scale_factor));
            }
        }
        // parallel pre-compute avoidance
        //tbb::parallel_for(tbb::blocked_range<size_t>(1, m_highest_overhang_layer), [&](const tbb::blocked_range<size_t> &range) {
            //for (size_t layer_nr = range.begin(); layer_nr < range.end(); layer_nr++) {
            for (size_t layer_nr = 0; layer_nr < all_layer_radius.size(); layer_nr++) {
                BOOST_LOG_TRIVIAL(debug) << "pre calculate_avoidance layer=" << layer_nr;
                for (auto node_radius : all_layer_radius[layer_nr]) {
                    m_ts_data->get_avoidance(0, layer_nr);
                    m_ts_data->get_avoidance(node_radius, layer_nr);
                }
            }
        //});

        double duration{ std::chrono::duration_cast<second_>(clock_::now() - t0).count() };
        BOOST_LOG_TRIVIAL(debug) << "before m_avoidance_cache.size()=" << m_ts_data->m_avoidance_cache.size()
            << ", takes " << duration << " secs.";
    }

    for (size_t layer_nr = contact_nodes.size() - 1; layer_nr > 0; layer_nr--) // Skip layer 0, since we can't drop down the vertices there.
    {
        if (m_object->print()->canceled())
            break;

        auto& layer_contact_nodes = contact_nodes[layer_nr];
        if (layer_contact_nodes.empty())
            continue;
        
        int      layer_nr_next = layer_heights[layer_nr].next_layer_nr;
        coordf_t print_z_next = layer_heights[layer_nr_next].print_z;
        coordf_t height_next = layer_heights[layer_nr_next].height;

        std::deque<std::pair<size_t, Node*>> unsupported_branch_leaves; // All nodes that are leaves on this layer that would result in unsupported ('mid-air') branches.
        const Layer* ts_layer = m_object->get_support_layer(layer_nr);

        m_object->print()->set_status(60, (boost::format(_L("Support: propagate branches at layer %d")) % layer_nr).str());

        Polygons layer_contours = m_ts_data->get_contours_with_holes(layer_nr);
        //std::unordered_map<Line, bool, LineHash>& mst_line_x_layer_contour_cache = m_mst_line_x_layer_contour_caches[layer_nr];
        std::unordered_map<Line, bool, LineHash> mst_line_x_layer_contour_cache;
        auto is_line_cut_by_contour = [&mst_line_x_layer_contour_cache,&layer_contours](Point a, Point b)
        {
            auto iter = mst_line_x_layer_contour_cache.find({ a, b });
            if (iter != mst_line_x_layer_contour_cache.end()) {
                if (iter->second)
                    return true;
            }
            else {
                profiler.tic();
                Line ln(b, a);
                Lines pls_intersect = intersection_ln(ln, layer_contours);
                mst_line_x_layer_contour_cache.insert({ {a, b}, !pls_intersect.empty() });
                mst_line_x_layer_contour_cache.insert({ ln, !pls_intersect.empty() });
                profiler.stage_add(STAGE_intersection_ln, true);
                if (!pls_intersect.empty())
                    return true;
            }
            return false;
        };

        //Group together all nodes for each part.
        const ExPolygons& parts = m_ts_data->get_avoidance(0, layer_nr);
        std::vector<std::unordered_map<Point, Node*, PointHash>> nodes_per_part(1 + parts.size()); //All nodes that aren't inside a part get grouped together in the 0th part.
        for (Node* p_node : layer_contact_nodes)
        {
            const Node& node = *p_node;

            if (node.distance_to_top < 0) {
                // gap nodes do not merge or move
                Node* next_node = new Node(p_node->position, p_node->distance_to_top + 1, layer_nr_next, p_node->support_roof_layers_below - 1, p_node->to_buildplate, p_node,
                    print_z_next, height_next);
                get_max_move_dist(next_node);
                next_node->is_merged = false;
                contact_nodes[layer_nr_next].emplace_back(next_node);
                continue;
            }
            if (support_on_buildplate_only && !node.to_buildplate) //Can't rest on model and unable to reach the build plate. Then we must drop the node and leave parts unsupported.
            {
                unsupported_branch_leaves.push_front({ layer_nr, p_node });
                continue;
            }
            if (node.to_buildplate || parts.empty()) //It's outside, so make it go towards the build plate.
            {
                nodes_per_part[0][node.position] = p_node;
                continue;
            }

            /* Find which part this node is located in and group the nodes in
             * the same part together. Since nodes have a radius and the
             * avoidance areas are offset by that radius, the set of parts may
             * be different per node. Here we consider a node to be inside the
             * part that is closest. The node may be inside a bigger part that
             * is actually two parts merged together due to an offset. In that
             * case we may incorrectly keep two nodes separate, but at least
             * every node falls into some group.
             */
            coordf_t closest_part_distance2 = std::numeric_limits<coordf_t>::max();
            size_t closest_part = -1;
            for (size_t part_index = 0; part_index < parts.size(); part_index++)
            {
                //constexpr bool border_result = true;
                if (is_inside_ex(parts[part_index], node.position)) //If it's inside, the distance is 0 and this part is considered the best.
                {
                    closest_part = part_index;
                    closest_part_distance2 = 0;
                    break;
                }

                Point closest_point = *parts[part_index].contour.closest_point(node.position);
                const coordf_t distance2 = vsize2_with_unscale(node.position - closest_point);
                if (distance2 < closest_part_distance2)
                {
                    closest_part_distance2 = distance2;
                    closest_part = part_index;
                }
            }
            //Put it in the best one.
            nodes_per_part[closest_part + 1][node.position] = p_node; //Index + 1 because the 0th index is the outside part.
        }

        //Create a MST for every part.
        profiler.tic();
        //std::vector<MinimumSpanningTree>& spanning_trees = m_spanning_trees[layer_nr];
        std::vector<MinimumSpanningTree> spanning_trees;
        for (const std::unordered_map<Point, Node*, PointHash>& group : nodes_per_part)
        {
            std::vector<Point> points_to_buildplate;
            for (const std::pair<const Point, Node*>& entry : group)
            {
                points_to_buildplate.emplace_back(entry.first); //Just the position of the node.
            }
            spanning_trees.emplace_back(points_to_buildplate);
        }
        profiler.stage_add(STAGE_MinimumSpanningTree,true);

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        coordf_t branch_radius_temp = 0;
        coordf_t max_y = std::numeric_limits<coordf_t>::min();
        draw_layer_mst(std::to_string(ts_layer->print_z), spanning_trees, m_object->get_layer(layer_nr)->lslices);
#endif
        for (size_t group_index = 0; group_index < nodes_per_part.size(); group_index++)
        {
            const MinimumSpanningTree& mst = spanning_trees[group_index];
            //In the first pass, merge all nodes that are close together.
            std::unordered_set<Node*> to_delete;
            for (const std::pair<const Point, Node*>& entry : nodes_per_part[group_index])
            {
                Node* p_node = entry.second;
                Node& node = *p_node;
                if (to_delete.find(p_node) != to_delete.end())
                {
                    continue; //Delete this node (don't create a new node for it on the next layer).
                }
                const std::vector<Point>& neighbours = mst.adjacent_nodes(node.position);
                if (node.type == ePolygon) {
                    // Remove all neighbours that are completely inside the polygon and merge them into this node.
                    for (const Point &neighbour : neighbours) {
                        Node *    neighbour_node          = nodes_per_part[group_index][neighbour];
                        coord_t    neighbour_radius = scale_(calc_branch_radius(branch_radius, neighbour_node->dist_mm_to_top, diameter_angle_scale_factor));
                        Point     pt_north = neighbour + Point(0, neighbour_radius), pt_south = neighbour - Point(0, neighbour_radius),
                              pt_west = neighbour - Point(neighbour_radius, 0), pt_east = neighbour + Point(neighbour_radius, 0);
                        if (is_inside_ex(*node.overhang, neighbour) && is_inside_ex(*node.overhang, pt_north) && is_inside_ex(*node.overhang, pt_south)
                            && is_inside_ex(*node.overhang, pt_west) && is_inside_ex(*node.overhang, pt_east)){
                            node.distance_to_top           = std::max(node.distance_to_top, neighbour_node->distance_to_top);
                            node.support_roof_layers_below = std::max(node.support_roof_layers_below, neighbour_node->support_roof_layers_below);
                            node.dist_mm_to_top            = std::max(node.dist_mm_to_top, neighbour_node->dist_mm_to_top);
                            node.merged_neighbours.push_front(neighbour_node);
                            node.merged_neighbours.insert(node.merged_neighbours.end(), neighbour_node->merged_neighbours.begin(), neighbour_node->merged_neighbours.end());
                            node.is_merged = true;
                            to_delete.insert(neighbour_node);
                        }
                    }
                }
                else if (neighbours.size() == 1 && vsize2_with_unscale(neighbours[0] - node.position) < max_move_distance2 && mst.adjacent_nodes(neighbours[0]).size() == 1 &&
                           nodes_per_part[group_index][neighbours[0]]->type!=ePolygon) // We have just two nodes left, and they're very close, and the only neighbor is not ePolygon
                {
                    //Insert a completely new node and let both original nodes fade.
                    Point next_position = (node.position + neighbours[0]) / 2; //Average position of the two nodes.

                    const coordf_t branch_radius_node = calc_branch_radius(branch_radius, node.dist_mm_to_top, diameter_angle_scale_factor);

                    auto avoid_layer = m_ts_data->get_avoidance(branch_radius_node, layer_nr_next);
                    if (group_index == 0)
                    {
                        //Avoid collisions.
                        const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; //100 micron extra for rounding errors.
                        move_out_expolys(avoid_layer, next_position, radius_sample_resolution + EPSILON, max_move_between_samples);
                    }

                    Node* neighbour = nodes_per_part[group_index][neighbours[0]];
                    Node* node_;
                    if (p_node->parent && neighbour->parent)
                        node_ = (node.dist_mm_to_top >= neighbour->dist_mm_to_top && p_node->parent) ? p_node : neighbour;
                    else
                        node_ = p_node->parent ? p_node : neighbour;
                    // Make sure the next pass doesn't drop down either of these (since that already happened).
                    node_->merged_neighbours.push_front(node_ == p_node ? neighbour : p_node);
                    const bool to_buildplate = !is_inside_ex(m_ts_data->get_avoidance(0, layer_nr_next), next_position);
                    Node *     next_node     = new Node(next_position, node_->distance_to_top + 1, layer_nr_next, node_->support_roof_layers_below-1, to_buildplate, node_,
                                               print_z_next, height_next);
                    next_node->movement = next_position - node.position;
                    get_max_move_dist(next_node);
                    next_node->is_merged     = true;
                    contact_nodes[layer_nr_next].push_back(next_node);


                    to_delete.insert(neighbour);
                    to_delete.insert(p_node);
                }
                else if (neighbours.size() > 1) //Don't merge leaf nodes because we would then incur movement greater than the maximum move distance.
                {
                    //Remove all neighbours that are too close and merge them into this node.
                    for (const Point& neighbour : neighbours)
                    {
                        if (vsize2_with_unscale(neighbour - node.position) < /*max_move_distance2*/get_max_move_dist(&node,2))
                        {
                            Node* neighbour_node = nodes_per_part[group_index][neighbour];
                            if (neighbour_node->type == ePolygon) continue;

                            node.distance_to_top = std::max(node.distance_to_top, neighbour_node->distance_to_top);
                            node.support_roof_layers_below = std::max(node.support_roof_layers_below, neighbour_node->support_roof_layers_below);
                            node.dist_mm_to_top            = std::max(node.dist_mm_to_top, neighbour_node->dist_mm_to_top);
                            node.merged_neighbours.push_front(neighbour_node);
                            node.merged_neighbours.insert(node.merged_neighbours.end(), neighbour_node->merged_neighbours.begin(), neighbour_node->merged_neighbours.end());
                            node.is_merged = true;
                            to_delete.insert(neighbour_node);
                        }
                    }
                }
            }

            //In the second pass, move all middle nodes.
            for (const std::pair<const Point, Node*>& entry : nodes_per_part[group_index])
            {
                Node* p_node = entry.second;
                const Node& node = *p_node;
                if (to_delete.find(p_node) != to_delete.end())
                {
                    continue;
                }
                if (node.type == ePolygon) {
                    // polygon node do not merge or move
                    const bool to_buildplate = !is_inside_ex(m_ts_data->m_layer_outlines[layer_nr], p_node->position);
                    Node *     next_node = new Node(p_node->position, p_node->distance_to_top + 1, layer_nr_next, p_node->support_roof_layers_below - 1, to_buildplate,
                                               p_node, print_z_next, height_next);
                    next_node->max_move_dist = 0;
                    next_node->is_merged     = false;
                    contact_nodes[layer_nr_next].emplace_back(next_node);
                    continue;
                }

                //If the branch falls completely inside a collision area (the entire branch would be removed by the X/Y offset), delete it.
                if (group_index > 0 && is_inside_ex(m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr), node.position))
                {
                    const coordf_t branch_radius_node = calc_branch_radius(branch_radius, node.dist_mm_to_top, diameter_angle_scale_factor);
                    Point to_outside = projection_onto(m_ts_data->get_collision(m_ts_data->m_xy_distance, layer_nr), node.position);
                    double dist2_to_outside = vsize2_with_unscale(node.position - to_outside);
                    if (dist2_to_outside >= branch_radius_node * branch_radius_node) //Too far inside.
                    {
                        if (support_on_buildplate_only)
                        {
                            unsupported_branch_leaves.push_front({ layer_nr, p_node });
                        }
                        else {
                            Node* pn = p_node;
                            for (int i = 0; i <= bottom_interface_layers && pn; i++, pn = pn->parent)
                                pn->support_floor_layers_above = bottom_interface_layers - i + 1; // +1 so the parent node has support_floor_layers_above=2
                            to_delete.insert(p_node);
                        }
                        continue;
                    }
                    // if the link between parent and current is cut by contours, mark current as bottom contact node
                    if (p_node->parent && intersection_ln({p_node->position, p_node->parent->position}, layer_contours).empty()==false)
                    {
                        Node* pn = p_node->parent;
                        for (int i = 0; i <= bottom_interface_layers && pn; i++, pn = pn->parent)
                            pn->support_floor_layers_above = bottom_interface_layers - i + 1;
                        to_delete.insert(p_node);
                        continue;
                    }
                }
                Point next_layer_vertex = node.position;
                Point move_to_neighbor_center;
                std::vector<Point>       moves;
                std::vector<float>       weights;
                const std::vector<Point> neighbours = mst.adjacent_nodes(node.position);
                // 1. do not merge neighbors under 5mm
                // 2. Only merge node with single neighbor in distance between [max_move_distance, 10mm/layer_height]
                float dist2_to_first_neighbor = neighbours.empty() ? 0 : vsize2_with_unscale(neighbours[0] - node.position);
                if (ts_layer->print_z > DO_NOT_MOVER_UNDER_MM &&
                    (neighbours.size() > 1 || (neighbours.size() == 1 && dist2_to_first_neighbor >= max_move_distance2))) // Only nodes that aren't about to collapse.
                {
                    // Move towards the average position of all neighbours.
                    Point sum_direction(0, 0);
                    for (const Point &neighbour : neighbours) {
                        // do not move to the neighbor to be deleted
                        Node *neighbour_node = nodes_per_part[group_index][neighbour];
                        if (to_delete.find(neighbour_node) != to_delete.end()) continue; 

                        Point direction = neighbour - node.position;
                        // do not move to neighbor that's too far away (即使以最大速度移动，在接触热床之前都无法汇聚)
                        float dist2_to_neighbor = vsize2_with_unscale(direction);

                        coordf_t branch_bottom_radius = calc_branch_radius(branch_radius, node.dist_mm_to_top + node.print_z, diameter_angle_scale_factor);
                        coordf_t neighbour_bottom_radius = calc_branch_radius(branch_radius, neighbour_node->dist_mm_to_top + neighbour_node->print_z, diameter_angle_scale_factor);
                        double max_converge_distance = tan_angle * (ts_layer->print_z - DO_NOT_MOVER_UNDER_MM) + std::max(branch_bottom_radius, neighbour_bottom_radius);
                        if (dist2_to_neighbor > max_converge_distance * max_converge_distance) continue;

                        if (is_line_cut_by_contour(node.position, neighbour)) continue;

                        if (!is_strong)
                            sum_direction += direction * (1 / dist2_to_neighbor);
                        else
                            sum_direction += direction;                        
                    }

                    if (!is_strong)
                        move_to_neighbor_center = sum_direction;
                    else {
                        if (vsize2_with_unscale(sum_direction) <= max_move_distance2) {
                            move_to_neighbor_center = sum_direction;
                        } else {
                            move_to_neighbor_center = normal(sum_direction, scale_(get_max_move_dist(&node)));
                        }
                    }
                }

				const coordf_t branch_radius_node = calc_branch_radius(branch_radius, node.dist_mm_to_top/*+node.print_z*/, diameter_angle_scale_factor);
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
                if (node.position(1) > max_y) {
                    max_y              = node.position(1);
                    branch_radius_temp = branch_radius_node;
                }
#endif
                auto avoid_layer = m_ts_data->get_avoidance(branch_radius_node, layer_nr_next);

                Point  to_outside         = projection_onto(avoid_layer, node.position);
                Point  direction_to_outer = to_outside - node.position;
                double dist2_to_outer     = vsize2_with_unscale(direction_to_outer);
                // don't move if
                // 1) line of node and to_outside is cut by contour (means supports may intersect with object)
                // 2) it's impossible to move to build plate
                if (is_line_cut_by_contour(node.position, to_outside) || dist2_to_outer > max_move_distance2 * SQ(layer_nr) ||
                    !is_inside_ex(avoid_layer, node.position)) {
                    // try move to outside of lower layer instead
                    Point candidate_vertex = node.position;
                    const coordf_t max_move_between_samples = max_move_distance + radius_sample_resolution + EPSILON; // 100 micron extra for rounding errors.
                    bool           is_outside               = move_out_expolys(avoid_layer, candidate_vertex, max_move_between_samples, max_move_between_samples);
                    if (is_outside) {
                        direction_to_outer = candidate_vertex - node.position;
                        dist2_to_outer    = vsize2_with_unscale(direction_to_outer);
                    } else {
                        direction_to_outer = Point(0, 0);
                        dist2_to_outer     = 0;
                    }
                }
                // move to the averaged direction of neighbor center and contour edge if they are roughly same direction
                Point movement;
                if (!is_strong)
                    movement = move_to_neighbor_center*2 + (dist2_to_outer > EPSILON ? direction_to_outer * (1 / dist2_to_outer) : Point(0, 0));
                else {
                    if (movement.dot(move_to_neighbor_center) >= 0.2 || move_to_neighbor_center == Point(0, 0))
                        movement = direction_to_outer + move_to_neighbor_center;
                    else
                        movement = move_to_neighbor_center; // otherwise move to neighbor center first
                }

                if (vsize2_with_unscale(movement) > get_max_move_dist(&node,2))
                    movement = normal(movement, scale_(get_max_move_dist(&node)));

                // add momentum to force smooth movement
                //movement = movement * 0.5 + p_node->movement * 0.5;

                next_layer_vertex += movement;

                if (group_index == 0) {
                    // Avoid collisions.
                    const coordf_t max_move_between_samples = get_max_move_dist(&node, 1) + radius_sample_resolution + EPSILON; // 100 micron extra for rounding errors.
                    bool           is_outside               = move_out_expolys(avoid_layer, next_layer_vertex, radius_sample_resolution + EPSILON, max_move_between_samples);
                    if (!is_outside) {
                        Point candidate_vertex = node.position;
                        is_outside             = move_out_expolys(avoid_layer, candidate_vertex, radius_sample_resolution + EPSILON, max_move_between_samples);
                        if (is_outside) { next_layer_vertex = candidate_vertex; }
                    }
                }

                const bool to_buildplate = !is_inside_ex(m_ts_data->m_layer_outlines[layer_nr], next_layer_vertex);// !is_inside_ex(m_ts_data->get_avoidance(m_ts_data->m_xy_distance, layer_nr - 1), next_layer_vertex);
                Node *     next_node     = new Node(next_layer_vertex, node.distance_to_top + 1, layer_nr_next, node.support_roof_layers_below - 1, to_buildplate, p_node,
                    print_z_next, height_next);
                next_node->movement  = movement;
                get_max_move_dist(next_node);
                next_node->is_merged     = false;
                contact_nodes[layer_nr_next].push_back(next_node);
            }
        }

#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        if (contact_nodes[layer_nr].empty() == false) {
            draw_contours_and_nodes_to_svg((boost::format("%.2f") % contact_nodes[layer_nr][0]->print_z).str(), m_ts_data->get_avoidance(0, layer_nr),
                                           m_ts_data->get_avoidance(branch_radius_temp, layer_nr),
                                           m_ts_data->m_layer_outlines_below[layer_nr],
            contact_nodes[layer_nr], contact_nodes[layer_nr_next], "contact_points", { "overhang","avoid","outline" }, { "blue","red","yellow" });

            BOOST_LOG_TRIVIAL(debug) << "drop_nodes layer " << layer_nr << ", print_z=" << ts_layer->print_z;
            for (size_t i = 0; i < std::min(size_t(5), contact_nodes[layer_nr].size()); i++) {
                auto &node = contact_nodes[layer_nr][i];
                BOOST_LOG_TRIVIAL(debug) << "\t node " << i << ", pos=" << node->position << ", move = " << node->movement << ", is_merged=" << node->is_merged;
            }
        }
#endif

        // Prune all branches that couldn't find support on either the model or the buildplate (resulting in 'mid-air' branches).
        for (;! unsupported_branch_leaves.empty(); unsupported_branch_leaves.pop_back())
        {
            const auto& entry = unsupported_branch_leaves.back();
            Node* i_node = entry.second;
            for (; i_node != nullptr; i_node = i_node->parent)
            {
                size_t i_layer = i_node->obj_layer_nr;
                std::vector<Node*>::iterator to_erase = std::find(contact_nodes[i_layer].begin(), contact_nodes[i_layer].end(), i_node);
                if (to_erase != contact_nodes[i_layer].end())
                {
                    // update the parent-child chain
                    if(i_node->parent)
                        i_node->parent->child = i_node->child;
                    if(i_node->child)
                        i_node->child->parent = i_node->parent;
                    contact_nodes[i_layer].erase(to_erase);
                    to_free_node_set.insert(i_node);

                    for (Node* neighbour : i_node->merged_neighbours)
                    {
                        unsupported_branch_leaves.push_front({ i_layer, neighbour });
                    }
                }
            }
        }
    }
    
    BOOST_LOG_TRIVIAL(debug) << "after m_avoidance_cache.size()=" << m_ts_data->m_avoidance_cache.size();

    for (Node *node : to_free_node_set)
    {
        delete node;
    }
    to_free_node_set.clear();
}

void TreeSupport::smooth_nodes(std::vector<std::vector<Node *>> &contact_nodes)
{
    for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
        std::vector<Node *> &curr_layer_nodes = contact_nodes[layer_nr];
        if (curr_layer_nodes.empty()) continue;
        for (Node *node : curr_layer_nodes) {
            node->is_processed = false;
            if (layer_nr == 0) node->is_merged = true;  // nodes on plate are also merged nodes
        }
    }

    for (int layer_nr = 0; layer_nr< contact_nodes.size(); layer_nr++) {
        std::vector<Node *> &curr_layer_nodes = contact_nodes[layer_nr];
        if (curr_layer_nodes.empty()) continue;
        for (Node *node : curr_layer_nodes) {
            if (!node->is_processed) {
                std::vector<Point> pts;
                std::vector<Node *>   branch;
                Node *              p_node = node;
                // add a fixed head
                if (node->child) {
                    pts.push_back(p_node->child->position);
                    branch.push_back(p_node->child);
                }
                do {
                    pts.push_back(p_node->position);
                    branch.push_back(p_node);
                    p_node = p_node->parent;
                } while (p_node && !p_node->is_processed);
                if (pts.size() < 3) continue;

                std::vector<Point> pts1 = pts;
                // TODO here we assume layer height gap is constant. If not true, need to consider height jump
                const int iterations = 100;
                for (size_t k = 0; k < iterations; k++) {
                    for (size_t i = 1; i < pts.size() - 1; i++) {
                        size_t   i2 = i >= 2 ? i - 2 : 0;
                        size_t   i3 = i < pts.size() - 2 ? i + 2 : pts.size() - 1;
                        Point pt  = (pts[i2] + pts[i - 1] + pts[i] + pts[i + 1] + pts[i3]) / 5;
                        pts1[i] = pt;
                        if (k == iterations - 1) {
                            branch[i]->position = pt;
                            branch[i]->movement = (pts[i + 1] - pts[i - 1]) / 2;
                            branch[i]->is_processed = true;
                        }
                    }
                    if (k < iterations - 1)
                        std::swap(pts, pts1);
                }
            }
        }
    }
    // save tree structure for viewing in python
    auto& tree_nodes = m_ts_data->tree_nodes;
    std::map<Node*, int> ptr2idx;
    std::map<int, Node*> idx2ptr;
    for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
        std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
        for (Node* node : curr_layer_nodes) {
            ptr2idx.emplace(node, tree_nodes.size());
            idx2ptr.emplace(tree_nodes.size(), node);
            tree_nodes.emplace_back(node->position, node->print_z);
        }
    }
    for (size_t i = 0; i < tree_nodes.size(); i++) {
        TreeNode& tree_node = tree_nodes[i];
        Node* p_node = idx2ptr[i];
        if (p_node->child)
            tree_node.children.push_back(ptr2idx[p_node->child]);
        if(p_node->parent)
            tree_node.parents.push_back(ptr2idx[p_node->parent]);
    }
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
    nlohmann::json jj;
    for (size_t i = 0; i < tree_nodes.size(); i++) {
        nlohmann::json j;
        j["pos"] = tree_nodes[i].pos;
        j["children"] = tree_nodes[i].children;
        j["linked"] = !(tree_nodes[i].pos.z() > 0.205 && tree_nodes[i].children.empty());
        jj.push_back(j);
    }
    
    std::ofstream ofs("tree_nodes.json");
    ofs << jj.dump();
    ofs.close();
#endif
}

void TreeSupport::adjust_layer_heights(std::vector<std::vector<Node*>>& contact_nodes)
{
    if (contact_nodes.empty())
        return;

    const PrintConfig& print_config = m_object->print()->config();
    const PrintObjectConfig& config = m_object->config();
    // don't merge layers for Vine support, or the branches will be unsmooth
    // TODO can we merge layers in a way that guaranttees smoothness?
    if (!print_config.independent_support_layer_height || is_slim) {
        for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
            std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
            for (Node* node : curr_layer_nodes) {
                node->print_z = m_object->get_layer(layer_nr)->print_z;
                node->height = m_object->get_layer(layer_nr)->height;
            }
        }
        return;
    }

    // extreme layer_id
    std::vector<int> extremes;
    const coordf_t layer_height = config.layer_height.value;
    const coordf_t max_layer_height = m_slicing_params.max_layer_height;
    const size_t bot_intf_layers = config.support_interface_bottom_layers.value;
    const size_t top_intf_layers = config.support_interface_top_layers.value;

    // if already using max layer height, no need to adjust
    if (layer_height == max_layer_height) return;

    extremes.push_back(0);
    for (Node* node : contact_nodes[0]) {
        node->print_z = m_object->get_layer(0)->print_z;
        node->height = m_object->get_layer(0)->height;
    }

    for (int layer_nr = 1; layer_nr < contact_nodes.size(); layer_nr++) {
        std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
        for (Node* node : curr_layer_nodes) {
            if (node->support_roof_layers_below >0 || node->support_floor_layers_above == bot_intf_layers) {
                extremes.push_back(layer_nr);
                break;
            }
        }

        if (extremes.back() == layer_nr) {
            // contact layer use the same print_z and layer height with object layer
            for (Node* node : curr_layer_nodes) {
                node->print_z = m_object->get_layer(layer_nr)->print_z;
                node->height = m_object->get_layer(layer_nr)->height;
            }
        }
    }

    // schedule new layer heights and print_z
    for (size_t idx_extreme = 0; idx_extreme < extremes.size(); idx_extreme++) {
        int extr2_layer_nr = extremes[idx_extreme];
        coordf_t extr2z = m_object->get_layer(extr2_layer_nr)->bottom_z();
        int extr1_layer_nr = idx_extreme == 0 ? -1 : extremes[idx_extreme - 1];
        coordf_t extr1z = idx_extreme == 0 ? 0.f : m_object->get_layer(extr1_layer_nr)->print_z;
        coordf_t dist = extr2z - extr1z;

        // Insert intermediate layers.
        size_t n_layers_extra = size_t(ceil(dist / m_slicing_params.max_suport_layer_height));
        if (n_layers_extra <= 1)
            continue;

        coordf_t step = dist / coordf_t(n_layers_extra);
        coordf_t print_z = extr1z + step;
        assert(step >= layer_height - EPSILON);
        for (int layer_nr = extr1_layer_nr + 1; layer_nr < extr2_layer_nr; layer_nr++) {
            std::vector<Node*>& curr_layer_nodes = contact_nodes[layer_nr];
            if (curr_layer_nodes.empty()) continue;

            if (std::abs(print_z - curr_layer_nodes[0]->print_z) < step / 2 + EPSILON) {
                for (Node* node : curr_layer_nodes) {
                    node->print_z = print_z;
                    node->height = step;
                }
                print_z += step;
            }
            else {
                // can't clear curr_layer_nodes, or the model will have empty layers
                for (Node* node : curr_layer_nodes) {
                    node->print_z = 0.0;
                    node->height  = 0.0;
                }
            }
        }
    }
}

std::vector<LayerHeightData> TreeSupport::plan_layer_heights(std::vector<std::vector<Node *>> &contact_nodes)
{
    const PrintObjectConfig& config = m_object->config();
    const PrintConfig &      print_config     = m_object->print()->config();
    const coordf_t max_layer_height = m_slicing_params.max_layer_height;
    const coordf_t layer_height = config.layer_height.value;
    coordf_t z_distance_top = m_slicing_params.gap_support_object;
    // BBS: add extra distance if thick bridge is enabled
    // Note: normal support uses print_z, but tree support uses integer layers, so we need to subtract layer_height
    if (!m_slicing_params.soluble_interface && m_object_config->thick_bridges) {
        z_distance_top += m_object->layers()[0]->regions()[0]->region().bridging_height_avg(m_object->print()->config()) - layer_height;
    }
    const size_t support_roof_layers = config.support_interface_top_layers.value;
    const int z_distance_top_layers = round_up_divide(scale_(z_distance_top), scale_(layer_height)) + 1;
    std::vector<LayerHeightData> layer_heights(contact_nodes.size());
    std::vector<int> bounds;

    if (!config.tree_support_adaptive_layer_height || layer_height == max_layer_height || !print_config.independent_support_layer_height) {
        for (int layer_nr = 0; layer_nr < contact_nodes.size(); layer_nr++) {
            layer_heights[layer_nr] = {m_object->get_layer(layer_nr)->print_z, m_object->get_layer(layer_nr)->height, layer_nr > 0 ? size_t(layer_nr - 1) : 0};
        }
        return layer_heights;
    }

    bounds.push_back(0);
    // Keep first layer still
    layer_heights[0] = {m_object->get_layer(0)->print_z, m_object->get_layer(0)->height, 0};
    // Collect top contact layers
    for (int layer_nr = 1; layer_nr < contact_nodes.size(); layer_nr++)
    {
        if (!contact_nodes[layer_nr].empty())
            for (int i = 0; i < support_roof_layers + z_distance_top_layers + 1; i++) {
                if (layer_nr - i > 0) {
                    bounds.push_back(layer_nr - i);
                    layer_heights[layer_nr - i].print_z = m_object->get_layer(layer_nr - i)->print_z;
                    layer_heights[layer_nr - i].height = m_object->get_layer(layer_nr - i)->height;
                } 
                else {
                    break;
                }

            }
    }
    std::set<int> s(bounds.begin(), bounds.end());
    bounds.assign(s.begin(), s.end());

    for (size_t idx_extreme = 0; idx_extreme < bounds.size(); idx_extreme++) {
        int extr2_layer_nr = bounds[idx_extreme];
        coordf_t extr2z = m_object->get_layer(extr2_layer_nr)->bottom_z();
        int extr1_layer_nr = idx_extreme == 0 ? -1 : bounds[idx_extreme - 1];
        coordf_t extr1z = idx_extreme == 0 ? 0.f : m_object->get_layer(extr1_layer_nr)->print_z;
        coordf_t dist = extr2z - extr1z;

        // Insert intermediate layers.
        size_t n_layers_extra = size_t(ceil(dist / (m_slicing_params.max_suport_layer_height + EPSILON)));
        int actual_internel_layers = extr2_layer_nr - extr1_layer_nr - 1;
        int extr_layers_left = extr2_layer_nr - extr1_layer_nr - n_layers_extra - 1;
        if (n_layers_extra < 1)
            continue;

        coordf_t step = dist / coordf_t(n_layers_extra);
        coordf_t print_z = extr1z + step;
        assert(step >= layer_height - EPSILON);
        for (int layer_nr = extr1_layer_nr + 1; layer_nr < extr2_layer_nr; layer_nr++) {
            // if (curr_layer_nodes.empty()) continue;
            if (std::abs(print_z - m_object->get_layer(layer_nr)->print_z) < step / 2 + EPSILON || extr_layers_left < 1) {
                layer_heights[layer_nr].print_z = print_z;
                layer_heights[layer_nr].height = step;
                print_z += step;
            }
            else {
                // can't clear curr_layer_nodes, or the model will have empty layers
                layer_heights[layer_nr].print_z = 0.0;
                layer_heights[layer_nr].height = 0.0;
                extr_layers_left--;
            }
        }
    }

    // fill in next_layer_nr
    int i = layer_heights.size() - 1, j = i;
    for (; j >= 0; i = j) {
        if (layer_heights[i].height < EPSILON) {
            j--;
            continue;
        }
        for (j = i - 1; j >= 0; j--) {
            if (layer_heights[j].height > EPSILON) {
                layer_heights[i].next_layer_nr = j;
                break;
            }
        }
        BOOST_LOG_TRIVIAL(trace) << "plan_layer_heights print_z, height, layer_nr->next_layer_nr: " << layer_heights[i].print_z << " " << layer_heights[i].height << "   "
            << i << "->" << layer_heights[i].next_layer_nr << std::endl;
    }

    return layer_heights;
}

void TreeSupport::generate_contact_points(std::vector<std::vector<TreeSupport::Node*>>& contact_nodes)
{
    const PrintObjectConfig &config = m_object->config();
    const coordf_t point_spread = scale_(config.tree_support_branch_distance.value);

    //First generate grid points to cover the entire area of the print.
    BoundingBox bounding_box = m_object->bounding_box();
    const Point bounding_box_size = bounding_box.max - bounding_box.min;
    constexpr double rotate_angle = 22.0 / 180.0 * M_PI;

    const auto center = bounding_box_middle(bounding_box);
    const auto sin_angle = std::sin(rotate_angle);
    const auto cos_angle = std::cos(rotate_angle);
    const Point rotated_dims = Point(
        bounding_box_size(0) * cos_angle + bounding_box_size(1) * sin_angle,
        bounding_box_size(0) * sin_angle + bounding_box_size(1) * cos_angle) / 2;

    std::vector<Point> grid_points;
    for (auto x = -rotated_dims(0); x < rotated_dims(0); x += point_spread) {
        for (auto y = -rotated_dims(1); y < rotated_dims(1); y += point_spread) {
            Point pt(x, y);
            pt.rotate(cos_angle, sin_angle);
            pt += center;
            if (bounding_box.contains(pt)) {
                grid_points.push_back(pt);
            }
        }
    }

    const coordf_t layer_height = config.layer_height.value;
    coordf_t z_distance_top = m_slicing_params.gap_support_object;
    // BBS: add extra distance if thick bridge is enabled
    // Note: normal support uses print_z, but tree support uses integer layers, so we need to subtract layer_height
    if (!m_slicing_params.soluble_interface && m_object_config->thick_bridges) {
        z_distance_top += m_object->layers()[0]->regions()[0]->region().bridging_height_avg(m_object->print()->config()) - layer_height;
    }
    const int z_distance_top_layers = round_up_divide(scale_(z_distance_top), scale_(layer_height)) + 1; //Support must always be 1 layer below overhang.

    size_t support_roof_layers = config.support_interface_top_layers.value;
    if (support_roof_layers > 0)
        support_roof_layers += 1; // BBS: add a normal support layer below interface (if we have interface)
    coordf_t  thresh_angle = std::min(89.f, config.support_threshold_angle.value < EPSILON ? 30.f : config.support_threshold_angle.value);
    coordf_t  half_overhang_distance = scale_(tan(thresh_angle * M_PI / 180.0) * layer_height / 2);

    // fix bug of generating support for very thin objects
    if (m_object->layers().size() <= z_distance_top_layers + 1)
        return;

    m_highest_overhang_layer = 0;
    int      nonempty_layers = 0;
    std::vector<Slic3r::Vec3f> all_nodes;
    for (size_t layer_nr = 1; layer_nr < m_object->layers().size(); layer_nr++)
    {
        if (m_object->print()->canceled())
            break;
        auto              ts_layer = m_object->get_support_layer(layer_nr + m_raft_layers);
        const ExPolygons &overhang = ts_layer->overhang_areas;
        auto &          curr_nodes = contact_nodes[layer_nr];
        if (overhang.empty())
            continue;

        m_highest_overhang_layer = std::max(m_highest_overhang_layer, layer_nr);
        auto print_z             = m_object->get_layer(layer_nr)->print_z;
        auto height              = m_object->get_layer(layer_nr)->height;

        for (const ExPolygon &overhang_part : overhang)
        {
            BoundingBox overhang_bounds = get_extents(overhang_part);
            if (support_style==smsTreeHybrid && overhang_part.area() > m_support_params.thresh_big_overhang) {
                Point candidate = overhang_bounds.center();
                if (!overhang_part.contains(candidate))
                    move_inside_expoly(overhang_part, candidate);
                if (!(config.support_on_build_plate_only && is_inside_ex(m_ts_data->m_layer_outlines_below[layer_nr], candidate))) {
                    Node* contact_node = new Node(candidate, -z_distance_top_layers, layer_nr, support_roof_layers + z_distance_top_layers, true, Node::NO_PARENT, print_z,
                        height, z_distance_top);
                    contact_node->type = ePolygon;
                    contact_node->overhang = &overhang_part;
                    curr_nodes.emplace_back(contact_node);
                    continue;
                }
            }

            overhang_bounds.inflated(half_overhang_distance);
            bool added = false; //Did we add a point this way?
            for (Point candidate : grid_points)
            {
                if (overhang_bounds.contains(candidate))
                {
                    // BBS: move_inside_expoly shouldn't be used if candidate is already inside, as it moves point to boundary and the inside is not well supported!
                    bool               is_inside       = is_inside_ex(overhang_part, candidate);
                    if (!is_inside) {
                        constexpr coordf_t distance_inside = 0; // Move point towards the border of the polygon if it is closer than half the overhang distance: Catch points that
                                                                // fall between overhang areas on constant surfaces.
                        move_inside_expoly(overhang_part, candidate, distance_inside, half_overhang_distance);
                        is_inside = is_inside_ex(overhang_part, candidate);
                    }
                    if (is_inside)
                    {
                        // collision radius has to be 0 or the supports are too few at curved slopes
                        //if (!is_inside_ex(m_ts_data->get_collision(0, layer_nr), candidate))
                        {
                            constexpr bool to_buildplate = true;
                            Node *         contact_node  = new Node(candidate, -z_distance_top_layers, layer_nr, support_roof_layers + z_distance_top_layers, to_buildplate,
                                                          Node::NO_PARENT, print_z, height, z_distance_top);
                            contact_node->overhang = &overhang_part;
                            curr_nodes.emplace_back(contact_node);
                            added = true;
                        }
                    }
                }
            }

            if (!added) //If we didn't add any points due to bad luck, we want to add one anyway such that loose parts are also supported.
            {
                auto bbox = overhang_part.contour.bounding_box();
                Points candidates;
                if (ts_layer->overhang_types[&overhang_part] == SupportLayer::Detected)
                    candidates = {bbox.min, bounding_box_middle(bbox), bbox.max};
                else
                    candidates = {bounding_box_middle(bbox)};

                for (Point candidate : candidates) {
                    if (!overhang_part.contains(candidate))
                        move_inside_expoly(overhang_part, candidate);
                    constexpr bool   to_buildplate   = true;
                    Node *contact_node = new Node(candidate, -z_distance_top_layers, layer_nr, support_roof_layers + z_distance_top_layers, to_buildplate, Node::NO_PARENT,
                                                  print_z, height, z_distance_top);
                    contact_node->overhang           = &overhang_part;
                    curr_nodes.emplace_back(contact_node);
                }
            }
            // add supports at corners for both auto and manual overhangs, github #2008
            if (/*ts_layer->overhang_types[&overhang_part] == SupportLayer::Detected*/1) {
                // add points at corners
                auto &points = overhang_part.contour.points;
                int   nSize  = points.size();
                for (int i = 0; i < nSize; i++) {
                    auto pt = points[i];
                    auto v1 = (pt - points[(i - 1 + nSize) % nSize]).cast<double>().normalized();
                    auto v2 = (pt - points[(i + 1) % nSize]).cast<double>().normalized();
                    if (v1.dot(v2) > -0.7) { // angle smaller than 135 degrees
                        Node *contact_node     = new Node(pt, -z_distance_top_layers, layer_nr, support_roof_layers + z_distance_top_layers, true, Node::NO_PARENT, print_z,
                                                      height, z_distance_top);
                        contact_node->overhang = &overhang_part;
                        contact_node->is_corner = true;
                        curr_nodes.emplace_back(contact_node);
                    }
                }
            } 
            if(ts_layer->overhang_types[&overhang_part] == SupportLayer::Enforced || is_slim){
                // remove close points in Enforcers
                // auto above_nodes = contact_nodes[layer_nr - 1];
                if (!curr_nodes.empty() /*&& !above_nodes.empty()*/) {
                    for (auto it = curr_nodes.begin(); it != curr_nodes.end();) {
                        bool is_duplicate = false;
                        if (!(*it)->is_corner) {
                            Slic3r::Vec3f curr_pt((*it)->position(0), (*it)->position(1), scale_((*it)->print_z));
                            for (auto &pt : all_nodes) {
                                auto dif = curr_pt - pt;
                                if (dif.norm() < point_spread / 2) {
                                    delete (*it);
                                    it           = curr_nodes.erase(it);
                                    is_duplicate = true;
                                    break;
                                }
                            }
                        }
                        if (!is_duplicate) it++;
                    }
                }
            }
        }
        if (!curr_nodes.empty()) nonempty_layers++;
        for (auto node : curr_nodes) { all_nodes.emplace_back(node->position(0), node->position(1), scale_(node->print_z)); }
#ifdef SUPPORT_TREE_DEBUG_TO_SVG
        draw_contours_and_nodes_to_svg(std::to_string(print_z), overhang, m_ts_data->m_layer_outlines_below[layer_nr], {},
            contact_nodes[layer_nr], {}, "init_contact_points", { "overhang","outlines","" });
#endif
    }
    int nNodes = all_nodes.size();
    avg_node_per_layer = nodes_angle = 0;
    if (nNodes > 0) {
        avg_node_per_layer = nNodes / nonempty_layers;
        // get orientation of nodes by line fitting
        // line: y=kx+b, where
        //       k=tan(nodes_angle)=(n\sum{xy}-\sum{x}\sum{y})/(n\sum{x^2}-\sum{x}^2)
        float mx = 0, my = 0, mxy = 0, mx2 = 0;
        for (auto &pt : all_nodes) {
            float x = unscale_(pt(0));
            float y = unscale_(pt(1));
            mx += x;
            my += y;
            mxy += x * y;
            mx2 += x * x;
        }
        nodes_angle = atan2(nNodes * mxy - mx * my, nNodes * mx2 - SQ(mx));
        
        BOOST_LOG_TRIVIAL(info) << "avg_node_per_layer=" << avg_node_per_layer << ", nodes_angle=" << nodes_angle;
    }
}

void TreeSupport::insert_dropped_node(std::vector<Node*>& nodes_layer, Node* p_node)
{
    std::vector<Node*>::iterator conflicting_node_it = std::find(nodes_layer.begin(), nodes_layer.end(), p_node);
    if (conflicting_node_it == nodes_layer.end()) //No conflict.
    {
        nodes_layer.emplace_back(p_node);
        return;
    }

    Node* conflicting_node = *conflicting_node_it;
    conflicting_node->distance_to_top = std::max(conflicting_node->distance_to_top, p_node->distance_to_top);
    conflicting_node->support_roof_layers_below = std::max(conflicting_node->support_roof_layers_below, p_node->support_roof_layers_below);
}

TreeSupportData::TreeSupportData(const PrintObject &object, coordf_t xy_distance, coordf_t max_move, coordf_t radius_sample_resolution)
    : m_xy_distance(xy_distance), m_max_move(max_move), m_radius_sample_resolution(radius_sample_resolution)
{
    for (std::size_t layer_nr  = 0; layer_nr < object.layers().size(); ++layer_nr)
    {
        const Layer* layer = object.get_layer(layer_nr);
        m_layer_outlines.push_back(ExPolygons());
        ExPolygons& outline = m_layer_outlines.back();
        for (const ExPolygon& poly : layer->lslices) {
            poly.simplify(scale_(m_radius_sample_resolution), &outline);
        }

        if (layer_nr == 0)
            m_layer_outlines_below.push_back(outline);
        else
            m_layer_outlines_below.push_back(union_ex(m_layer_outlines_below.end()[-1], outline));
    }
}

const ExPolygons& TreeSupportData::get_collision(coordf_t radius, size_t layer_nr) const
{
    profiler.tic();
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_nr};
    const auto it = m_collision_cache.find(key);
    const ExPolygons& collision = it != m_collision_cache.end() ? it->second : calculate_collision(key);
    profiler.stage_add(STAGE_get_collision, true);
    return collision;
}

const ExPolygons& TreeSupportData::get_avoidance(coordf_t radius, size_t layer_nr, int recursions) const
{
    profiler.tic();
    radius = ceil_radius(radius);
    RadiusLayerPair key{radius, layer_nr, recursions };
    const auto it = m_avoidance_cache.find(key);
    const ExPolygons& avoidance = it != m_avoidance_cache.end() ? it->second : calculate_avoidance(key);

    profiler.stage_add(STAGE_GET_AVOIDANCE, true);
    return avoidance;
}

Polygons TreeSupportData::get_contours(size_t layer_nr) const
{
    Polygons contours;
    for (const ExPolygon& expoly : m_layer_outlines[layer_nr]) {
        contours.push_back(expoly.contour);
    }

    return contours;
}

Polygons TreeSupportData::get_contours_with_holes(size_t layer_nr) const
{
    Polygons contours;
    for (const ExPolygon& expoly : m_layer_outlines[layer_nr]) {
        for(int i=0;i<expoly.num_contours();i++)
            contours.push_back(expoly.contour_or_hole(i));
    }
    return contours;
}

coordf_t TreeSupportData::ceil_radius(coordf_t radius) const
{
#if 1
    size_t factor = (size_t)(radius / m_radius_sample_resolution);
    coordf_t remains = radius - m_radius_sample_resolution * factor;
    if (remains > EPSILON) {
        return radius + m_radius_sample_resolution - remains;
    }
    else {
        return radius;
    }
#else
    coordf_t resolution = m_radius_sample_resolution;
    return ceil(radius / resolution) * resolution;
#endif
}

const ExPolygons& TreeSupportData::calculate_collision(const RadiusLayerPair& key) const
{
    assert(key.layer_nr < m_layer_outlines.size());

    ExPolygons collision_areas = offset_ex(m_layer_outlines[key.layer_nr], scale_(key.radius));
    const auto ret = m_collision_cache.insert({ key, std::move(collision_areas) });
    return ret.first->second;
}

const ExPolygons& TreeSupportData::calculate_avoidance(const RadiusLayerPair& key) const
{
    const auto& radius = key.radius;
    const auto& layer_nr = key.layer_nr;
    BOOST_LOG_TRIVIAL(debug) << "calculate_avoidance on radius=" << radius << ", layer=" << layer_nr<<", recursion="<<key.recursions;
    constexpr auto max_recursion_depth = 100;
    if (key.recursions <= max_recursion_depth*2) {
        if (layer_nr == 0) {
            m_avoidance_cache[key] = get_collision(radius, 0);
            return m_avoidance_cache[key];
        }

        // Avoidance for a given layer depends on all layers beneath it so could have very deep recursion depths if
        // called at high layer heights. We can limit the reqursion depth to N by checking if the layer N
        // below the current one exists and if not, forcing the calculation of that layer. This may cause another recursion
        // if the layer at 2N below the current one but we won't exceed our limit unless there are N*N uncalculated layers
        // below our current one.
        size_t         layer_nr_next       = layer_nr;
        int            layers_below;
        for (layers_below = 0; layers_below < max_recursion_depth && layer_nr_next > 0; layers_below++) { layer_nr_next = layer_heights[layer_nr_next].next_layer_nr; }
        // Check if we would exceed the recursion limit by trying to process this layer
        if (layers_below >= max_recursion_depth && m_avoidance_cache.find({radius, layer_nr_next}) == m_avoidance_cache.end()) {
            // Force the calculation of the layer `max_recursion_depth` below our current one, ignoring the result.
            get_avoidance(radius, layer_nr_next, key.recursions + 1);
        }

        layer_nr_next   = layer_heights[layer_nr].next_layer_nr;
        ExPolygons        avoidance_areas = offset_ex(get_avoidance(radius, layer_nr_next, key.recursions+1), scale_(-m_max_move));
        const ExPolygons &collision       = get_collision(radius, layer_nr);
        avoidance_areas.insert(avoidance_areas.end(), collision.begin(), collision.end());
        avoidance_areas = std::move(union_ex(avoidance_areas));
        auto ret = m_avoidance_cache.insert({ key, std::move(avoidance_areas) });
        //assert(ret.second);
        return ret.first->second;
    } else {
        ExPolygons avoidance_areas = offset_ex(m_layer_outlines_below[layer_nr], scale_(m_xy_distance + radius));
        auto ret = m_avoidance_cache.insert({ key, std::move(avoidance_areas) });
        assert(ret.second);
        return ret.first->second;
    }
}

} //namespace Slic3r
