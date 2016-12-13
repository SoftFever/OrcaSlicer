#ifndef slic3r_ClipperUtils_hpp_
#define slic3r_ClipperUtils_hpp_

#include <libslic3r.h>
#include "clipper.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Surface.hpp"

// import these wherever we're included
using ClipperLib::jtMiter;
using ClipperLib::jtRound;
using ClipperLib::jtSquare;

namespace Slic3r {

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons& expolygons);
void PolyTreeToExPolygons(ClipperLib::PolyTree& polytree, Slic3r::ExPolygons& expolygons);
//-----------------------------------------------------------

ClipperLib::Path Slic3rMultiPoint_to_ClipperPath(const Slic3r::MultiPoint &input);
template <class T>
ClipperLib::Paths Slic3rMultiPoints_to_ClipperPaths(const T &input);
template <class T>
T ClipperPath_to_Slic3rMultiPoint(const ClipperLib::Path &input);
template <class T>
T ClipperPaths_to_Slic3rMultiPoints(const ClipperLib::Paths &input);
Slic3r::ExPolygons ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input);

// offset Polygons
ClipperLib::Paths _offset(ClipperLib::Path &input, ClipperLib::EndType endType, const float delta, ClipperLib::JoinType joinType, double miterLimit);
ClipperLib::Paths _offset(ClipperLib::Paths &input, ClipperLib::EndType endType, const float delta, ClipperLib::JoinType joinType, double miterLimit);
inline Slic3r::Polygons offset(const Slic3r::Polygon &polygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter,  double miterLimit = 3)
    { return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_offset(Slic3rMultiPoint_to_ClipperPath(polygon), ClipperLib::etClosedPolygon, delta, joinType, miterLimit)); }
inline Slic3r::Polygons offset(const Slic3r::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_offset(Slic3rMultiPoints_to_ClipperPaths(polygons), ClipperLib::etClosedPolygon, delta, joinType, miterLimit)); }

// offset Polylines
inline Slic3r::Polygons offset(const Slic3r::Polyline &polyline, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtSquare, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_offset(Slic3rMultiPoint_to_ClipperPath(polyline), ClipperLib::etOpenButt, delta, joinType, miterLimit)); }
inline Slic3r::Polygons offset(const Slic3r::Polylines &polylines, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtSquare, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_offset(Slic3rMultiPoints_to_ClipperPaths(polylines), ClipperLib::etOpenButt, delta, joinType, miterLimit)); }

// offset expolygons and surfaces
ClipperLib::Paths _offset(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType, double miterLimit);
ClipperLib::Paths _offset(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType, double miterLimit);
inline Slic3r::Polygons offset(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_offset(expolygon, delta, joinType, miterLimit)); }
inline Slic3r::Polygons offset(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rMultiPoints<Polygons>(_offset(expolygons, delta, joinType, miterLimit)); }
inline Slic3r::ExPolygons offset_ex(const Slic3r::Polygon &polygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rExPolygons(_offset(Slic3rMultiPoint_to_ClipperPath(polygon), ClipperLib::etClosedPolygon, delta, joinType, miterLimit)); }    
inline Slic3r::ExPolygons offset_ex(const Slic3r::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rExPolygons(_offset(Slic3rMultiPoints_to_ClipperPaths(polygons), ClipperLib::etClosedPolygon, delta, joinType, miterLimit)); }
inline Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rExPolygons(_offset(expolygon, delta, joinType, miterLimit)); }
inline Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rExPolygons(_offset(expolygons, delta, joinType, miterLimit)); }

ClipperLib::Paths _offset2(const Slic3r::Polygons &polygons, const float delta1,
    const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
Slic3r::Polygons offset2(const Slic3r::Polygons &polygons, const float delta1,
    const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);
Slic3r::ExPolygons offset2_ex(const Slic3r::Polygons &polygons, const float delta1,
    const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

template <class T>
T _clipper_do(ClipperLib::ClipType clipType, const Slic3r::Polygons &subject, 
    const Slic3r::Polygons &clip, const ClipperLib::PolyFillType fillType, bool safety_offset_ = false);

ClipperLib::PolyTree _clipper_do(ClipperLib::ClipType clipType, const Slic3r::Polylines &subject, 
    const Slic3r::Polygons &clip, const ClipperLib::PolyFillType fillType, bool safety_offset_ = false);

Slic3r::Polygons _clipper(ClipperLib::ClipType clipType,
    const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false);
Slic3r::ExPolygons _clipper_ex(ClipperLib::ClipType clipType,
    const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false);
Slic3r::Polylines _clipper_pl(ClipperLib::ClipType clipType,
    const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false);
Slic3r::Polylines _clipper_pl(ClipperLib::ClipType clipType,
    const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false);
Slic3r::Lines _clipper_ln(ClipperLib::ClipType clipType,
    const Slic3r::Lines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false);

// diff
inline Slic3r::Polygons
diff(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctDifference, subject, clip, safety_offset_);
}

inline Slic3r::ExPolygons
diff_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctDifference, subject, clip, safety_offset_);
}

inline Slic3r::ExPolygons
diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctDifference, to_polygons(subject), to_polygons(clip), safety_offset_);
}

inline Slic3r::Polygons
diff(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctDifference, to_polygons(subject), to_polygons(clip), safety_offset_);
}

inline Slic3r::Polylines
diff_pl(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_pl(ClipperLib::ctDifference, subject, clip, safety_offset_);
}

inline Slic3r::Polylines
diff_pl(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_pl(ClipperLib::ctDifference, subject, clip, safety_offset_);
}

inline Slic3r::Lines
diff_ln(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_ln(ClipperLib::ctDifference, subject, clip, safety_offset_);
}

// intersection
inline Slic3r::Polygons
intersection(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctIntersection, subject, clip, safety_offset_);
}

inline Slic3r::ExPolygons
intersection_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctIntersection, subject, clip, safety_offset_);
}

inline Slic3r::ExPolygons
intersection_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctIntersection, to_polygons(subject), to_polygons(clip), safety_offset_);
}

inline Slic3r::Polygons
intersection(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctIntersection, to_polygons(subject), to_polygons(clip), safety_offset_);
}

inline Slic3r::Polylines
intersection_pl(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_pl(ClipperLib::ctIntersection, subject, clip, safety_offset_);
}

inline Slic3r::Polylines
intersection_pl(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_pl(ClipperLib::ctIntersection, subject, clip, safety_offset_);
}

inline Slic3r::Lines
intersection_ln(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_ln(ClipperLib::ctIntersection, subject, clip, safety_offset_);
}

// union
inline Slic3r::Polygons
union_(const Slic3r::Polygons &subject, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctUnion, subject, Slic3r::Polygons(), safety_offset_);
}

inline Slic3r::Polygons
union_(const Slic3r::Polygons &subject, const Slic3r::Polygons &subject2, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctUnion, subject, subject2, safety_offset_);
}

inline Slic3r::ExPolygons
union_ex(const Slic3r::Polygons &subject, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctUnion, subject, Slic3r::Polygons(), safety_offset_);
}

inline Slic3r::ExPolygons
union_ex(const Slic3r::ExPolygons &subject, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctUnion, to_polygons(subject), Slic3r::Polygons(), safety_offset_);
}

inline Slic3r::ExPolygons
union_ex(const Slic3r::Surfaces &subject, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctUnion, to_polygons(subject), Slic3r::Polygons(), safety_offset_);
}


ClipperLib::PolyTree union_pt(const Slic3r::Polygons &subject, bool safety_offset_ = false);
Slic3r::Polygons union_pt_chained(const Slic3r::Polygons &subject, bool safety_offset_ = false);
void traverse_pt(ClipperLib::PolyNodes &nodes, Slic3r::Polygons* retval);

/* OTHER */
Slic3r::Polygons simplify_polygons(const Slic3r::Polygons &subject, bool preserve_collinear = false);
Slic3r::ExPolygons simplify_polygons_ex(const Slic3r::Polygons &subject, bool preserve_collinear = false);

void safety_offset(ClipperLib::Paths* paths);

Polygons top_level_islands(const Slic3r::Polygons &polygons);

}

#endif