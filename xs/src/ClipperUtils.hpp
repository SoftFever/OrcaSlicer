#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

#include <myinit.h>
#include "clipper.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"

// import these wherever we're included
using ClipperLib::jtMiter;
using ClipperLib::jtRound;
using ClipperLib::jtSquare;

namespace Slic3r {

#define CLIPPER_OFFSET_SCALE 100000.0

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons& expolygons);
void PolyTreeToExPolygons(ClipperLib::PolyTree& polytree, Slic3r::ExPolygons& expolygons);
//-----------------------------------------------------------

void Slic3rMultiPoint_to_ClipperPath(const Slic3r::MultiPoint &input, ClipperLib::Path &output);
template <class T>
void Slic3rMultiPoints_to_ClipperPaths(const T &input, ClipperLib::Paths &output);
template <class T>
void ClipperPath_to_Slic3rMultiPoint(const ClipperLib::Path &input, T &output);
template <class T>
void ClipperPaths_to_Slic3rMultiPoints(const ClipperLib::Paths &input, T &output);
void ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input, Slic3r::ExPolygons &output);

void scaleClipperPolygons(ClipperLib::Paths &polygons, const double scale);

// offset Polygons
void offset(Slic3r::Polygons &polygons, ClipperLib::Paths &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset(Slic3r::Polygons &polygons, Slic3r::Polygons &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

// offset Polylines
void offset(Slic3r::Polylines &polylines, ClipperLib::Paths &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtSquare, 
    double miterLimit = 3);
void offset(Slic3r::Polylines &polylines, Slic3r::Polygons &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtSquare, 
    double miterLimit = 3);

void offset_ex(Slic3r::Polygons &polygons, Slic3r::ExPolygons &retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

void offset2(Slic3r::Polygons &polygons, ClipperLib::Paths &retval, const float delta1,
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
    Slic3r::Polygons &clip, T &retval, bool safety_offset_);
void _clipper_do(ClipperLib::ClipType clipType, Slic3r::Polylines &subject, 
    Slic3r::Polygons &clip, ClipperLib::Paths &retval);
void _clipper(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, Slic3r::Polygons &retval, bool safety_offset_);
void _clipper(ClipperLib::ClipType clipType, Slic3r::Polygons &subject, 
    Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, bool safety_offset_);
void _clipper(ClipperLib::ClipType clipType, Slic3r::Polylines &subject, 
    Slic3r::Polygons &clip, Slic3r::Polylines &retval);

template <class T>
void diff(Slic3r::Polygons &subject, Slic3r::Polygons &clip, T &retval, bool safety_offset_);

void diff(Slic3r::Polylines &subject, Slic3r::Polygons &clip, Slic3r::Polylines &retval);

template <class T>
void intersection(Slic3r::Polygons &subject, Slic3r::Polygons &clip, T &retval, bool safety_offset_);

void intersection(Slic3r::Polylines &subject, Slic3r::Polygons &clip, Slic3r::Polylines &retval);

void xor_ex(Slic3r::Polygons &subject, Slic3r::Polygons &clip, Slic3r::ExPolygons &retval, 
    bool safety_offset_ = false);

template <class T>
void union_(Slic3r::Polygons &subject, T &retval, bool safety_offset_ = false);

void union_pt(Slic3r::Polygons &subject, ClipperLib::PolyTree &retval, bool safety_offset_ = false);

void simplify_polygons(Slic3r::Polygons &subject, Slic3r::Polygons &retval);

void safety_offset(ClipperLib::Paths* &subject);

/////////////////

#ifdef SLIC3RXS
SV* polynode_children_2_perl(const ClipperLib::PolyNode& node);
SV* polynode2perl(const ClipperLib::PolyNode& node);
#endif

}

#endif
