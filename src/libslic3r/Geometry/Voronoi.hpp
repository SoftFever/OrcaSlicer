#ifndef slic3r_Geometry_Voronoi_hpp_
#define slic3r_Geometry_Voronoi_hpp_

#include "../Line.hpp"
#include "../Polyline.hpp"

#define BOOST_VORONOI_USE_GMP 1

#ifdef _MSC_VER
// Suppress warning C4146 in OpenVDB: unary minus operator applied to unsigned type, result still unsigned 
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include "boost/polygon/voronoi.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

namespace Slic3r { 

namespace Geometry {

class VoronoiDiagram : public boost::polygon::voronoi_diagram<double> {
public:
    typedef double                                          coord_type;
    typedef boost::polygon::point_data<coordinate_type>     point_type;
    typedef boost::polygon::segment_data<coordinate_type>   segment_type;
    typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
};

} } // namespace Slicer::Geometry

#endif // slic3r_Geometry_Voronoi_hpp_
