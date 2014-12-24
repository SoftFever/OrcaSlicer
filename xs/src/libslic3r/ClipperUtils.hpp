#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

#include <myinit.h>
#include "clipper.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Surface.hpp"

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

void Slic3rMultiPoint_to_ClipperPath(const Slic3r::MultiPoint &input, ClipperLib::Path* output);
template <class T>
void Slic3rMultiPoints_to_ClipperPaths(const T &input, ClipperLib::Paths* output);
template <class T>
void ClipperPath_to_Slic3rMultiPoint(const ClipperLib::Path &input, T* output);
template <class T>
void ClipperPaths_to_Slic3rMultiPoints(const ClipperLib::Paths &input, T* output);
void ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input, Slic3r::ExPolygons* output);

void scaleClipperPolygons(ClipperLib::Paths &polygons, const double scale);

// offset Polygons
void offset(const Slic3r::Polygons &polygons, ClipperLib::Paths* retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset(const Slic3r::Polygons &polygons, Slic3r::Polygons* retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

// offset Polylines
void offset(const Slic3r::Polylines &polylines, ClipperLib::Paths* retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtSquare, 
    double miterLimit = 3);
void offset(const Slic3r::Polylines &polylines, Slic3r::Polygons* retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtSquare, 
    double miterLimit = 3);
void offset(const Slic3r::Surface &surface, Slic3r::Surfaces* retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtSquare, 
    double miterLimit = 3);

void offset(const Slic3r::Polygons &polygons, Slic3r::ExPolygons* retval, const float delta,
    double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

void offset2(const Slic3r::Polygons &polygons, ClipperLib::Paths* retval, const float delta1,
    const float delta2, double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset2(const Slic3r::Polygons &polygons, Slic3r::Polygons* retval, const float delta1,
    const float delta2, double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
void offset2(const Slic3r::Polygons &polygons, Slic3r::ExPolygons* retval, const float delta1,
    const float delta2, double scale = 100000, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

template <class T>
void _clipper_do(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, T* retval, bool safety_offset_);
void _clipper_do(ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, ClipperLib::Paths* retval, bool safety_offset_);
void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polygons* retval, bool safety_offset_);
void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, Slic3r::ExPolygons* retval, bool safety_offset_);
void _clipper(ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, Slic3r::Polylines* retval);
void _clipper(ClipperLib::ClipType clipType, const Slic3r::Lines &subject, 
    const Slic3r::Polygons &clip, Slic3r::Lines* retval);

template <class SubjectType, class ResultType>
void diff(const SubjectType &subject, const Slic3r::Polygons &clip, ResultType* retval, bool safety_offset_ = false);

template <class SubjectType, class ResultType>
void diff(const SubjectType &subject, const Slic3r::ExPolygons &clip, ResultType* retval, bool safety_offset_ = false);

template <class SubjectType, class ResultType>
void intersection(const SubjectType &subject, const Slic3r::Polygons &clip, ResultType* retval, bool safety_offset_ = false);

template <class SubjectType>
bool intersects(const SubjectType &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false);

void xor_(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, Slic3r::ExPolygons* retval, 
    bool safety_offset_ = false);

template <class T>
void union_(const Slic3r::Polygons &subject, T* retval, bool safety_offset_ = false);

void union_(const Slic3r::Polygons &subject1, const Slic3r::Polygons &subject2, Slic3r::Polygons* retval, bool safety_offset = false);

void union_pt(const Slic3r::Polygons &subject, ClipperLib::PolyTree* retval, bool safety_offset_ = false);
void union_pt_chained(const Slic3r::Polygons &subject, Slic3r::Polygons* retval, bool safety_offset_ = false);
static void traverse_pt(ClipperLib::PolyNodes &nodes, Slic3r::Polygons* retval);

void simplify_polygons(const Slic3r::Polygons &subject, Slic3r::Polygons* retval, bool preserve_collinear = false);
void simplify_polygons(const Slic3r::Polygons &subject, Slic3r::ExPolygons* retval, bool preserve_collinear = false);

void safety_offset(ClipperLib::Paths* paths);

/////////////////

#ifdef SLIC3RXS
SV* polynode_children_2_perl(const ClipperLib::PolyNode& node);
SV* polynode2perl(const ClipperLib::PolyNode& node);
#endif

}

#endif
