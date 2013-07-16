#include "ClipperUtils.hpp"

namespace Slic3r {

void
ClipperPolygon_to_Slic3rPolygon(ClipperLib::Polygon &input, Slic3r::Polygon &output)
{
    output.points.clear();
    for (ClipperLib::Polygon::iterator pit = input.begin(); pit != input.end(); ++pit) {
        output.points.push_back(Slic3r::Point( (*pit).X, (*pit).Y ));
    }
}

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
Slic3rPolygon_to_ClipperPolygon(Slic3r::Polygon &input, ClipperLib::Polygon &output)
{
    output.clear();
    for (Slic3r::Points::iterator pit = input.points.begin(); pit != input.points.end(); ++pit) {
        output.push_back(ClipperLib::IntPoint( (*pit).x, (*pit).y ));
    }
}

void
Slic3rPolygons_to_ClipperPolygons(Slic3r::Polygons &input, ClipperLib::Polygons &output)
{
    output.clear();
    for (Slic3r::Polygons::iterator it = input.begin(); it != input.end(); ++it) {
        ClipperLib::Polygon p;
        Slic3rPolygon_to_ClipperPolygon(*it, p);
        output.push_back(p);
    }
}

void
ClipperPolygons_to_Slic3rExPolygons(ClipperLib::Polygons &input, Slic3r::ExPolygons &output)
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
offset_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta,
    double scale, ClipperLib::JoinType joinType, double miterLimit)
{
    // read input
    ClipperLib::Polygons* input = new ClipperLib::Polygons();
    Slic3rPolygons_to_ClipperPolygons(polygons, *input);
    
    // scale input
    scaleClipperPolygons(*input, scale);
    
    // perform offset
    ClipperLib::Polygons* output = new ClipperLib::Polygons();
    ClipperLib::OffsetPolygons(*input, *output, (delta*scale), joinType, miterLimit);
    delete input;
    
    // unscale output
    scaleClipperPolygons(*output, 1/scale);
    
    // convert into ExPolygons
    ClipperPolygons_to_Slic3rExPolygons(*output, retval);
    delete output;
}

void
offset2_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta1,
    const float delta2, double scale, ClipperLib::JoinType joinType, double miterLimit)
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
    ClipperLib::Polygons* output2 = new ClipperLib::Polygons();
    ClipperLib::OffsetPolygons(*output1, *output2, (delta2*scale), joinType, miterLimit);
    delete output1;
    
    // unscale output
    scaleClipperPolygons(*output2, 1/scale);
    
    // convert into ExPolygons
    ClipperPolygons_to_Slic3rExPolygons(*output2, retval);
    delete output2;
}

void
diff_ex(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset)
{
    // read input
    ClipperLib::Polygons* input_subject = new ClipperLib::Polygons();
    ClipperLib::Polygons* input_clip    = new ClipperLib::Polygons();
    Slic3rPolygons_to_ClipperPolygons(subject, *input_subject);
    Slic3rPolygons_to_ClipperPolygons(clip,    *input_clip);
    
    // perform safety offset
    if (safety_offset) {
        // SafetyOffset(*input_clip);
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
    ClipperLib::PolyTree* polytree = new ClipperLib::PolyTree();
    clipper.Execute(ClipperLib::ctDifference, *polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    
    // convert into ExPolygons
    PolyTreeToExPolygons(*polytree, retval);
    delete polytree;
}



}
