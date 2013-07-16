/*******************************************************************************
*                                                                              *
* Author    :  Angus Johnson                                                   *
* Version   :  5.1.6                                                           *
* Date      :  23 May 2013                                                     *
* Website   :  http://www.angusj.com                                           *
* Copyright :  Angus Johnson 2010-2013                                         *
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

/*******************************************************************************
*                                                                              *
* This is a translation of the Delphi Clipper library and the naming style     *
* used has retained a Delphi flavour.                                          *
*                                                                              *
*******************************************************************************/

#include "clipper.hpp"
#include <cmath>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <ostream>

namespace ClipperLib {

static long64 const loRange = 0x3FFFFFFF;
static long64 const hiRange = 0x3FFFFFFFFFFFFFFFLL;
static double const pi = 3.141592653589793238;
enum Direction { dRightToLeft, dLeftToRight };

#define HORIZONTAL (-1.0E+40)
#define TOLERANCE (1.0e-20)
#define NEAR_ZERO(val) (((val) > -TOLERANCE) && ((val) < TOLERANCE))
#define NEAR_EQUAL(a, b) NEAR_ZERO((a) - (b))

const char coords_range_error[] = "Coordinate exceeds range bounds.";

inline long64 Abs(long64 val)
{
  return val < 0 ? -val : val;
}

//------------------------------------------------------------------------------
// PolyTree methods ...
//------------------------------------------------------------------------------

void PolyTree::Clear()
{
    for (PolyNodes::size_type i = 0; i < AllNodes.size(); ++i)
      delete AllNodes[i];
    AllNodes.resize(0); 
    Childs.resize(0);
}
//------------------------------------------------------------------------------

PolyNode* PolyTree::GetFirst() const
{
  if (!Childs.empty())
      return Childs[0];
  else
      return 0;
}
//------------------------------------------------------------------------------

int PolyTree::Total() const
{
  return AllNodes.size();
}

//------------------------------------------------------------------------------
// PolyNode methods ...
//------------------------------------------------------------------------------

PolyNode::PolyNode(): Childs(), Parent(0), Index(0)
{
}
//------------------------------------------------------------------------------

int PolyNode::ChildCount() const
{
  return Childs.size();
}
//------------------------------------------------------------------------------

void PolyNode::AddChild(PolyNode& child)
{
  unsigned cnt = Childs.size();
  Childs.push_back(&child);
  child.Parent = this;
  child.Index = cnt;
}
//------------------------------------------------------------------------------

PolyNode* PolyNode::GetNext() const
{ 
  if (!Childs.empty()) 
      return Childs[0]; 
  else
      return GetNextSiblingUp();    
}  
//------------------------------------------------------------------------------

PolyNode* PolyNode::GetNextSiblingUp() const
{ 
  if (!Parent) //protects against PolyTree.GetNextSiblingUp()
      return 0;
  else if (Index == Parent->Childs.size() - 1)
      return Parent->GetNextSiblingUp();
  else
      return Parent->Childs[Index + 1];
}  
//------------------------------------------------------------------------------

bool PolyNode::IsHole() const
{ 
  bool result = true;
  PolyNode* node = Parent;
  while (node)
  {
      result = !result;
      node = node->Parent;
  }
  return result;
}  

//------------------------------------------------------------------------------
// Int128 class (enables safe math on signed 64bit integers)
// eg Int128 val1((long64)9223372036854775807); //ie 2^63 -1
//    Int128 val2((long64)9223372036854775807);
//    Int128 val3 = val1 * val2;
//    val3.AsString => "85070591730234615847396907784232501249" (8.5e+37)
//------------------------------------------------------------------------------

class Int128
{
  public:

    ulong64 lo;
    long64 hi;

    Int128(long64 _lo = 0)
    {
      lo = (ulong64)_lo;   
      if (_lo < 0)  hi = -1; else hi = 0; 
    }


    Int128(const Int128 &val): lo(val.lo), hi(val.hi){}

    Int128(const long64& _hi, const ulong64& _lo): lo(_lo), hi(_hi){}
    
    long64 operator = (const long64 &val)
    {
      lo = (ulong64)val;
      if (val < 0) hi = -1; else hi = 0;
      return val;
    }

    bool operator == (const Int128 &val) const
      {return (hi == val.hi && lo == val.lo);}

    bool operator != (const Int128 &val) const
      { return !(*this == val);}

    bool operator > (const Int128 &val) const
    {
      if (hi != val.hi)
        return hi > val.hi;
      else
        return lo > val.lo;
    }

    bool operator < (const Int128 &val) const
    {
      if (hi != val.hi)
        return hi < val.hi;
      else
        return lo < val.lo;
    }

    bool operator >= (const Int128 &val) const
      { return !(*this < val);}

    bool operator <= (const Int128 &val) const
      { return !(*this > val);}

    Int128& operator += (const Int128 &rhs)
    {
      hi += rhs.hi;
      lo += rhs.lo;
      if (lo < rhs.lo) hi++;
      return *this;
    }

    Int128 operator + (const Int128 &rhs) const
    {
      Int128 result(*this);
      result+= rhs;
      return result;
    }

    Int128& operator -= (const Int128 &rhs)
    {
      *this += -rhs;
      return *this;
    }

    Int128 operator - (const Int128 &rhs) const
    {
      Int128 result(*this);
      result -= rhs;
      return result;
    }

    Int128 operator-() const //unary negation
    {
      if (lo == 0)
        return Int128(-hi,0);
      else 
        return Int128(~hi,~lo +1);
    }

    Int128 operator/ (const Int128 &rhs) const
    {
      if (rhs.lo == 0 && rhs.hi == 0)
        throw "Int128 operator/: divide by zero";

      bool negate = (rhs.hi < 0) != (hi < 0);
      Int128 dividend = *this;
      Int128 divisor = rhs;
      if (dividend.hi < 0) dividend = -dividend;
      if (divisor.hi < 0) divisor = -divisor;

      if (divisor < dividend)
      {
          Int128 result = Int128(0);
          Int128 cntr = Int128(1);
          while (divisor.hi >= 0 && !(divisor > dividend))
          {
              divisor.hi <<= 1;
              if ((long64)divisor.lo < 0) divisor.hi++;
              divisor.lo <<= 1;

              cntr.hi <<= 1;
              if ((long64)cntr.lo < 0) cntr.hi++;
              cntr.lo <<= 1;
          }
          divisor.lo >>= 1;
          if ((divisor.hi & 1) == 1)
              divisor.lo |= 0x8000000000000000LL; 
          divisor.hi = (ulong64)divisor.hi >> 1;

          cntr.lo >>= 1;
          if ((cntr.hi & 1) == 1)
              cntr.lo |= 0x8000000000000000LL; 
          cntr.hi >>= 1;

          while (cntr.hi != 0 || cntr.lo != 0)
          {
              if (!(dividend < divisor))
              {
                  dividend -= divisor;
                  result.hi |= cntr.hi;
                  result.lo |= cntr.lo;
              }
              divisor.lo >>= 1;
              if ((divisor.hi & 1) == 1)
                  divisor.lo |= 0x8000000000000000LL; 
              divisor.hi >>= 1;

              cntr.lo >>= 1;
              if ((cntr.hi & 1) == 1)
                  cntr.lo |= 0x8000000000000000LL; 
              cntr.hi >>= 1;
          }
          if (negate) result = -result;
          return result;
      }
      else if (rhs.hi == this->hi && rhs.lo == this->lo)
          return Int128(1);
      else
          return Int128(0);
    }

    double AsDouble() const
    {
      const double shift64 = 18446744073709551616.0; //2^64
      if (hi < 0)
      {
        if (lo == 0) return (double)hi * shift64;
        else return -(double)(~lo + ~hi * shift64);
      }
      else
        return (double)(lo + hi * shift64);
    }
};

Int128 Int128Mul (long64 lhs, long64 rhs)
{
  bool negate = (lhs < 0) != (rhs < 0);

  if (lhs < 0) lhs = -lhs;
  ulong64 int1Hi = ulong64(lhs) >> 32;
  ulong64 int1Lo = ulong64(lhs & 0xFFFFFFFF);

  if (rhs < 0) rhs = -rhs;
  ulong64 int2Hi = ulong64(rhs) >> 32;
  ulong64 int2Lo = ulong64(rhs & 0xFFFFFFFF);

  //nb: see comments in clipper.pas
  ulong64 a = int1Hi * int2Hi;
  ulong64 b = int1Lo * int2Lo;
  ulong64 c = int1Hi * int2Lo + int1Lo * int2Hi;

  Int128 tmp;
  tmp.hi = long64(a + (c >> 32));
  tmp.lo = long64(c << 32);
  tmp.lo += long64(b);
  if (tmp.lo < b) tmp.hi++;
  if (negate) tmp = -tmp;
  return tmp;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

bool FullRangeNeeded(const Polygon &pts)
{
  bool result = false;
  for (Polygon::size_type i = 0; i <  pts.size(); ++i)
  {
    if (Abs(pts[i].X) > hiRange || Abs(pts[i].Y) > hiRange)
        throw coords_range_error;
      else if (Abs(pts[i].X) > loRange || Abs(pts[i].Y) > loRange)
        result = true;
  }
  return result;
}
//------------------------------------------------------------------------------
  
bool Orientation(const Polygon &poly)
{
    return Area(poly) >= 0;
}
//------------------------------------------------------------------------------

inline bool PointsEqual( const IntPoint &pt1, const IntPoint &pt2)
{
  return ( pt1.X == pt2.X && pt1.Y == pt2.Y );
}
//------------------------------------------------------------------------------

double Area(const Polygon &poly)
{
  int highI = (int)poly.size() -1;
  if (highI < 2) return 0;

  if (FullRangeNeeded(poly)) {
    Int128 a;
    a = Int128Mul(poly[highI].X + poly[0].X, poly[0].Y - poly[highI].Y);
    for (int i = 1; i <= highI; ++i)
      a += Int128Mul(poly[i - 1].X + poly[i].X, poly[i].Y - poly[i -1].Y);
    return a.AsDouble() / 2;
  }
  else
  {
    double a;
    a = ((double)poly[highI].X + poly[0].X) * ((double)poly[0].Y - poly[highI].Y);
    for (int i = 1; i <= highI; ++i)
      a += ((double)poly[i - 1].X + poly[i].X) * ((double)poly[i].Y - poly[i - 1].Y);
    return a / 2;
  }
}
//------------------------------------------------------------------------------

double Area(const OutRec &outRec, bool UseFullInt64Range)
{
  OutPt *op = outRec.pts;
  if (!op) return 0;
  if (UseFullInt64Range) {
    Int128 a(0);
    do {
      a += Int128Mul(op->pt.X + op->prev->pt.X, op->prev->pt.Y - op->pt.Y);
      op = op->next;
    } while (op != outRec.pts);
    return a.AsDouble() / 2;
  }
  else
  {
    double a = 0;
    do {
      a = a + (op->pt.X + op->prev->pt.X) * (op->prev->pt.Y - op->pt.Y);
      op = op->next;
    } while (op != outRec.pts);
    return a / 2;
  }
}
//------------------------------------------------------------------------------

bool PointIsVertex(const IntPoint &pt, OutPt *pp)
{
  OutPt *pp2 = pp;
  do
  {
    if (PointsEqual(pp2->pt, pt)) return true;
    pp2 = pp2->next;
  }
  while (pp2 != pp);
  return false;
}
//------------------------------------------------------------------------------

bool PointOnLineSegment(const IntPoint pt, 
  const IntPoint linePt1, const IntPoint linePt2, bool UseFullInt64Range)
{
  if (UseFullInt64Range)
    return ((pt.X == linePt1.X) && (pt.Y == linePt1.Y)) ||
      ((pt.X == linePt2.X) && (pt.Y == linePt2.Y)) ||
      (((pt.X > linePt1.X) == (pt.X < linePt2.X)) &&
      ((pt.Y > linePt1.Y) == (pt.Y < linePt2.Y)) &&
      ((Int128Mul((pt.X - linePt1.X), (linePt2.Y - linePt1.Y)) ==
      Int128Mul((linePt2.X - linePt1.X), (pt.Y - linePt1.Y)))));
  else
    return ((pt.X == linePt1.X) && (pt.Y == linePt1.Y)) ||
      ((pt.X == linePt2.X) && (pt.Y == linePt2.Y)) ||
      (((pt.X > linePt1.X) == (pt.X < linePt2.X)) &&
      ((pt.Y > linePt1.Y) == (pt.Y < linePt2.Y)) &&
      ((pt.X - linePt1.X) * (linePt2.Y - linePt1.Y) ==
        (linePt2.X - linePt1.X) * (pt.Y - linePt1.Y)));
}
//------------------------------------------------------------------------------

bool PointOnPolygon(const IntPoint pt, OutPt *pp, bool UseFullInt64Range)
{
  OutPt *pp2 = pp;
  while (true)
  {
    if (PointOnLineSegment(pt, pp2->pt, pp2->next->pt, UseFullInt64Range))
      return true;
    pp2 = pp2->next;
    if (pp2 == pp) break;
  } 
  return false;
}
//------------------------------------------------------------------------------

bool PointInPolygon(const IntPoint &pt, OutPt *pp, bool UseFullInt64Range)
{
  OutPt *pp2 = pp;
  bool result = false;
  if (UseFullInt64Range) {
    do
    {
      if ((((pp2->pt.Y <= pt.Y) && (pt.Y < pp2->prev->pt.Y)) ||
          ((pp2->prev->pt.Y <= pt.Y) && (pt.Y < pp2->pt.Y))) &&
          Int128(pt.X - pp2->pt.X) < 
          Int128Mul(pp2->prev->pt.X - pp2->pt.X, pt.Y - pp2->pt.Y) / 
          Int128(pp2->prev->pt.Y - pp2->pt.Y))
            result = !result;
      pp2 = pp2->next;
    }
    while (pp2 != pp);
  }
  else
  {
    do
    {
      if ((((pp2->pt.Y <= pt.Y) && (pt.Y < pp2->prev->pt.Y)) ||
        ((pp2->prev->pt.Y <= pt.Y) && (pt.Y < pp2->pt.Y))) &&
        (pt.X < (pp2->prev->pt.X - pp2->pt.X) * (pt.Y - pp2->pt.Y) /
        (pp2->prev->pt.Y - pp2->pt.Y) + pp2->pt.X )) result = !result;
      pp2 = pp2->next;
    }
    while (pp2 != pp);
  }
  return result;
}
//------------------------------------------------------------------------------

bool SlopesEqual(const TEdge &e1, const TEdge &e2, bool UseFullInt64Range)
{
  if (UseFullInt64Range)
    return Int128Mul(e1.deltaY, e2.deltaX) == Int128Mul(e1.deltaX, e2.deltaY);
  else return e1.deltaY * e2.deltaX == e1.deltaX * e2.deltaY;
}
//------------------------------------------------------------------------------

bool SlopesEqual(const IntPoint pt1, const IntPoint pt2,
  const IntPoint pt3, bool UseFullInt64Range)
{
  if (UseFullInt64Range)
    return Int128Mul(pt1.Y-pt2.Y, pt2.X-pt3.X) == Int128Mul(pt1.X-pt2.X, pt2.Y-pt3.Y);
  else return (pt1.Y-pt2.Y)*(pt2.X-pt3.X) == (pt1.X-pt2.X)*(pt2.Y-pt3.Y);
}
//------------------------------------------------------------------------------

bool SlopesEqual(const IntPoint pt1, const IntPoint pt2,
  const IntPoint pt3, const IntPoint pt4, bool UseFullInt64Range)
{
  if (UseFullInt64Range)
    return Int128Mul(pt1.Y-pt2.Y, pt3.X-pt4.X) == Int128Mul(pt1.X-pt2.X, pt3.Y-pt4.Y);
  else return (pt1.Y-pt2.Y)*(pt3.X-pt4.X) == (pt1.X-pt2.X)*(pt3.Y-pt4.Y);
}
//------------------------------------------------------------------------------

double GetDx(const IntPoint pt1, const IntPoint pt2)
{
  return (pt1.Y == pt2.Y) ?
    HORIZONTAL : (double)(pt2.X - pt1.X) / (pt2.Y - pt1.Y);
}
//---------------------------------------------------------------------------

void SetDx(TEdge &e)
{
  e.deltaX = (e.xtop - e.xbot);
  e.deltaY = (e.ytop - e.ybot);

  if (e.deltaY == 0) e.dx = HORIZONTAL;
  else e.dx = (double)(e.deltaX) / e.deltaY;
}
//---------------------------------------------------------------------------

void SwapSides(TEdge &edge1, TEdge &edge2)
{
  EdgeSide side =  edge1.side;
  edge1.side = edge2.side;
  edge2.side = side;
}
//------------------------------------------------------------------------------

void SwapPolyIndexes(TEdge &edge1, TEdge &edge2)
{
  int outIdx =  edge1.outIdx;
  edge1.outIdx = edge2.outIdx;
  edge2.outIdx = outIdx;
}
//------------------------------------------------------------------------------

inline long64 Round(double val)
{
  return (val < 0) ? static_cast<long64>(val - 0.5) : static_cast<long64>(val + 0.5);
}
//------------------------------------------------------------------------------

long64 TopX(TEdge &edge, const long64 currentY)
{
  return ( currentY == edge.ytop ) ?
    edge.xtop : edge.xbot + Round(edge.dx *(currentY - edge.ybot));
}
//------------------------------------------------------------------------------

bool IntersectPoint(TEdge &edge1, TEdge &edge2,
  IntPoint &ip, bool UseFullInt64Range)
{
  double b1, b2;
  if (SlopesEqual(edge1, edge2, UseFullInt64Range))
  {
    if (edge2.ybot > edge1.ybot) ip.Y = edge2.ybot;
    else ip.Y = edge1.ybot;
    return false;
  }
  else if (NEAR_ZERO(edge1.dx))
  {
    ip.X = edge1.xbot;
    if (NEAR_EQUAL(edge2.dx, HORIZONTAL))
      ip.Y = edge2.ybot;
    else
    {
      b2 = edge2.ybot - (edge2.xbot / edge2.dx);
      ip.Y = Round(ip.X / edge2.dx + b2);
    }
  }
  else if (NEAR_ZERO(edge2.dx))
  {
    ip.X = edge2.xbot;
    if (NEAR_EQUAL(edge1.dx, HORIZONTAL))
      ip.Y = edge1.ybot;
    else
    {
      b1 = edge1.ybot - (edge1.xbot / edge1.dx);
      ip.Y = Round(ip.X / edge1.dx + b1);
    }
  } 
  else 
  {
    b1 = edge1.xbot - edge1.ybot * edge1.dx;
    b2 = edge2.xbot - edge2.ybot * edge2.dx;
    double q = (b2-b1) / (edge1.dx - edge2.dx);
    ip.Y = Round(q);
    if (std::fabs(edge1.dx) < std::fabs(edge2.dx))
      ip.X = Round(edge1.dx * q + b1);
    else 
      ip.X = Round(edge2.dx * q + b2);
  }

  if (ip.Y < edge1.ytop || ip.Y < edge2.ytop) 
  {
    if (edge1.ytop > edge2.ytop)
    {
      ip.X = edge1.xtop;
      ip.Y = edge1.ytop;
      return TopX(edge2, edge1.ytop) < edge1.xtop;
    } 
    else
    {
      ip.X = edge2.xtop;
      ip.Y = edge2.ytop;
      return TopX(edge1, edge2.ytop) > edge2.xtop;
    }
  } 
  else 
    return true;
}
//------------------------------------------------------------------------------

void ReversePolyPtLinks(OutPt *pp)
{
  if (!pp) return;
  OutPt *pp1, *pp2;
  pp1 = pp;
  do {
  pp2 = pp1->next;
  pp1->next = pp1->prev;
  pp1->prev = pp2;
  pp1 = pp2;
  } while( pp1 != pp );
}
//------------------------------------------------------------------------------

void DisposeOutPts(OutPt*& pp)
{
  if (pp == 0) return;
  pp->prev->next = 0;
  while( pp )
  {
    OutPt *tmpPp = pp;
    pp = pp->next;
    delete tmpPp;
  }
}
//------------------------------------------------------------------------------

void InitEdge(TEdge *e, TEdge *eNext,
  TEdge *ePrev, const IntPoint &pt, PolyType polyType)
{
  std::memset(e, 0, sizeof(TEdge));
  e->next = eNext;
  e->prev = ePrev;
  e->xcurr = pt.X;
  e->ycurr = pt.Y;
  if (e->ycurr >= e->next->ycurr)
  {
    e->xbot = e->xcurr;
    e->ybot = e->ycurr;
    e->xtop = e->next->xcurr;
    e->ytop = e->next->ycurr;
    e->windDelta = 1;
  } else
  {
    e->xtop = e->xcurr;
    e->ytop = e->ycurr;
    e->xbot = e->next->xcurr;
    e->ybot = e->next->ycurr;
    e->windDelta = -1;
  }
  SetDx(*e);
  e->polyType = polyType;
  e->outIdx = -1;
}
//------------------------------------------------------------------------------

inline void SwapX(TEdge &e)
{
  //swap horizontal edges' top and bottom x's so they follow the natural
  //progression of the bounds - ie so their xbots will align with the
  //adjoining lower edge. [Helpful in the ProcessHorizontal() method.]
  e.xcurr = e.xtop;
  e.xtop = e.xbot;
  e.xbot = e.xcurr;
}
//------------------------------------------------------------------------------

void SwapPoints(IntPoint &pt1, IntPoint &pt2)
{
  IntPoint tmp = pt1;
  pt1 = pt2;
  pt2 = tmp;
}
//------------------------------------------------------------------------------

bool GetOverlapSegment(IntPoint pt1a, IntPoint pt1b, IntPoint pt2a,
  IntPoint pt2b, IntPoint &pt1, IntPoint &pt2)
{
  //precondition: segments are colinear.
  if (Abs(pt1a.X - pt1b.X) > Abs(pt1a.Y - pt1b.Y))
  {
    if (pt1a.X > pt1b.X) SwapPoints(pt1a, pt1b);
    if (pt2a.X > pt2b.X) SwapPoints(pt2a, pt2b);
    if (pt1a.X > pt2a.X) pt1 = pt1a; else pt1 = pt2a;
    if (pt1b.X < pt2b.X) pt2 = pt1b; else pt2 = pt2b;
    return pt1.X < pt2.X;
  } else
  {
    if (pt1a.Y < pt1b.Y) SwapPoints(pt1a, pt1b);
    if (pt2a.Y < pt2b.Y) SwapPoints(pt2a, pt2b);
    if (pt1a.Y < pt2a.Y) pt1 = pt1a; else pt1 = pt2a;
    if (pt1b.Y > pt2b.Y) pt2 = pt1b; else pt2 = pt2b;
    return pt1.Y > pt2.Y;
  }
}
//------------------------------------------------------------------------------

bool FirstIsBottomPt(const OutPt* btmPt1, const OutPt* btmPt2)
{
  OutPt *p = btmPt1->prev;
  while (PointsEqual(p->pt, btmPt1->pt) && (p != btmPt1)) p = p->prev;
  double dx1p = std::fabs(GetDx(btmPt1->pt, p->pt));
  p = btmPt1->next;
  while (PointsEqual(p->pt, btmPt1->pt) && (p != btmPt1)) p = p->next;
  double dx1n = std::fabs(GetDx(btmPt1->pt, p->pt));

  p = btmPt2->prev;
  while (PointsEqual(p->pt, btmPt2->pt) && (p != btmPt2)) p = p->prev;
  double dx2p = std::fabs(GetDx(btmPt2->pt, p->pt));
  p = btmPt2->next;
  while (PointsEqual(p->pt, btmPt2->pt) && (p != btmPt2)) p = p->next;
  double dx2n = std::fabs(GetDx(btmPt2->pt, p->pt));
  return (dx1p >= dx2p && dx1p >= dx2n) || (dx1n >= dx2p && dx1n >= dx2n);
}
//------------------------------------------------------------------------------

OutPt* GetBottomPt(OutPt *pp)
{
  OutPt* dups = 0;
  OutPt* p = pp->next;
  while (p != pp)
  {
    if (p->pt.Y > pp->pt.Y)
    {
      pp = p;
      dups = 0;
    }
    else if (p->pt.Y == pp->pt.Y && p->pt.X <= pp->pt.X)
    {
      if (p->pt.X < pp->pt.X)
      {
        dups = 0;
        pp = p;
      } else
      {
        if (p->next != pp && p->prev != pp) dups = p;
      }
    }
    p = p->next;
  }
  if (dups)
  {
    //there appears to be at least 2 vertices at bottomPt so ...
    while (dups != p)
    {
      if (!FirstIsBottomPt(p, dups)) pp = dups;
      dups = dups->next;
      while (!PointsEqual(dups->pt, pp->pt)) dups = dups->next;
    }
  }
  return pp;
}
//------------------------------------------------------------------------------

bool FindSegment(OutPt* &pp, bool UseFullInt64Range, 
  IntPoint &pt1, IntPoint &pt2)
{
  //outPt1 & outPt2 => the overlap segment (if the function returns true)
  if (!pp) return false;
  OutPt* pp2 = pp;
  IntPoint pt1a = pt1, pt2a = pt2;
  do
  {
    if (SlopesEqual(pt1a, pt2a, pp->pt, pp->prev->pt, UseFullInt64Range) &&
      SlopesEqual(pt1a, pt2a, pp->pt, UseFullInt64Range) &&
      GetOverlapSegment(pt1a, pt2a, pp->pt, pp->prev->pt, pt1, pt2))
        return true;
    pp = pp->next;
  }
  while (pp != pp2);
  return false;
}
//------------------------------------------------------------------------------

bool Pt3IsBetweenPt1AndPt2(const IntPoint pt1,
  const IntPoint pt2, const IntPoint pt3)
{
  if (PointsEqual(pt1, pt3) || PointsEqual(pt2, pt3)) return true;
  else if (pt1.X != pt2.X) return (pt1.X < pt3.X) == (pt3.X < pt2.X);
  else return (pt1.Y < pt3.Y) == (pt3.Y < pt2.Y);
}
//------------------------------------------------------------------------------

OutPt* InsertPolyPtBetween(OutPt* p1, OutPt* p2, const IntPoint pt)
{
  if (p1 == p2) throw "JoinError";
  OutPt* result = new OutPt;
  result->pt = pt;
  if (p2 == p1->next)
  {
    p1->next = result;
    p2->prev = result;
    result->next = p2;
    result->prev = p1;
  } else
  {
    p2->next = result;
    p1->prev = result;
    result->next = p1;
    result->prev = p2;
  }
  return result;
}

//------------------------------------------------------------------------------
// ClipperBase class methods ...
//------------------------------------------------------------------------------

ClipperBase::ClipperBase() //constructor
{
  m_MinimaList = 0;
  m_CurrentLM = 0;
  m_UseFullRange = true;
}
//------------------------------------------------------------------------------

ClipperBase::~ClipperBase() //destructor
{
  Clear();
}
//------------------------------------------------------------------------------

void RangeTest(const IntPoint& pt, long64& maxrange)
{
  if (Abs(pt.X) > maxrange)
  {
    if (Abs(pt.X) > hiRange) 
      throw coords_range_error;
    else maxrange = hiRange;
  }
  if (Abs(pt.Y) > maxrange)
  {
    if (Abs(pt.Y) > hiRange)
      throw coords_range_error;
    else maxrange = hiRange;
  }
}
//------------------------------------------------------------------------------

bool ClipperBase::AddPolygon(const Polygon &pg, PolyType polyType)
{
  int len = (int)pg.size();
  if (len < 3) return false;

  long64 maxVal;
  if (m_UseFullRange) maxVal = hiRange; else maxVal = loRange;
  RangeTest(pg[0], maxVal);

  Polygon p(len);
  p[0] = pg[0];
  int j = 0;

  for (int i = 0; i < len; ++i)
  {
    RangeTest(pg[i], maxVal);

    if (i == 0 || PointsEqual(p[j], pg[i])) continue;
    else if (j > 0 && SlopesEqual(p[j-1], p[j], pg[i], m_UseFullRange))
    {
      if (PointsEqual(p[j-1], pg[i])) j--;
    } else j++;
    p[j] = pg[i];
  }
  if (j < 2) return false;

  len = j+1;
  while (len > 2)
  {
    //nb: test for point equality before testing slopes ...
    if (PointsEqual(p[j], p[0])) j--;
    else if (PointsEqual(p[0], p[1]) ||
      SlopesEqual(p[j], p[0], p[1], m_UseFullRange))
      p[0] = p[j--];
    else if (SlopesEqual(p[j-1], p[j], p[0], m_UseFullRange)) j--;
    else if (SlopesEqual(p[0], p[1], p[2], m_UseFullRange))
    {
      for (int i = 2; i <= j; ++i) p[i-1] = p[i];
      j--;
    }
    else break;
    len--;
  }
  if (len < 3) return false;

  //create a new edge array ...
  TEdge *edges = new TEdge [len];
  m_edges.push_back(edges);

  //convert vertices to a double-linked-list of edges and initialize ...
  edges[0].xcurr = p[0].X;
  edges[0].ycurr = p[0].Y;
  InitEdge(&edges[len-1], &edges[0], &edges[len-2], p[len-1], polyType);
  for (int i = len-2; i > 0; --i)
    InitEdge(&edges[i], &edges[i+1], &edges[i-1], p[i], polyType);
  InitEdge(&edges[0], &edges[1], &edges[len-1], p[0], polyType);

  //reset xcurr & ycurr and find 'eHighest' (given the Y axis coordinates
  //increase downward so the 'highest' edge will have the smallest ytop) ...
  TEdge *e = &edges[0];
  TEdge *eHighest = e;
  do
  {
    e->xcurr = e->xbot;
    e->ycurr = e->ybot;
    if (e->ytop < eHighest->ytop) eHighest = e;
    e = e->next;
  }
  while ( e != &edges[0]);

  //make sure eHighest is positioned so the following loop works safely ...
  if (eHighest->windDelta > 0) eHighest = eHighest->next;
  if (NEAR_EQUAL(eHighest->dx, HORIZONTAL)) eHighest = eHighest->next;

  //finally insert each local minima ...
  e = eHighest;
  do {
    e = AddBoundsToLML(e);
  }
  while( e != eHighest );
  return true;
}
//------------------------------------------------------------------------------

void ClipperBase::InsertLocalMinima(LocalMinima *newLm)
{
  if( ! m_MinimaList )
  {
    m_MinimaList = newLm;
  }
  else if( newLm->Y >= m_MinimaList->Y )
  {
    newLm->next = m_MinimaList;
    m_MinimaList = newLm;
  } else
  {
    LocalMinima* tmpLm = m_MinimaList;
    while( tmpLm->next  && ( newLm->Y < tmpLm->next->Y ) )
      tmpLm = tmpLm->next;
    newLm->next = tmpLm->next;
    tmpLm->next = newLm;
  }
}
//------------------------------------------------------------------------------

TEdge* ClipperBase::AddBoundsToLML(TEdge *e)
{
  //Starting at the top of one bound we progress to the bottom where there's
  //a local minima. We then go to the top of the next bound. These two bounds
  //form the left and right (or right and left) bounds of the local minima.
  e->nextInLML = 0;
  e = e->next;
  for (;;)
  {
    if (NEAR_EQUAL(e->dx, HORIZONTAL))
    {
      //nb: proceed through horizontals when approaching from their right,
      //    but break on horizontal minima if approaching from their left.
      //    This ensures 'local minima' are always on the left of horizontals.
      if (e->next->ytop < e->ytop && e->next->xbot > e->prev->xbot) break;
      if (e->xtop != e->prev->xbot) SwapX(*e);
      e->nextInLML = e->prev;
    }
    else if (e->ycurr == e->prev->ycurr) break;
    else e->nextInLML = e->prev;
    e = e->next;
  }

  //e and e.prev are now at a local minima ...
  LocalMinima* newLm = new LocalMinima;
  newLm->next = 0;
  newLm->Y = e->prev->ybot;

  if ( NEAR_EQUAL(e->dx, HORIZONTAL) ) //horizontal edges never start a left bound
  {
    if (e->xbot != e->prev->xbot) SwapX(*e);
    newLm->leftBound = e->prev;
    newLm->rightBound = e;
  } else if (e->dx < e->prev->dx)
  {
    newLm->leftBound = e->prev;
    newLm->rightBound = e;
  } else
  {
    newLm->leftBound = e;
    newLm->rightBound = e->prev;
  }
  newLm->leftBound->side = esLeft;
  newLm->rightBound->side = esRight;
  InsertLocalMinima( newLm );

  for (;;)
  {
    if ( e->next->ytop == e->ytop && !NEAR_EQUAL(e->next->dx, HORIZONTAL) ) break;
    e->nextInLML = e->next;
    e = e->next;
    if ( NEAR_EQUAL(e->dx, HORIZONTAL) && e->xbot != e->prev->xtop) SwapX(*e);
  }
  return e->next;
}
//------------------------------------------------------------------------------

bool ClipperBase::AddPolygons(const Polygons &ppg, PolyType polyType)
{
  bool result = false;
  for (Polygons::size_type i = 0; i < ppg.size(); ++i)
    if (AddPolygon(ppg[i], polyType)) result = true;
  return result;
}
//------------------------------------------------------------------------------

void ClipperBase::Clear()
{
  DisposeLocalMinimaList();
  for (EdgeList::size_type i = 0; i < m_edges.size(); ++i) delete [] m_edges[i];
  m_edges.clear();
  m_UseFullRange = false;
}
//------------------------------------------------------------------------------

void ClipperBase::Reset()
{
  m_CurrentLM = m_MinimaList;
  if( !m_CurrentLM ) return; //ie nothing to process

  //reset all edges ...
  LocalMinima* lm = m_MinimaList;
  while( lm )
  {
    TEdge* e = lm->leftBound;
    while( e )
    {
      e->xcurr = e->xbot;
      e->ycurr = e->ybot;
      e->side = esLeft;
      e->outIdx = -1;
      e = e->nextInLML;
    }
    e = lm->rightBound;
    while( e )
    {
      e->xcurr = e->xbot;
      e->ycurr = e->ybot;
      e->side = esRight;
      e->outIdx = -1;
      e = e->nextInLML;
    }
    lm = lm->next;
  }
}
//------------------------------------------------------------------------------

void ClipperBase::DisposeLocalMinimaList()
{
  while( m_MinimaList )
  {
    LocalMinima* tmpLm = m_MinimaList->next;
    delete m_MinimaList;
    m_MinimaList = tmpLm;
  }
  m_CurrentLM = 0;
}
//------------------------------------------------------------------------------

void ClipperBase::PopLocalMinima()
{
  if( ! m_CurrentLM ) return;
  m_CurrentLM = m_CurrentLM->next;
}
//------------------------------------------------------------------------------

IntRect ClipperBase::GetBounds()
{
  IntRect result;
  LocalMinima* lm = m_MinimaList;
  if (!lm)
  {
    result.left = result.top = result.right = result.bottom = 0;
    return result;
  }
  result.left = lm->leftBound->xbot;
  result.top = lm->leftBound->ybot;
  result.right = lm->leftBound->xbot;
  result.bottom = lm->leftBound->ybot;
  while (lm)
  {
    if (lm->leftBound->ybot > result.bottom)
      result.bottom = lm->leftBound->ybot;
    TEdge* e = lm->leftBound;
    for (;;) {
      TEdge* bottomE = e;
      while (e->nextInLML)
      {
        if (e->xbot < result.left) result.left = e->xbot;
        if (e->xbot > result.right) result.right = e->xbot;
        e = e->nextInLML;
      }
      if (e->xbot < result.left) result.left = e->xbot;
      if (e->xbot > result.right) result.right = e->xbot;
      if (e->xtop < result.left) result.left = e->xtop;
      if (e->xtop > result.right) result.right = e->xtop;
      if (e->ytop < result.top) result.top = e->ytop;

      if (bottomE == lm->leftBound) e = lm->rightBound;
      else break;
    }
    lm = lm->next;
  }
  return result;
}


//------------------------------------------------------------------------------
// TClipper methods ...
//------------------------------------------------------------------------------

Clipper::Clipper() : ClipperBase() //constructor
{
  m_Scanbeam = 0;
  m_ActiveEdges = 0;
  m_SortedEdges = 0;
  m_IntersectNodes = 0;
  m_ExecuteLocked = false;
  m_UseFullRange = false;
  m_ReverseOutput = false;
  m_ForceSimple = false;
}
//------------------------------------------------------------------------------

Clipper::~Clipper() //destructor
{
  Clear();
  DisposeScanbeamList();
}
//------------------------------------------------------------------------------

void Clipper::Clear()
{
  if (m_edges.empty()) return; //avoids problems with ClipperBase destructor
  DisposeAllPolyPts();
  ClipperBase::Clear();
}
//------------------------------------------------------------------------------

void Clipper::DisposeScanbeamList()
{
  while ( m_Scanbeam ) {
  Scanbeam* sb2 = m_Scanbeam->next;
  delete m_Scanbeam;
  m_Scanbeam = sb2;
  }
}
//------------------------------------------------------------------------------

void Clipper::Reset()
{
  ClipperBase::Reset();
  m_Scanbeam = 0;
  m_ActiveEdges = 0;
  m_SortedEdges = 0;
  DisposeAllPolyPts();
  LocalMinima* lm = m_MinimaList;
  while (lm)
  {
    InsertScanbeam(lm->Y);
    lm = lm->next;
  }
}
//------------------------------------------------------------------------------

bool Clipper::Execute(ClipType clipType, Polygons &solution,
    PolyFillType subjFillType, PolyFillType clipFillType)
{
  if( m_ExecuteLocked ) return false;
  m_ExecuteLocked = true;
  solution.resize(0);
  m_SubjFillType = subjFillType;
  m_ClipFillType = clipFillType;
  m_ClipType = clipType;
  m_UsingPolyTree = false;
  bool succeeded = ExecuteInternal();
  if (succeeded) BuildResult(solution);
  m_ExecuteLocked = false;
  return succeeded;
}
//------------------------------------------------------------------------------

bool Clipper::Execute(ClipType clipType, PolyTree& polytree,
    PolyFillType subjFillType, PolyFillType clipFillType)
{
  if( m_ExecuteLocked ) return false;
  m_ExecuteLocked = true;
  m_SubjFillType = subjFillType;
  m_ClipFillType = clipFillType;
  m_ClipType = clipType;
  m_UsingPolyTree = true;
  bool succeeded = ExecuteInternal();
  if (succeeded) BuildResult2(polytree);
  m_ExecuteLocked = false;
  return succeeded;
}
//------------------------------------------------------------------------------

void Clipper::FixHoleLinkage(OutRec &outrec)
{
  //skip OutRecs that (a) contain outermost polygons or
  //(b) already have the correct owner/child linkage ...
  if (!outrec.FirstLeft ||                
      (outrec.isHole != outrec.FirstLeft->isHole &&
      outrec.FirstLeft->pts)) return;

  OutRec* orfl = outrec.FirstLeft;
  while (orfl && ((orfl->isHole == outrec.isHole) || !orfl->pts))
      orfl = orfl->FirstLeft;
  outrec.FirstLeft = orfl;
}
//------------------------------------------------------------------------------

bool Clipper::ExecuteInternal()
{
  bool succeeded;
  try {
    Reset();
    if (!m_CurrentLM ) return true;
    long64 botY = PopScanbeam();
    do {
      InsertLocalMinimaIntoAEL(botY);
      ClearHorzJoins();
      ProcessHorizontals();
      long64 topY = PopScanbeam();
      succeeded = ProcessIntersections(botY, topY);
      if (!succeeded) break;
      ProcessEdgesAtTopOfScanbeam(topY);
      botY = topY;
    } while(m_Scanbeam || m_CurrentLM);
  }
  catch(...) {
    succeeded = false;
  }

  if (succeeded)
  {
    //tidy up output polygons and fix orientations where necessary ...
    for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); ++i)
    {
      OutRec *outRec = m_PolyOuts[i];
      if (!outRec->pts) continue;
      FixupOutPolygon(*outRec);
      if (!outRec->pts) continue;

      if ((outRec->isHole ^ m_ReverseOutput) == (Area(*outRec, m_UseFullRange) > 0))
        ReversePolyPtLinks(outRec->pts);
    }

    if (!m_Joins.empty()) JoinCommonEdges();
    if (m_ForceSimple) DoSimplePolygons();
  }

  ClearJoins();
  ClearHorzJoins();
  return succeeded;
}
//------------------------------------------------------------------------------

void Clipper::InsertScanbeam(const long64 Y)
{
  if( !m_Scanbeam )
  {
    m_Scanbeam = new Scanbeam;
    m_Scanbeam->next = 0;
    m_Scanbeam->Y = Y;
  }
  else if(  Y > m_Scanbeam->Y )
  {
    Scanbeam* newSb = new Scanbeam;
    newSb->Y = Y;
    newSb->next = m_Scanbeam;
    m_Scanbeam = newSb;
  } else
  {
    Scanbeam* sb2 = m_Scanbeam;
    while( sb2->next  && ( Y <= sb2->next->Y ) ) sb2 = sb2->next;
    if(  Y == sb2->Y ) return; //ie ignores duplicates
    Scanbeam* newSb = new Scanbeam;
    newSb->Y = Y;
    newSb->next = sb2->next;
    sb2->next = newSb;
  }
}
//------------------------------------------------------------------------------

long64 Clipper::PopScanbeam()
{
  long64 Y = m_Scanbeam->Y;
  Scanbeam* sb2 = m_Scanbeam;
  m_Scanbeam = m_Scanbeam->next;
  delete sb2;
  return Y;
}
//------------------------------------------------------------------------------

void Clipper::DisposeAllPolyPts(){
  for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); ++i)
    DisposeOutRec(i);
  m_PolyOuts.clear();
}
//------------------------------------------------------------------------------

void Clipper::DisposeOutRec(PolyOutList::size_type index)
{
  OutRec *outRec = m_PolyOuts[index];
  if (outRec->pts) DisposeOutPts(outRec->pts);
  delete outRec;
  m_PolyOuts[index] = 0;
}
//------------------------------------------------------------------------------

void Clipper::SetWindingCount(TEdge &edge)
{
  TEdge *e = edge.prevInAEL;
  //find the edge of the same polytype that immediately preceeds 'edge' in AEL
  while ( e  && e->polyType != edge.polyType ) e = e->prevInAEL;
  if ( !e )
  {
    edge.windCnt = edge.windDelta;
    edge.windCnt2 = 0;
    e = m_ActiveEdges; //ie get ready to calc windCnt2
  } else if ( IsEvenOddFillType(edge) )
  {
    //EvenOdd filling ...
    edge.windCnt = 1;
    edge.windCnt2 = e->windCnt2;
    e = e->nextInAEL; //ie get ready to calc windCnt2
  } else
  {
    //nonZero, Positive or Negative filling ...
    if ( e->windCnt * e->windDelta < 0 )
    {
      if (Abs(e->windCnt) > 1)
      {
        if (e->windDelta * edge.windDelta < 0) edge.windCnt = e->windCnt;
        else edge.windCnt = e->windCnt + edge.windDelta;
      } else
        edge.windCnt = e->windCnt + e->windDelta + edge.windDelta;
    } else
    {
      if ( Abs(e->windCnt) > 1 && e->windDelta * edge.windDelta < 0)
        edge.windCnt = e->windCnt;
      else if ( e->windCnt + edge.windDelta == 0 )
        edge.windCnt = e->windCnt;
      else edge.windCnt = e->windCnt + edge.windDelta;
    }
    edge.windCnt2 = e->windCnt2;
    e = e->nextInAEL; //ie get ready to calc windCnt2
  }

  //update windCnt2 ...
  if ( IsEvenOddAltFillType(edge) )
  {
    //EvenOdd filling ...
    while ( e != &edge )
    {
      edge.windCnt2 = (edge.windCnt2 == 0) ? 1 : 0;
      e = e->nextInAEL;
    }
  } else
  {
    //nonZero, Positive or Negative filling ...
    while ( e != &edge )
    {
      edge.windCnt2 += e->windDelta;
      e = e->nextInAEL;
    }
  }
}
//------------------------------------------------------------------------------

bool Clipper::IsEvenOddFillType(const TEdge& edge) const
{
  if (edge.polyType == ptSubject)
    return m_SubjFillType == pftEvenOdd; else
    return m_ClipFillType == pftEvenOdd;
}
//------------------------------------------------------------------------------

bool Clipper::IsEvenOddAltFillType(const TEdge& edge) const
{
  if (edge.polyType == ptSubject)
    return m_ClipFillType == pftEvenOdd; else
    return m_SubjFillType == pftEvenOdd;
}
//------------------------------------------------------------------------------

bool Clipper::IsContributing(const TEdge& edge) const
{
  PolyFillType pft, pft2;
  if (edge.polyType == ptSubject)
  {
    pft = m_SubjFillType;
    pft2 = m_ClipFillType;
  } else
  {
    pft = m_ClipFillType;
    pft2 = m_SubjFillType;
  }

  switch(pft)
  {
    case pftEvenOdd: 
    case pftNonZero:
      if (Abs(edge.windCnt) != 1) return false;
      break;
    case pftPositive: 
      if (edge.windCnt != 1) return false;
      break;
    default: //pftNegative
      if (edge.windCnt != -1) return false;
  }

  switch(m_ClipType)
  {
    case ctIntersection:
      switch(pft2)
      {
        case pftEvenOdd: 
        case pftNonZero: 
          return (edge.windCnt2 != 0);
        case pftPositive: 
          return (edge.windCnt2 > 0);
        default: 
          return (edge.windCnt2 < 0);
      }
      break;
    case ctUnion:
      switch(pft2)
      {
        case pftEvenOdd: 
        case pftNonZero: 
          return (edge.windCnt2 == 0);
        case pftPositive: 
          return (edge.windCnt2 <= 0);
        default: 
          return (edge.windCnt2 >= 0);
      }
      break;
    case ctDifference:
      if (edge.polyType == ptSubject)
        switch(pft2)
        {
          case pftEvenOdd: 
          case pftNonZero: 
            return (edge.windCnt2 == 0);
          case pftPositive: 
            return (edge.windCnt2 <= 0);
          default: 
            return (edge.windCnt2 >= 0);
        }
      else
        switch(pft2)
        {
          case pftEvenOdd: 
          case pftNonZero: 
            return (edge.windCnt2 != 0);
          case pftPositive: 
            return (edge.windCnt2 > 0);
          default: 
            return (edge.windCnt2 < 0);
        }
      break;
    default:
      return true;
  }
}
//------------------------------------------------------------------------------

void Clipper::AddLocalMinPoly(TEdge *e1, TEdge *e2, const IntPoint &pt)
{
  TEdge *e, *prevE;
  if( NEAR_EQUAL(e2->dx, HORIZONTAL) || ( e1->dx > e2->dx ) )
  {
    AddOutPt( e1, pt );
    e2->outIdx = e1->outIdx;
    e1->side = esLeft;
    e2->side = esRight;
    e = e1;
    if (e->prevInAEL == e2)
      prevE = e2->prevInAEL; 
    else
      prevE = e->prevInAEL;
  } else
  {
    AddOutPt( e2, pt );
    e1->outIdx = e2->outIdx;
    e1->side = esRight;
    e2->side = esLeft;
    e = e2;
    if (e->prevInAEL == e1)
        prevE = e1->prevInAEL;
    else
        prevE = e->prevInAEL;
  }
  if (prevE && prevE->outIdx >= 0 &&
      (TopX(*prevE, pt.Y) == TopX(*e, pt.Y)) &&
        SlopesEqual(*e, *prevE, m_UseFullRange))
          AddJoin(e, prevE, -1, -1);
}
//------------------------------------------------------------------------------

void Clipper::AddLocalMaxPoly(TEdge *e1, TEdge *e2, const IntPoint &pt)
{
  AddOutPt( e1, pt );
  if( e1->outIdx == e2->outIdx )
  {
    e1->outIdx = -1;
    e2->outIdx = -1;
  }
  else if (e1->outIdx < e2->outIdx) 
    AppendPolygon(e1, e2); 
  else 
    AppendPolygon(e2, e1);
}
//------------------------------------------------------------------------------

void Clipper::AddEdgeToSEL(TEdge *edge)
{
  //SEL pointers in PEdge are reused to build a list of horizontal edges.
  //However, we don't need to worry about order with horizontal edge processing.
  if( !m_SortedEdges )
  {
    m_SortedEdges = edge;
    edge->prevInSEL = 0;
    edge->nextInSEL = 0;
  }
  else
  {
    edge->nextInSEL = m_SortedEdges;
    edge->prevInSEL = 0;
    m_SortedEdges->prevInSEL = edge;
    m_SortedEdges = edge;
  }
}
//------------------------------------------------------------------------------

void Clipper::CopyAELToSEL()
{
  TEdge* e = m_ActiveEdges;
  m_SortedEdges = e;
  while ( e )
  {
    e->prevInSEL = e->prevInAEL;
    e->nextInSEL = e->nextInAEL;
    e = e->nextInAEL;
  }
}
//------------------------------------------------------------------------------

void Clipper::AddJoin(TEdge *e1, TEdge *e2, int e1OutIdx, int e2OutIdx)
{
  JoinRec* jr = new JoinRec;
  if (e1OutIdx >= 0)
    jr->poly1Idx = e1OutIdx; else
    jr->poly1Idx = e1->outIdx;
  jr->pt1a = IntPoint(e1->xcurr, e1->ycurr);
  jr->pt1b = IntPoint(e1->xtop, e1->ytop);
  if (e2OutIdx >= 0)
    jr->poly2Idx = e2OutIdx; else
    jr->poly2Idx = e2->outIdx;
  jr->pt2a = IntPoint(e2->xcurr, e2->ycurr);
  jr->pt2b = IntPoint(e2->xtop, e2->ytop);
  m_Joins.push_back(jr);
}
//------------------------------------------------------------------------------

void Clipper::ClearJoins()
{
  for (JoinList::size_type i = 0; i < m_Joins.size(); i++)
    delete m_Joins[i];
  m_Joins.resize(0);
}
//------------------------------------------------------------------------------

void Clipper::AddHorzJoin(TEdge *e, int idx)
{
  HorzJoinRec* hj = new HorzJoinRec;
  hj->edge = e;
  hj->savedIdx = idx;
  m_HorizJoins.push_back(hj);
}
//------------------------------------------------------------------------------

void Clipper::ClearHorzJoins()
{
  for (HorzJoinList::size_type i = 0; i < m_HorizJoins.size(); i++)
    delete m_HorizJoins[i];
  m_HorizJoins.resize(0);
}
//------------------------------------------------------------------------------

void Clipper::InsertLocalMinimaIntoAEL(const long64 botY)
{
  while(  m_CurrentLM  && ( m_CurrentLM->Y == botY ) )
  {
    TEdge* lb = m_CurrentLM->leftBound;
    TEdge* rb = m_CurrentLM->rightBound;

    InsertEdgeIntoAEL( lb );
    InsertScanbeam( lb->ytop );
    InsertEdgeIntoAEL( rb );

    if (IsEvenOddFillType(*lb))
    {
      lb->windDelta = 1;
      rb->windDelta = 1;
    }
    else
    {
      rb->windDelta = -lb->windDelta;
    }
    SetWindingCount( *lb );
    rb->windCnt = lb->windCnt;
    rb->windCnt2 = lb->windCnt2;

    if( NEAR_EQUAL(rb->dx, HORIZONTAL) )
    {
      //nb: only rightbounds can have a horizontal bottom edge
      AddEdgeToSEL( rb );
      InsertScanbeam( rb->nextInLML->ytop );
    }
    else
      InsertScanbeam( rb->ytop );

    if( IsContributing(*lb) )
      AddLocalMinPoly( lb, rb, IntPoint(lb->xcurr, m_CurrentLM->Y) );

    //if any output polygons share an edge, they'll need joining later ...
    if (rb->outIdx >= 0 && NEAR_EQUAL(rb->dx, HORIZONTAL))
    {
      for (HorzJoinList::size_type i = 0; i < m_HorizJoins.size(); ++i)
      {
        IntPoint pt, pt2; //returned by GetOverlapSegment() but unused here.
        HorzJoinRec* hj = m_HorizJoins[i];
        //if horizontals rb and hj.edge overlap, flag for joining later ...
        if (GetOverlapSegment(IntPoint(hj->edge->xbot, hj->edge->ybot),
          IntPoint(hj->edge->xtop, hj->edge->ytop),
          IntPoint(rb->xbot, rb->ybot),
          IntPoint(rb->xtop, rb->ytop), pt, pt2))
            AddJoin(hj->edge, rb, hj->savedIdx);
      }
    }

    if( lb->nextInAEL != rb )
    {
      if (rb->outIdx >= 0 && rb->prevInAEL->outIdx >= 0 &&
        SlopesEqual(*rb->prevInAEL, *rb, m_UseFullRange))
          AddJoin(rb, rb->prevInAEL);

      TEdge* e = lb->nextInAEL;
      IntPoint pt = IntPoint(lb->xcurr, lb->ycurr);
      while( e != rb )
      {
        if(!e) throw clipperException("InsertLocalMinimaIntoAEL: missing rightbound!");
        //nb: For calculating winding counts etc, IntersectEdges() assumes
        //that param1 will be to the right of param2 ABOVE the intersection ...
        IntersectEdges( rb , e , pt , ipNone); //order important here
        e = e->nextInAEL;
      }
    }
    PopLocalMinima();
  }
}
//------------------------------------------------------------------------------

void Clipper::DeleteFromAEL(TEdge *e)
{
  TEdge* AelPrev = e->prevInAEL;
  TEdge* AelNext = e->nextInAEL;
  if(  !AelPrev &&  !AelNext && (e != m_ActiveEdges) ) return; //already deleted
  if( AelPrev ) AelPrev->nextInAEL = AelNext;
  else m_ActiveEdges = AelNext;
  if( AelNext ) AelNext->prevInAEL = AelPrev;
  e->nextInAEL = 0;
  e->prevInAEL = 0;
}
//------------------------------------------------------------------------------

void Clipper::DeleteFromSEL(TEdge *e)
{
  TEdge* SelPrev = e->prevInSEL;
  TEdge* SelNext = e->nextInSEL;
  if( !SelPrev &&  !SelNext && (e != m_SortedEdges) ) return; //already deleted
  if( SelPrev ) SelPrev->nextInSEL = SelNext;
  else m_SortedEdges = SelNext;
  if( SelNext ) SelNext->prevInSEL = SelPrev;
  e->nextInSEL = 0;
  e->prevInSEL = 0;
}
//------------------------------------------------------------------------------

void Clipper::IntersectEdges(TEdge *e1, TEdge *e2,
     const IntPoint &pt, const IntersectProtects protects)
{
  //e1 will be to the left of e2 BELOW the intersection. Therefore e1 is before
  //e2 in AEL except when e1 is being inserted at the intersection point ...
  bool e1stops = !(ipLeft & protects) &&  !e1->nextInLML &&
    e1->xtop == pt.X && e1->ytop == pt.Y;
  bool e2stops = !(ipRight & protects) &&  !e2->nextInLML &&
    e2->xtop == pt.X && e2->ytop == pt.Y;
  bool e1Contributing = ( e1->outIdx >= 0 );
  bool e2contributing = ( e2->outIdx >= 0 );

  //update winding counts...
  //assumes that e1 will be to the right of e2 ABOVE the intersection
  if ( e1->polyType == e2->polyType )
  {
    if ( IsEvenOddFillType( *e1) )
    {
      int oldE1WindCnt = e1->windCnt;
      e1->windCnt = e2->windCnt;
      e2->windCnt = oldE1WindCnt;
    } else
    {
      if (e1->windCnt + e2->windDelta == 0 ) e1->windCnt = -e1->windCnt;
      else e1->windCnt += e2->windDelta;
      if ( e2->windCnt - e1->windDelta == 0 ) e2->windCnt = -e2->windCnt;
      else e2->windCnt -= e1->windDelta;
    }
  } else
  {
    if (!IsEvenOddFillType(*e2)) e1->windCnt2 += e2->windDelta;
    else e1->windCnt2 = ( e1->windCnt2 == 0 ) ? 1 : 0;
    if (!IsEvenOddFillType(*e1)) e2->windCnt2 -= e1->windDelta;
    else e2->windCnt2 = ( e2->windCnt2 == 0 ) ? 1 : 0;
  }

  PolyFillType e1FillType, e2FillType, e1FillType2, e2FillType2;
  if (e1->polyType == ptSubject)
  {
    e1FillType = m_SubjFillType;
    e1FillType2 = m_ClipFillType;
  } else
  {
    e1FillType = m_ClipFillType;
    e1FillType2 = m_SubjFillType;
  }
  if (e2->polyType == ptSubject)
  {
    e2FillType = m_SubjFillType;
    e2FillType2 = m_ClipFillType;
  } else
  {
    e2FillType = m_ClipFillType;
    e2FillType2 = m_SubjFillType;
  }

  long64 e1Wc, e2Wc;
  switch (e1FillType)
  {
    case pftPositive: e1Wc = e1->windCnt; break;
    case pftNegative: e1Wc = -e1->windCnt; break;
    default: e1Wc = Abs(e1->windCnt);
  }
  switch(e2FillType)
  {
    case pftPositive: e2Wc = e2->windCnt; break;
    case pftNegative: e2Wc = -e2->windCnt; break;
    default: e2Wc = Abs(e2->windCnt);
  }

  if ( e1Contributing && e2contributing )
  {
    if ( e1stops || e2stops || 
      (e1Wc != 0 && e1Wc != 1) || (e2Wc != 0 && e2Wc != 1) ||
      (e1->polyType != e2->polyType && m_ClipType != ctXor) )
        AddLocalMaxPoly(e1, e2, pt); 
    else
    {
      AddOutPt(e1, pt);
      AddOutPt(e2, pt);
      SwapSides( *e1 , *e2 );
      SwapPolyIndexes( *e1 , *e2 );
    }
  }
  else if ( e1Contributing )
  {
    if (e2Wc == 0 || e2Wc == 1) 
    {
      AddOutPt(e1, pt);
      SwapSides(*e1, *e2);
      SwapPolyIndexes(*e1, *e2);
    }
  }
  else if ( e2contributing )
  {
    if (e1Wc == 0 || e1Wc == 1) 
    {
      AddOutPt(e2, pt);
      SwapSides(*e1, *e2);
      SwapPolyIndexes(*e1, *e2);
    }
  } 
  else if ( (e1Wc == 0 || e1Wc == 1) && 
    (e2Wc == 0 || e2Wc == 1) && !e1stops && !e2stops )
  {
    //neither edge is currently contributing ...

    long64 e1Wc2, e2Wc2;
    switch (e1FillType2)
    {
      case pftPositive: e1Wc2 = e1->windCnt2; break;
      case pftNegative : e1Wc2 = -e1->windCnt2; break;
      default: e1Wc2 = Abs(e1->windCnt2);
    }
    switch (e2FillType2)
    {
      case pftPositive: e2Wc2 = e2->windCnt2; break;
      case pftNegative: e2Wc2 = -e2->windCnt2; break;
      default: e2Wc2 = Abs(e2->windCnt2);
    }

    if (e1->polyType != e2->polyType)
        AddLocalMinPoly(e1, e2, pt);
    else if (e1Wc == 1 && e2Wc == 1)
      switch( m_ClipType ) {
        case ctIntersection:
          if (e1Wc2 > 0 && e2Wc2 > 0)
            AddLocalMinPoly(e1, e2, pt);
          break;
        case ctUnion:
          if ( e1Wc2 <= 0 && e2Wc2 <= 0 )
            AddLocalMinPoly(e1, e2, pt);
          break;
        case ctDifference:
          if (((e1->polyType == ptClip) && (e1Wc2 > 0) && (e2Wc2 > 0)) ||
              ((e1->polyType == ptSubject) && (e1Wc2 <= 0) && (e2Wc2 <= 0)))
                AddLocalMinPoly(e1, e2, pt);
          break;
        case ctXor:
          AddLocalMinPoly(e1, e2, pt);
      }
    else
      SwapSides( *e1, *e2 );
  }

  if(  (e1stops != e2stops) &&
    ( (e1stops && (e1->outIdx >= 0)) || (e2stops && (e2->outIdx >= 0)) ) )
  {
    SwapSides( *e1, *e2 );
    SwapPolyIndexes( *e1, *e2 );
  }

  //finally, delete any non-contributing maxima edges  ...
  if( e1stops ) DeleteFromAEL( e1 );
  if( e2stops ) DeleteFromAEL( e2 );
}
//------------------------------------------------------------------------------

void Clipper::SetHoleState(TEdge *e, OutRec *outrec)
{
  bool isHole = false;
  TEdge *e2 = e->prevInAEL;
  while (e2)
  {
    if (e2->outIdx >= 0)
    {
      isHole = !isHole;
      if (! outrec->FirstLeft)
        outrec->FirstLeft = m_PolyOuts[e2->outIdx];
    }
    e2 = e2->prevInAEL;
  }
  if (isHole) outrec->isHole = true;
}
//------------------------------------------------------------------------------

OutRec* GetLowermostRec(OutRec *outRec1, OutRec *outRec2)
{
  //work out which polygon fragment has the correct hole state ...
  if (!outRec1->bottomPt) 
    outRec1->bottomPt = GetBottomPt(outRec1->pts);
  if (!outRec2->bottomPt) 
    outRec2->bottomPt = GetBottomPt(outRec2->pts);
  OutPt *outPt1 = outRec1->bottomPt;
  OutPt *outPt2 = outRec2->bottomPt;
  if (outPt1->pt.Y > outPt2->pt.Y) return outRec1;
  else if (outPt1->pt.Y < outPt2->pt.Y) return outRec2;
  else if (outPt1->pt.X < outPt2->pt.X) return outRec1;
  else if (outPt1->pt.X > outPt2->pt.X) return outRec2;
  else if (outPt1->next == outPt1) return outRec2;
  else if (outPt2->next == outPt2) return outRec1;
  else if (FirstIsBottomPt(outPt1, outPt2)) return outRec1;
  else return outRec2;
}
//------------------------------------------------------------------------------

bool Param1RightOfParam2(OutRec* outRec1, OutRec* outRec2)
{
  do
  {
    outRec1 = outRec1->FirstLeft;
    if (outRec1 == outRec2) return true;
  } while (outRec1);
  return false;
}
//------------------------------------------------------------------------------

OutRec* Clipper::GetOutRec(int idx)
{
  OutRec* outrec = m_PolyOuts[idx];
  while (outrec != m_PolyOuts[outrec->idx])
    outrec = m_PolyOuts[outrec->idx];
  return outrec;
}
//------------------------------------------------------------------------------

void Clipper::AppendPolygon(TEdge *e1, TEdge *e2)
{
  //get the start and ends of both output polygons ...
  OutRec *outRec1 = m_PolyOuts[e1->outIdx];
  OutRec *outRec2 = m_PolyOuts[e2->outIdx];

  OutRec *holeStateRec;
  if (Param1RightOfParam2(outRec1, outRec2)) 
    holeStateRec = outRec2;
  else if (Param1RightOfParam2(outRec2, outRec1)) 
    holeStateRec = outRec1;
  else 
    holeStateRec = GetLowermostRec(outRec1, outRec2);

  OutPt* p1_lft = outRec1->pts;
  OutPt* p1_rt = p1_lft->prev;
  OutPt* p2_lft = outRec2->pts;
  OutPt* p2_rt = p2_lft->prev;

  EdgeSide side;
  //join e2 poly onto e1 poly and delete pointers to e2 ...
  if(  e1->side == esLeft )
  {
    if(  e2->side == esLeft )
    {
      //z y x a b c
      ReversePolyPtLinks(p2_lft);
      p2_lft->next = p1_lft;
      p1_lft->prev = p2_lft;
      p1_rt->next = p2_rt;
      p2_rt->prev = p1_rt;
      outRec1->pts = p2_rt;
    } else
    {
      //x y z a b c
      p2_rt->next = p1_lft;
      p1_lft->prev = p2_rt;
      p2_lft->prev = p1_rt;
      p1_rt->next = p2_lft;
      outRec1->pts = p2_lft;
    }
    side = esLeft;
  } else
  {
    if(  e2->side == esRight )
    {
      //a b c z y x
      ReversePolyPtLinks(p2_lft);
      p1_rt->next = p2_rt;
      p2_rt->prev = p1_rt;
      p2_lft->next = p1_lft;
      p1_lft->prev = p2_lft;
    } else
    {
      //a b c x y z
      p1_rt->next = p2_lft;
      p2_lft->prev = p1_rt;
      p1_lft->prev = p2_rt;
      p2_rt->next = p1_lft;
    }
    side = esRight;
  }

  outRec1->bottomPt = 0;
  if (holeStateRec == outRec2)
  {
    if (outRec2->FirstLeft != outRec1)
      outRec1->FirstLeft = outRec2->FirstLeft;
    outRec1->isHole = outRec2->isHole;
  }
  outRec2->pts = 0;
  outRec2->bottomPt = 0;

  outRec2->FirstLeft = outRec1;

  int OKIdx = e1->outIdx;
  int ObsoleteIdx = e2->outIdx;

  e1->outIdx = -1; //nb: safe because we only get here via AddLocalMaxPoly
  e2->outIdx = -1;

  TEdge* e = m_ActiveEdges;
  while( e )
  {
    if( e->outIdx == ObsoleteIdx )
    {
      e->outIdx = OKIdx;
      e->side = side;
      break;
    }
    e = e->nextInAEL;
  }

  outRec2->idx = outRec1->idx;
}
//------------------------------------------------------------------------------

OutRec* Clipper::CreateOutRec()
{
  OutRec* result = new OutRec;
  result->isHole = false;
  result->FirstLeft = 0;
  result->pts = 0;
  result->bottomPt = 0;
  result->polyNode = 0;
  m_PolyOuts.push_back(result);
  result->idx = (int)m_PolyOuts.size()-1;
  return result;
}
//------------------------------------------------------------------------------

void Clipper::AddOutPt(TEdge *e, const IntPoint &pt)
{
  bool ToFront = (e->side == esLeft);
  if(  e->outIdx < 0 )
  {
    OutRec *outRec = CreateOutRec();
    e->outIdx = outRec->idx;
    OutPt* newOp = new OutPt;
    outRec->pts = newOp;
    newOp->pt = pt;
    newOp->idx = outRec->idx;
    newOp->next = newOp;
    newOp->prev = newOp;
    SetHoleState(e, outRec);
  } else
  {
    OutRec *outRec = m_PolyOuts[e->outIdx];
    OutPt* op = outRec->pts;
    if ((ToFront && PointsEqual(pt, op->pt)) ||
      (!ToFront && PointsEqual(pt, op->prev->pt))) return;

    OutPt* newOp = new OutPt;
    newOp->pt = pt;
    newOp->idx = outRec->idx;
    newOp->next = op;
    newOp->prev = op->prev;
    newOp->prev->next = newOp;
    op->prev = newOp;
    if (ToFront) outRec->pts = newOp;
  }
}
//------------------------------------------------------------------------------

void Clipper::ProcessHorizontals()
{
  TEdge* horzEdge = m_SortedEdges;
  while( horzEdge )
  {
    DeleteFromSEL( horzEdge );
    ProcessHorizontal( horzEdge );
    horzEdge = m_SortedEdges;
  }
}
//------------------------------------------------------------------------------

bool Clipper::IsTopHorz(const long64 XPos)
{
  TEdge* e = m_SortedEdges;
  while( e )
  {
    if(  ( XPos >= std::min(e->xcurr, e->xtop) ) &&
      ( XPos <= std::max(e->xcurr, e->xtop) ) ) return false;
    e = e->nextInSEL;
  }
  return true;
}
//------------------------------------------------------------------------------

inline bool IsMinima(TEdge *e)
{
  return e  && (e->prev->nextInLML != e) && (e->next->nextInLML != e);
}
//------------------------------------------------------------------------------

inline bool IsMaxima(TEdge *e, const long64 Y)
{
  return e && e->ytop == Y && !e->nextInLML;
}
//------------------------------------------------------------------------------

inline bool IsIntermediate(TEdge *e, const long64 Y)
{
  return e->ytop == Y && e->nextInLML;
}
//------------------------------------------------------------------------------

TEdge *GetMaximaPair(TEdge *e)
{
  if( !IsMaxima(e->next, e->ytop) || e->next->xtop != e->xtop )
    return e->prev; else
    return e->next;
}
//------------------------------------------------------------------------------

void Clipper::SwapPositionsInAEL(TEdge *edge1, TEdge *edge2)
{
  if(  edge1->nextInAEL == edge2 )
  {
    TEdge* next = edge2->nextInAEL;
    if( next ) next->prevInAEL = edge1;
    TEdge* prev = edge1->prevInAEL;
    if( prev ) prev->nextInAEL = edge2;
    edge2->prevInAEL = prev;
    edge2->nextInAEL = edge1;
    edge1->prevInAEL = edge2;
    edge1->nextInAEL = next;
  }
  else if(  edge2->nextInAEL == edge1 )
  {
    TEdge* next = edge1->nextInAEL;
    if( next ) next->prevInAEL = edge2;
    TEdge* prev = edge2->prevInAEL;
    if( prev ) prev->nextInAEL = edge1;
    edge1->prevInAEL = prev;
    edge1->nextInAEL = edge2;
    edge2->prevInAEL = edge1;
    edge2->nextInAEL = next;
  }
  else
  {
    TEdge* next = edge1->nextInAEL;
    TEdge* prev = edge1->prevInAEL;
    edge1->nextInAEL = edge2->nextInAEL;
    if( edge1->nextInAEL ) edge1->nextInAEL->prevInAEL = edge1;
    edge1->prevInAEL = edge2->prevInAEL;
    if( edge1->prevInAEL ) edge1->prevInAEL->nextInAEL = edge1;
    edge2->nextInAEL = next;
    if( edge2->nextInAEL ) edge2->nextInAEL->prevInAEL = edge2;
    edge2->prevInAEL = prev;
    if( edge2->prevInAEL ) edge2->prevInAEL->nextInAEL = edge2;
  }

  if( !edge1->prevInAEL ) m_ActiveEdges = edge1;
  else if( !edge2->prevInAEL ) m_ActiveEdges = edge2;
}
//------------------------------------------------------------------------------

void Clipper::SwapPositionsInSEL(TEdge *edge1, TEdge *edge2)
{
  if(  !( edge1->nextInSEL ) &&  !( edge1->prevInSEL ) ) return;
  if(  !( edge2->nextInSEL ) &&  !( edge2->prevInSEL ) ) return;

  if(  edge1->nextInSEL == edge2 )
  {
    TEdge* next = edge2->nextInSEL;
    if( next ) next->prevInSEL = edge1;
    TEdge* prev = edge1->prevInSEL;
    if( prev ) prev->nextInSEL = edge2;
    edge2->prevInSEL = prev;
    edge2->nextInSEL = edge1;
    edge1->prevInSEL = edge2;
    edge1->nextInSEL = next;
  }
  else if(  edge2->nextInSEL == edge1 )
  {
    TEdge* next = edge1->nextInSEL;
    if( next ) next->prevInSEL = edge2;
    TEdge* prev = edge2->prevInSEL;
    if( prev ) prev->nextInSEL = edge1;
    edge1->prevInSEL = prev;
    edge1->nextInSEL = edge2;
    edge2->prevInSEL = edge1;
    edge2->nextInSEL = next;
  }
  else
  {
    TEdge* next = edge1->nextInSEL;
    TEdge* prev = edge1->prevInSEL;
    edge1->nextInSEL = edge2->nextInSEL;
    if( edge1->nextInSEL ) edge1->nextInSEL->prevInSEL = edge1;
    edge1->prevInSEL = edge2->prevInSEL;
    if( edge1->prevInSEL ) edge1->prevInSEL->nextInSEL = edge1;
    edge2->nextInSEL = next;
    if( edge2->nextInSEL ) edge2->nextInSEL->prevInSEL = edge2;
    edge2->prevInSEL = prev;
    if( edge2->prevInSEL ) edge2->prevInSEL->nextInSEL = edge2;
  }

  if( !edge1->prevInSEL ) m_SortedEdges = edge1;
  else if( !edge2->prevInSEL ) m_SortedEdges = edge2;
}
//------------------------------------------------------------------------------

TEdge* GetNextInAEL(TEdge *e, Direction dir)
{
  return dir == dLeftToRight ? e->nextInAEL : e->prevInAEL;
}
//------------------------------------------------------------------------------

void Clipper::ProcessHorizontal(TEdge *horzEdge)
{
  Direction dir;
  long64 horzLeft, horzRight;

  if( horzEdge->xcurr < horzEdge->xtop )
  {
    horzLeft = horzEdge->xcurr;
    horzRight = horzEdge->xtop;
    dir = dLeftToRight;
  } else
  {
    horzLeft = horzEdge->xtop;
    horzRight = horzEdge->xcurr;
    dir = dRightToLeft;
  }

  TEdge* eMaxPair;
  if( horzEdge->nextInLML ) eMaxPair = 0;
  else eMaxPair = GetMaximaPair(horzEdge);

  TEdge* e = GetNextInAEL( horzEdge , dir );
  while( e )
  {
    if ( e->xcurr == horzEdge->xtop && !eMaxPair )
    {
      if (SlopesEqual(*e, *horzEdge->nextInLML, m_UseFullRange))
      {
        //if output polygons share an edge, they'll need joining later ...
        if (horzEdge->outIdx >= 0 && e->outIdx >= 0)
          AddJoin(horzEdge->nextInLML, e, horzEdge->outIdx);
        break; //we've reached the end of the horizontal line
      }
      else if (e->dx < horzEdge->nextInLML->dx)
      //we really have got to the end of the intermediate horz edge so quit.
      //nb: More -ve slopes follow more +ve slopes ABOVE the horizontal.
        break;
    }

    TEdge* eNext = GetNextInAEL( e, dir );

    if (eMaxPair ||
      ((dir == dLeftToRight) && (e->xcurr < horzRight)) ||
      ((dir == dRightToLeft) && (e->xcurr > horzLeft)))
    {
      //so far we're still in range of the horizontal edge
      if( e == eMaxPair )
      {
        //horzEdge is evidently a maxima horizontal and we've arrived at its end.
        if (dir == dLeftToRight)
          IntersectEdges(horzEdge, e, IntPoint(e->xcurr, horzEdge->ycurr), ipNone);
        else
          IntersectEdges(e, horzEdge, IntPoint(e->xcurr, horzEdge->ycurr), ipNone);
        if (eMaxPair->outIdx >= 0) throw clipperException("ProcessHorizontal error");
        return;
      }
      else if( NEAR_EQUAL(e->dx, HORIZONTAL) &&  !IsMinima(e) && !(e->xcurr > e->xtop) )
      {
        //An overlapping horizontal edge. Overlapping horizontal edges are
        //processed as if layered with the current horizontal edge (horizEdge)
        //being infinitesimally lower that the next (e). Therfore, we
        //intersect with e only if e.xcurr is within the bounds of horzEdge ...
        if( dir == dLeftToRight )
          IntersectEdges( horzEdge , e, IntPoint(e->xcurr, horzEdge->ycurr),
            (IsTopHorz( e->xcurr ))? ipLeft : ipBoth );
        else
          IntersectEdges( e, horzEdge, IntPoint(e->xcurr, horzEdge->ycurr),
            (IsTopHorz( e->xcurr ))? ipRight : ipBoth );
      }
      else if( dir == dLeftToRight )
      {
        IntersectEdges( horzEdge, e, IntPoint(e->xcurr, horzEdge->ycurr),
          (IsTopHorz( e->xcurr ))? ipLeft : ipBoth );
      }
      else
      {
        IntersectEdges( e, horzEdge, IntPoint(e->xcurr, horzEdge->ycurr),
          (IsTopHorz( e->xcurr ))? ipRight : ipBoth );
      }
      SwapPositionsInAEL( horzEdge, e );
    }
    else if( (dir == dLeftToRight && e->xcurr >= horzRight) ||
     (dir == dRightToLeft && e->xcurr <= horzLeft) ) break;
    e = eNext;
  } //end while

  if( horzEdge->nextInLML )
  {
    if( horzEdge->outIdx >= 0 )
      AddOutPt( horzEdge, IntPoint(horzEdge->xtop, horzEdge->ytop));
    UpdateEdgeIntoAEL( horzEdge );
  }
  else
  {
    if ( horzEdge->outIdx >= 0 )
      IntersectEdges( horzEdge, eMaxPair,
      IntPoint(horzEdge->xtop, horzEdge->ycurr), ipBoth);
    if (eMaxPair->outIdx >= 0) throw clipperException("ProcessHorizontal error");
    DeleteFromAEL(eMaxPair);
    DeleteFromAEL(horzEdge);
  }
}
//------------------------------------------------------------------------------

void Clipper::UpdateEdgeIntoAEL(TEdge *&e)
{
  if( !e->nextInLML ) throw
    clipperException("UpdateEdgeIntoAEL: invalid call");
  TEdge* AelPrev = e->prevInAEL;
  TEdge* AelNext = e->nextInAEL;
  e->nextInLML->outIdx = e->outIdx;
  if( AelPrev ) AelPrev->nextInAEL = e->nextInLML;
  else m_ActiveEdges = e->nextInLML;
  if( AelNext ) AelNext->prevInAEL = e->nextInLML;
  e->nextInLML->side = e->side;
  e->nextInLML->windDelta = e->windDelta;
  e->nextInLML->windCnt = e->windCnt;
  e->nextInLML->windCnt2 = e->windCnt2;
  e = e->nextInLML;
  e->prevInAEL = AelPrev;
  e->nextInAEL = AelNext;
  if( !NEAR_EQUAL(e->dx, HORIZONTAL) ) InsertScanbeam( e->ytop );
}
//------------------------------------------------------------------------------

bool Clipper::ProcessIntersections(const long64 botY, const long64 topY)
{
  if( !m_ActiveEdges ) return true;
  try {
    BuildIntersectList(botY, topY);
    if (!m_IntersectNodes) return true;
    if (!m_IntersectNodes->next || FixupIntersectionOrder()) ProcessIntersectList();
    else return false;
  }
  catch(...) {
    m_SortedEdges = 0;
    DisposeIntersectNodes();
    throw clipperException("ProcessIntersections error");
  }
  m_SortedEdges = 0;
  return true;
}
//------------------------------------------------------------------------------

void Clipper::DisposeIntersectNodes()
{
  while ( m_IntersectNodes )
  {
    IntersectNode* iNode = m_IntersectNodes->next;
    delete m_IntersectNodes;
    m_IntersectNodes = iNode;
  }
}
//------------------------------------------------------------------------------

void Clipper::BuildIntersectList(const long64 botY, const long64 topY)
{
  if ( !m_ActiveEdges ) return;

  //prepare for sorting ...
  TEdge* e = m_ActiveEdges;
  m_SortedEdges = e;
  while( e )
  {
    e->prevInSEL = e->prevInAEL;
    e->nextInSEL = e->nextInAEL;
    e->xcurr = TopX( *e, topY );
    e = e->nextInAEL;
  }

  //bubblesort ...
  bool isModified;
  do
  {
    isModified = false;
    e = m_SortedEdges;
    while( e->nextInSEL )
    {
      TEdge *eNext = e->nextInSEL;
      IntPoint pt;
      if(e->xcurr > eNext->xcurr)
      {
        if (!IntersectPoint(*e, *eNext, pt, m_UseFullRange) && e->xcurr > eNext->xcurr +1)
          throw clipperException("Intersection error");
        if (pt.Y > botY)
        {
            pt.Y = botY;
            pt.X = TopX(*e, pt.Y);
        }
        InsertIntersectNode( e, eNext, pt );
        SwapPositionsInSEL(e, eNext);
        isModified = true;
      }
      else
        e = eNext;
    }
    if( e->prevInSEL ) e->prevInSEL->nextInSEL = 0;
    else break;
  }
  while ( isModified );
  m_SortedEdges = 0; //important
}
//------------------------------------------------------------------------------

void Clipper::InsertIntersectNode(TEdge *e1, TEdge *e2, const IntPoint &pt)
{
  IntersectNode* newNode = new IntersectNode;
  newNode->edge1 = e1;
  newNode->edge2 = e2;
  newNode->pt = pt;
  newNode->next = 0;
  if( !m_IntersectNodes ) m_IntersectNodes = newNode;
  else if(newNode->pt.Y > m_IntersectNodes->pt.Y )
  {
    newNode->next = m_IntersectNodes;
    m_IntersectNodes = newNode;
  }
  else
  {
    IntersectNode* iNode = m_IntersectNodes;
    while(iNode->next  && newNode->pt.Y <= iNode->next->pt.Y)
      iNode = iNode->next;
    newNode->next = iNode->next;
    iNode->next = newNode;
  }
}
//------------------------------------------------------------------------------

void Clipper::ProcessIntersectList()
{
  while( m_IntersectNodes )
  {
    IntersectNode* iNode = m_IntersectNodes->next;
    {
      IntersectEdges( m_IntersectNodes->edge1 ,
        m_IntersectNodes->edge2 , m_IntersectNodes->pt, ipBoth );
      SwapPositionsInAEL( m_IntersectNodes->edge1 , m_IntersectNodes->edge2 );
    }
    delete m_IntersectNodes;
    m_IntersectNodes = iNode;
  }
}
//------------------------------------------------------------------------------

void Clipper::DoMaxima(TEdge *e, long64 topY)
{
  TEdge* eMaxPair = GetMaximaPair(e);
  long64 X = e->xtop;
  TEdge* eNext = e->nextInAEL;
  while( eNext != eMaxPair )
  {
    if (!eNext) throw clipperException("DoMaxima error");
    IntersectEdges( e, eNext, IntPoint(X, topY), ipBoth );
    SwapPositionsInAEL(e, eNext);
    eNext = e->nextInAEL;
  }
  if( e->outIdx < 0 && eMaxPair->outIdx < 0 )
  {
    DeleteFromAEL( e );
    DeleteFromAEL( eMaxPair );
  }
  else if( e->outIdx >= 0 && eMaxPair->outIdx >= 0 )
  {
    IntersectEdges( e, eMaxPair, IntPoint(X, topY), ipNone );
  }
  else throw clipperException("DoMaxima error");
}
//------------------------------------------------------------------------------

void Clipper::ProcessEdgesAtTopOfScanbeam(const long64 topY)
{
  TEdge* e = m_ActiveEdges;
  while( e )
  {
    //1. process maxima, treating them as if they're 'bent' horizontal edges,
    //   but exclude maxima with horizontal edges. nb: e can't be a horizontal.
    if( IsMaxima(e, topY) && !NEAR_EQUAL(GetMaximaPair(e)->dx, HORIZONTAL) )
    {
      //'e' might be removed from AEL, as may any following edges so ...
      TEdge* ePrev = e->prevInAEL;
      DoMaxima(e, topY);
      if( !ePrev ) e = m_ActiveEdges;
      else e = ePrev->nextInAEL;
    }
    else
    {
      bool intermediateVert = IsIntermediate(e, topY);
      //2. promote horizontal edges, otherwise update xcurr and ycurr ...
      if (intermediateVert && NEAR_EQUAL(e->nextInLML->dx, HORIZONTAL) )
      {
        if (e->outIdx >= 0)
        {
          AddOutPt(e, IntPoint(e->xtop, e->ytop));

          for (HorzJoinList::size_type i = 0; i < m_HorizJoins.size(); ++i)
          {
            IntPoint pt, pt2;
            HorzJoinRec* hj = m_HorizJoins[i];
            if (GetOverlapSegment(IntPoint(hj->edge->xbot, hj->edge->ybot),
              IntPoint(hj->edge->xtop, hj->edge->ytop),
              IntPoint(e->nextInLML->xbot, e->nextInLML->ybot),
              IntPoint(e->nextInLML->xtop, e->nextInLML->ytop), pt, pt2))
                AddJoin(hj->edge, e->nextInLML, hj->savedIdx, e->outIdx);
          }

          AddHorzJoin(e->nextInLML, e->outIdx);
        }
        UpdateEdgeIntoAEL(e);
        AddEdgeToSEL(e);
      } else
      {
        e->xcurr = TopX( *e, topY );
        e->ycurr = topY;

        if (m_ForceSimple && e->prevInAEL &&
          e->prevInAEL->xcurr == e->xcurr && 
          e->outIdx >= 0 && e->prevInAEL->outIdx >= 0)
        {
          if (intermediateVert)             
            AddOutPt(e->prevInAEL, IntPoint(e->xcurr, topY));
          else
            AddOutPt(e, IntPoint(e->xcurr, topY));
        }
      }
      e = e->nextInAEL;
    }
  }

  //3. Process horizontals at the top of the scanbeam ...
  ProcessHorizontals();

  //4. Promote intermediate vertices ...
  e = m_ActiveEdges;
  while( e )
  {
    if( IsIntermediate( e, topY ) )
    {
      if( e->outIdx >= 0 ) AddOutPt(e, IntPoint(e->xtop,e->ytop));
      UpdateEdgeIntoAEL(e);

      //if output polygons share an edge, they'll need joining later ...
      TEdge* ePrev = e->prevInAEL;
      TEdge* eNext = e->nextInAEL;
      if (ePrev && ePrev->xcurr == e->xbot &&
        ePrev->ycurr == e->ybot && e->outIdx >= 0 &&
        ePrev->outIdx >= 0 && ePrev->ycurr > ePrev->ytop &&
        SlopesEqual(*e, *ePrev, m_UseFullRange))
      {
        AddOutPt(ePrev, IntPoint(e->xbot, e->ybot));
        AddJoin(e, ePrev);
      }
      else if (eNext && eNext->xcurr == e->xbot &&
        eNext->ycurr == e->ybot && e->outIdx >= 0 &&
        eNext->outIdx >= 0 && eNext->ycurr > eNext->ytop &&
        SlopesEqual(*e, *eNext, m_UseFullRange))
      {
        AddOutPt(eNext, IntPoint(e->xbot, e->ybot));
        AddJoin(e, eNext);
      }
    }
    e = e->nextInAEL;
  }
}
//------------------------------------------------------------------------------

void Clipper::FixupOutPolygon(OutRec &outrec)
{
  //FixupOutPolygon() - removes duplicate points and simplifies consecutive
  //parallel edges by removing the middle vertex.
  OutPt *lastOK = 0;
  outrec.bottomPt = 0;
  OutPt *pp = outrec.pts;

  for (;;)
  {
    if (pp->prev == pp || pp->prev == pp->next )
    {
      DisposeOutPts(pp);
      outrec.pts = 0;
      return;
    }
    //test for duplicate points and for same slope (cross-product) ...
    if ( PointsEqual(pp->pt, pp->next->pt) ||
      SlopesEqual(pp->prev->pt, pp->pt, pp->next->pt, m_UseFullRange) )
    {
      lastOK = 0;
      OutPt *tmp = pp;
      pp->prev->next = pp->next;
      pp->next->prev = pp->prev;
      pp = pp->prev;
      delete tmp;
    }
    else if (pp == lastOK) break;
    else
    {
      if (!lastOK) lastOK = pp;
      pp = pp->next;
    }
  }
  outrec.pts = pp;
}
//------------------------------------------------------------------------------

void Clipper::BuildResult(Polygons &polys)
{
  polys.reserve(m_PolyOuts.size());
  for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); ++i)
  {
    if (m_PolyOuts[i]->pts)
    {
      Polygon pg;
      OutPt* p = m_PolyOuts[i]->pts;
      do
      {
        pg.push_back(p->pt);
        p = p->prev;
      } while (p != m_PolyOuts[i]->pts);
      if (pg.size() > 2) 
        polys.push_back(pg);
    }
  }
}
//------------------------------------------------------------------------------

int PointCount(OutPt *pts)
{
    if (!pts) return 0;
    int result = 0;
    OutPt* p = pts;
    do
    {
        result++;
        p = p->next;
    }
    while (p != pts);
    return result;
}
//------------------------------------------------------------------------------

void Clipper::BuildResult2(PolyTree& polytree)
{
    polytree.Clear();
    polytree.AllNodes.reserve(m_PolyOuts.size());
    //add each output polygon/contour to polytree ...
    for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); i++)
    {
        OutRec* outRec = m_PolyOuts[i];
        int cnt = PointCount(outRec->pts);
        if (cnt < 3) continue;
        FixHoleLinkage(*outRec);
        PolyNode* pn = new PolyNode();
        //nb: polytree takes ownership of all the PolyNodes
        polytree.AllNodes.push_back(pn);
        outRec->polyNode = pn;
        pn->Parent = 0;
        pn->Index = 0;
        pn->Contour.reserve(cnt);
        OutPt *op = outRec->pts;
        for (int j = 0; j < cnt; j++)
        {
            pn->Contour.push_back(op->pt);
            op = op->prev;
        }
    }

    //fixup PolyNode links etc ...
    polytree.Childs.reserve(m_PolyOuts.size());
    for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); i++)
    {
        OutRec* outRec = m_PolyOuts[i];
        if (!outRec->polyNode) continue;
        if (outRec->FirstLeft) 
          outRec->FirstLeft->polyNode->AddChild(*outRec->polyNode);
        else
          polytree.AddChild(*outRec->polyNode);
    }
}
//------------------------------------------------------------------------------

void SwapIntersectNodes(IntersectNode &int1, IntersectNode &int2)
{
  //just swap the contents (because fIntersectNodes is a single-linked-list)
  IntersectNode inode = int1; //gets a copy of Int1
  int1.edge1 = int2.edge1;
  int1.edge2 = int2.edge2;
  int1.pt = int2.pt;
  int2.edge1 = inode.edge1;
  int2.edge2 = inode.edge2;
  int2.pt = inode.pt;
}
//------------------------------------------------------------------------------

inline bool EdgesAdjacent(const IntersectNode &inode)
{
  return (inode.edge1->nextInSEL == inode.edge2) ||
    (inode.edge1->prevInSEL == inode.edge2);
}
//------------------------------------------------------------------------------

bool Clipper::FixupIntersectionOrder()
{
  //pre-condition: intersections are sorted bottom-most (then left-most) first.
  //Now it's crucial that intersections are made only between adjacent edges,
  //so to ensure this the order of intersections may need adjusting ...
  IntersectNode *inode = m_IntersectNodes;  
  CopyAELToSEL();
  while (inode) 
  {
    if (!EdgesAdjacent(*inode))
    {
      IntersectNode *nextNode = inode->next;
      while (nextNode && !EdgesAdjacent(*nextNode))
        nextNode = nextNode->next;
      if (!nextNode) 
        return false;
      SwapIntersectNodes(*inode, *nextNode);
    }
    SwapPositionsInSEL(inode->edge1, inode->edge2);
    inode = inode->next;
  }
  return true;
}
//------------------------------------------------------------------------------

inline bool E2InsertsBeforeE1(TEdge &e1, TEdge &e2)
{
  if (e2.xcurr == e1.xcurr) 
  {
    if (e2.ytop > e1.ytop)
      return e2.xtop < TopX(e1, e2.ytop); 
      else return e1.xtop > TopX(e2, e1.ytop);
  } 
  else return e2.xcurr < e1.xcurr;
}
//------------------------------------------------------------------------------

void Clipper::InsertEdgeIntoAEL(TEdge *edge)
{
  edge->prevInAEL = 0;
  edge->nextInAEL = 0;
  if( !m_ActiveEdges )
  {
    m_ActiveEdges = edge;
  }
  else if( E2InsertsBeforeE1(*m_ActiveEdges, *edge) )
  {
    edge->nextInAEL = m_ActiveEdges;
    m_ActiveEdges->prevInAEL = edge;
    m_ActiveEdges = edge;
  } else
  {
    TEdge* e = m_ActiveEdges;
    while( e->nextInAEL  && !E2InsertsBeforeE1(*e->nextInAEL , *edge) )
      e = e->nextInAEL;
    edge->nextInAEL = e->nextInAEL;
    if( e->nextInAEL ) e->nextInAEL->prevInAEL = edge;
    edge->prevInAEL = e;
    e->nextInAEL = edge;
  }
}
//----------------------------------------------------------------------

bool Clipper::JoinPoints(const JoinRec *j, OutPt *&p1, OutPt *&p2)
{
  OutRec *outRec1 = m_PolyOuts[j->poly1Idx];
  OutRec *outRec2 = m_PolyOuts[j->poly2Idx];
  if (!outRec1 || !outRec2)  return false;  
  OutPt *pp1a = outRec1->pts;
  OutPt *pp2a = outRec2->pts;
  IntPoint pt1 = j->pt2a, pt2 = j->pt2b;
  IntPoint pt3 = j->pt1a, pt4 = j->pt1b;
  if (!FindSegment(pp1a, m_UseFullRange, pt1, pt2)) return false;
  if (outRec1 == outRec2)
  {
    //we're searching the same polygon for overlapping segments so
    //segment 2 mustn't be the same as segment 1 ...
    pp2a = pp1a->next;
    if (!FindSegment(pp2a, m_UseFullRange, pt3, pt4) || (pp2a == pp1a)) 
      return false;
  }
  else if (!FindSegment(pp2a, m_UseFullRange, pt3, pt4)) return false;

  if (!GetOverlapSegment(pt1, pt2, pt3, pt4, pt1, pt2)) return false;

  OutPt *p3, *p4, *prev = pp1a->prev;
  //get p1 & p2 polypts - the overlap start & endpoints on poly1
  if (PointsEqual(pp1a->pt, pt1)) p1 = pp1a;
  else if (PointsEqual(prev->pt, pt1)) p1 = prev;
  else p1 = InsertPolyPtBetween(pp1a, prev, pt1);

  if (PointsEqual(pp1a->pt, pt2)) p2 = pp1a;
  else if (PointsEqual(prev->pt, pt2)) p2 = prev;
  else if ((p1 == pp1a) || (p1 == prev))
    p2 = InsertPolyPtBetween(pp1a, prev, pt2);
  else if (Pt3IsBetweenPt1AndPt2(pp1a->pt, p1->pt, pt2))
    p2 = InsertPolyPtBetween(pp1a, p1, pt2); else
    p2 = InsertPolyPtBetween(p1, prev, pt2);

  //get p3 & p4 polypts - the overlap start & endpoints on poly2
  prev = pp2a->prev;
  if (PointsEqual(pp2a->pt, pt1)) p3 = pp2a;
  else if (PointsEqual(prev->pt, pt1)) p3 = prev;
  else p3 = InsertPolyPtBetween(pp2a, prev, pt1);

  if (PointsEqual(pp2a->pt, pt2)) p4 = pp2a;
  else if (PointsEqual(prev->pt, pt2)) p4 = prev;
  else if ((p3 == pp2a) || (p3 == prev))
    p4 = InsertPolyPtBetween(pp2a, prev, pt2);
  else if (Pt3IsBetweenPt1AndPt2(pp2a->pt, p3->pt, pt2))
    p4 = InsertPolyPtBetween(pp2a, p3, pt2); else
    p4 = InsertPolyPtBetween(p3, prev, pt2);

  //p1.pt == p3.pt and p2.pt == p4.pt so join p1 to p3 and p2 to p4 ...
  if (p1->next == p2 && p3->prev == p4)
  {
    p1->next = p3;
    p3->prev = p1;
    p2->prev = p4;
    p4->next = p2;
    return true;
  }
  else if (p1->prev == p2 && p3->next == p4)
  {
    p1->prev = p3;
    p3->next = p1;
    p2->next = p4;
    p4->prev = p2;
    return true;
  }
  else
    return false; //an orientation is probably wrong
}
//----------------------------------------------------------------------

void Clipper::FixupJoinRecs(JoinRec *j, OutPt *pt, unsigned startIdx)
{
  for (JoinList::size_type k = startIdx; k < m_Joins.size(); k++)
    {
      JoinRec* j2 = m_Joins[k];
      if (j2->poly1Idx == j->poly1Idx && PointIsVertex(j2->pt1a, pt))
        j2->poly1Idx = j->poly2Idx;
      if (j2->poly2Idx == j->poly1Idx && PointIsVertex(j2->pt2a, pt))
        j2->poly2Idx = j->poly2Idx;
    }
}
//----------------------------------------------------------------------

bool Poly2ContainsPoly1(OutPt* outPt1, OutPt* outPt2, bool UseFullInt64Range)
{
  OutPt* pt = outPt1;
  //Because the polygons may be touching, we need to find a vertex that
  //isn't touching the other polygon ...
  if (PointOnPolygon(pt->pt, outPt2, UseFullInt64Range))
  {
    pt = pt->next;
    while (pt != outPt1 && PointOnPolygon(pt->pt, outPt2, UseFullInt64Range))
        pt = pt->next;
    if (pt == outPt1) return true;
  }
  return PointInPolygon(pt->pt, outPt2, UseFullInt64Range);
}
//----------------------------------------------------------------------

void Clipper::FixupFirstLefts1(OutRec* OldOutRec, OutRec* NewOutRec)
{ 
  
  for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); ++i)
  {
    OutRec* outRec = m_PolyOuts[i];
    if (outRec->pts && outRec->FirstLeft == OldOutRec) 
    {
      if (Poly2ContainsPoly1(outRec->pts, NewOutRec->pts, m_UseFullRange))
        outRec->FirstLeft = NewOutRec;
    }
  }
}
//----------------------------------------------------------------------

void Clipper::FixupFirstLefts2(OutRec* OldOutRec, OutRec* NewOutRec)
{ 
  for (PolyOutList::size_type i = 0; i < m_PolyOuts.size(); ++i)
  {
    OutRec* outRec = m_PolyOuts[i];
    if (outRec->FirstLeft == OldOutRec) outRec->FirstLeft = NewOutRec;
  }
}
//----------------------------------------------------------------------

void Clipper::JoinCommonEdges()
{
  for (JoinList::size_type i = 0; i < m_Joins.size(); i++)
  {
    JoinRec* j = m_Joins[i];

    OutRec *outRec1 = GetOutRec(j->poly1Idx);
    OutRec *outRec2 = GetOutRec(j->poly2Idx);

    if (!outRec1->pts || !outRec2->pts) continue;

    //get the polygon fragment with the correct hole state (FirstLeft)
    //before calling JoinPoints() ...
    OutRec *holeStateRec;
    if (outRec1 == outRec2) holeStateRec = outRec1;
    else if (Param1RightOfParam2(outRec1, outRec2)) holeStateRec = outRec2;
    else if (Param1RightOfParam2(outRec2, outRec1)) holeStateRec = outRec1;
    else holeStateRec = GetLowermostRec(outRec1, outRec2);

    OutPt *p1, *p2;
    if (!JoinPoints(j, p1, p2)) continue;

    if (outRec1 == outRec2)
    {
      //instead of joining two polygons, we've just created a new one by
      //splitting one polygon into two.
      outRec1->pts = p1;
      outRec1->bottomPt = 0;
      outRec2 = CreateOutRec();
      outRec2->pts = p2;

      if (Poly2ContainsPoly1(outRec2->pts, outRec1->pts, m_UseFullRange))
      {
        //outRec2 is contained by outRec1 ...
        outRec2->isHole = !outRec1->isHole;
        outRec2->FirstLeft = outRec1;

        FixupJoinRecs(j, p2, i+1);

        //fixup FirstLeft pointers that may need reassigning to OutRec1
        if (m_UsingPolyTree) FixupFirstLefts2(outRec2, outRec1);

        FixupOutPolygon(*outRec1); //nb: do this BEFORE testing orientation
        FixupOutPolygon(*outRec2); //    but AFTER calling FixupJoinRecs()


        if ((outRec2->isHole ^ m_ReverseOutput) == (Area(*outRec2, m_UseFullRange) > 0))
          ReversePolyPtLinks(outRec2->pts);
            
      } else if (Poly2ContainsPoly1(outRec1->pts, outRec2->pts, m_UseFullRange))
      {
        //outRec1 is contained by outRec2 ...
        outRec2->isHole = outRec1->isHole;
        outRec1->isHole = !outRec2->isHole;
        outRec2->FirstLeft = outRec1->FirstLeft;
        outRec1->FirstLeft = outRec2;

        FixupJoinRecs(j, p2, i+1);

        //fixup FirstLeft pointers that may need reassigning to OutRec1
        if (m_UsingPolyTree) FixupFirstLefts2(outRec1, outRec2);

        FixupOutPolygon(*outRec1); //nb: do this BEFORE testing orientation
        FixupOutPolygon(*outRec2); //    but AFTER calling FixupJoinRecs()

        if ((outRec1->isHole ^ m_ReverseOutput) == (Area(*outRec1, m_UseFullRange) > 0))
          ReversePolyPtLinks(outRec1->pts);
      } 
      else
      {
        //the 2 polygons are completely separate ...
        outRec2->isHole = outRec1->isHole;
        outRec2->FirstLeft = outRec1->FirstLeft;

        FixupJoinRecs(j, p2, i+1);

        //fixup FirstLeft pointers that may need reassigning to OutRec2
        if (m_UsingPolyTree) FixupFirstLefts1(outRec1, outRec2);

        FixupOutPolygon(*outRec1); //nb: do this BEFORE testing orientation
        FixupOutPolygon(*outRec2); //    but AFTER calling FixupJoinRecs()
      }
     
    } else
    {
      //joined 2 polygons together ...

      //cleanup redundant edges ...
      FixupOutPolygon(*outRec1);

      outRec2->pts = 0;
      outRec2->bottomPt = 0;
      outRec2->idx = outRec1->idx;

      outRec1->isHole = holeStateRec->isHole;
      if (holeStateRec == outRec2) 
        outRec1->FirstLeft = outRec2->FirstLeft;
      outRec2->FirstLeft = outRec1;

      //fixup FirstLeft pointers that may need reassigning to OutRec1
      if (m_UsingPolyTree) FixupFirstLefts2(outRec2, outRec1);
    }
  }
}
//------------------------------------------------------------------------------

inline void UpdateOutPtIdxs(OutRec& outrec)
{  
  OutPt* op = outrec.pts;
  do
  {
    op->idx = outrec.idx;
    op = op->prev;
  }
  while(op != outrec.pts);
}
//------------------------------------------------------------------------------

void Clipper::DoSimplePolygons()
{
  PolyOutList::size_type i = 0;
  while (i < m_PolyOuts.size()) 
  {
    OutRec* outrec = m_PolyOuts[i++];
    OutPt* op = outrec->pts;
    if (!op) continue;
    do //for each Pt in Polygon until duplicate found do ...
    {
      OutPt* op2 = op->next;
      while (op2 != outrec->pts) 
      {
        if (PointsEqual(op->pt, op2->pt) && op2->next != op && op2->prev != op) 
        {
          //split the polygon into two ...
          OutPt* op3 = op->prev;
          OutPt* op4 = op2->prev;
          op->prev = op4;
          op4->next = op;
          op2->prev = op3;
          op3->next = op2;

          outrec->pts = op;
          OutRec* outrec2 = CreateOutRec();
          outrec2->pts = op2;
          UpdateOutPtIdxs(*outrec2);
          if (Poly2ContainsPoly1(outrec2->pts, outrec->pts, m_UseFullRange))
          {
            //OutRec2 is contained by OutRec1 ...
            outrec2->isHole = !outrec->isHole;
            outrec2->FirstLeft = outrec;
          }
          else
            if (Poly2ContainsPoly1(outrec->pts, outrec2->pts, m_UseFullRange))
          {
            //OutRec1 is contained by OutRec2 ...
            outrec2->isHole = outrec->isHole;
            outrec->isHole = !outrec2->isHole;
            outrec2->FirstLeft = outrec->FirstLeft;
            outrec->FirstLeft = outrec2;
          } else
          {
            //the 2 polygons are separate ...
            outrec2->isHole = outrec->isHole;
            outrec2->FirstLeft = outrec->FirstLeft;
          }
          op2 = op; //ie get ready for the next iteration
        }
        op2 = op2->next;
      }
      op = op->next;
    }
    while (op != outrec->pts);
  }
}
//------------------------------------------------------------------------------

void ReversePolygon(Polygon& p)
{
  std::reverse(p.begin(), p.end());
}
//------------------------------------------------------------------------------

void ReversePolygons(Polygons& p)
{
  for (Polygons::size_type i = 0; i < p.size(); ++i)
    ReversePolygon(p[i]);
}

//------------------------------------------------------------------------------
// OffsetPolygon functions ...
//------------------------------------------------------------------------------

struct DoublePoint
{
  double X;
  double Y;
  DoublePoint(double x = 0, double y = 0) : X(x), Y(y) {}
};
//------------------------------------------------------------------------------

Polygon BuildArc(const IntPoint &pt,
  const double a1, const double a2, const double r, double limit)
{
  //see notes in clipper.pas regarding steps
  double arcFrac = std::fabs(a2 - a1) / (2 * pi);
  int steps = (int)(arcFrac * pi / std::acos(1 - limit / std::fabs(r)));
  if (steps < 2) steps = 2;
  else if (steps > (int)(222.0 * arcFrac)) steps = (int)(222.0 * arcFrac);

  double x = std::cos(a1); 
  double y = std::sin(a1);
  double c = std::cos((a2 - a1) / steps);
  double s = std::sin((a2 - a1) / steps);
  Polygon result(steps +1);
  for (int i = 0; i <= steps; ++i)
  {
      result[i].X = pt.X + Round(x * r);
      result[i].Y = pt.Y + Round(y * r);
      double x2 = x;
      x = x * c - s * y;  //cross product
      y = x2 * s + y * c; //dot product
  }
  return result;
}
//------------------------------------------------------------------------------

DoublePoint GetUnitNormal(const IntPoint &pt1, const IntPoint &pt2)
{
  if(pt2.X == pt1.X && pt2.Y == pt1.Y) 
    return DoublePoint(0, 0);

  double dx = (double)(pt2.X - pt1.X);
  double dy = (double)(pt2.Y - pt1.Y);
  double f = 1 *1.0/ std::sqrt( dx*dx + dy*dy );
  dx *= f;
  dy *= f;
  return DoublePoint(dy, -dx);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

class OffsetBuilder
{
private:
  const Polygons& m_p;
  Polygon* m_curr_poly;
  std::vector<DoublePoint> normals;
  double m_delta, m_rmin, m_r;
  size_t m_i, m_j, m_k;
  static const int buffLength = 128;
 
public:

OffsetBuilder(const Polygons& in_polys, Polygons& out_polys,
  bool isPolygon, double delta, JoinType jointype, EndType endtype, double limit): m_p(in_polys)
{
    //precondition: &out_polys != &in_polys

    if (NEAR_ZERO(delta)) {out_polys = in_polys; return;}
    m_rmin = 0.5;
    m_delta = delta;
    if (jointype == jtMiter)
    {
      if (limit > 2) m_rmin = 2.0 / (limit * limit);
      limit = 0.25; //just in case endtype == etRound
    }
    else
    {
      if (limit <= 0) limit = 0.25;
      else if (limit > std::fabs(delta)) limit = std::fabs(delta);
    }
 
    double deltaSq = delta*delta;
    out_polys.clear();
    out_polys.resize(m_p.size());
    for (m_i = 0; m_i < m_p.size(); m_i++)
    {
        size_t len = m_p[m_i].size();

        if (len == 0 || (len < 3 && delta <= 0))
            continue;
        else if (len == 1)
        {
            out_polys[m_i] = BuildArc(m_p[m_i][0], 0, 2*pi, delta, limit);
            continue;
        }

        bool forceClose = PointsEqual(m_p[m_i][0], m_p[m_i][len -1]);
        if (forceClose) len--;

        //build normals ...
        normals.clear();
        normals.resize(len);
        for (m_j = 0; m_j < len -1; ++m_j)
            normals[m_j] = GetUnitNormal(m_p[m_i][m_j], m_p[m_i][m_j +1]);
        if (isPolygon || forceClose) 
          normals[len-1] = GetUnitNormal(m_p[m_i][len-1], m_p[m_i][0]);
        else //is open polyline
          normals[len-1] = normals[len-2];
        
        m_curr_poly = &out_polys[m_i];
        m_curr_poly->reserve(len);

        if (isPolygon || forceClose) 
        {
          m_k = len -1;
          for (m_j = 0; m_j < len; ++m_j)
            OffsetPoint(jointype, limit);

          if (!isPolygon)
          {
            size_t j = out_polys.size();
            out_polys.resize(j+1);
            m_curr_poly = &out_polys[j];
            m_curr_poly->reserve(len);
            m_delta = -m_delta;

            m_k = len -1;
            for (m_j = 0; m_j < len; ++m_j)
              OffsetPoint(jointype, limit);
            m_delta = -m_delta;
            ReversePolygon(*m_curr_poly);
          }
        }
        else //is open polyline
        {
          //offset the polyline going forward ...
          m_k = 0;
          for (m_j = 1; m_j < len -1; ++m_j)
            OffsetPoint(jointype, limit);

          //handle the end (butt, round or square) ...
          IntPoint pt1;
          if (endtype == etButt)
          {
            m_j = len - 1;
            pt1 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_j].X * m_delta), 
              Round(m_p[m_i][m_j].Y + normals[m_j].Y * m_delta));
            AddPoint(pt1);
            pt1 = IntPoint(Round(m_p[m_i][m_j].X - normals[m_j].X * m_delta), 
              Round(m_p[m_i][m_j].Y - normals[m_j].Y * m_delta));
            AddPoint(pt1);
          } 
          else
          {
            m_j = len - 1;
            m_k = len - 2;
            normals[m_j].X = -normals[m_j].X;
            normals[m_j].Y = -normals[m_j].Y;
            if (endtype == etSquare) DoSquare();
            else DoRound(limit);
          }

          //re-build Normals ...
          for (int j = len - 1; j > 0; --j)
          {
              normals[j].X = -normals[j - 1].X;
              normals[j].Y = -normals[j - 1].Y;
          }
          normals[0].X = -normals[1].X;
          normals[0].Y = -normals[1].Y;

          //offset the polyline going backward ...
          m_k = len -1;
          for (m_j = m_k - 1; m_j > 0; --m_j)
            OffsetPoint(jointype, limit);

          //finally handle the start (butt, round or square) ...
          if (endtype == etButt) 
          {
            pt1 = IntPoint(Round(m_p[m_i][0].X - normals[0].X * m_delta), 
              Round(m_p[m_i][0].Y - normals[0].Y * m_delta));
            AddPoint(pt1);
            pt1 = IntPoint(Round(m_p[m_i][0].X + normals[0].X * m_delta), 
              Round(m_p[m_i][0].Y + normals[0].Y * m_delta));
            AddPoint(pt1);
          } else
          {
            m_k = 1;
            if (endtype == etSquare) DoSquare(); 
            else DoRound(limit);
          }
        }
    }

    //and clean up untidy corners using Clipper ...
    Clipper clpr;
    clpr.AddPolygons(out_polys, ptSubject);
    if (delta > 0)
    {
        if (!clpr.Execute(ctUnion, out_polys, pftPositive, pftPositive))
            out_polys.clear();
    }
    else
    {
        IntRect r = clpr.GetBounds();
        Polygon outer(4);
        outer[0] = IntPoint(r.left - 10, r.bottom + 10);
        outer[1] = IntPoint(r.right + 10, r.bottom + 10);
        outer[2] = IntPoint(r.right + 10, r.top - 10);
        outer[3] = IntPoint(r.left - 10, r.top - 10);

        clpr.AddPolygon(outer, ptSubject);
        clpr.ReverseSolution(true);
        if (clpr.Execute(ctUnion, out_polys, pftNegative, pftNegative))
            out_polys.erase(out_polys.begin());
        else
            out_polys.clear();
    }
}
//------------------------------------------------------------------------------

private:

void OffsetPoint(JoinType jointype, double limit)
{
    switch (jointype)
    {
      case jtMiter:
      {
        m_r = 1 + (normals[m_j].X*normals[m_k].X + 
          normals[m_j].Y*normals[m_k].Y);
        if (m_r >= m_rmin) DoMiter(); else DoSquare();
        break;
      }
      case jtSquare: DoSquare(); break;
      case jtRound: DoRound(limit); break;
    }
    m_k = m_j;
}
//------------------------------------------------------------------------------

void AddPoint(const IntPoint& pt)
{
    if (m_curr_poly->size() == m_curr_poly->capacity())
        m_curr_poly->reserve(m_curr_poly->capacity() + buffLength);
    m_curr_poly->push_back(pt);
}
//------------------------------------------------------------------------------

void DoSquare()
{
    IntPoint pt1 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_k].X * m_delta),
        Round(m_p[m_i][m_j].Y + normals[m_k].Y * m_delta));
    IntPoint pt2 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_j].X * m_delta),
        Round(m_p[m_i][m_j].Y + normals[m_j].Y * m_delta));
    if ((normals[m_k].X * normals[m_j].Y - normals[m_j].X * normals[m_k].Y) * m_delta >= 0)
    {
      double a1 = std::atan2(normals[m_k].Y, normals[m_k].X);
      double a2 = std::atan2(-normals[m_j].Y, -normals[m_j].X);
      a1 = std::fabs(a2 - a1);
      if (a1 > pi) a1 = pi * 2 - a1;
      double dx = std::tan((pi - a1) / 4) * std::fabs(m_delta);
      pt1 = IntPoint((long64)(pt1.X -normals[m_k].Y * dx), (long64)(pt1.Y + normals[m_k].X * dx));
      AddPoint(pt1);
      pt2 = IntPoint((long64)(pt2.X + normals[m_j].Y * dx), (long64)(pt2.Y -normals[m_j].X * dx));
      AddPoint(pt2);
    }
    else
    {
      AddPoint(pt1);
      AddPoint(m_p[m_i][m_j]);
      AddPoint(pt2);
    }
}
//------------------------------------------------------------------------------

void DoMiter()
{
    if ((normals[m_k].X * normals[m_j].Y - normals[m_j].X * normals[m_k].Y) * m_delta >= 0)
    {
        double q = m_delta / m_r;
        AddPoint(IntPoint(Round(m_p[m_i][m_j].X + (normals[m_k].X + normals[m_j].X) * q),
            Round(m_p[m_i][m_j].Y + (normals[m_k].Y + normals[m_j].Y) * q)));
    }
    else
    {
        IntPoint pt1 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_k].X * m_delta), 
          Round(m_p[m_i][m_j].Y + normals[m_k].Y * m_delta));
        IntPoint pt2 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_j].X * m_delta), 
          Round(m_p[m_i][m_j].Y + normals[m_j].Y * m_delta));
        AddPoint(pt1);
        AddPoint(m_p[m_i][m_j]);
        AddPoint(pt2);
    }
}
//------------------------------------------------------------------------------

void DoRound(double limit)
{
    IntPoint pt1 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_k].X * m_delta),
        Round(m_p[m_i][m_j].Y + normals[m_k].Y * m_delta));
    IntPoint pt2 = IntPoint(Round(m_p[m_i][m_j].X + normals[m_j].X * m_delta),
        Round(m_p[m_i][m_j].Y + normals[m_j].Y * m_delta));
    AddPoint(pt1);
    //round off reflex angles (ie > 180 deg) unless almost flat (ie < ~10deg).
    if ((normals[m_k].X*normals[m_j].Y - normals[m_j].X*normals[m_k].Y) * m_delta >= 0)
    {
      if (normals[m_j].X * normals[m_k].X + normals[m_j].Y * normals[m_k].Y < 0.985)
      {
        double a1 = std::atan2(normals[m_k].Y, normals[m_k].X);
        double a2 = std::atan2(normals[m_j].Y, normals[m_j].X);
        if (m_delta > 0 && a2 < a1) a2 += pi *2;
        else if (m_delta < 0 && a2 > a1) a2 -= pi *2;
        Polygon arc = BuildArc(m_p[m_i][m_j], a1, a2, m_delta, limit);
        for (Polygon::size_type m = 0; m < arc.size(); m++)
          AddPoint(arc[m]);
      }
    }
    else
      AddPoint(m_p[m_i][m_j]);
    AddPoint(pt2);
}
//--------------------------------------------------------------------------

}; //end PolyOffsetBuilder

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

bool UpdateBotPt(const IntPoint &pt, IntPoint &botPt)
{
    if (pt.Y > botPt.Y || (pt.Y == botPt.Y && pt.X < botPt.X))
    {
        botPt = pt;
        return true;
    }
    else return false;
}
//--------------------------------------------------------------------------

void OffsetPolygons(const Polygons &in_polys, Polygons &out_polys,
  double delta, JoinType jointype, double limit, bool autoFix)
{
  if (!autoFix && &in_polys != &out_polys)
  {
    OffsetBuilder(in_polys, out_polys, true, delta, jointype, etClosed, limit);
    return;
  }

  Polygons inPolys = Polygons(in_polys);
  out_polys.clear();

  //ChecksInput - fixes polygon orientation if necessary and removes 
  //duplicate vertices. Can be set false when you're sure that polygon
  //orientation is correct and that there are no duplicate vertices.
  if (autoFix) 
  {
    size_t polyCount = inPolys.size(), botPoly = 0;
    while (botPoly < polyCount && inPolys[botPoly].empty()) botPoly++;
    if (botPoly == polyCount) return;
      
    //botPt: used to find the lowermost (in inverted Y-axis) & leftmost point
    //This point (on m_p[botPoly]) must be on an outer polygon ring and if 
    //its orientation is false (counterclockwise) then assume all polygons 
    //need reversing ...
    IntPoint botPt = inPolys[botPoly][0];      
    for (size_t i = botPoly; i < polyCount; ++i)
    {
      if (inPolys[i].size() < 3) { inPolys[i].clear(); continue; }
      if (UpdateBotPt(inPolys[i][0], botPt)) botPoly = i;
      Polygon::iterator it = inPolys[i].begin() +1;
      while (it != inPolys[i].end())
      {
        if (PointsEqual(*it, *(it -1)))
          it = inPolys[i].erase(it);
        else 
        {
          if (UpdateBotPt(*it, botPt)) botPoly = i;
          ++it;
        }
      }
    }
    if (!Orientation(inPolys[botPoly]))
      ReversePolygons(inPolys);
  }
  OffsetBuilder(inPolys, out_polys, true, delta, jointype, etClosed, limit);
}
//------------------------------------------------------------------------------

void OffsetPolyLines(const Polygons &in_lines, Polygons &out_lines,
  double delta, JoinType jointype, EndType endtype, 
  double limit, bool autoFix)
{
  if (!autoFix && endtype != etClosed && &in_lines != &out_lines)
  {
    OffsetBuilder(in_lines, out_lines, false, delta, jointype, endtype, limit);
    return;
  }

  Polygons inLines = Polygons(in_lines);
  if (autoFix) 
    for (size_t i = 0; i < inLines.size(); ++i)
    {
      if (inLines[i].size() < 2) { inLines[i].clear(); continue; }
      Polygon::iterator it = inLines[i].begin() +1;
      while (it != inLines[i].end())
      {
        if (PointsEqual(*it, *(it -1)))
          it = inLines[i].erase(it);
        else
          ++it;
      }
    }

  if (endtype == etClosed)
  {
    size_t sz = inLines.size();
    inLines.resize(sz * 2);
    for (size_t i = 0; i < sz; ++i)
    {
      inLines[sz+i] = inLines[i];
      ReversePolygon(inLines[sz+i]);
    }
    OffsetBuilder(inLines, out_lines, true, delta, jointype, endtype, limit);
  } 
  else
    OffsetBuilder(inLines, out_lines, false, delta, jointype, endtype, limit);
}
//------------------------------------------------------------------------------

void SimplifyPolygon(const Polygon &in_poly, Polygons &out_polys, PolyFillType fillType)
{
  Clipper c;
  c.ForceSimple(true);
  c.AddPolygon(in_poly, ptSubject);
  c.Execute(ctUnion, out_polys, fillType, fillType);
}
//------------------------------------------------------------------------------

void SimplifyPolygons(const Polygons &in_polys, Polygons &out_polys, PolyFillType fillType)
{
  Clipper c;
  c.ForceSimple(true);
  c.AddPolygons(in_polys, ptSubject);
  c.Execute(ctUnion, out_polys, fillType, fillType);
}
//------------------------------------------------------------------------------

void SimplifyPolygons(Polygons &polys, PolyFillType fillType)
{
  SimplifyPolygons(polys, polys, fillType);
}
//------------------------------------------------------------------------------

inline double DistanceSqrd(const IntPoint& pt1, const IntPoint& pt2)
{
  double dx = ((double)pt1.X - pt2.X);
  double dy = ((double)pt1.Y - pt2.Y);
  return (dx*dx + dy*dy);
}
//------------------------------------------------------------------------------

DoublePoint ClosestPointOnLine(const IntPoint& pt, const IntPoint& linePt1, const IntPoint& linePt2)
{
  double dx = ((double)linePt2.X - linePt1.X);
  double dy = ((double)linePt2.Y - linePt1.Y);
  if (dx == 0 && dy == 0) 
    return DoublePoint((double)linePt1.X, (double)linePt1.Y);
  double q = ((pt.X-linePt1.X)*dx + (pt.Y-linePt1.Y)*dy) / (dx*dx + dy*dy);
  return DoublePoint(
    (1-q)*linePt1.X + q*linePt2.X,
    (1-q)*linePt1.Y + q*linePt2.Y);
}
//------------------------------------------------------------------------------

bool SlopesNearColinear(const IntPoint& pt1, 
    const IntPoint& pt2, const IntPoint& pt3, double distSqrd)
{
  if (DistanceSqrd(pt1, pt2) > DistanceSqrd(pt1, pt3)) return false;
  DoublePoint cpol = ClosestPointOnLine(pt2, pt1, pt3);
  double dx = pt2.X - cpol.X;
  double dy = pt2.Y - cpol.Y;
  return (dx*dx + dy*dy) < distSqrd;
}
//------------------------------------------------------------------------------

bool PointsAreClose(IntPoint pt1, IntPoint pt2, double distSqrd)
{
    double dx = (double)pt1.X - pt2.X;
    double dy = (double)pt1.Y - pt2.Y;
    return ((dx * dx) + (dy * dy) <= distSqrd);
}
//------------------------------------------------------------------------------

void CleanPolygon(const Polygon& in_poly, Polygon& out_poly, double distance)
{
  //distance = proximity in units/pixels below which vertices
  //will be stripped. Default ~= sqrt(2).
  int highI = in_poly.size() -1;
  double distSqrd = distance * distance;
  while (highI > 0 && PointsAreClose(in_poly[highI], in_poly[0], distSqrd)) highI--;
  if (highI < 2) { out_poly.clear(); return; }
  
  if (&in_poly != &out_poly) 
    out_poly.resize(highI + 1);

  IntPoint pt = in_poly[highI];
  int i = 0, k = 0;
  for (;;)
  {
    while (i < highI && PointsAreClose(pt, in_poly[i+1], distSqrd)) i+=2;
    int i2 = i;
    while (i < highI && (PointsAreClose(in_poly[i], in_poly[i+1], distSqrd) ||
      SlopesNearColinear(pt, in_poly[i], in_poly[i+1], distSqrd))) i++;
    if (i >= highI) break;
    else if (i != i2) continue;
    pt = in_poly[i++];
    out_poly[k++] = pt;
  }
  if (i <= highI) out_poly[k++] = in_poly[i];
  if (k > 2 && SlopesNearColinear(out_poly[k -2], out_poly[k -1], out_poly[0], distSqrd)) k--;    
  if (k < 3) out_poly.clear();
  else if (k <= highI) out_poly.resize(k);
}
//------------------------------------------------------------------------------

void CleanPolygons(const Polygons& in_polys, Polygons& out_polys, double distance)
{
  for (Polygons::size_type i = 0; i < in_polys.size(); ++i)
    CleanPolygon(in_polys[i], out_polys[i], distance);
}
//------------------------------------------------------------------------------

void AddPolyNodeToPolygons(const PolyNode& polynode, Polygons& polygons)
{
  if (!polynode.Contour.empty())
    polygons.push_back(polynode.Contour);
  for (int i = 0; i < polynode.ChildCount(); ++i)
    AddPolyNodeToPolygons(*polynode.Childs[i], polygons);
}
//------------------------------------------------------------------------------

void PolyTreeToPolygons(const PolyTree& polytree, Polygons& polygons)
{
  polygons.resize(0); 
  polygons.reserve(polytree.Total());
  AddPolyNodeToPolygons(polytree, polygons);
}
//------------------------------------------------------------------------------

std::ostream& operator <<(std::ostream &s, IntPoint& p)
{
  s << p.X << ' ' << p.Y << "\n";
  return s;
}
//------------------------------------------------------------------------------

std::ostream& operator <<(std::ostream &s, Polygon &p)
{
  for (Polygon::size_type i = 0; i < p.size(); i++)
    s << p[i];
  s << "\n";
  return s;
}
//------------------------------------------------------------------------------

std::ostream& operator <<(std::ostream &s, Polygons &p)
{
  for (Polygons::size_type i = 0; i < p.size(); i++)
    s << p[i];
  s << "\n";
  return s;
}
//------------------------------------------------------------------------------

} //ClipperLib namespace
