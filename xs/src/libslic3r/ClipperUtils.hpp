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

// Factor to convert from coord_t (which is int32) to an int64 type used by the Clipper library
// for general offsetting (the offset(), offset2(), offset_ex() functions) and for the safety offset,
// which is optionally executed by other functions (union, intersection, diff).
// This scaling (cca 130t) is applied over the usual SCALING_FACTOR.
// By the way, is the scalling for offset needed at all?
// The reason to apply this scaling may be to match the resolution of the double mantissa.
#define CLIPPER_OFFSET_POWER_OF_2 17
// 2^17=131072
#define CLIPPER_OFFSET_SCALE (1 << CLIPPER_OFFSET_POWER_OF_2)
#define CLIPPER_OFFSET_SCALE_ROUNDING_DELTA ((1 << (CLIPPER_OFFSET_POWER_OF_2 - 1)) - 1)
#define CLIPPER_MAX_COORD_UNSCALED (ClipperLib::hiRange / CLIPPER_OFFSET_SCALE)

namespace Slic3r {

//-----------------------------------------------------------
// legacy code from Clipper documentation
void AddOuterPolyNodeToExPolygons(ClipperLib::PolyNode& polynode, Slic3r::ExPolygons& expolygons);
void PolyTreeToExPolygons(ClipperLib::PolyTree& polytree, Slic3r::ExPolygons& expolygons);
//-----------------------------------------------------------

ClipperLib::Path   Slic3rMultiPoint_to_ClipperPath(const Slic3r::MultiPoint &input);
ClipperLib::Paths  Slic3rMultiPoints_to_ClipperPaths(const Polygons &input);
ClipperLib::Paths  Slic3rMultiPoints_to_ClipperPaths(const Polylines &input);
Slic3r::Polygon    ClipperPath_to_Slic3rPolygon(const ClipperLib::Path &input);
Slic3r::Polyline   ClipperPath_to_Slic3rPolyline(const ClipperLib::Path &input);
Slic3r::Polygons   ClipperPaths_to_Slic3rPolygons(const ClipperLib::Paths &input);
Slic3r::Polylines  ClipperPaths_to_Slic3rPolylines(const ClipperLib::Paths &input);
Slic3r::ExPolygons ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input);

// offset Polygons
ClipperLib::Paths _offset(ClipperLib::Path &&input, ClipperLib::EndType endType, const float delta, ClipperLib::JoinType joinType, double miterLimit);
ClipperLib::Paths _offset(ClipperLib::Paths &&input, ClipperLib::EndType endType, const float delta, ClipperLib::JoinType joinType, double miterLimit);
inline Slic3r::Polygons offset(const Slic3r::Polygon &polygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter,  double miterLimit = 3)
    { return ClipperPaths_to_Slic3rPolygons(_offset(Slic3rMultiPoint_to_ClipperPath(polygon), ClipperLib::etClosedPolygon, delta, joinType, miterLimit)); }
inline Slic3r::Polygons offset(const Slic3r::Polygons &polygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rPolygons(_offset(Slic3rMultiPoints_to_ClipperPaths(polygons), ClipperLib::etClosedPolygon, delta, joinType, miterLimit)); }

// offset Polylines
inline Slic3r::Polygons offset(const Slic3r::Polyline &polyline, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtSquare, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rPolygons(_offset(Slic3rMultiPoint_to_ClipperPath(polyline), ClipperLib::etOpenButt, delta, joinType, miterLimit)); }
inline Slic3r::Polygons offset(const Slic3r::Polylines &polylines, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtSquare, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rPolygons(_offset(Slic3rMultiPoints_to_ClipperPaths(polylines), ClipperLib::etOpenButt, delta, joinType, miterLimit)); }

// offset expolygons and surfaces
ClipperLib::Paths _offset(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType, double miterLimit);
ClipperLib::Paths _offset(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType, double miterLimit);
inline Slic3r::Polygons offset(const Slic3r::ExPolygon &expolygon, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rPolygons(_offset(expolygon, delta, joinType, miterLimit)); }
inline Slic3r::Polygons offset(const Slic3r::ExPolygons &expolygons, const float delta, ClipperLib::JoinType joinType = ClipperLib::jtMiter, double miterLimit = 3)
    { return ClipperPaths_to_Slic3rPolygons(_offset(expolygons, delta, joinType, miterLimit)); }
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
Slic3r::ExPolygons offset2_ex(const Slic3r::ExPolygons &expolygons, const float delta1,
    const float delta2, ClipperLib::JoinType joinType = ClipperLib::jtMiter, 
    double miterLimit = 3);

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

inline Slic3r::Lines intersection_ln(const Slic3r::Lines &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    return _clipper_ln(ClipperLib::ctIntersection, subject, clip, safety_offset_);
}

inline Slic3r::Lines intersection_ln(const Slic3r::Line &subject, const Slic3r::Polygons &clip, bool safety_offset_ = false)
{
    Slic3r::Lines lines;
    lines.emplace_back(subject);
    return _clipper_ln(ClipperLib::ctIntersection, lines, clip, safety_offset_);
}

// union
inline Slic3r::Polygons union_(const Slic3r::Polygons &subject, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctUnion, subject, Slic3r::Polygons(), safety_offset_);
}

inline Slic3r::Polygons union_(const Slic3r::Polygons &subject, const Slic3r::Polygons &subject2, bool safety_offset_ = false)
{
    return _clipper(ClipperLib::ctUnion, subject, subject2, safety_offset_);
}

inline Slic3r::ExPolygons union_ex(const Slic3r::Polygons &subject, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctUnion, subject, Slic3r::Polygons(), safety_offset_);
}

inline Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons &subject, bool safety_offset_ = false)
{
    return _clipper_ex(ClipperLib::ctUnion, to_polygons(subject), Slic3r::Polygons(), safety_offset_);
}

inline Slic3r::ExPolygons union_ex(const Slic3r::Surfaces &subject, bool safety_offset_ = false)
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