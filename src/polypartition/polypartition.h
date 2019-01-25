//Copyright (C) 2011 by Ivan Fratric
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions:
//
//The above copyright notice and this permission notice shall be included in
//all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

#ifndef POLYPARTITION_H
#define POLYPARTITION_H

#include <list>
#include <set>

typedef double tppl_float;

#define TPPL_CCW 1
#define TPPL_CW -1

//2D point structure
struct TPPLPoint {
    tppl_float x;
    tppl_float y;
    // User-specified vertex identifier.  Note that this isn't used internally
    // by the library, but will be faithfully copied around.
    int id;
    
    TPPLPoint operator + (const TPPLPoint& p) const {
        TPPLPoint r;
        r.x = x + p.x;
        r.y = y + p.y;
        return r;
    }
    
    TPPLPoint operator - (const TPPLPoint& p) const {
        TPPLPoint r;
        r.x = x - p.x;
        r.y = y - p.y;
        return r;
    }
    
    TPPLPoint operator * (const tppl_float f ) const {
        TPPLPoint r;
        r.x = x*f;
        r.y = y*f;
        return r;
    }
    
    TPPLPoint operator / (const tppl_float f ) const {
        TPPLPoint r;
        r.x = x/f;
        r.y = y/f;
        return r;
    }
    
    bool operator==(const TPPLPoint& p) const {
        if((x == p.x)&&(y==p.y)) return true;
        else return false;
    }
    
    bool operator!=(const TPPLPoint& p) const {
        if((x == p.x)&&(y==p.y)) return false;
        else return true;
    }
};


//Polygon implemented as an array of points with a 'hole' flag
class TPPLPoly {
    protected:
        
        TPPLPoint *points;
        long numpoints;
        bool hole;
        
    public:
        
        //constructors/destructors
        TPPLPoly();
        ~TPPLPoly();
        
        TPPLPoly(const TPPLPoly &src);
        TPPLPoly& operator=(const TPPLPoly &src);
        
        //getters and setters
        long GetNumPoints() const {
            return numpoints;
        }
        
        bool IsHole() const {
            return hole;
        }
        
        void SetHole(bool hole) {
            this->hole = hole;
        }
        
        TPPLPoint &GetPoint(long i) {
            return points[i];
        }
        
        const TPPLPoint &GetPoint(long i) const {
            return points[i];
        }

        TPPLPoint *GetPoints() {
            return points;
        }
        
        TPPLPoint& operator[] (int i) {
            return points[i];
        }

        const TPPLPoint& operator[] (int i) const {
            return points[i];
        }
        
        //clears the polygon points
        void Clear();
        
        //inits the polygon with numpoints vertices
        void Init(long numpoints);
        
        //creates a triangle with points p1,p2,p3
        void Triangle(TPPLPoint &p1, TPPLPoint &p2, TPPLPoint &p3);
        
        //inverts the orfer of vertices
        void Invert();
        
        //returns the orientation of the polygon
        //possible values:
        //   TPPL_CCW : polygon vertices are in counter-clockwise order
        //   TPPL_CW : polygon vertices are in clockwise order
        //       0 : the polygon has no (measurable) area
        int GetOrientation() const;
        
        //sets the polygon orientation
        //orientation can be
        //   TPPL_CCW : sets vertices in counter-clockwise order
        //   TPPL_CW : sets vertices in clockwise order
        void SetOrientation(int orientation);

        //checks whether a polygon is valid or not
        inline bool Valid() const { return this->numpoints >= 3; }
};

#ifdef TPPL_ALLOCATOR
typedef std::list<TPPLPoly, TPPL_ALLOCATOR(TPPLPoly)> TPPLPolyList;
#else
typedef std::list<TPPLPoly> TPPLPolyList;
#endif

class TPPLPartition {
    protected:
        struct PartitionVertex {
            bool isActive;
            bool isConvex;
            bool isEar;
            
            TPPLPoint p;
            tppl_float angle;
            PartitionVertex *previous;
            PartitionVertex *next;
            
            PartitionVertex();
        };
        
        struct MonotoneVertex {
            TPPLPoint p;
            long previous;
            long next;
        };
        
        class VertexSorter{
            MonotoneVertex *vertices;
        public:
            VertexSorter(MonotoneVertex *v) : vertices(v) {}
            bool operator() (long index1, long index2);
        };
        
        struct Diagonal {
            long index1;
            long index2;
        };

#ifdef TPPL_ALLOCATOR
        typedef std::list<Diagonal, TPPL_ALLOCATOR(Diagonal)> DiagonalList;
#else
        typedef std::list<Diagonal> DiagonalList;
#endif
        
        //dynamic programming state for minimum-weight triangulation
        struct DPState {
            bool visible;
            tppl_float weight;
            long bestvertex;
        };
        
        //dynamic programming state for convex partitioning
        struct DPState2 {
            bool visible;
            long weight;
            DiagonalList pairs;
        };
        
        //edge that intersects the scanline
        struct ScanLineEdge {
            mutable long index;
            TPPLPoint p1;
            TPPLPoint p2;
            
            //determines if the edge is to the left of another edge
            bool operator< (const ScanLineEdge & other) const;
            
            bool IsConvex(const TPPLPoint& p1, const TPPLPoint& p2, const TPPLPoint& p3) const;
        };
        
        //standard helper functions
        bool IsConvex(TPPLPoint& p1, TPPLPoint& p2, TPPLPoint& p3);
        bool IsReflex(TPPLPoint& p1, TPPLPoint& p2, TPPLPoint& p3);
        bool IsInside(TPPLPoint& p1, TPPLPoint& p2, TPPLPoint& p3, TPPLPoint &p);
        
        bool InCone(TPPLPoint &p1, TPPLPoint &p2, TPPLPoint &p3, TPPLPoint &p);
        bool InCone(PartitionVertex *v, TPPLPoint &p);
        
        int Intersects(TPPLPoint &p11, TPPLPoint &p12, TPPLPoint &p21, TPPLPoint &p22);
        
        TPPLPoint Normalize(const TPPLPoint &p);
        tppl_float Distance(const TPPLPoint &p1, const TPPLPoint &p2);
        
        //helper functions for Triangulate_EC
        void UpdateVertexReflexity(PartitionVertex *v);
        void UpdateVertex(PartitionVertex *v,PartitionVertex *vertices, long numvertices);
        
        //helper functions for ConvexPartition_OPT
        void UpdateState(long a, long b, long w, long i, long j, DPState2 **dpstates);
        void TypeA(long i, long j, long k, PartitionVertex *vertices, DPState2 **dpstates);
        void TypeB(long i, long j, long k, PartitionVertex *vertices, DPState2 **dpstates);
        
        //helper functions for MonotonePartition
        bool Below(TPPLPoint &p1, TPPLPoint &p2);
        void AddDiagonal(MonotoneVertex *vertices, long *numvertices, long index1, long index2,
            char *vertextypes, std::set<ScanLineEdge>::iterator *edgeTreeIterators,
            std::set<ScanLineEdge> *edgeTree, long *helpers);
        
        //triangulates a monotone polygon, used in Triangulate_MONO
        int TriangulateMonotone(TPPLPoly *inPoly, TPPLPolyList *triangles);
        
    public:
        
        //simple heuristic procedure for removing holes from a list of polygons
        //works by creating a diagonal from the rightmost hole vertex to some visible vertex
        //time complexity: O(h*(n^2)), h is the number of holes, n is the number of vertices
        //space complexity: O(n)
        //params:
        //   inpolys : a list of polygons that can contain holes
        //             vertices of all non-hole polys have to be in counter-clockwise order
        //             vertices of all hole polys have to be in clockwise order
        //   outpolys : a list of polygons without holes
        //returns 1 on success, 0 on failure
        int RemoveHoles(TPPLPolyList *inpolys, TPPLPolyList *outpolys);
        
        //triangulates a polygon by ear clipping
        //time complexity O(n^2), n is the number of vertices
        //space complexity: O(n)
        //params:
        //   poly : an input polygon to be triangulated
        //          vertices have to be in counter-clockwise order
        //   triangles : a list of triangles (result)
        //returns 1 on success, 0 on failure
        int Triangulate_EC(TPPLPoly *poly, TPPLPolyList *triangles);
        
        //triangulates a list of polygons that may contain holes by ear clipping algorithm
        //first calls RemoveHoles to get rid of the holes, and then Triangulate_EC for each resulting polygon
        //time complexity: O(h*(n^2)), h is the number of holes, n is the number of vertices
        //space complexity: O(n)
        //params:
        //   inpolys : a list of polygons to be triangulated (can contain holes)
        //             vertices of all non-hole polys have to be in counter-clockwise order
        //             vertices of all hole polys have to be in clockwise order
        //   triangles : a list of triangles (result)
        //returns 1 on success, 0 on failure
        int Triangulate_EC(TPPLPolyList *inpolys, TPPLPolyList *triangles);
        
        //creates an optimal polygon triangulation in terms of minimal edge length
        //time complexity: O(n^3), n is the number of vertices
        //space complexity: O(n^2)
        //params:
        //   poly : an input polygon to be triangulated
        //          vertices have to be in counter-clockwise order
        //   triangles : a list of triangles (result)
        //returns 1 on success, 0 on failure
        int Triangulate_OPT(TPPLPoly *poly, TPPLPolyList *triangles);
        
        //triangulates a polygons by firstly partitioning it into monotone polygons
        //time complexity: O(n*log(n)), n is the number of vertices
        //space complexity: O(n)
        //params:
        //   poly : an input polygon to be triangulated
        //          vertices have to be in counter-clockwise order
        //   triangles : a list of triangles (result)
        //returns 1 on success, 0 on failure
        int Triangulate_MONO(TPPLPoly *poly, TPPLPolyList *triangles);
        
        //triangulates a list of polygons by firstly partitioning them into monotone polygons
        //time complexity: O(n*log(n)), n is the number of vertices
        //space complexity: O(n)
        //params:
        //   inpolys : a list of polygons to be triangulated (can contain holes)
        //             vertices of all non-hole polys have to be in counter-clockwise order
        //             vertices of all hole polys have to be in clockwise order
        //   triangles : a list of triangles (result)
        //returns 1 on success, 0 on failure
        int Triangulate_MONO(TPPLPolyList *inpolys, TPPLPolyList *triangles);
        
        //creates a monotone partition of a list of polygons that can contain holes
        //time complexity: O(n*log(n)), n is the number of vertices
        //space complexity: O(n)
        //params:
        //   inpolys : a list of polygons to be triangulated (can contain holes)
        //             vertices of all non-hole polys have to be in counter-clockwise order
        //             vertices of all hole polys have to be in clockwise order
        //   monotonePolys : a list of monotone polygons (result)
        //returns 1 on success, 0 on failure
        int MonotonePartition(TPPLPolyList *inpolys, TPPLPolyList *monotonePolys);
        
        //partitions a polygon into convex polygons by using Hertel-Mehlhorn algorithm
        //the algorithm gives at most four times the number of parts as the optimal algorithm
        //however, in practice it works much better than that and often gives optimal partition
        //uses triangulation obtained by ear clipping as intermediate result
        //time complexity O(n^2), n is the number of vertices
        //space complexity: O(n)
        //params:
        //   poly : an input polygon to be partitioned
        //          vertices have to be in counter-clockwise order
        //   parts : resulting list of convex polygons
        //returns 1 on success, 0 on failure
        int ConvexPartition_HM(TPPLPoly *poly, TPPLPolyList *parts);
        
        //partitions a list of polygons into convex parts by using Hertel-Mehlhorn algorithm
        //the algorithm gives at most four times the number of parts as the optimal algorithm
        //however, in practice it works much better than that and often gives optimal partition
        //uses triangulation obtained by ear clipping as intermediate result
        //time complexity O(n^2), n is the number of vertices
        //space complexity: O(n)
        //params:
        //   inpolys : an input list of polygons to be partitioned
        //             vertices of all non-hole polys have to be in counter-clockwise order
        //             vertices of all hole polys have to be in clockwise order
        //   parts : resulting list of convex polygons
        //returns 1 on success, 0 on failure
        int ConvexPartition_HM(TPPLPolyList *inpolys, TPPLPolyList *parts);
        
        //optimal convex partitioning (in terms of number of resulting convex polygons)
        //using the Keil-Snoeyink algorithm
        //M. Keil, J. Snoeyink, "On the time bound for convex decomposition of simple polygons", 1998
        //time complexity O(n^3), n is the number of vertices
        //space complexity: O(n^3)
        //   poly : an input polygon to be partitioned
        //          vertices have to be in counter-clockwise order
        //   parts : resulting list of convex polygons
        //returns 1 on success, 0 on failure
        int ConvexPartition_OPT(TPPLPoly *poly, TPPLPolyList *parts);
};


#endif
