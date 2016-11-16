#include "ClipperUtils.hpp"
#include "Geometry.hpp"

// #define CLIPPER_UTILS_DEBUG

#ifdef CLIPPER_UTILS_DEBUG
#include "SVG.hpp"
#endif /* CLIPPER_UTILS_DEBUG */

#include <Shiny/Shiny.h>

namespace Slic3r {

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons* expolygons)
{  
  size_t cnt = expolygons->size();
  expolygons->resize(cnt + 1);
  ClipperPath_to_Slic3rMultiPoint(polynode.Contour, &(*expolygons)[cnt].contour);
  (*expolygons)[cnt].holes.resize(polynode.ChildCount());
  for (int i = 0; i < polynode.ChildCount(); ++i)
  {
    ClipperPath_to_Slic3rMultiPoint(polynode.Childs[i]->Contour, &(*expolygons)[cnt].holes[i]);
    //Add outer polygons contained by (nested within) holes ...
    for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++j)
      AddOuterPolyNodeToExPolygons(*polynode.Childs[i]->Childs[j], expolygons);
  }
}
 
void PolyTreeToExPolygons(ClipperLib::PolyTree& polytree, Slic3r::ExPolygons* expolygons)
{
    PROFILE_FUNC();
    expolygons->clear();
    for (int i = 0; i < polytree.ChildCount(); ++i)
        AddOuterPolyNodeToExPolygons(*polytree.Childs[i], expolygons);
}
//-----------------------------------------------------------

template <class T>
void
ClipperPath_to_Slic3rMultiPoint(const ClipperLib::Path &input, T* output)
{
    PROFILE_FUNC();
    output->points.clear();
    output->points.reserve(input.size());
    for (ClipperLib::Path::const_iterator pit = input.begin(); pit != input.end(); ++pit)
        output->points.push_back(Slic3r::Point( (*pit).X, (*pit).Y ));
}
template void ClipperPath_to_Slic3rMultiPoint<Slic3r::Polygon>(const ClipperLib::Path &input, Slic3r::Polygon* output);

template <class T>
void
ClipperPaths_to_Slic3rMultiPoints(const ClipperLib::Paths &input, T* output)
{
    PROFILE_FUNC();
    output->clear();
    output->reserve(input.size());
    for (ClipperLib::Paths::const_iterator it = input.begin(); it != input.end(); ++it) {
        typename T::value_type p;
        ClipperPath_to_Slic3rMultiPoint(*it, &p);
        output->push_back(p);
    }
}

void
ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input, Slic3r::ExPolygons* output)
{
    PROFILE_FUNC();

    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // perform union
    clipper.AddPaths(input, ClipperLib::ptSubject, true);
    ClipperLib::PolyTree polytree;
    clipper.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);  // offset results work with both EvenOdd and NonZero
    
    // write to ExPolygons object
    output->clear();
    PolyTreeToExPolygons(polytree, output);
}

void
Slic3rMultiPoint_to_ClipperPath(const Slic3r::MultiPoint &input, ClipperLib::Path* output)
{
    PROFILE_FUNC();

    output->clear();
    output->reserve(input.points.size());
    for (Slic3r::Points::const_iterator pit = input.points.begin(); pit != input.points.end(); ++pit)
        output->push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
}

void
Slic3rMultiPoint_to_ClipperPath_reversed(const Slic3r::MultiPoint &input, ClipperLib::Path* output)
{
    PROFILE_FUNC();

    output->clear();
    output->reserve(input.points.size());
    for (Slic3r::Points::const_reverse_iterator pit = input.points.rbegin(); pit != input.points.rend(); ++pit)
        output->push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
}

template <class T>
void
Slic3rMultiPoints_to_ClipperPaths(const T &input, ClipperLib::Paths* output)
{
    PROFILE_FUNC();

    output->clear();
    output->reserve(input.size());
    for (typename T::const_iterator it = input.begin(); it != input.end(); ++it) {
        // std::vector< IntPoint >, IntPoint is a pair of int64_t
        ClipperLib::Path p;
        Slic3rMultiPoint_to_ClipperPath(*it, &p);
        output->push_back(p);
    }
}

void
scaleClipperPolygon(ClipperLib::Path &polygon, const double scale)
{
    PROFILE_FUNC();

    for (ClipperLib::Path::iterator pit = polygon.begin(); pit != polygon.end(); ++pit) {
        //FIXME multiplication of int64_t by double!
        // Replace by bit shifts?
        (*pit).X *= scale;
        (*pit).Y *= scale;
    }
}

void
scaleClipperPolygons(ClipperLib::Paths &polygons, const double scale)
{
    PROFILE_FUNC();

    for (ClipperLib::Paths::iterator it = polygons.begin(); it != polygons.end(); ++it) {
        for (ClipperLib::Path::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            //FIXME multiplication of int64_t by double!
            // Replace by bit shifts?
            (*pit).X *= scale;
            (*pit).Y *= scale;
        }
    }
}

void
offset(const Slic3r::Polygons &polygons, ClipperLib::Paths* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    PROFILE_FUNC();
    // read input
    ClipperLib::Paths input;
    Slic3rMultiPoints_to_ClipperPaths(polygons, &input);
    
    // scale input
    scaleClipperPolygons(input, scale);
    
    // perform offset
    ClipperLib::ClipperOffset co;
    if (joinType == jtRound) {
        co.ArcTolerance = miterLimit;
    } else {
        co.MiterLimit = miterLimit;
    }
    {
        PROFILE_BLOCK(offset_AddPaths);
        co.AddPaths(input, joinType, ClipperLib::etClosedPolygon);
    }
    {
        PROFILE_BLOCK(offset_Execute);
        co.Execute(*retval, (delta*scale));
    }
    
    // unscale output
    scaleClipperPolygons(*retval, 1/scale);
}

void
offset(const Slic3r::Polygons &polygons, Slic3r::Polygons* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(polygons, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

Slic3r::Polygons
offset(const Slic3r::Polygons &polygons, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    Slic3r::Polygons pp;
    offset(polygons, &pp, delta, scale, joinType, miterLimit);
    return pp;
}

void
offset(const Slic3r::Polylines &polylines, ClipperLib::Paths* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // read input
    ClipperLib::Paths input;
    Slic3rMultiPoints_to_ClipperPaths(polylines, &input);
    
    // scale input
    scaleClipperPolygons(input, scale);
    
    // perform offset
    ClipperLib::ClipperOffset co;
    if (joinType == jtRound) {
        co.ArcTolerance = miterLimit;
    } else {
        co.MiterLimit = miterLimit;
    }
    co.AddPaths(input, joinType, ClipperLib::etOpenButt);
    co.Execute(*retval, (delta*scale));
    
    // unscale output
    scaleClipperPolygons(*retval, 1/scale);
}

void
offset(const Slic3r::Polylines &polylines, Slic3r::Polygons* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(polylines, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

void
offset(const Slic3r::Surface &surface, Slic3r::Surfaces* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    Slic3r::ExPolygons expp;
    offset(surface.expolygon, &expp, delta, scale, joinType, miterLimit);
    
    // clone the input surface for each expolygon we got
    retval->clear();
    retval->reserve(expp.size());
    for (ExPolygons::iterator it = expp.begin(); it != expp.end(); ++it) {
        Surface s = surface;  // clone
        s.expolygon = *it;
        retval->push_back(s);
    }
}

void
offset(const Slic3r::Polygons &polygons, Slic3r::ExPolygons* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(polygons, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rExPolygons(output, retval);
}

// This is a safe variant of the polygon offset, tailored for a single ExPolygon:
// a single polygon with multiple non-overlapping holes.
// Each contour and hole is offsetted separately, then the holes are subtracted from the outer contours.
void offset(const Slic3r::ExPolygon &expolygon, ClipperLib::Paths* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
//    printf("new ExPolygon offset\n");
    // 1) Offset the outer contour.
    const float delta_scaled = float(delta * scale);
    ClipperLib::Paths contours;
    {
        ClipperLib::Path input;
        Slic3rMultiPoint_to_ClipperPath(expolygon.contour, &input);
        scaleClipperPolygon(input, scale);
        ClipperLib::ClipperOffset co;
        if (joinType == jtRound)
            co.ArcTolerance = miterLimit;
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
            ClipperLib::Path input;
            Slic3rMultiPoint_to_ClipperPath_reversed(*it_hole, &input);
            scaleClipperPolygon(input, scale);
            ClipperLib::ClipperOffset co;
            if (joinType == jtRound)
                co.ArcTolerance = miterLimit;
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
        clipper.Execute(ClipperLib::ctDifference, *retval, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }
    
    // 4) Unscale the output.
    scaleClipperPolygons(*retval, 1/scale);
}

Slic3r::Polygons offset(const Slic3r::ExPolygon &expolygon, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(expolygon, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    Slic3r::Polygons retval;
    ClipperPaths_to_Slic3rMultiPoints(output, &retval);
    return retval;
}

Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygon &expolygon, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(expolygon, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    Slic3r::ExPolygons retval;
    ClipperPaths_to_Slic3rExPolygons(output, &retval);
    return retval;
}

// This is a safe variant of the polygon offset, tailored for a single ExPolygon:
// a single polygon with multiple non-overlapping holes.
// Each contour and hole is offsetted separately, then the holes are subtracted from the outer contours.
void offset(const Slic3r::ExPolygons &expolygons, ClipperLib::Paths* retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
//    printf("new ExPolygon offset\n");
    const float delta_scaled = float(delta * scale);
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
            ClipperLib::Path input;
            Slic3rMultiPoint_to_ClipperPath(it_expoly->contour, &input);
            scaleClipperPolygon(input, scale);
            ClipperLib::ClipperOffset co;
            if (joinType == jtRound)
                co.ArcTolerance = miterLimit;
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
                ClipperLib::Path input;
                Slic3rMultiPoint_to_ClipperPath_reversed(*it_hole, &input);
                scaleClipperPolygon(input, scale);
                ClipperLib::ClipperOffset co;
                if (joinType == jtRound)
                    co.ArcTolerance = miterLimit;
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
        clipper.Execute(ClipperLib::ctDifference, *retval, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }
    
    // 4) Unscale the output.
    scaleClipperPolygons(*retval, 1/scale);
}

Slic3r::Polygons offset(const Slic3r::ExPolygons &expolygons, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(expolygons, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    Slic3r::Polygons retval;
    ClipperPaths_to_Slic3rMultiPoints(output, &retval);
    return retval;
}

Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygons &expolygons, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(expolygons, &output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    Slic3r::ExPolygons retval;
    ClipperPaths_to_Slic3rExPolygons(output, &retval);
    return retval;
}

Slic3r::ExPolygons
offset_ex(const Slic3r::Polygons &polygons, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    Slic3r::ExPolygons expp;
    offset(polygons, &expp, delta, scale, joinType, miterLimit);
    return expp;
}

void
offset2(const Slic3r::Polygons &polygons, ClipperLib::Paths* retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    if (delta1 * delta2 >= 0) {
        // Both deltas are the same signum
        offset(polygons, retval, delta1 + delta2, scale, joinType, miterLimit);
        return;
    }
#ifdef CLIPPER_UTILS_DEBUG
    BoundingBox bbox = get_extents(polygons);
    coordf_t stroke_width = scale_(0.005);
    static int iRun = 0;
    ++ iRun;
    bool flipY = false;
    SVG svg(debug_out_path("offset2-%d.svg", iRun), bbox, scale_(1.), flipY);
    for (Slic3r::Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        svg.draw(it->lines(), "gray", stroke_width);
#endif /* CLIPPER_UTILS_DEBUG */

    // read input
    ClipperLib::Paths input;
    Slic3rMultiPoints_to_ClipperPaths(polygons, &input);
    
    // scale input
    scaleClipperPolygons(input, scale);
    
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
    co.Execute(output1, (delta1*scale));
#ifdef CLIPPER_UTILS_DEBUG
    svg.draw(output1, 1./CLIPPER_OFFSET_SCALE, "red", stroke_width);
#endif /* CLIPPER_UTILS_DEBUG */
    
    // perform second offset
    co.Clear();
    co.AddPaths(output1, joinType, ClipperLib::etClosedPolygon);
    co.Execute(*retval, (delta2*scale));
#ifdef CLIPPER_UTILS_DEBUG
    svg.draw(*retval, 1./CLIPPER_OFFSET_SCALE, "green", stroke_width);
#endif /* CLIPPER_UTILS_DEBUG */

    // unscale output
    scaleClipperPolygons(*retval, 1/scale);
}

void
offset2(const Slic3r::Polygons &polygons, Slic3r::Polygons* retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset2(polygons, &output, delta1, delta2, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

Slic3r::Polygons
offset2(const Slic3r::Polygons &polygons, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    Slic3r::Polygons pp;
    offset2(polygons, &pp, delta1, delta2, scale, joinType, miterLimit);
    return pp;
}

void
offset2(const Slic3r::Polygons &polygons, Slic3r::ExPolygons* retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset2(polygons, &output, delta1, delta2, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rExPolygons(output, retval);
}

Slic3r::ExPolygons
offset2_ex(const Slic3r::Polygons &polygons, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    Slic3r::ExPolygons expp;
    offset2(polygons, &expp, delta1, delta2, scale, joinType, miterLimit);
    return expp;
}

template <class T>
void _clipper_do(const ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, T* retval, const ClipperLib::PolyFillType fillType, const bool safety_offset_)
{
    PROFILE_BLOCK(_clipper_do_polygons);

    // read input
    ClipperLib::Paths input_subject, input_clip;
    Slic3rMultiPoints_to_ClipperPaths(subject, &input_subject);
    Slic3rMultiPoints_to_ClipperPaths(clip,    &input_clip);
    
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
    {
        PROFILE_BLOCK(_clipper_do_polygons_AddPaths);
        clipper.AddPaths(input_subject, ClipperLib::ptSubject, true);
        clipper.AddPaths(input_clip, ClipperLib::ptClip, true);
    }
    
    // perform operation
    { 
        PROFILE_BLOCK(_clipper_do_polygons_Execute);
        clipper.Execute(clipType, *retval, fillType, fillType);
    }
}

void _clipper_do(const ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, ClipperLib::PolyTree* retval, const ClipperLib::PolyFillType fillType,
    const bool safety_offset_)
{
    PROFILE_BLOCK(_clipper_do_polylines);

    // read input
    ClipperLib::Paths input_subject, input_clip;
    Slic3rMultiPoints_to_ClipperPaths(subject, &input_subject);
    Slic3rMultiPoints_to_ClipperPaths(clip,    &input_clip);
    
    // perform safety offset
    if (safety_offset_) safety_offset(&input_clip);
    
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // add polygons
    {
        PROFILE_BLOCK(_clipper_do_polylines_AddPaths);
        clipper.AddPaths(input_subject, ClipperLib::ptSubject, false);
        clipper.AddPaths(input_clip,    ClipperLib::ptClip,    true);
    }
    
    // perform operation
    {
        PROFILE_BLOCK(_clipper_do_polylines_Execute);
        clipper.Execute(clipType, *retval, fillType, fillType);
    }
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polygons* retval, bool safety_offset_)
{
    PROFILE_FUNC();
    // perform operation
    ClipperLib::Paths output;
    _clipper_do<ClipperLib::Paths>(clipType, subject, clip, &output, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into Polygons
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::ExPolygons* retval, bool safety_offset_)
{
    PROFILE_FUNC();
    // perform operation
    ClipperLib::PolyTree polytree;
    _clipper_do<ClipperLib::PolyTree>(clipType, subject, clip, &polytree, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into ExPolygons
    PolyTreeToExPolygons(polytree, retval);
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polylines* retval, bool safety_offset_)
{
    PROFILE_FUNC();
    // perform operation
    ClipperLib::PolyTree polytree;
    _clipper_do(clipType, subject, clip, &polytree, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into Polylines
    ClipperLib::Paths output;
    ClipperLib::PolyTreeToPaths(polytree, output);
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Lines &subject, 
    const Slic3r::Polygons &clip, Slic3r::Lines* retval, bool safety_offset_)
{
    // convert Lines to Polylines
    Slic3r::Polylines polylines;
    polylines.reserve(subject.size());
    for (Slic3r::Lines::const_iterator line = subject.begin(); line != subject.end(); ++line)
        polylines.push_back(*line);
    
    // perform operation
    _clipper(clipType, polylines, clip, &polylines, safety_offset_);
    
    // convert Polylines to Lines
    for (Slic3r::Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline)
        retval->push_back(*polyline);
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polylines* retval, bool safety_offset_)
{
    // transform input polygons into polylines
    Slic3r::Polylines polylines;
    polylines.reserve(subject.size());
    for (Slic3r::Polygons::const_iterator polygon = subject.begin(); polygon != subject.end(); ++polygon)
        polylines.push_back(*polygon);  // implicit call to split_at_first_point()
    
    // perform clipping
    _clipper(clipType, polylines, clip, retval, safety_offset_);
    
    /* If the split_at_first_point() call above happens to split the polygon inside the clipping area
       we would get two consecutive polylines instead of a single one, so we go through them in order
       to recombine continuous polylines. */
    for (size_t i = 0; i < retval->size(); ++i) {
        for (size_t j = i+1; j < retval->size(); ++j) {
            if ((*retval)[i].points.back().coincides_with((*retval)[j].points.front())) {
                /* If last point of i coincides with first point of j,
                   append points of j to i and delete j */
                (*retval)[i].points.insert((*retval)[i].points.end(), (*retval)[j].points.begin()+1, (*retval)[j].points.end());
                retval->erase(retval->begin() + j);
                --j;
            } else if ((*retval)[i].points.front().coincides_with((*retval)[j].points.back())) {
                /* If first point of i coincides with last point of j,
                   prepend points of j to i and delete j */
                (*retval)[i].points.insert((*retval)[i].points.begin(), (*retval)[j].points.begin(), (*retval)[j].points.end()-1);
                retval->erase(retval->begin() + j);
                --j;
            } else if ((*retval)[i].points.front().coincides_with((*retval)[j].points.front())) {
                /* Since Clipper does not preserve orientation of polylines, 
                   also check the case when first point of i coincides with first point of j. */
                (*retval)[j].reverse();
                (*retval)[i].points.insert((*retval)[i].points.begin(), (*retval)[j].points.begin(), (*retval)[j].points.end()-1);
                retval->erase(retval->begin() + j);
                --j;
            } else if ((*retval)[i].points.back().coincides_with((*retval)[j].points.back())) {
                /* Since Clipper does not preserve orientation of polylines, 
                   also check the case when last point of i coincides with last point of j. */
                (*retval)[j].reverse();
                (*retval)[i].points.insert((*retval)[i].points.end(), (*retval)[j].points.begin()+1, (*retval)[j].points.end());
                retval->erase(retval->begin() + j);
                --j;
            }
        }
    }   
}

template <class SubjectType, class ResultType>
void diff(const SubjectType &subject, const Slic3r::Polygons &clip, ResultType* retval, bool safety_offset_)
{
    _clipper(ClipperLib::ctDifference, subject, clip, retval, safety_offset_);
}
template void diff<Slic3r::Polygons, Slic3r::ExPolygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons* retval, bool safety_offset_);
template void diff<Slic3r::Polygons, Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::Polygons* retval, bool safety_offset_);
template void diff<Slic3r::Polygons, Slic3r::Polylines>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::Polylines* retval, bool safety_offset_);
template void diff<Slic3r::Polylines, Slic3r::Polylines>(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, Slic3r::Polylines* retval, bool safety_offset_);
template void diff<Slic3r::Lines, Slic3r::Lines>(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, Slic3r::Lines* retval, bool safety_offset_);

template <class SubjectType, class ResultType>
void diff(const SubjectType &subject, const Slic3r::ExPolygons &clip, ResultType* retval, bool safety_offset_)
{
    Slic3r::Polygons pp;
    for (Slic3r::ExPolygons::const_iterator ex = clip.begin(); ex != clip.end(); ++ex) {
        Slic3r::Polygons ppp = *ex;
        pp.insert(pp.end(), ppp.begin(), ppp.end());
    }
    diff(subject, pp, retval, safety_offset_);
}
template void diff<Slic3r::Polygons, Slic3r::ExPolygons>(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, Slic3r::ExPolygons* retval, bool safety_offset_);

template <class ResultType>
void diff(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ResultType* retval, bool safety_offset_)
{
    Slic3r::Polygons pp;
    for (Slic3r::ExPolygons::const_iterator ex = subject.begin(); ex != subject.end(); ++ex) {
        Slic3r::Polygons ppp = *ex;
        pp.insert(pp.end(), ppp.begin(), ppp.end());
    }
    diff(pp, clip, retval, safety_offset_);
}

Slic3r::Polygons
diff(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_)
{
    Slic3r::Polygons pp;
    diff(subject, clip, &pp, safety_offset_);
    return pp;
}

template <class SubjectType, class ClipType>
Slic3r::ExPolygons
diff_ex(const SubjectType &subject, const ClipType &clip, bool safety_offset_)
{
    Slic3r::ExPolygons expp;
    diff(subject, clip, &expp, safety_offset_);
    return expp;
}
template Slic3r::ExPolygons diff_ex<Slic3r::Polygons, Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_);
template Slic3r::ExPolygons diff_ex<Slic3r::Polygons, Slic3r::ExPolygons>(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, bool safety_offset_);
template Slic3r::ExPolygons diff_ex<Slic3r::ExPolygons, Slic3r::ExPolygons>(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, bool safety_offset_);

template <class SubjectType, class ResultType>
void intersection(const SubjectType &subject, const Slic3r::Polygons &clip, ResultType* retval, bool safety_offset_)
{
    _clipper(ClipperLib::ctIntersection, subject, clip, retval, safety_offset_);
}
template void intersection<Slic3r::Polygons, Slic3r::ExPolygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons* retval, bool safety_offset_);
template void intersection<Slic3r::Polygons, Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::Polygons* retval, bool safety_offset_);
template void intersection<Slic3r::Polygons, Slic3r::Polylines>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::Polylines* retval, bool safety_offset_);
template void intersection<Slic3r::Polylines, Slic3r::Polylines>(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, Slic3r::Polylines* retval, bool safety_offset_);
template void intersection<Slic3r::Lines, Slic3r::Lines>(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, Slic3r::Lines* retval, bool safety_offset_);

template <class SubjectType>
SubjectType intersection(const SubjectType &subject, const Slic3r::Polygons &clip, bool safety_offset_)
{
    SubjectType pp;
    intersection(subject, clip, &pp, safety_offset_);
    return pp;
}

template Slic3r::Polygons intersection<Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_);
template Slic3r::Polylines intersection<Slic3r::Polylines>(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, bool safety_offset_);
template Slic3r::Lines intersection<Slic3r::Lines>(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, bool safety_offset_);

Slic3r::ExPolygons
intersection_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_)
{
    Slic3r::ExPolygons expp;
    intersection(subject, clip, &expp, safety_offset_);
    return expp;
}

template <class SubjectType>
bool intersects(const SubjectType &subject, const Slic3r::Polygons &clip, bool safety_offset_)
{
    SubjectType retval;
    intersection(subject, clip, &retval, safety_offset_);
    return !retval.empty();
}
template bool intersects<Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_);
template bool intersects<Slic3r::Polylines>(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, bool safety_offset_);
template bool intersects<Slic3r::Lines>(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, bool safety_offset_);

void xor_(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons* retval, 
    bool safety_offset_)
{
    _clipper(ClipperLib::ctXor, subject, clip, retval, safety_offset_);
}

template <class T>
void union_(const Slic3r::Polygons &subject, T* retval, bool safety_offset_)
{
    Slic3r::Polygons p;
    _clipper(ClipperLib::ctUnion, subject, p, retval, safety_offset_);
}
template void union_<Slic3r::ExPolygons>(const Slic3r::Polygons &subject, Slic3r::ExPolygons* retval, bool safety_offset_);
template void union_<Slic3r::Polygons>(const Slic3r::Polygons &subject, Slic3r::Polygons* retval, bool safety_offset_);

Slic3r::Polygons
union_(const Slic3r::Polygons &subject, bool safety_offset)
{
    Polygons pp;
    union_(subject, &pp, safety_offset);
    return pp;
}

Slic3r::ExPolygons
union_ex(const Slic3r::Polygons &subject, bool safety_offset)
{
    ExPolygons expp;
    union_(subject, &expp, safety_offset);
    return expp;
}

Slic3r::ExPolygons
union_ex(const Slic3r::Surfaces &subject, bool safety_offset)
{
    Polygons pp;
    for (Slic3r::Surfaces::const_iterator s = subject.begin(); s != subject.end(); ++s) {
        Polygons spp = *s;
        pp.insert(pp.end(), spp.begin(), spp.end());
    }
    return union_ex(pp, safety_offset);
}

void union_(const Slic3r::Polygons &subject1, const Slic3r::Polygons &subject2, Slic3r::Polygons* retval, bool safety_offset)
{
    Polygons pp = subject1;
    pp.insert(pp.end(), subject2.begin(), subject2.end());
    union_(pp, retval, safety_offset);
}

Slic3r::Polygons
union_(const Slic3r::ExPolygons &subject1, const Slic3r::ExPolygons &subject2, bool safety_offset)
{
    Polygons pp;
    for (Slic3r::ExPolygons::const_iterator it = subject1.begin(); it != subject1.end(); ++it) {
        Polygons spp = *it;
        pp.insert(pp.end(), spp.begin(), spp.end());
    }
    for (Slic3r::ExPolygons::const_iterator it = subject2.begin(); it != subject2.end(); ++it) {
        Polygons spp = *it;
        pp.insert(pp.end(), spp.begin(), spp.end());
    }
    Polygons retval;
    union_(pp, &retval, safety_offset);
    return retval;
}

void union_pt(const Slic3r::Polygons &subject, ClipperLib::PolyTree* retval, bool safety_offset_)
{
    Slic3r::Polygons clip;
    _clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, subject, clip, retval, ClipperLib::pftEvenOdd, safety_offset_);
}

void union_pt_chained(const Slic3r::Polygons &subject, Slic3r::Polygons* retval, bool safety_offset_)
{
    ClipperLib::PolyTree pt;
    union_pt(subject, &pt, safety_offset_);
    if (&subject == retval)
        // It is safe to use the same variable for input and output, because this function makes
        // a temporary copy of the results.
        retval->clear();
    traverse_pt(pt.Childs, retval);
}

static void traverse_pt(ClipperLib::PolyNodes &nodes, Slic3r::Polygons* retval)
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
        
        Polygon p;
        ClipperPath_to_Slic3rMultiPoint((*it)->Contour, &p);
        retval->push_back(p);
        if ((*it)->IsHole()) retval->back().reverse();  // ccw
    }
}

void simplify_polygons(const Slic3r::Polygons &subject, Slic3r::Polygons* retval, bool preserve_collinear)
{
    PROFILE_FUNC();

    // convert into Clipper polygons
    ClipperLib::Paths input_subject, output;
    Slic3rMultiPoints_to_ClipperPaths(subject, &input_subject);
    
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
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

void simplify_polygons(const Slic3r::Polygons &subject, Slic3r::ExPolygons* retval, bool preserve_collinear)
{
    PROFILE_FUNC();

    if (!preserve_collinear) {
        Polygons polygons;
        simplify_polygons(subject, &polygons, preserve_collinear);
        union_(polygons, retval);
        return;
    }
    
    // convert into Clipper polygons
    ClipperLib::Paths input_subject;
    Slic3rMultiPoints_to_ClipperPaths(subject, &input_subject);
    
    ClipperLib::PolyTree polytree;
    
    ClipperLib::Clipper c;
    c.PreserveCollinear(true);
    c.StrictlySimple(true);
    c.AddPaths(input_subject, ClipperLib::ptSubject, true);
    c.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    
    // convert into ExPolygons
    PolyTreeToExPolygons(polytree, retval);
}

void safety_offset(ClipperLib::Paths* paths)
{
    PROFILE_FUNC();

    // scale input
    scaleClipperPolygons(*paths, CLIPPER_OFFSET_SCALE);
    
    // perform offset (delta = scale 1e-05)
    ClipperLib::ClipperOffset co;
    co.MiterLimit = 2;
    {
        PROFILE_BLOCK(safety_offset_AddPaths);
        co.AddPaths(*paths, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
    }
    {
        PROFILE_BLOCK(safety_offset_Execute);
        co.Execute(*paths, 10.0 * CLIPPER_OFFSET_SCALE);
    }
    
    // unscale output
    scaleClipperPolygons(*paths, 1.0/CLIPPER_OFFSET_SCALE);
}

}
