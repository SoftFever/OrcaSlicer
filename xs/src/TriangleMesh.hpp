#ifndef slic3r_TriangleMesh_hpp_
#define slic3r_TriangleMesh_hpp_

#include <myinit.h>
#include <admesh/stl.h>
#include <vector>
#include "Point.hpp"
#include "Polygon.hpp"

namespace Slic3r {

class TriangleMesh;
typedef std::vector<TriangleMesh*> TriangleMeshPtrs;

class TriangleMesh
{
    public:
    TriangleMesh();
    TriangleMesh(const TriangleMesh &other);
    ~TriangleMesh();
    void ReadSTLFile(char* input_file);
    void write_ascii(char* output_file);
    void write_binary(char* output_file);
    void repair();
    void WriteOBJFile(char* output_file);
    void scale(float factor);
    void scale(std::vector<double> versor);
    void translate(float x, float y, float z);
    void align_to_origin();
    void rotate(double angle, Point* center);
    std::vector<Polygons>* slice(const std::vector<double> &z);
    TriangleMeshPtrs split() const;
    void merge(const TriangleMesh* mesh);
    stl_file stl;
    bool repaired;
    
    #ifdef SLIC3RXS
    SV* to_SV();
    void ReadFromPerl(SV* vertices, SV* facets);
    #endif
};

enum FacetEdgeType { feNone, feTop, feBottom };

class IntersectionPoint : public Point
{
    public:
    int point_id;
    int edge_id;
    IntersectionPoint() : point_id(-1), edge_id(-1) {};
};

class IntersectionLine
{
    public:
    Point           a;
    Point           b;
    int             a_id;
    int             b_id;
    int             edge_a_id;
    int             edge_b_id;
    FacetEdgeType   edge_type;
    bool            skip;
    IntersectionLine() : a_id(-1), b_id(-1), edge_a_id(-1), edge_b_id(-1), edge_type(feNone), skip(false) {};
};
typedef std::vector<IntersectionLine> IntersectionLines;
typedef std::vector<IntersectionLine*> IntersectionLinePtrs;

}

#endif
