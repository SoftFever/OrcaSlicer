/*******************************************************************************
*                                                                              *
* Author    :  Angus Johnson                                                   *
* Version   :  6.2.9                                                           *
* Date      :  16 February 2015                                                *
* Website   :  http://www.angusj.com                                           *
* Copyright :  Angus Johnson 2010-2015                                         *
*                                                                              *
* License:                                                                     *
* Use, modification & distribution is subject to Boost Software License Ver 1. *
* http://www.boost.org/LICENSE_1_0.txt                                         *
*                                                                              *
* Attributions:                                                                *
* The code in this library is an extension of Bala Vatti's clipping algorithm: *
* "A generic solution to polygon clipping"                                     *
* Communications of the ACM, Vol 35, Issue 7 (July 1992) pp 56-63.             *
* http://portal.acm.org/citation.cfm?id=129906                                 *
*                                                                              *
* Computer graphics and geometric modeling: implementation and algorithms      *
* By Max K. Agoston                                                            *
* Springer; 1 edition (January 4, 2005)                                        *
* http://books.google.com/books?q=vatti+clipping+agoston                       *
*                                                                              *
* See also:                                                                    *
* "Polygon Offsetting by Computing Winding Numbers"                            *
* Paper no. DETC2005-85513 pp. 565-575                                         *
* ASME 2005 International Design Engineering Technical Conferences             *
* and Computers and Information in Engineering Conference (IDETC/CIE2005)      *
* September 24-28, 2005 , Long Beach, California, USA                          *
* http://www.me.berkeley.edu/~mcmains/pubs/DAC05OffsetPolygon.pdf              *
*                                                                              *
*******************************************************************************/

#ifndef clipper_hpp
#define clipper_hpp

#include <inttypes.h>
#include <functional>

#define CLIPPER_VERSION "6.2.6"

//use_xyz: adds a Z member to IntPoint. Adds a minor cost to perfomance.
//#define use_xyz

//use_lines: Enables line clipping. Adds a very minor cost to performance.
#define use_lines
  
//use_deprecated: Enables temporary support for the obsolete functions
//#define use_deprecated  

#include <vector>
#include <deque>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <ostream>
#include <functional>
#include <queue>

#ifdef use_xyz
namespace ClipperLib_Z {
#else /* use_xyz */
namespace ClipperLib {
#endif /* use_xyz */

enum ClipType { ctIntersection, ctUnion, ctDifference, ctXor };
enum PolyType { ptSubject, ptClip };
//By far the most widely used winding rules for polygon filling are
//EvenOdd & NonZero (GDI, GDI+, XLib, OpenGL, Cairo, AGG, Quartz, SVG, Gr32)
//Others rules include Positive, Negative and ABS_GTR_EQ_TWO (only in OpenGL)
//see http://glprogramming.com/red/chapter11.html
enum PolyFillType { pftEvenOdd, pftNonZero, pftPositive, pftNegative };

// Point coordinate type
typedef int64_t cInt;
// Maximum cInt value to allow a cross product calculation using 32bit expressions.
static cInt const loRange = 0x3FFFFFFF;
// Maximum allowed cInt value.
static cInt const hiRange = 0x3FFFFFFFFFFFFFFFLL;

struct IntPoint {
  cInt X;
  cInt Y;
#ifdef use_xyz
  cInt Z;
  IntPoint(cInt x = 0, cInt y = 0, cInt z = 0): X(x), Y(y), Z(z) {};
#else
  IntPoint(cInt x = 0, cInt y = 0): X(x), Y(y) {};
#endif

  friend inline bool operator== (const IntPoint& a, const IntPoint& b)
  {
    return a.X == b.X && a.Y == b.Y;
  }
  friend inline bool operator!= (const IntPoint& a, const IntPoint& b)
  {
    return a.X != b.X  || a.Y != b.Y; 
  }
};
//------------------------------------------------------------------------------

typedef std::vector< IntPoint > Path;
typedef std::vector< Path > Paths;

inline Path& operator <<(Path& poly, const IntPoint& p) {poly.push_back(p); return poly;}
inline Paths& operator <<(Paths& polys, const Path& p) {polys.push_back(p); return polys;}

std::ostream& operator <<(std::ostream &s, const IntPoint &p);
std::ostream& operator <<(std::ostream &s, const Path &p);
std::ostream& operator <<(std::ostream &s, const Paths &p);

struct DoublePoint
{
  double X;
  double Y;
  DoublePoint(double x = 0, double y = 0) : X(x), Y(y) {}
  DoublePoint(IntPoint ip) : X((double)ip.X), Y((double)ip.Y) {}
};
//------------------------------------------------------------------------------

#ifdef use_xyz
typedef std::function<void(const IntPoint& e1bot, const IntPoint& e1top, const IntPoint& e2bot, const IntPoint& e2top, IntPoint& pt)> ZFillCallback;
#endif

enum InitOptions {ioReverseSolution = 1, ioStrictlySimple = 2, ioPreserveCollinear = 4};
enum JoinType {jtSquare, jtRound, jtMiter};
enum EndType {etClosedPolygon, etClosedLine, etOpenButt, etOpenSquare, etOpenRound};

class PolyNode;
typedef std::vector< PolyNode* > PolyNodes;

class PolyNode 
{ 
public:
    PolyNode() : Childs(), Parent(0), Index(0), m_IsOpen(false) {}
    virtual ~PolyNode(){};
    Path Contour;
    PolyNodes Childs;
    PolyNode* Parent;
    // Traversal of the polygon tree in a depth first fashion.
    PolyNode* GetNext() const { return Childs.empty() ? GetNextSiblingUp() : Childs.front(); }
    bool IsHole() const;
    bool IsOpen() const { return m_IsOpen; }  
    int  ChildCount() const { return (int)Childs.size(); }
private:
    unsigned Index; //node index in Parent.Childs
    bool m_IsOpen;
    JoinType m_jointype;
    EndType m_endtype;
    PolyNode* GetNextSiblingUp() const { return Parent ? ((Index == Parent->Childs.size() - 1) ? Parent->GetNextSiblingUp() : Parent->Childs[Index + 1]) : nullptr; }
    void AddChild(PolyNode& child);
    friend class Clipper; //to access Index
    friend class ClipperOffset;
    friend class PolyTree; //to implement the PolyTree::move operator
};

class PolyTree: public PolyNode
{ 
public:
    PolyTree() {}
    PolyTree(PolyTree &&src) { *this = std::move(src); }
    virtual ~PolyTree(){Clear();};
    PolyTree& operator=(PolyTree &&src) { 
        AllNodes   = std::move(src.AllNodes);
        Contour    = std::move(src.Contour);
        Childs     = std::move(src.Childs);
        Parent     = nullptr;
        Index      = src.Index;
        m_IsOpen   = src.m_IsOpen;
        m_jointype = src.m_jointype;
        m_endtype  = src.m_endtype;
        for (size_t i = 0; i < Childs.size(); ++ i)
          Childs[i]->Parent = this;
        return *this; 
    }
    PolyNode* GetFirst() const { return Childs.empty() ? nullptr : Childs.front(); }
    void Clear() {  AllNodes.clear(); Childs.clear(); }
    int Total() const;
private:
    PolyTree(const PolyTree &src) = delete;
    PolyTree& operator=(const PolyTree &src) = delete;
    std::vector<PolyNode> AllNodes;
    friend class Clipper; //to access AllNodes
};

double Area(const Path &poly);
inline bool Orientation(const Path &poly) { return Area(poly) >= 0; }
int PointInPolygon(const IntPoint &pt, const Path &path);

void SimplifyPolygon(const Path &in_poly, Paths &out_polys, PolyFillType fillType = pftEvenOdd);
void SimplifyPolygons(const Paths &in_polys, Paths &out_polys, PolyFillType fillType = pftEvenOdd);
void SimplifyPolygons(Paths &polys, PolyFillType fillType = pftEvenOdd);

void CleanPolygon(const Path& in_poly, Path& out_poly, double distance = 1.415);
void CleanPolygon(Path& poly, double distance = 1.415);
void CleanPolygons(const Paths& in_polys, Paths& out_polys, double distance = 1.415);
void CleanPolygons(Paths& polys, double distance = 1.415);

void MinkowskiSum(const Path& pattern, const Path& path, Paths& solution, bool pathIsClosed);
void MinkowskiSum(const Path& pattern, const Paths& paths, Paths& solution, bool pathIsClosed);
void MinkowskiDiff(const Path& poly1, const Path& poly2, Paths& solution);

void PolyTreeToPaths(const PolyTree& polytree, Paths& paths);
void ClosedPathsFromPolyTree(const PolyTree& polytree, Paths& paths);
void OpenPathsFromPolyTree(PolyTree& polytree, Paths& paths);

void ReversePath(Path& p);
void ReversePaths(Paths& p);

struct IntRect { cInt left; cInt top; cInt right; cInt bottom; };

//enums that are used internally ...
enum EdgeSide { esLeft = 1, esRight = 2};

// namespace Internal {
  //forward declarations (for stuff used internally) ...
  struct TEdge {
    // Bottom point of this edge (with minimum Y).
    IntPoint Bot;
    // Current position.
    IntPoint Curr;
    // Top point of this edge (with maximum Y).
    IntPoint Top;
    // Vector from Bot to Top.
    IntPoint Delta;
    // Slope (dx/dy). For horiontal edges, the slope is set to HORIZONTAL (-1.0E+40).
    double Dx;
    PolyType PolyTyp;
    EdgeSide Side;
    // Winding number delta. 1 or -1 depending on winding direction, 0 for open paths and flat closed paths.
    int WindDelta;
    int WindCnt;
    int WindCnt2; //winding count of the opposite polytype
    int OutIdx;
    // Next edge in the input path.
    TEdge *Next;
    // Previous edge in the input path.
    TEdge *Prev;
    // Next edge in the Local Minima List chain.
    TEdge *NextInLML;
    TEdge *NextInAEL;
    TEdge *PrevInAEL;
    TEdge *NextInSEL;
    TEdge *PrevInSEL;
  };

  struct IntersectNode {
    IntersectNode(TEdge *Edge1, TEdge *Edge2, IntPoint Pt) :
      Edge1(Edge1), Edge2(Edge2), Pt(Pt) {}
    TEdge          *Edge1;
    TEdge          *Edge2;
    IntPoint        Pt;
  };

  struct LocalMinimum {
    cInt          Y;
    TEdge        *LeftBound;
    TEdge        *RightBound;
  };

  // Point of an output polygon.
  // 36B on 64bit system without use_xyz.
  struct OutPt {
    // 4B
    int       Idx;
    // 16B without use_xyz / 24B with use_xyz
    IntPoint  Pt;
    // 4B on 32bit system, 8B on 64bit system
    OutPt    *Next;
    // 4B on 32bit system, 8B on 64bit system
    OutPt    *Prev;
  };

  struct OutRec;
  struct Join {
    Join(OutPt *OutPt1, OutPt *OutPt2, IntPoint OffPt) :
      OutPt1(OutPt1), OutPt2(OutPt2), OffPt(OffPt) {}
    OutPt    *OutPt1;
    OutPt    *OutPt2;
    IntPoint  OffPt;
  };
// }; // namespace Internal

//------------------------------------------------------------------------------

//ClipperBase is the ancestor to the Clipper class. It should not be
//instantiated directly. This class simply abstracts the conversion of sets of
//polygon coordinates into edge objects that are stored in a LocalMinima list.
class ClipperBase
{
public:
  ClipperBase() : m_UseFullRange(false), m_HasOpenPaths(false) {}
  ~ClipperBase() { Clear(); }
  bool AddPath(const Path &pg, PolyType PolyTyp, bool Closed);
  bool AddPaths(const Paths &ppg, PolyType PolyTyp, bool Closed);
  void Clear();
  IntRect GetBounds();
  // By default, when three or more vertices are collinear in input polygons (subject or clip), the Clipper object removes the 'inner' vertices before clipping.
  // When enabled the PreserveCollinear property prevents this default behavior to allow these inner vertices to appear in the solution.
  bool PreserveCollinear() const {return m_PreserveCollinear;};
  void PreserveCollinear(bool value) {m_PreserveCollinear = value;};
protected:
  bool AddPathInternal(const Path &pg, int highI, PolyType PolyTyp, bool Closed, TEdge* edges);
  TEdge* AddBoundsToLML(TEdge *e, bool IsClosed);
  void Reset();
  TEdge* ProcessBound(TEdge* E, bool IsClockwise);
  TEdge* DescendToMin(TEdge *&E);
  void AscendToMax(TEdge *&E, bool Appending, bool IsClosed);

  // Local minima (Y, left edge, right edge) sorted by ascending Y.
  std::vector<LocalMinimum> m_MinimaList;

  // True if the input polygons have abs values higher than loRange, but lower than hiRange.
  // False if the input polygons have abs values lower or equal to loRange.
  bool              m_UseFullRange;
  // A vector of edges per each input path.
  std::vector<std::vector<TEdge>> m_edges;
  // Don't remove intermediate vertices of a collinear sequence of points.
  bool             m_PreserveCollinear;
  // Is any of the paths inserted by AddPath() or AddPaths() open?
  bool             m_HasOpenPaths;
};
//------------------------------------------------------------------------------

class Clipper : public ClipperBase
{
public:
  Clipper(int initOptions = 0);
  ~Clipper() { Clear(); }
  void Clear() { ClipperBase::Clear(); DisposeAllOutRecs(); }
  bool Execute(ClipType clipType,
      Paths &solution,
      PolyFillType fillType = pftEvenOdd) 
    { return Execute(clipType, solution, fillType, fillType); }
  bool Execute(ClipType clipType,
      Paths &solution,
      PolyFillType subjFillType,
      PolyFillType clipFillType);
  bool Execute(ClipType clipType,
      PolyTree &polytree,
      PolyFillType fillType = pftEvenOdd)
    { return Execute(clipType, polytree, fillType, fillType); }
  bool Execute(ClipType clipType,
      PolyTree &polytree,
      PolyFillType subjFillType,
      PolyFillType clipFillType);
  bool ReverseSolution() const { return m_ReverseOutput; };
  void ReverseSolution(bool value) {m_ReverseOutput = value;};
  bool StrictlySimple() const {return m_StrictSimple;};
  void StrictlySimple(bool value) {m_StrictSimple = value;};
  //set the callback function for z value filling on intersections (otherwise Z is 0)
#ifdef use_xyz
  void ZFillFunction(ZFillCallback zFillFunc) { m_ZFill = zFillFunc; }
#endif
protected:
  void Reset();
  virtual bool ExecuteInternal();
private:
  
  // Output polygons.
  std::vector<OutRec*>  m_PolyOuts;
  // Output points, allocated by a continuous sets of m_OutPtsChunkSize.
  std::vector<OutPt*>   m_OutPts;
  // List of free output points, to be used before taking a point from m_OutPts or allocating a new chunk.
  OutPt                *m_OutPtsFree;
  size_t                m_OutPtsChunkSize;
  size_t                m_OutPtsChunkLast;

  std::vector<Join>     m_Joins;
  std::vector<Join>     m_GhostJoins;
  std::vector<IntersectNode> m_IntersectList;
  ClipType              m_ClipType;
  // A priority queue (a binary heap) of Y coordinates.
  std::priority_queue<cInt> m_Scanbeam;
  // Maxima are collected by ProcessEdgesAtTopOfScanbeam(), consumed by ProcessHorizontal().
  std::vector<cInt>     m_Maxima;
  TEdge                *m_ActiveEdges;
  TEdge                *m_SortedEdges;
  PolyFillType          m_ClipFillType;
  PolyFillType          m_SubjFillType;
  bool                  m_ReverseOutput;
  // Does the result go to a PolyTree or Paths?
  bool                  m_UsingPolyTree; 
  bool                  m_StrictSimple;
#ifdef use_xyz
  ZFillCallback         m_ZFill; //custom callback 
#endif
  void SetWindingCount(TEdge& edge) const;
  bool IsEvenOddFillType(const TEdge& edge) const 
    { return (edge.PolyTyp == ptSubject) ? m_SubjFillType == pftEvenOdd : m_ClipFillType == pftEvenOdd; }
  bool IsEvenOddAltFillType(const TEdge& edge) const
    { return (edge.PolyTyp == ptSubject) ? m_ClipFillType == pftEvenOdd : m_SubjFillType == pftEvenOdd; }
  void InsertLocalMinimaIntoAEL(const cInt botY);
  void InsertEdgeIntoAEL(TEdge *edge, TEdge* startEdge);
  void AddEdgeToSEL(TEdge *edge);
  void CopyAELToSEL();
  void DeleteFromSEL(TEdge *e);
  void DeleteFromAEL(TEdge *e);
  void UpdateEdgeIntoAEL(TEdge *&e);
  void SwapPositionsInSEL(TEdge *edge1, TEdge *edge2);
  bool IsContributing(const TEdge& edge) const;
  bool IsTopHorz(const cInt XPos);
  void SwapPositionsInAEL(TEdge *edge1, TEdge *edge2);
  void DoMaxima(TEdge *e);
  void ProcessHorizontals();
  void ProcessHorizontal(TEdge *horzEdge);
  void AddLocalMaxPoly(TEdge *e1, TEdge *e2, const IntPoint &pt);
  OutPt* AddLocalMinPoly(TEdge *e1, TEdge *e2, const IntPoint &pt);
  OutRec* GetOutRec(int idx);
  void AppendPolygon(TEdge *e1, TEdge *e2) const;
  void IntersectEdges(TEdge *e1, TEdge *e2, IntPoint &pt);
  OutRec* CreateOutRec();
  OutPt* AddOutPt(TEdge *e, const IntPoint &pt);
  OutPt* GetLastOutPt(TEdge *e);
  OutPt* AllocateOutPt();
  OutPt* DupOutPt(OutPt* outPt, bool InsertAfter);
  // Add the point to a list of free points.
  void DisposeOutPt(OutPt *pt) { pt->Next = m_OutPtsFree; m_OutPtsFree = pt; }
  void DisposeOutPts(OutPt*& pp) { if (pp != nullptr) { pp->Prev->Next = m_OutPtsFree; m_OutPtsFree = pp; } }
  void DisposeAllOutRecs();
  bool ProcessIntersections(const cInt topY);
  void BuildIntersectList(const cInt topY);
  void ProcessEdgesAtTopOfScanbeam(const cInt topY);
  void BuildResult(Paths& polys);
  void BuildResult2(PolyTree& polytree);
  void SetHoleState(TEdge *e, OutRec *outrec) const;
  bool FixupIntersectionOrder();
  void FixupOutPolygon(OutRec &outrec);
  void FixupOutPolyline(OutRec &outrec);
  bool FindOwnerFromSplitRecs(OutRec &outRec, OutRec *&currOrfl);
  void FixHoleLinkage(OutRec &outrec);
  bool JoinPoints(Join *j, OutRec* outRec1, OutRec* outRec2);
  bool JoinHorz(OutPt* op1, OutPt* op1b, OutPt* op2, OutPt* op2b, const IntPoint &Pt, bool DiscardLeft);
  void JoinCommonEdges();
  void DoSimplePolygons();
  void FixupFirstLefts1(OutRec* OldOutRec, OutRec* NewOutRec) const;
  void FixupFirstLefts2(OutRec* OldOutRec, OutRec* NewOutRec) const;
#ifdef use_xyz
  void SetZ(IntPoint& pt, TEdge& e1, TEdge& e2);
#endif
};
//------------------------------------------------------------------------------

class ClipperOffset 
{
public:
  ClipperOffset(double miterLimit = 2.0, double roundPrecision = 0.25, double shortestEdgeLength = 0.) :
    MiterLimit(miterLimit), ArcTolerance(roundPrecision), ShortestEdgeLength(shortestEdgeLength), m_lowest(-1, 0) {}
  ~ClipperOffset() { Clear(); }
  void AddPath(const Path& path, JoinType joinType, EndType endType);
  void AddPaths(const Paths& paths, JoinType joinType, EndType endType);
  void Execute(Paths& solution, double delta);
  void Execute(PolyTree& solution, double delta);
  void Clear();
  double MiterLimit;
  double ArcTolerance;
  double ShortestEdgeLength;
private:
  Paths m_destPolys;
  Path m_srcPoly;
  Path m_destPoly;
  std::vector<DoublePoint> m_normals;
  double m_delta, m_sinA, m_sin, m_cos;
  double m_miterLim, m_StepsPerRad;
  IntPoint m_lowest;
  PolyNode m_polyNodes;

  void FixOrientations();
  void DoOffset(double delta);
  void OffsetPoint(int j, int& k, JoinType jointype);
  void DoSquare(int j, int k);
  void DoMiter(int j, int k, double r);
  void DoRound(int j, int k);
};
//------------------------------------------------------------------------------

class clipperException : public std::exception
{
  public:
    clipperException(const char* description): m_descr(description) {}
    virtual ~clipperException() throw() {}
    virtual const char* what() const throw() {return m_descr.c_str();}
  private:
    std::string m_descr;
};
//------------------------------------------------------------------------------

} //ClipperLib namespace

#endif //clipper_hpp


