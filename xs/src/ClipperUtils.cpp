#include "ClipperUtils.hpp"

namespace Slic3r {

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons& expolygons)
{  
  size_t cnt = expolygons.size();
  expolygons.resize(cnt + 1);
  ClipperPolygon_to_Slic3rPolygon(polynode.Contour, expolygons[cnt].contour);
  expolygons[cnt].holes.resize(polynode.ChildCount());
  for (int i = 0; i < polynode.ChildCount(); ++i)
  {
    ClipperPolygon_to_Slic3rPolygon(polynode.Childs[i]->Contour, expolygons[cnt].holes[i]);
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

void
ClipperPolygon_to_Slic3rPolygon(const ClipperLib::Polygon &input, Slic3r::Polygon &output)
{
    output.points.clear();
    for (ClipperLib::Polygon::const_iterator pit = input.begin(); pit != input.end(); ++pit) {
        output.points.push_back(Slic3r::Point( (*pit).X, (*pit).Y ));
    }
}

void
ClipperPolygons_to_Slic3rPolygons(const ClipperLib::Polygons &input, Slic3r::Polygons &output)
{
    output.clear();
    for (ClipperLib::Polygons::const_iterator it = input.begin(); it != input.end(); ++it) {
        Slic3r::Polygon p;
        ClipperPolygon_to_Slic3rPolygon(*it, p);
        output.push_back(p);
    }
}

void
ClipperPolygons_to_Slic3rExPolygons(const ClipperLib::Polygons &input, Slic3r::ExPolygons &output)
{
    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    
    // perform union
    clipper.AddPolygons(input, ClipperLib::ptSubject);
    ClipperLib::PolyTree* polytree = new ClipperLib::PolyTree();
    clipper.Execute(ClipperLib::ctUnion, *polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd);  // offset results work with both EvenOdd and NonZero
    
    // write to ExPolygons object
    output.clear();
    PolyTreeToExPolygons(*polytree, output);
    
    delete polytree;
}

void
Slic3rPolygon_to_ClipperPolygon(const Slic3r::Polygon &input, ClipperLib::Polygon &output)
{
    output.clear();
    for (Slic3r::Points::const_iterator pit = input.points.begin(); pit != input.points.end(); ++pit) {
        output.push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
    }
}

void
Slic3rPolygons_to_ClipperPolygons(const Slic3r::Polygons &input, ClipperLib::Polygons &output)
{
    output.clear();
    for (Slic3r::Polygons::const_iterator it = input.begin(); it != input.end(); ++it) {
        ClipperLib::Polygon p;
        Slic3rPolygon_to_ClipperPolygon(*it, p);
        output.push_back(p);
    }
}

void
scaleClipperPolygons(ClipperLib::Polygons &polygons, const double scale)
{
    for (ClipperLib::Polygons::iterator it = polygons.begin(); it != polygons.end(); ++it) {
        for (ClipperLib::Polygon::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            (*pit).X *= scale;
            (*pit).Y *= scale;
        }
    }
}

void
offset(Slic3r::Polygons &polygons, ClipperLib::Polygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // read input
    ClipperLib::Polygons* input = new ClipperLib::Polygons();
    Slic3rPolygons_to_ClipperPolygons(polygons, *input);
    
    // scale input
    scaleClipperPolygons(*input, scale);
    
    // perform offset
    ClipperLib::OffsetPolygons(*input, retval, (delta*scale), joinType, miterLimit);
    delete input;
    
    // unscale output
    scaleClipperPolygons(retval, 1/scale);
}

void
offset(Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    offset(polygons, *output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPolygons_to_Slic3rPolygons(*output, retval);
    delete output;
}

void
offset_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // perform offset
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    offset(polygons, *output, delta, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPolygons_to_Slic3rExPolygons(*output, retval);
    delete output;
}

void
offset2(Slic3r::Polygons &polygons, ClipperLib::Polygons &retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // read input
    ClipperLib::Polygons* input = new ClipperLib::Polygons();
    Slic3rPolygons_to_ClipperPolygons(polygons, *input);
    
    // scale input
    scaleClipperPolygons(*input, scale);
    
    // perform first offset
    ClipperLib::Polygons* output1 = new ClipperLib::Polygons();
    ClipperLib::OffsetPolygons(*input, *output1, (delta1*scale), joinType, miterLimit);
    delete input;
    
    // perform second offset
    ClipperLib::OffsetPolygons(*output1, retval, (delta2*scale), joinType, miterLimit);
    delete output1;
    
    // unscale output
    scaleClipperPolygons(retval, 1/scale);
}

void
offset2(Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    offset2(polygons, *output, delta1, delta2, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPolygons_to_Slic3rPolygons(*output, retval);
    delete output;
}

void
offset2_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta1,
    const float delta2, const double scale, const ClipperLib::JoinType joinType, const double miterLimit)
{
    // perform offset
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    offset2(polygons, *output, delta1, delta2, scale, joinType, miterLimit);
    
    // convert into ExPolygons
    ClipperPolygons_to_Slic3rExPolygons(*output, retval);
    delete output;
}

template <class T>
void _clipper_do(const ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, T &retval, const ClipperLib::PolyFillType fillType, const bool safety_offset_)
{
    // read input
    ClipperLib::Polygons* input_subject = new ClipperLib::Polygons();
    ClipperLib::Polygons* input_clip    = new ClipperLib::Polygons();
    Slic3rPolygons_to_ClipperPolygons(subject, *input_subject);
    Slic3rPolygons_to_ClipperPolygons(clip,    *input_clip);
    
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
    clipper.AddPolygons(*input_subject, ClipperLib::ptSubject);
    delete input_subject;
    clipper.AddPolygons(*input_clip, ClipperLib::ptClip);
    delete input_clip;
    
    // perform operation
    clipper.Execute(clipType, retval, fillType, fillType);
}

void _clipper(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_)
{
    // perform operation
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    _clipper_do<ClipperLib::Polygons>(clipType, subject, clip, *output, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into Polygons
    ClipperPolygons_to_Slic3rPolygons(*output, retval);
    delete output;
}

void _clipper(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_)
{
    // perform operation
    ClipperLib::PolyTree* polytree = new ClipperLib::PolyTree();
    _clipper_do<ClipperLib::PolyTree>(clipType, subject, clip, *polytree, ClipperLib::pftNonZero, safety_offset_);
    
    // convert into ExPolygons
    PolyTreeToExPolygons(*polytree, retval);
    delete polytree;
}

template <class T>
void diff(Slic3r::Polygons &subject, Slic3r::Polygons &clip, T &retval, bool safety_offset_)
{
    _clipper(ClipperLib::ctDifference, subject, clip, retval, safety_offset_);
}
template void diff<Slic3r::ExPolygons>(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_);
template void diff<Slic3r::Polygons>(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_);

template <class T>
void intersection(Slic3r::Polygons &subject, Slic3r::Polygons &clip, T &retval, bool safety_offset_)
{
    _clipper(ClipperLib::ctIntersection, subject, clip, retval, safety_offset_);
}
template void intersection<Slic3r::ExPolygons>(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_);
template void intersection<Slic3r::Polygons>(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_);

void xor_ex(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, 
    bool safety_offset_)
{
    _clipper(ClipperLib::ctXor, subject, clip, retval, safety_offset_);
}

template <class T>
void union_(Slic3r::Polygons &subject, T &retval, bool safety_offset_)
{
    Slic3r::Polygons p;
    _clipper(ClipperLib::ctUnion, subject, p, retval, safety_offset_);
}
template void union_<Slic3r::ExPolygons>(Slic3r::Polygons &subject, Slic3r::ExPolygons &retval, bool safety_offset_);
template void union_<Slic3r::Polygons>(Slic3r::Polygons &subject, Slic3r::Polygons &retval, bool safety_offset_);

void union_pt(Slic3r::Polygons &subject, ClipperLib::PolyTree &retval, bool safety_offset_)
{
    Slic3r::Polygons clip;
    _clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, subject, clip, retval, ClipperLib::pftEvenOdd, safety_offset_);
}

void simplify_polygons(Slic3r::Polygons &subject, Slic3r::Polygons &retval)
{
    // convert into Clipper polygons
    ClipperLib::Polygons* input_subject = new ClipperLib::Polygons();
    Slic3rPolygons_to_ClipperPolygons(subject, *input_subject);
    
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    ClipperLib::SimplifyPolygons(*input_subject, *output, ClipperLib::pftNonZero);
    delete input_subject;
    
    // convert into Slic3r polygons
    ClipperPolygons_to_Slic3rPolygons(*output, retval);
    delete output;
}

void safety_offset(ClipperLib::Polygons* &subject)
{
    // scale input
    scaleClipperPolygons(*subject, CLIPPER_OFFSET_SCALE);
    
    // perform offset (delta = scale 1e-05)
    ClipperLib::Polygons* retval = new ClipperLib::Polygons();
    ClipperLib::OffsetPolygons(*subject, *retval, 10.0 * CLIPPER_OFFSET_SCALE, ClipperLib::jtMiter, 2);
    
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
    ClipperPolygon_to_Slic3rPolygon(node.Contour, p);
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
