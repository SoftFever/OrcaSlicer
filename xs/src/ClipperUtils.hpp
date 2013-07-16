#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "clipper.hpp"

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


}

#endif
