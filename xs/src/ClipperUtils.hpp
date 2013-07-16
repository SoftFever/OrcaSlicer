#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

#include <myinit.h>
#include "clipper.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"

namespace Slic3r {


//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons& expolygons);
void PolyTreeToExPolygons(ClipperLib::PolyTree& polytree, Slic3r::ExPolygons& expolygons);
//-----------------------------------------------------------

void Slic3rPolygon_to_ClipperPolygon(Slic3r::Polygon &input, ClipperLib::Polygon &output);
void Slic3rPolygons_to_ClipperPolygons(Slic3r::Polygons &input, ClipperLib::Polygons &output);
void ClipperPolygon_to_Slic3rPolygon(ClipperLib::Polygon &input, Slic3r::Polygon &output);
void ClipperPolygons_to_Slic3rPolygons(ClipperLib::Polygons &input, Slic3r::Polygons &output);
void ClipperPolygons_to_Slic3rExPolygons(ClipperLib::Polygons &input, Slic3r::ExPolygons &output);

void scaleClipperPolygons(ClipperLib::Polygons &polygons, const double scale);

void offset(Slic3r::Polygons &polygons, ClipperLib::Polygons &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset(Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

void offset2(Slic3r::Polygons &polygons, ClipperLib::Polygons &retval, const float delta1,
    const float delta2, double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset2(Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta1,
    const float delta2, double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset2_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta1,
    const float delta2, double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

template <class T>
void _clipper_do(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, T &retval, bool safety_offset);
void _clipper(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset);
void _clipper(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset);

template <class T>
void diff(Slic3r::Polygons &subject, Slic3r::Polygons &clip, T &retval, bool safety_offset);

template <class T>
void intersection(Slic3r::Polygons &subject, Slic3r::Polygons &clip, T &retval, bool safety_offset);

void xor_ex(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, 
    bool safety_offset = false);

void union_ex(Slic3r::Polygons &subject, Slic3r::ExPolygons &retval, bool safety_offset = false);

}

#endif
