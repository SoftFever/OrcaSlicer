#include "ClipperUtils.hpp"
#include "Geometry.hpp"

namespace Slic3r {

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons& expolygons)
{  
  size_t cnt = expolygons.size();
  expolygons.resize(cnt + 1);
  ClipperPath_to_Slic3rMultiPoint(polynode.Contour, expolygons[cnt].contour);
  expolygons[cnt].holes.resize(polynode.ChildCount());
  for (int i = 0; i < polynode.ChildCount(); ++i)
  {
    ClipperPath_to_Slic3rMultiPoint(polynode.Childs[i]->Contour, expolygons[cnt].holes[i]);
    //Add outer polygons contained by (nested within) holes ...
    for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++j)
      AddOuterPolyNodeToExPolygons(*polynode.Childs[i]->Childs[j], expolygons);
  }
}
 
void PolyTreeToExPolygons(ClipperLib::PolyTree& polytree, Slic3r::ExPolygons& expolygons)
{
  expolygons.clear();
  for (int i = 0; i < polytree.ChildCount(); ++i)
    AddOuterPolyNodeToExPolygons(*polytree.Childs[i], expolygons);
}
//-----------------------------------------------------------

template <class T>
void
ClipperPath_to_Slic3rMultiPoint(const ClipperLib::Path &input, T &output)
{
    output.points.clear();
    for (ClipperLib::Path::const_iterator pit = input.begin(); pit != input.end(); ++pit) {
        output.points.push_back(Slic3r::Point( (*pit).X, (*pit).Y ));
    }
}

template <class T>
void
ClipperPaths_to_Slic3rMultiPoints(const ClipperLib::Paths &input, T &output)
{
    output.clear();
    for (ClipperLib::Paths::const_iterator it = input.begin(); it != input.end(); ++it) {
        typename T::value_type p;
        ClipperPath_to_Slic3rMultiPoint(*it, p);
        output.push_back(p);
    }
}

void
ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input, Slic3r::ExPolygons &output)
{
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // perform union
    clipper.AddPaths(input, ClipperLib::ptSubject, true);
    ClipperLib::PolyTree* polytree = new ClipperLib::PolyTree();
    clipper.Execute(ClipperLib::ctUnion, *polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);  // offset results work with both EvenOdd and NonZero
    
    // write to ExPolygons object
    output.clear();
    PolyTreeToExPolygons(*polytree, output);
    
    delete polytree;
}

void
Slic3rMultiPoint_to_ClipperPath(const Slic3r::MultiPoint &input, ClipperLib::Path &output)
{
    output.clear();
    for (Slic3r::Points::const_iterator pit = input.points.begin(); pit != input.points.end(); ++pit) {
        output.push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
    }
}

template <class T>
void
Slic3rMultiPoints_to_ClipperPaths(const T &input, ClipperLib::Paths &output)
{
    output.clear();
    for (typename T::const_iterator it = input.begin(); it != input.end(); ++it) {
        ClipperLib::Path p;
        Slic3rMultiPoint_to_ClipperPath(*it, p);
        output.push_back(p);
    }
}

void
scaleClipperPolygons(ClipperLib::Paths &polygons, const double scale)
{
    for (ClipperLib::Paths::iterator it = polygons.begin(); it != polygons.end(); ++it) {
        for (ClipperLib::Path::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            (*pit).X *= scale;
            (*pit).Y *= scale;
        }
    }
}

void
offset(const Slic3r::Polygons &polygons, ClipperLib::Paths &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // read input
    ClipperLib::Paths input;
    Slic3rMultiPoints_to_ClipperPaths(polygons, input);
    
    // scale input
    scaleClipperPolygons(input, scale);
    
    // perform offset
    ClipperLib::ClipperOffset co;
    if (joinType == jtRound) {
        co.ArcTolerance = miterLimit;
    } else {
        co.MiterLimit = miterLimit;
    }
    co.AddPaths(input, joinType, ClipperLib::etClosedPolygon);
    co.Execute(retval, (delta*scale));
    
    // unscale output
    scaleClipperPolygons(retval, 1/scale);
}

void
offset(const Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths output;
    offset(polygons, output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

void
offset(const Slic3r::Polylines &polylines, ClipperLib::Paths &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // read input
    ClipperLib::Paths input;
    Slic3rMultiPoints_to_ClipperPaths(polylines, input);
    
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
    co.Execute(retval, (delta*scale));
    
    // unscale output
    scaleClipperPolygons(retval, 1/scale);
}

void
offset(const Slic3r::Polylines &polylines, Slic3r::Polygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths* output = new ClipperLib::Paths();
    offset(polylines, *output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rMultiPoints(*output, retval);
    delete output;
}

void
offset(const Slic3r::Surface &surface, Slic3r::Surfaces &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    Slic3r::ExPolygons expp;
    offset_ex(surface.expolygon, expp, delta, scale, joinType, miterLimit);
    
    // clone the input surface for each expolygon we got
    retval.clear();
    retval.reserve(expp.size());
    for (ExPolygons::iterator it = expp.begin(); it != expp.end(); ++it) {
        Surface s = surface;  // clone
        s.expolygon = *it;
        retval.push_back(s);
    }
}

void
offset_ex(const Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Paths* output = new ClipperLib::Paths();
    offset(polygons, *output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rExPolygons(*output, retval);
    delete output;
}

void
offset2(const Slic3r::Polygons &polygons, ClipperLib::Paths &retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // read input
    ClipperLib::Paths input;
    Slic3rMultiPoints_to_ClipperPaths(polygons, input);
    
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
    
    // perform second offset
    co.Clear();
    co.AddPaths(output1, joinType, ClipperLib::etClosedPolygon);
    co.Execute(retval, (delta2*scale));
    
    // unscale output
    scaleClipperPolygons(retval, 1/scale);
}

void
offset2(const Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Paths* output = new ClipperLib::Paths();
    offset2(polygons, *output, delta1, delta2, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rMultiPoints(*output, retval);
    delete output;
}

void
offset2_ex(const Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Paths* output = new ClipperLib::Paths();
    offset2(polygons, *output, delta1, delta2, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPaths_to_Slic3rExPolygons(*output, retval);
    delete output;
}

template <class T>
void _clipper_do(const ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, T &retval, const ClipperLib::PolyFillType fillType, const bool safety_offset_)
{
    // read input
    ClipperLib::Paths* input_subject = new ClipperLib::Paths();
    ClipperLib::Paths* input_clip    = new ClipperLib::Paths();
    Slic3rMultiPoints_to_ClipperPaths(subject, *input_subject);
    Slic3rMultiPoints_to_ClipperPaths(clip,    *input_clip);
    
    // perform safety offset
    if (safety_offset_) {
        if (clipType == ClipperLib::ctUnion) {
            safety_offset(input_subject);
        } else {
            safety_offset(input_clip);
        }
    }
    
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // add polygons
    clipper.AddPaths(*input_subject, ClipperLib::ptSubject, true);
    delete input_subject;
    clipper.AddPaths(*input_clip, ClipperLib::ptClip, true);
    delete input_clip;
    
    // perform operation
    clipper.Execute(clipType, retval, fillType, fillType);
}

void _clipper_do(const ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, ClipperLib::PolyTree &retval, const ClipperLib::PolyFillType fillType)
{
    // read input
    ClipperLib::Paths* input_subject = new ClipperLib::Paths();
    ClipperLib::Paths* input_clip    = new ClipperLib::Paths();
    Slic3rMultiPoints_to_ClipperPaths(subject, *input_subject);
    Slic3rMultiPoints_to_ClipperPaths(clip,    *input_clip);
    
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // add polygons
    clipper.AddPaths(*input_subject, ClipperLib::ptSubject, false);
    delete input_subject;
    clipper.AddPaths(*input_clip, ClipperLib::ptClip, true);
    delete input_clip;
    
    // perform operation
    clipper.Execute(clipType, retval, fillType, fillType);
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_)
{
    // perform operation
    ClipperLib::Paths* output = new ClipperLib::Paths();
    _clipper_do<ClipperLib::Paths>(clipType, subject, clip, *output, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into Polygons
    ClipperPaths_to_Slic3rMultiPoints(*output, retval);
    delete output;
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_)
{
    // perform operation
    ClipperLib::PolyTree* polytree = new ClipperLib::PolyTree();
    _clipper_do<ClipperLib::PolyTree>(clipType, subject, clip, *polytree, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into ExPolygons
    PolyTreeToExPolygons(*polytree, retval);
    delete polytree;
}

void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polylines &retval)
{
    // perform operation
    ClipperLib::PolyTree polytree;
    _clipper_do(clipType, subject, clip, polytree, ClipperLib::pftNonZero);
    
    // convert into Polygons
    ClipperLib::Paths output;
    ClipperLib::PolyTreeToPaths(polytree, output);
    ClipperPaths_to_Slic3rMultiPoints(output, retval);
}

template <class T>
void diff(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, T &retval, bool safety_offset_)
{
    _clipper(ClipperLib::ctDifference, subject, clip, retval, safety_offset_);
}
template void diff<Slic3r::ExPolygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_);
template void diff<Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_);

void diff(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, Slic3r::Polylines &retval)
{
    _clipper(ClipperLib::ctDifference, subject, clip, retval);
}

template <class T>
void intersection(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, T &retval, bool safety_offset_)
{
    _clipper(ClipperLib::ctIntersection, subject, clip, retval, safety_offset_);
}
template void intersection<Slic3r::ExPolygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_);
template void intersection<Slic3r::Polygons>(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_);

void intersection(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, Slic3r::Polylines &retval)
{
    _clipper(ClipperLib::ctIntersection, subject, clip, retval);
}

void xor_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, 
    bool safety_offset_)
{
    _clipper(ClipperLib::ctXor, subject, clip, retval, safety_offset_);
}

template <class T>
void union_(const Slic3r::Polygons &subject, T &retval, bool safety_offset_)
{
    Slic3r::Polygons p;
    _clipper(ClipperLib::ctUnion, subject, p, retval, safety_offset_);
}
template void union_<Slic3r::ExPolygons>(const Slic3r::Polygons &subject, Slic3r::ExPolygons &retval, bool safety_offset_);
template void union_<Slic3r::Polygons>(const Slic3r::Polygons &subject, Slic3r::Polygons &retval, bool safety_offset_);

void union_pt(const Slic3r::Polygons &subject, ClipperLib::PolyTree &retval, bool safety_offset_)
{
    Slic3r::Polygons clip;
    _clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, subject, clip, retval, ClipperLib::pftEvenOdd, safety_offset_);
}

void union_pt_chained(const Slic3r::Polygons &subject, Slic3r::Polygons &retval, bool safety_offset_)
{
    ClipperLib::PolyTree pt;
    union_pt(subject, pt, safety_offset_);
    traverse_pt(pt.Childs, retval);
}

static void traverse_pt(ClipperLib::PolyNodes &nodes, Slic3r::Polygons &retval)
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
        ClipperPath_to_Slic3rMultiPoint((*it)->Contour, p);
        retval.push_back(p);
        if ((*it)->IsHole()) retval.back().reverse();  // ccw
    }
}

void simplify_polygons(const Slic3r::Polygons &subject, Slic3r::Polygons &retval, bool preserve_collinear)
{
    // convert into Clipper polygons
    ClipperLib::Paths* input_subject = new ClipperLib::Paths();
    Slic3rMultiPoints_to_ClipperPaths(subject, *input_subject);
    
    ClipperLib::Paths* output = new ClipperLib::Paths();
    
    if (preserve_collinear) {
        ClipperLib::Clipper c;
        c.PreserveCollinear(true);
        c.StrictlySimple(true);
        c.AddPaths(*input_subject, ClipperLib::ptSubject, true);
        c.Execute(ClipperLib::ctUnion, *output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    } else {
        ClipperLib::SimplifyPolygons(*input_subject, *output, ClipperLib::pftNonZero);
    }
    
    delete input_subject;
    
    // convert into Slic3r polygons
    ClipperPaths_to_Slic3rMultiPoints(*output, retval);
    delete output;
}

void simplify_polygons(const Slic3r::Polygons &subject, Slic3r::ExPolygons &retval, bool preserve_collinear)
{
    if (!preserve_collinear) {
        Polygons polygons;
        simplify_polygons(subject, polygons, preserve_collinear);
        union_(polygons, retval);
        return;
    }
    
    // convert into Clipper polygons
    ClipperLib::Paths input_subject;
    Slic3rMultiPoints_to_ClipperPaths(subject, input_subject);
    
    ClipperLib::PolyTree polytree;
    
    ClipperLib::Clipper c;
    c.PreserveCollinear(true);
    c.StrictlySimple(true);
    c.AddPaths(input_subject, ClipperLib::ptSubject, true);
    c.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    
    // convert into ExPolygons
    PolyTreeToExPolygons(polytree, retval);
}

void safety_offset(ClipperLib::Paths* &subject)
{
    // scale input
    scaleClipperPolygons(*subject, CLIPPER_OFFSET_SCALE);
    
    // perform offset (delta = scale 1e-05)
    ClipperLib::Paths* retval = new ClipperLib::Paths();
    ClipperLib::ClipperOffset co;
    co.MiterLimit = 2;
    co.AddPaths(*subject, ClipperLib::jtMiter, ClipperLib::etClosedPolygon);
    co.Execute(*retval, 10.0 * CLIPPER_OFFSET_SCALE);
    
    // unscale output
    scaleClipperPolygons(*retval, 1.0/CLIPPER_OFFSET_SCALE);
    
    // delete original data and switch pointer
    delete subject;
    subject = retval;
}

///////////////////////

#ifdef SLIC3RXS
SV*
polynode_children_2_perl(const ClipperLib::PolyNode& node)
{
    AV* av = newAV();
    const unsigned int len = node.ChildCount();
    av_extend(av, len-1);
    for (int i = 0; i < len; ++i) {
        av_store(av, i, polynode2perl(*node.Childs[i]));
    }
    return (SV*)newRV_noinc((SV*)av);
}

SV*
polynode2perl(const ClipperLib::PolyNode& node)
{
    HV* hv = newHV();
    Slic3r::Polygon p;
    ClipperPath_to_Slic3rMultiPoint(node.Contour, p);
    if (node.IsHole()) {
        (void)hv_stores( hv, "hole", p.to_SV_clone_ref() );
    } else {
        (void)hv_stores( hv, "outer", p.to_SV_clone_ref() );
    }
    (void)hv_stores( hv, "children", polynode_children_2_perl(node) );
    return (SV*)newRV_noinc((SV*)hv);
}
#endif

}
