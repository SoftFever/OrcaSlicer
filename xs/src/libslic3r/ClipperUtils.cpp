#include "ClipperUtils.hpp"
#include "Geometry.hpp"

// #define CLIPPER_UTILS_DEBUG

#ifdef CLIPPER_UTILS_DEBUG
#include "SVG.hpp"
#endif /* CLIPPER_UTILS_DEBUG */

#include <Shiny/Shiny.h>

// Factor to convert from coord_t (which is int32) to an int64 type used by the Clipper library
// for general offsetting (the offset(), offset2(), offset_ex() functions) and for the safety offset,
// which is optionally executed by other functions (union, intersection, diff).
// By the way, is the scalling for offset needed at all?
#define CLIPPER_OFFSET_POWER_OF_2 17
// 2^17=131072
#define CLIPPER_OFFSET_SCALE (1 << CLIPPER_OFFSET_POWER_OF_2)
#define CLIPPER_OFFSET_SCALE_ROUNDING_DELTA ((1 << (CLIPPER_OFFSET_POWER_OF_2 - 1)) - 1)

namespace Slic3r {

#ifdef CLIPPER_UTILS_DEBUG
bool clipper_export_enabled = false;
// For debugging the Clipper library, for providing bug reports to the Clipper author.
bool export_clipper_input_polygons_bin(const char *path, const ClipperLib::Paths &input_subject, const ClipperLib::Paths &input_clip)
{
    FILE *pfile = fopen(path, "wb");
    if (pfile == NULL)
        return false;

    uint32_t sz = uint32_t(input_subject.size());
    fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_subject.size(); ++i) {
        const ClipperLib::Path &path = input_subject[i];
        sz = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(ClipperLib::IntPoint), sz, pfile);
    }
    sz = uint32_t(input_clip.size());
    ::fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_clip.size(); ++i) {
        const ClipperLib::Path &path = input_clip[i];
        sz = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(ClipperLib::IntPoint), sz, pfile);
    }
    ::fclose(pfile);
    return true;

err:
    ::fclose(pfile);
    return false;
}
#endif /* CLIPPER_UTILS_DEBUG */

void scaleClipperPolygon(ClipperLib::Path &polygon)
{
    PROFILE_FUNC();
    for (ClipperLib::Path::iterator pit = polygon.begin(); pit != polygon.end(); ++pit) {
        pit->X <<= CLIPPER_OFFSET_POWER_OF_2;
        pit->Y <<= CLIPPER_OFFSET_POWER_OF_2;
    }
}

void scaleClipperPolygons(ClipperLib::Paths &polygons)
{
    PROFILE_FUNC();
    for (ClipperLib::Paths::iterator it = polygons.begin(); it != polygons.end(); ++it)
        for (ClipperLib::Path::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            pit->X <<= CLIPPER_OFFSET_POWER_OF_2;
            pit->Y <<= CLIPPER_OFFSET_POWER_OF_2;
        }
}

void unscaleClipperPolygon(ClipperLib::Path &polygon)
{
    PROFILE_FUNC();
    for (ClipperLib::Path::iterator pit = polygon.begin(); pit != polygon.end(); ++pit) {
        pit->X += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
        pit->Y += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
        pit->X >>= CLIPPER_OFFSET_POWER_OF_2;
        pit->Y >>= CLIPPER_OFFSET_POWER_OF_2;
    }
}

void unscaleClipperPolygons(ClipperLib::Paths &polygons)
{
    PROFILE_FUNC();
    for (ClipperLib::Paths::iterator it = polygons.begin(); it != polygons.end(); ++it)
        for (ClipperLib::Path::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            pit->X += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
            pit->Y += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
            pit->X >>= CLIPPER_OFFSET_POWER_OF_2;
            pit->Y >>= CLIPPER_OFFSET_POWER_OF_2;
        }
}

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, ExPolygons* expolygons)
{  
  size_t cnt = expolygons->size();
  expolygons->resize(cnt + 1);
  (*expolygons)[cnt].contour = ClipperPath_to_Slic3rMultiPoint<Polygon>(polynode.Contour);
  (*expolygons)[cnt].holes.resize(polynode.ChildCount());
  for (int i = 0; i < polynode.ChildCount(); ++i)
  {
    (*expolygons)[cnt].holes[i] = ClipperPath_to_Slic3rMultiPoint<Polygon>(polynode.Childs[i]->Contour);
    //Add outer polygons contained by (nested within) holes ...
    for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++j)
      AddOuterPolyNodeToExPolygons(*polynode.Childs[i]->Childs[j], expolygons);
  }
}
 
ExPolygons
PolyTreeToExPolygons(ClipperLib::PolyTree& polytree)
{
    ExPolygons retval;
    for (int i = 0; i < polytree.ChildCount(); ++i)
        AddOuterPolyNodeToExPolygons(*polytree.Childs[i], &retval);
    return retval;
}
//-----------------------------------------------------------

template <class T>
T
ClipperPath_to_Slic3rMultiPoint(const ClipperLib::Path &input)
{
    T retval;
    for (ClipperLib::Path::const_iterator pit = input.begin(); pit != input.end(); ++pit)
        retval.points.push_back(Point( (*pit).X, (*pit).Y ));
    return retval;
}

template <class T>
T
ClipperPaths_to_Slic3rMultiPoints(const ClipperLib::Paths &input)
{
    T retval;
    for (ClipperLib::Paths::const_iterator it = input.begin(); it != input.end(); ++it)
        retval.push_back(ClipperPath_to_Slic3rMultiPoint<typename T::value_type>(*it));
    return retval;
}

ExPolygons
ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input)
{
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // perform union
    clipper.AddPaths(input, ClipperLib::ptSubject, true);
    ClipperLib::PolyTree polytree;
    clipper.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);  // offset results work with both EvenOdd and NonZero
    
    // write to ExPolygons object
    return PolyTreeToExPolygons(polytree);
}

ClipperLib::Path
Slic3rMultiPoint_to_ClipperPath(const MultiPoint &input)
{
    ClipperLib::Path retval;
    for (Points::const_iterator pit = input.points.begin(); pit != input.points.end(); ++pit)
        retval.push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
    return retval;
}

ClipperLib::Path
Slic3rMultiPoint_to_ClipperPath_reversed(const Slic3r::MultiPoint &input)
{
    ClipperLib::Path output;
    output.reserve(input.points.size());
    for (Slic3r::Points::const_reverse_iterator pit = input.points.rbegin(); pit != input.points.rend(); ++pit)
        output.push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
    return output;
}

template <class T>
ClipperLib::Paths
Slic3rMultiPoints_to_ClipperPaths(const T &input)
{
    ClipperLib::Paths retval;
    for (typename T::const_iterator it = input.begin(); it != input.end(); ++it)
        retval.push_back(Slic3rMultiPoint_to_ClipperPath(*it));
    return retval;
}

ClipperLib::Paths _offset(ClipperLib::Paths &&input, ClipperLib::EndType endType, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    // scale input
    scaleClipperPolygons(input);
    
    // perform offset
    ClipperLib::ClipperOffset co;
    if (joinType == jtRound)
        co.ArcTolerance = miterLimit;
    else
        co.MiterLimit = miterLimit;
    co.AddPaths(input, joinType, endType);
    ClipperLib::Paths retval;
    co.Execute(retval, delta * float(CLIPPER_OFFSET_SCALE));
    
    // unscale output
    unscaleClipperPolygons(retval);
    return retval;
}

ClipperLib::Paths _offset(ClipperLib::Path &&input, ClipperLib::EndType endType, const float delta, ClipperLib::JoinType joinType, double miterLimit)
{
    ClipperLib::Paths paths;
    paths.push_back(std::move(input));
	return _offset(std::move(paths), endType, delta, joinType, miterLimit);
}

// This is a safe variant of the polygon offset, tailored for a single ExPolygon:
// a single polygon with multiple non-overlapping holes.
// Each contour and hole is offsetted separately, then the holes are subtracted from the outer contours.
ClipperLib::Paths _offset(const Slic3r::ExPolygon &expolygon, const float delta,
    ClipperLib::JoinType joinType, double miterLimit)
{
//    printf("new ExPolygon offset\n");
    // 1) Offset the outer contour.
    const float delta_scaled = delta * float(CLIPPER_OFFSET_SCALE);
    ClipperLib::Paths contours;
    {
        ClipperLib::Path input = Slic3rMultiPoint_to_ClipperPath(expolygon.contour);
        scaleClipperPolygon(input);
        ClipperLib::ClipperOffset co;
        if (joinType == jtRound)
            co.ArcTolerance = miterLimit * double(CLIPPER_OFFSET_SCALE);
        else
            co.MiterLimit = miterLimit;
        co.AddPath(input, joinType, ClipperLib::etClosedPolygon);
        co.Execute(contours, delta_scaled);
    }

    // 2) Offset the holes one by one, collect the results.
    ClipperLib::Paths holes;
    {
        holes.reserve(expolygon.holes.size());
        for (Polygons::const_iterator it_hole = expolygon.holes.begin(); it_hole != expolygon.holes.end(); ++ it_hole) {
            ClipperLib::Path input = Slic3rMultiPoint_to_ClipperPath_reversed(*it_hole);
            scaleClipperPolygon(input);
            ClipperLib::ClipperOffset co;
            if (joinType == jtRound)
                co.ArcTolerance = miterLimit * double(CLIPPER_OFFSET_SCALE);
            else
                co.MiterLimit = miterLimit;
            co.AddPath(input, joinType, ClipperLib::etClosedPolygon);
            ClipperLib::Paths out;
            co.Execute(out, - delta_scaled);
            holes.insert(holes.end(), out.begin(), out.end());
        }
    }

    // 3) Subtract holes from the contours.
    ClipperLib::Paths output;
    {
        ClipperLib::Clipper clipper;
        clipper.Clear();
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }
    
    // 4) Unscale the output.
    unscaleClipperPolygons(output);
    return output;
}

// This is a safe variant of the polygon offset, tailored for a single ExPolygon:
// a single polygon with multiple non-overlapping holes.
// Each contour and hole is offsetted separately, then the holes are subtracted from the outer contours.
ClipperLib::Paths _offset(const Slic3r::ExPolygons &expolygons, const float delta,
    ClipperLib::JoinType joinType, double miterLimit)
{
//    printf("new ExPolygon offset\n");
    const float delta_scaled = delta * float(CLIPPER_OFFSET_SCALE);
    ClipperLib::Paths contours;
    ClipperLib::Paths holes;
    contours.reserve(expolygons.size());
    {
        size_t n_holes = 0;
        for (size_t i = 0; i < expolygons.size(); ++ i)
            n_holes += expolygons[i].holes.size();
        holes.reserve(n_holes);
    }

    for (Slic3r::ExPolygons::const_iterator it_expoly = expolygons.begin(); it_expoly != expolygons.end(); ++ it_expoly) {
        // 1) Offset the outer contour.
        {
            ClipperLib::Path input = Slic3rMultiPoint_to_ClipperPath(it_expoly->contour);
            scaleClipperPolygon(input);
            ClipperLib::ClipperOffset co;
            if (joinType == jtRound)
                co.ArcTolerance = miterLimit * double(CLIPPER_OFFSET_SCALE);
            else
                co.MiterLimit = miterLimit;
            co.AddPath(input, joinType, ClipperLib::etClosedPolygon);
            ClipperLib::Paths out;
            co.Execute(out, delta_scaled);
            contours.insert(contours.end(), out.begin(), out.end());
        }

        // 2) Offset the holes one by one, collect the results.
        {
            for (Polygons::const_iterator it_hole = it_expoly->holes.begin(); it_hole != it_expoly->holes.end(); ++ it_hole) {
                ClipperLib::Path input = Slic3rMultiPoint_to_ClipperPath_reversed(*it_hole);
                scaleClipperPolygon(input);
                ClipperLib::ClipperOffset co;
                if (joinType == jtRound)
                    co.ArcTolerance = miterLimit * double(CLIPPER_OFFSET_SCALE);
                else
                    co.MiterLimit = miterLimit;
                co.AddPath(input, joinType, ClipperLib::etClosedPolygon);
                ClipperLib::Paths out;
                co.Execute(out, - delta_scaled);
                holes.insert(holes.end(), out.begin(), out.end());
            }
        }
    }

    // 3) Subtract holes from the contours.
    ClipperLib::Paths output;
    {
        ClipperLib::Clipper clipper;
        clipper.Clear();
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }
    
    // 4) Unscale the output.
    unscaleClipperPolygons(output);
    return output;
}

ClipperLib::Paths
_offset2(const Polygons &polygons, const float delta1, const float delta2,
    const ClipperLib::JoinType joinType, const double miterLimit)
{
    // read input
    ClipperLib::Paths input = Slic3rMultiPoints_to_ClipperPaths(polygons);
    
    // scale input
    scaleClipperPolygons(input);
    
    // prepare ClipperOffset object
    ClipperLib::ClipperOffset co;
    if (joinType == jtRound) {
        co.ArcTolerance = miterLimit;
    } else {
        co.MiterLimit = miterLimit;
    }
    
    // perform first offset
    ClipperLib::Paths output1;
    co.AddPaths(input, joinType, ClipperLib::etClosedPolygon);
    co.Execute(output1, delta1 * float(CLIPPER_OFFSET_SCALE));
    
    // perform second offset
    co.Clear();
    co.AddPaths(output1, joinType, ClipperLib::etClosedPolygon);
    ClipperLib::Paths retval;
    co.Execute(retval, delta2 * float(CLIPPER_OFFSET_SCALE));
    
    // unscale output
    unscaleClipperPolygons(retval);
    return retval;
}

Polygons
offset2(const Polygons &polygons, const float delta1, const float delta2,
    const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Paths output = _offset2(polygons, delta1, delta2, joinType, miterLimit);
    
    // convert into ExPolygons
    return ClipperPaths_to_Slic3rMultiPoints<Polygons>(output);
}

ExPolygons
offset2_ex(const Polygons &polygons, const float delta1, const float delta2,
    const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Paths output = _offset2(polygons, delta1, delta2, joinType, miterLimit);
    
    // convert into ExPolygons
    return ClipperPaths_to_Slic3rExPolygons(output);
}

template <class T>
T
_clipper_do(const ClipperLib::ClipType clipType, const Polygons &subject, 
    const Polygons &clip, const ClipperLib::PolyFillType fillType, const bool safety_offset_)
{
    // read input
    ClipperLib::Paths input_subject = Slic3rMultiPoints_to_ClipperPaths(subject);
    ClipperLib::Paths input_clip    = Slic3rMultiPoints_to_ClipperPaths(clip);
    
    // perform safety offset
    if (safety_offset_) {
        if (clipType == ClipperLib::ctUnion) {
            safety_offset(&input_subject);
        } else {
            safety_offset(&input_clip);
        }
    }
    
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // add polygons
    clipper.AddPaths(input_subject, ClipperLib::ptSubject, true);
    clipper.AddPaths(input_clip,    ClipperLib::ptClip,    true);
    
    // perform operation
    T retval;
    clipper.Execute(clipType, retval, fillType, fillType);
    return retval;
}

ClipperLib::PolyTree
_clipper_do(const ClipperLib::ClipType clipType, const Polylines &subject, 
    const Polygons &clip, const ClipperLib::PolyFillType fillType,
    const bool safety_offset_)
{
    // read input
    ClipperLib::Paths input_subject = Slic3rMultiPoints_to_ClipperPaths(subject);
    ClipperLib::Paths input_clip    = Slic3rMultiPoints_to_ClipperPaths(clip);
    
    // perform safety offset
    if (safety_offset_) safety_offset(&input_clip);
    
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // add polygons
    clipper.AddPaths(input_subject, ClipperLib::ptSubject, false);
    clipper.AddPaths(input_clip,    ClipperLib::ptClip,    true);
    
    // perform operation
    ClipperLib::PolyTree retval;
    clipper.Execute(clipType, retval, fillType, fillType);
    return retval;
}

Polygons
_clipper(ClipperLib::ClipType clipType, const Polygons &subject, 
    const Polygons &clip, bool safety_offset_)
{
    return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_clipper_do<ClipperLib::Paths>(clipType, subject, clip, ClipperLib::pftNonZero, safety_offset_));
}

ExPolygons
_clipper_ex(ClipperLib::ClipType clipType, const Polygons &subject, 
    const Polygons &clip, bool safety_offset_)
{
    ClipperLib::PolyTree polytree = _clipper_do<ClipperLib::PolyTree>(clipType, subject, clip, ClipperLib::pftNonZero, safety_offset_);
    return PolyTreeToExPolygons(polytree);
}

Polylines
_clipper_pl(ClipperLib::ClipType clipType, const Polylines &subject, 
    const Polygons &clip, bool safety_offset_)
{
    ClipperLib::Paths output;
    ClipperLib::PolyTreeToPaths(_clipper_do(clipType, subject, clip, ClipperLib::pftNonZero, safety_offset_), output);
    return ClipperPaths_to_Slic3rMultiPoints<Polylines>(output);
}

Polylines
_clipper_pl(ClipperLib::ClipType clipType, const Polygons &subject, 
    const Polygons &clip, bool safety_offset_)
{
    // transform input polygons into polylines
    Polylines polylines;
    polylines.reserve(subject.size());
    for (Polygons::const_iterator polygon = subject.begin(); polygon != subject.end(); ++polygon)
        polylines.push_back(*polygon);  // implicit call to split_at_first_point()
    
    // perform clipping
    Polylines retval = _clipper_pl(clipType, polylines, clip, safety_offset_);
    
    /* If the split_at_first_point() call above happens to split the polygon inside the clipping area
       we would get two consecutive polylines instead of a single one, so we go through them in order
       to recombine continuous polylines. */
    for (size_t i = 0; i < retval.size(); ++i) {
        for (size_t j = i+1; j < retval.size(); ++j) {
            if (retval[i].points.back().coincides_with(retval[j].points.front())) {
                /* If last point of i coincides with first point of j,
                   append points of j to i and delete j */
                retval[i].points.insert(retval[i].points.end(), retval[j].points.begin()+1, retval[j].points.end());
                retval.erase(retval.begin() + j);
                --j;
            } else if (retval[i].points.front().coincides_with(retval[j].points.back())) {
                /* If first point of i coincides with last point of j,
                   prepend points of j to i and delete j */
                retval[i].points.insert(retval[i].points.begin(), retval[j].points.begin(), retval[j].points.end()-1);
                retval.erase(retval.begin() + j);
                --j;
            } else if (retval[i].points.front().coincides_with(retval[j].points.front())) {
                /* Since Clipper does not preserve orientation of polylines, 
                   also check the case when first point of i coincides with first point of j. */
                retval[j].reverse();
                retval[i].points.insert(retval[i].points.begin(), retval[j].points.begin(), retval[j].points.end()-1);
                retval.erase(retval.begin() + j);
                --j;
            } else if (retval[i].points.back().coincides_with(retval[j].points.back())) {
                /* Since Clipper does not preserve orientation of polylines, 
                   also check the case when last point of i coincides with last point of j. */
                retval[j].reverse();
                retval[i].points.insert(retval[i].points.end(), retval[j].points.begin()+1, retval[j].points.end());
                retval.erase(retval.begin() + j);
                --j;
            }
        }
    }
    return retval;
}

Lines
_clipper_ln(ClipperLib::ClipType clipType, const Lines &subject, const Polygons &clip,
    bool safety_offset_)
{
    // convert Lines to Polylines
    Polylines polylines;
    polylines.reserve(subject.size());
    for (Lines::const_iterator line = subject.begin(); line != subject.end(); ++line)
        polylines.push_back(*line);
    
    // perform operation
    polylines = _clipper_pl(clipType, polylines, clip, safety_offset_);
    
    // convert Polylines to Lines
    Lines retval;
    for (Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline)
        retval.push_back(*polyline);
    return retval;
}

ClipperLib::PolyTree
union_pt(const Polygons &subject, bool safety_offset_)
{
    return _clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, subject, Polygons(), ClipperLib::pftEvenOdd, safety_offset_);
}

Polygons
union_pt_chained(const Polygons &subject, bool safety_offset_)
{
    ClipperLib::PolyTree polytree = union_pt(subject, safety_offset_);
    
    Polygons retval;
    traverse_pt(polytree.Childs, &retval);
    return retval;
}

void
traverse_pt(ClipperLib::PolyNodes &nodes, Polygons* retval)
{
    /* use a nearest neighbor search to order these children
       TODO: supply start_near to chained_path() too? */
    
    // collect ordering points
    Points ordering_points;
    ordering_points.reserve(nodes.size());
    for (ClipperLib::PolyNodes::const_iterator it = nodes.begin(); it != nodes.end(); ++it) {
        Point p((*it)->Contour.front().X, (*it)->Contour.front().Y);
        ordering_points.push_back(p);
    }
    
    // perform the ordering
    ClipperLib::PolyNodes ordered_nodes;
    Slic3r::Geometry::chained_path_items(ordering_points, nodes, ordered_nodes);
    
    // push results recursively
    for (ClipperLib::PolyNodes::iterator it = ordered_nodes.begin(); it != ordered_nodes.end(); ++it) {
        // traverse the next depth
        traverse_pt((*it)->Childs, retval);
        
        Polygon p = ClipperPath_to_Slic3rMultiPoint<Polygon>((*it)->Contour);
        retval->push_back(p);
        if ((*it)->IsHole()) retval->back().reverse();  // ccw
    }
}

Polygons
simplify_polygons(const Polygons &subject, bool preserve_collinear)
{
    // convert into Clipper polygons
    ClipperLib::Paths input_subject = Slic3rMultiPoints_to_ClipperPaths(subject);
    
    ClipperLib::Paths output;
    if (preserve_collinear) {
        ClipperLib::Clipper c;
        c.PreserveCollinear(true);
        c.StrictlySimple(true);
        c.AddPaths(input_subject, ClipperLib::ptSubject, true);
        c.Execute(ClipperLib::ctUnion, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    } else {
        ClipperLib::SimplifyPolygons(input_subject, output, ClipperLib::pftNonZero);
    }
    
    // convert into Slic3r polygons
    return ClipperPaths_to_Slic3rMultiPoints<Polygons>(output);
}

ExPolygons
simplify_polygons_ex(const Polygons &subject, bool preserve_collinear)
{
    if (!preserve_collinear) {
        return union_ex(simplify_polygons(subject, preserve_collinear));
    }
    
    // convert into Clipper polygons
    ClipperLib::Paths input_subject = Slic3rMultiPoints_to_ClipperPaths(subject);
    
    ClipperLib::PolyTree polytree;
    
    ClipperLib::Clipper c;
    c.PreserveCollinear(true);
    c.StrictlySimple(true);
    c.AddPaths(input_subject, ClipperLib::ptSubject, true);
    c.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    
    // convert into ExPolygons
    return PolyTreeToExPolygons(polytree);
}

void safety_offset(ClipperLib::Paths* paths)
{
    PROFILE_FUNC();

    // scale input
    scaleClipperPolygons(*paths);
    
    // perform offset (delta = scale 1e-05)
    ClipperLib::ClipperOffset co;
#ifdef CLIPPER_UTILS_DEBUG
    if (clipper_export_enabled) {
        static int iRun = 0;
        export_clipper_input_polygons_bin(debug_out_path("safety_offset-polygons-%d", ++iRun).c_str(), *paths, ClipperLib::Paths());
    }
#endif /* CLIPPER_UTILS_DEBUG */
    ClipperLib::Paths out;
    for (size_t i = 0; i < paths->size(); ++ i) {
        ClipperLib::Path &path = (*paths)[i];
        co.Clear();
        co.MiterLimit = 2;
        bool ccw = ClipperLib::Orientation(path);
        if (! ccw)
            std::reverse(path.begin(), path.end());
        {
            PROFILE_BLOCK(safety_offset_AddPaths);
            co.AddPath((*paths)[i], ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
        }
        {
            PROFILE_BLOCK(safety_offset_Execute);
            // offset outside by 10um
            ClipperLib::Paths out_this;
            co.Execute(out_this, ccw ? 10.f * float(CLIPPER_OFFSET_SCALE) : -10.f * float(CLIPPER_OFFSET_SCALE));
            if (! ccw) {
                // Reverse the resulting contours once again.
                for (ClipperLib::Paths::iterator it = out_this.begin(); it != out_this.end(); ++ it)
                    std::reverse(it->begin(), it->end());
            }
            if (out.empty())
                out = std::move(out_this);
            else
                std::move(std::begin(out_this), std::end(out_this), std::back_inserter(out));
        }
    }
    *paths = std::move(out);
    
    // unscale output
    unscaleClipperPolygons(*paths);
}

Polygons top_level_islands(const Slic3r::Polygons &polygons)
{
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    // perform union
    clipper.AddPaths(Slic3rMultiPoints_to_ClipperPaths(polygons), ClipperLib::ptSubject, true);
    ClipperLib::PolyTree polytree;
    clipper.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd); 
    // Convert only the top level islands to the output.
    Polygons out;
    out.reserve(polytree.ChildCount());
    for (int i = 0; i < polytree.ChildCount(); ++i)
        out.push_back(ClipperPath_to_Slic3rMultiPoint<Slic3r::Polygon>(polytree.Childs[i]->Contour));
    return out;
}

}