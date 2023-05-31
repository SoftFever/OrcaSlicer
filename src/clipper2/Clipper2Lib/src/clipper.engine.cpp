/*******************************************************************************
* Author    :  Angus Johnson                                                   *
* Date      :  4 November 2022                                                 *
* Website   :  http://www.angusj.com                                           *
* Copyright :  Angus Johnson 2010-2022                                         *
* Purpose   :  This is the main polygon clipping module                        *
* License   :  http://www.boost.org/LICENSE_1_0.txt                            *
*******************************************************************************/

#include <stdlib.h>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <numeric>
#include <algorithm>
#include "clipper2/clipper.engine.h"

#include <cstddef>
#include <cstdint>

namespace Clipper2Lib {

	static const double FloatingPointTolerance = 1.0e-12;
	static const Rect64 invalid_rect = Rect64(
		std::numeric_limits<int64_t>::max(),
		std::numeric_limits<int64_t>::max(),
		-std::numeric_limits<int64_t>::max(),
		-std::numeric_limits<int64_t>::max()
	);

	//Every closed path (or polygon) is made up of a series of vertices forming
	//edges that alternate between going up (relative to the Y-axis) and going
	//down. Edges consecutively going up or consecutively going down are called
	//'bounds' (or sides if they're simple polygons). 'Local Minima' refer to
	//vertices where descending bounds become ascending ones.

	struct Scanline {
		int64_t y = 0;
		Scanline* next = nullptr;

		explicit Scanline(int64_t y_) : y(y_) {}
	};

	struct  Joiner {
		int			idx;
		OutPt* op1;
		OutPt* op2;
		Joiner* next1;
		Joiner* next2;
		Joiner* nextH;

		explicit Joiner(OutPt* op1_, OutPt* op2_, Joiner* nexth) :
			op1(op1_), op2(op2_), nextH(nexth)
		{
			idx = -1;
			next1 = op1->joiner;
			op1->joiner = this;

			if (op2)
			{
				next2 = op2->joiner;
				op2->joiner = this;
			}
			else
				next2 = nullptr;
		}

	};

	struct LocMinSorter {
		inline bool operator()(const LocalMinima* locMin1, const LocalMinima* locMin2)
		{
			if (locMin2->vertex->pt.y != locMin1->vertex->pt.y)
				return locMin2->vertex->pt.y < locMin1->vertex->pt.y;
			else
				return locMin2->vertex->pt.x < locMin1->vertex->pt.x;
		}
	};

	inline bool IsOdd(int val)
	{
		return (val & 1) ? true : false;
	}


	inline bool IsHotEdge(const Active& e)
	{
		return (e.outrec);
	}


	inline bool IsOpen(const Active& e)
	{
		return (e.local_min->is_open);
	}


	inline bool IsOpenEnd(const Vertex& v)
	{
		return (v.flags & (VertexFlags::OpenStart | VertexFlags::OpenEnd)) !=
			VertexFlags::None;
	}


	inline bool IsOpenEnd(const Active& ae)
	{
		return IsOpenEnd(*ae.vertex_top);
	}


	inline Active* GetPrevHotEdge(const Active& e)
	{
		Active* prev = e.prev_in_ael;
		while (prev && (IsOpen(*prev) || !IsHotEdge(*prev)))
			prev = prev->prev_in_ael;
		return prev;
	}

	inline bool IsFront(const Active& e)
	{
		return (&e == e.outrec->front_edge);
	}

	inline bool IsInvalidPath(OutPt* op)
	{
		return (!op || op->next == op);
	}

	/*******************************************************************************
		*  Dx:                             0(90deg)                                    *
		*                                  |                                           *
		*               +inf (180deg) <--- o ---> -inf (0deg)                          *
		*******************************************************************************/

	inline double GetDx(const Point64& pt1, const Point64& pt2)
	{
		double dy = double(pt2.y - pt1.y);
		if (dy != 0)
			return double(pt2.x - pt1.x) / dy;
		else if (pt2.x > pt1.x)
			return -std::numeric_limits<double>::max();
		else
			return std::numeric_limits<double>::max();
	}


	inline int64_t TopX(const Active& ae, const int64_t currentY)
	{
		if ((currentY == ae.top.y) || (ae.top.x == ae.bot.x)) return ae.top.x;
		else if (currentY == ae.bot.y) return ae.bot.x;
		else return ae.bot.x + static_cast<int64_t>(std::nearbyint(ae.dx * (currentY - ae.bot.y)));
		// nb: std::nearbyint (or std::round) substantially *improves* performance here
		// as it greatly improves the likelihood of edge adjacency in ProcessIntersectList().
	}


	inline bool IsHorizontal(const Active& e)
	{
		return (e.top.y == e.bot.y);
	}


	inline bool IsHeadingRightHorz(const Active& e)
	{
		return e.dx == -std::numeric_limits<double>::max();
	}


	inline bool IsHeadingLeftHorz(const Active& e)
	{
		return e.dx == std::numeric_limits<double>::max();
	}


	inline void SwapActives(Active*& e1, Active*& e2)
	{
		Active* e = e1;
		e1 = e2;
		e2 = e;
	}


	inline PathType GetPolyType(const Active& e)
	{
		return e.local_min->polytype;
	}


	inline bool IsSamePolyType(const Active& e1, const Active& e2)
	{
		return e1.local_min->polytype == e2.local_min->polytype;
	}

	inline Point64 GetEndE1ClosestToEndE2(
		const Active& e1, const Active& e2)
	{
		double d[] = {
			DistanceSqr(e1.bot, e2.bot),
			DistanceSqr(e1.top, e2.top),
			DistanceSqr(e1.top, e2.bot),
			DistanceSqr(e1.bot, e2.top)
		};
		if (d[0] == 0) return e1.bot;
		int idx = 0;
		for (int i = 1; i < 4; ++i)
		{
			if (d[i] < d[idx]) idx = i;
			if (d[i] == 0) break;
		}
		switch (idx)
		{
		case 1: case 2: return e1.top;
		default: return e1.bot;
		}
	}

	Point64 GetIntersectPoint(const Active& e1, const Active& e2)
	{
		double b1, b2, q = (e1.dx - e2.dx);
		if (std::abs(q) < 1e-5)			// 1e-5 is a rough empirical limit
			return GetEndE1ClosestToEndE2(e1, e2); // ie almost parallel

		if (e1.dx == 0)
		{
			if (IsHorizontal(e2)) return Point64(e1.bot.x, e2.bot.y);
			b2 = e2.bot.y - (e2.bot.x / e2.dx);
			return Point64(e1.bot.x,
				static_cast<int64_t>(std::round(e1.bot.x / e2.dx + b2)));
		}
		else if (e2.dx == 0)
		{
			if (IsHorizontal(e1)) return Point64(e2.bot.x, e1.bot.y);
			b1 = e1.bot.y - (e1.bot.x / e1.dx);
			return Point64(e2.bot.x,
				static_cast<int64_t>(std::round(e2.bot.x / e1.dx + b1)));
		}
		else
		{
			b1 = e1.bot.x - e1.bot.y * e1.dx;
			b2 = e2.bot.x - e2.bot.y * e2.dx;

			q = (b2 - b1) / q;
			return (abs(e1.dx) < abs(e2.dx)) ?
				Point64(static_cast<int64_t>(e1.dx * q + b1),
					static_cast<int64_t>((q))) :
				Point64(static_cast<int64_t>(e2.dx * q + b2),
					static_cast<int64_t>((q)));
		}
	}

	bool GetIntersectPoint(const Point64& ln1a, const Point64& ln1b,
		const Point64& ln2a, const Point64& ln2b, PointD& ip)
	{
		ip = PointD(0, 0);
		double m1, b1, m2, b2;
		if (ln1b.x == ln1a.x)
		{
			if (ln2b.x == ln2a.x) return false;
			m2 = static_cast<double>(ln2b.y - ln2a.y) /
				static_cast<double>(ln2b.x - ln2a.x);
			b2 = ln2a.y - m2 * ln2a.x;
			ip.x = static_cast<double>(ln1a.x);
			ip.y = m2 * ln1a.x + b2;
		}
		else if (ln2b.x == ln2a.x)
		{
			m1 = static_cast<double>(ln1b.y - ln1a.y) /
				static_cast<double>(ln1b.x - ln1a.x);
			b1 = ln1a.y - m1 * ln1a.x;
			ip.x = static_cast<double>(ln2a.x);
			ip.y = m1 * ln2a.x + b1;
		}
		else
		{
			m1 = static_cast<double>(ln1b.y - ln1a.y) /
				static_cast<double>(ln1b.x - ln1a.x);
			b1 = ln1a.y - m1 * ln1a.x;
			m2 = static_cast<double>(ln2b.y - ln2a.y) /
				static_cast<double>(ln2b.x - ln2a.x);
			b2 = ln2a.y - m2 * ln2a.x;
			if (std::abs(m1 - m2) > FloatingPointTolerance)
			{
				ip.x = (b2 - b1) / (m1 - m2);
				ip.y = m1 * ip.x + b1;
			}
			else
			{
				ip.x = static_cast<double>(ln1a.x + ln1b.x) / 2;
				ip.y = static_cast<double>(ln1a.y + ln1b.y) / 2;
			}
		}
		return true;
	}


	inline void SetDx(Active& e)
	{
		e.dx = GetDx(e.bot, e.top);
	}


	inline Vertex* NextVertex(const Active& e)
	{
		if (e.wind_dx > 0)
			return e.vertex_top->next;
		else
			return e.vertex_top->prev;
	}


	//PrevPrevVertex: useful to get the (inverted Y-axis) top of the
	//alternate edge (ie left or right bound) during edge insertion.
	inline Vertex* PrevPrevVertex(const Active& ae)
	{
		if (ae.wind_dx > 0)
			return ae.vertex_top->prev->prev;
		else
			return ae.vertex_top->next->next;
	}


	inline Active* ExtractFromSEL(Active* ae)
	{
		Active* res = ae->next_in_sel;
		if (res)
			res->prev_in_sel = ae->prev_in_sel;
		ae->prev_in_sel->next_in_sel = res;
		return res;
	}


	inline void Insert1Before2InSEL(Active* ae1, Active* ae2)
	{
		ae1->prev_in_sel = ae2->prev_in_sel;
		if (ae1->prev_in_sel)
			ae1->prev_in_sel->next_in_sel = ae1;
		ae1->next_in_sel = ae2;
		ae2->prev_in_sel = ae1;
	}

	inline bool IsMaxima(const Vertex& v)
	{
		return ((v.flags & VertexFlags::LocalMax) != VertexFlags::None);
	}


	inline bool IsMaxima(const Active& e)
	{
		return IsMaxima(*e.vertex_top);
	}

	Vertex* GetCurrYMaximaVertex(const Active& e)
	{
		Vertex* result = e.vertex_top;
		if (e.wind_dx > 0)
			while (result->next->pt.y == result->pt.y) result = result->next;
		else
			while (result->prev->pt.y == result->pt.y) result = result->prev;
		if (!IsMaxima(*result)) result = nullptr; // not a maxima
		return result;
	}

	Active* GetMaximaPair(const Active& e)
	{
		Active* e2;
		e2 = e.next_in_ael;
		while (e2)
		{
			if (e2->vertex_top == e.vertex_top) return e2;  // Found!
			e2 = e2->next_in_ael;
		}
		return nullptr;
	}

	Active* GetHorzMaximaPair(const Active& horz, const Vertex* vert_max)
	{
		//we can't be sure whether the MaximaPair is on the left or right, so ...
		Active* result = horz.prev_in_ael;
		while (result && result->curr_x >= vert_max->pt.x)
		{
			if (result->vertex_top == vert_max) return result;  // Found!
			result = result->prev_in_ael;
		}
		result = horz.next_in_ael;
		while (result && TopX(*result, horz.top.y) <= vert_max->pt.x)
		{
			if (result->vertex_top == vert_max) return result;  // Found!
			result = result->next_in_ael;
		}
		return nullptr;
	}

	inline int PointCount(OutPt* op)
	{
		OutPt* op2 = op;
		int cnt = 0;
		do
		{
			op2 = op2->next;
			++cnt;
		} while (op2 != op);
		return cnt;
	}


	inline OutPt* InsertOp(const Point64& pt, OutPt* insertAfter)
	{
		OutPt* result = new OutPt(pt, insertAfter->outrec);
		result->next = insertAfter->next;
		insertAfter->next->prev = result;
		insertAfter->next = result;
		result->prev = insertAfter;
		return result;
	}


	inline OutPt* DisposeOutPt(OutPt* op)
	{
		OutPt* result = op->next;
		op->prev->next = op->next;
		op->next->prev = op->prev;
		delete op;
		return result;
	}


	inline void DisposeOutPts(OutRec& outrec)
	{
		if (!outrec.pts) return;
		OutPt* op2 = outrec.pts->next;
		while (op2 != outrec.pts)
		{
			OutPt* tmp = op2->next;
			delete op2;
			op2 = tmp;
		}
		delete outrec.pts;
		outrec.pts = nullptr;
	}


	bool IntersectListSort(const IntersectNode& a, const IntersectNode& b)
	{
		//note different inequality tests ...
		return (a.pt.y == b.pt.y) ? (a.pt.x < b.pt.x) : (a.pt.y > b.pt.y);
	}


	inline void SetSides(OutRec& outrec, Active& start_edge, Active& end_edge)
	{
		outrec.front_edge = &start_edge;
		outrec.back_edge = &end_edge;
	}


	void SwapOutrecs(Active& e1, Active& e2)
	{
		OutRec* or1 = e1.outrec;
		OutRec* or2 = e2.outrec;
		if (or1 == or2)
		{
			Active* e = or1->front_edge;
			or1->front_edge = or1->back_edge;
			or1->back_edge = e;
			return;
		}
		if (or1)
		{
			if (&e1 == or1->front_edge)
				or1->front_edge = &e2;
			else
				or1->back_edge = &e2;
		}
		if (or2)
		{
			if (&e2 == or2->front_edge)
				or2->front_edge = &e1;
			else
				or2->back_edge = &e1;
		}
		e1.outrec = or2;
		e2.outrec = or1;
	}


	double Area(OutPt* op)
	{
		//https://en.wikipedia.org/wiki/Shoelace_formula
		double result = 0.0;
		OutPt* op2 = op;
		do
		{
			result += static_cast<double>(op2->prev->pt.y + op2->pt.y) *
				static_cast<double>(op2->prev->pt.x - op2->pt.x);
			op2 = op2->next;
		} while (op2 != op);
		return result * 0.5;
	}

	inline double AreaTriangle(const Point64& pt1,
		const Point64& pt2, const Point64& pt3)
	{
		return (static_cast<double>(pt3.y + pt1.y) * static_cast<double>(pt3.x - pt1.x) +
			static_cast<double>(pt1.y + pt2.y) * static_cast<double>(pt1.x - pt2.x) +
			static_cast<double>(pt2.y + pt3.y) * static_cast<double>(pt2.x - pt3.x));
	}

	void ReverseOutPts(OutPt* op)
	{
		if (!op) return;

		OutPt* op1 = op;
		OutPt* op2;

		do
		{
			op2 = op1->next;
			op1->next = op1->prev;
			op1->prev = op2;
			op1 = op2;
		} while (op1 != op);
	}


	inline void SwapSides(OutRec& outrec)
	{
		Active* e2 = outrec.front_edge;
		outrec.front_edge = outrec.back_edge;
		outrec.back_edge = e2;
		outrec.pts = outrec.pts->next;
	}


	inline OutRec* GetRealOutRec(OutRec* outrec)
	{
		while (outrec && !outrec->pts) outrec = outrec->owner;
		return outrec;
	}


	inline void UncoupleOutRec(Active ae)
	{
		OutRec* outrec = ae.outrec;
		if (!outrec) return;
		outrec->front_edge->outrec = nullptr;
		outrec->back_edge->outrec = nullptr;
		outrec->front_edge = nullptr;
		outrec->back_edge = nullptr;
	}


	inline bool PtsReallyClose(const Point64& pt1, const Point64& pt2)
	{
		return (std::llabs(pt1.x - pt2.x) < 2) && (std::llabs(pt1.y - pt2.y) < 2);
	}

	inline bool IsVerySmallTriangle(const OutPt& op)
	{
		return op.next->next == op.prev &&
			(PtsReallyClose(op.prev->pt, op.next->pt) ||
				PtsReallyClose(op.pt, op.next->pt) ||
				PtsReallyClose(op.pt, op.prev->pt));
	}

	inline bool IsValidClosedPath(const OutPt* op)
	{
		return op && (op->next != op) && (op->next != op->prev) &&
			!IsVerySmallTriangle(*op);
	}

	inline bool OutrecIsAscending(const Active* hotEdge)
	{
		return (hotEdge == hotEdge->outrec->front_edge);
	}

	inline void SwapFrontBackSides(OutRec& outrec)
	{
		Active* tmp = outrec.front_edge;
		outrec.front_edge = outrec.back_edge;
		outrec.back_edge = tmp;
		outrec.pts = outrec.pts->next;
	}

	inline bool EdgesAdjacentInAEL(const IntersectNode& inode)
	{
		return (inode.edge1->next_in_ael == inode.edge2) || (inode.edge1->prev_in_ael == inode.edge2);
	}

	inline bool TestJoinWithPrev1(const Active& e)
	{
		//this is marginally quicker than TestJoinWithPrev2
		//but can only be used when e.PrevInAEL.currX is accurate
		return IsHotEdge(e) && !IsOpen(e) &&
			e.prev_in_ael && e.prev_in_ael->curr_x == e.curr_x &&
			IsHotEdge(*e.prev_in_ael) && !IsOpen(*e.prev_in_ael) &&
			(CrossProduct(e.prev_in_ael->top, e.bot, e.top) == 0);
	}

	inline bool TestJoinWithPrev2(const Active& e, const Point64& curr_pt)
	{
		return IsHotEdge(e) && !IsOpen(e) &&
			e.prev_in_ael && !IsOpen(*e.prev_in_ael) &&
			IsHotEdge(*e.prev_in_ael) && (e.prev_in_ael->top.y < e.bot.y) &&
			(std::llabs(TopX(*e.prev_in_ael, curr_pt.y) - curr_pt.x) < 2) &&
			(CrossProduct(e.prev_in_ael->top, curr_pt, e.top) == 0);
	}

	inline bool TestJoinWithNext1(const Active& e)
	{
		//this is marginally quicker than TestJoinWithNext2
		//but can only be used when e.NextInAEL.currX is accurate
		return IsHotEdge(e) && !IsOpen(e) &&
			e.next_in_ael && (e.next_in_ael->curr_x == e.curr_x) &&
			IsHotEdge(*e.next_in_ael) && !IsOpen(*e.next_in_ael) &&
			(CrossProduct(e.next_in_ael->top, e.bot, e.top) == 0);
	}

	inline bool TestJoinWithNext2(const Active& e, const Point64& curr_pt)
	{
		return IsHotEdge(e) && !IsOpen(e) &&
			e.next_in_ael && !IsOpen(*e.next_in_ael) &&
			IsHotEdge(*e.next_in_ael) && (e.next_in_ael->top.y < e.bot.y) &&
			(std::llabs(TopX(*e.next_in_ael, curr_pt.y) - curr_pt.x) < 2) &&
			(CrossProduct(e.next_in_ael->top, curr_pt, e.top) == 0);
	}

	//------------------------------------------------------------------------------
	// ClipperBase methods ...
	//------------------------------------------------------------------------------

	ClipperBase::~ClipperBase()
	{
		Clear();
	}

	void ClipperBase::DeleteEdges(Active*& e)
	{
		while (e)
		{
			Active* e2 = e;
			e = e->next_in_ael;
			delete e2;
		}
	}

	void ClipperBase::CleanUp()
	{
		DeleteEdges(actives_);
		scanline_list_ = std::priority_queue<int64_t>();
		intersect_nodes_.clear();
		DisposeAllOutRecs();
	}


	void ClipperBase::Clear()
	{
		CleanUp();
		DisposeVerticesAndLocalMinima();
		current_locmin_iter_ = minima_list_.begin();
		minima_list_sorted_ = false;
		has_open_paths_ = false;
	}


	void ClipperBase::Reset()
	{
		if (!minima_list_sorted_)
		{
			std::sort(minima_list_.begin(), minima_list_.end(), LocMinSorter());
			minima_list_sorted_ = true;
		}
		std::vector<LocalMinima*>::const_reverse_iterator i;
		for (i = minima_list_.rbegin(); i != minima_list_.rend(); ++i)
			InsertScanline((*i)->vertex->pt.y);

		current_locmin_iter_ = minima_list_.begin();
		actives_ = nullptr;
		sel_ = nullptr;
		succeeded_ = true;
	}


#ifdef USINGZ
	void ClipperBase::SetZ(const Active& e1, const Active& e2, Point64& ip)
	{
		if (!zCallback_) return;
		// prioritize subject over clip vertices by passing
		// subject vertices before clip vertices in the callback
		if (GetPolyType(e1) == PathType::Subject)
		{
			if (ip == e1.bot) ip.z = e1.bot.z;
			else if (ip == e1.top) ip.z = e1.top.z;
			else if (ip == e2.bot) ip.z = e2.bot.z;
			else if (ip == e2.top) ip.z = e2.top.z;
			zCallback_(e1.bot, e1.top, e2.bot, e2.top, ip);
		}
		else
		{
			if (ip == e2.bot) ip.z = e2.bot.z;
			else if (ip == e2.top) ip.z = e2.top.z;
			else if (ip == e1.bot) ip.z = e1.bot.z;
			else if (ip == e1.top) ip.z = e1.top.z;
			zCallback_(e2.bot, e2.top, e1.bot, e1.top, ip);
		}
	}
#endif

	void ClipperBase::AddPath(const Path64& path, PathType polytype, bool is_open)
	{
		Paths64 tmp;
		tmp.push_back(path);
		AddPaths(tmp, polytype, is_open);
	}


	void ClipperBase::AddPaths(const Paths64& paths, PathType polytype, bool is_open)
	{
		if (is_open) has_open_paths_ = true;
		minima_list_sorted_ = false;

		Path64::size_type total_vertex_count = 0;
		for (const Path64& path : paths) total_vertex_count += path.size();
		if (total_vertex_count == 0) return;
		Vertex* vertices = new Vertex[total_vertex_count], *v = vertices;
		for (const Path64& path : paths)
		{
			//for each path create a circular double linked list of vertices
			Vertex *v0 = v, *curr_v = v, *prev_v = nullptr;

			v->prev = nullptr;
			int cnt = 0;
			for (const Point64& pt : path)
			{
				if (prev_v)
				{
					if (prev_v->pt == pt) continue; // ie skips duplicates
					prev_v->next = curr_v;
				}
				curr_v->prev = prev_v;
				curr_v->pt = pt;
				curr_v->flags = VertexFlags::None;
				prev_v = curr_v++;
				cnt++;
			}
			if (!prev_v || !prev_v->prev) continue;
			if (!is_open && prev_v->pt == v0->pt)
				prev_v = prev_v->prev;
			prev_v->next = v0;
			v0->prev = prev_v;
			v = curr_v; // ie get ready for next path
			if (cnt < 2 || (cnt == 2 && !is_open)) continue;

			//now find and assign local minima
			bool going_up, going_up0;
			if (is_open)
			{
				curr_v = v0->next;
				while (curr_v != v0 && curr_v->pt.y == v0->pt.y)
					curr_v = curr_v->next;
				going_up = curr_v->pt.y <= v0->pt.y;
				if (going_up)
				{
					v0->flags = VertexFlags::OpenStart;
					AddLocMin(*v0, polytype, true);
				}
				else
					v0->flags = VertexFlags::OpenStart | VertexFlags::LocalMax;
			}
			else // closed path
			{
				prev_v = v0->prev;
				while (prev_v != v0 && prev_v->pt.y == v0->pt.y)
					prev_v = prev_v->prev;
				if (prev_v == v0)
					continue; // only open paths can be completely flat
				going_up = prev_v->pt.y > v0->pt.y;
			}

			going_up0 = going_up;
			prev_v = v0;
			curr_v = v0->next;
			while (curr_v != v0)
			{
				if (curr_v->pt.y > prev_v->pt.y && going_up)
				{
					prev_v->flags = (prev_v->flags | VertexFlags::LocalMax);
					going_up = false;
				}
				else if (curr_v->pt.y < prev_v->pt.y && !going_up)
				{
					going_up = true;
					AddLocMin(*prev_v, polytype, is_open);
				}
				prev_v = curr_v;
				curr_v = curr_v->next;
			}

			if (is_open)
			{
				prev_v->flags = prev_v->flags | VertexFlags::OpenEnd;
				if (going_up)
					prev_v->flags = prev_v->flags | VertexFlags::LocalMax;
				else
					AddLocMin(*prev_v, polytype, is_open);
			}
			else if (going_up != going_up0)
			{
				if (going_up0) AddLocMin(*prev_v, polytype, false);
				else prev_v->flags = prev_v->flags | VertexFlags::LocalMax;
			}
		} // end processing current path

		vertex_lists_.emplace_back(vertices);
	} // end AddPaths


	inline void ClipperBase::InsertScanline(int64_t y)
	{
		scanline_list_.push(y);
	}


	bool ClipperBase::PopScanline(int64_t& y)
	{
		if (scanline_list_.empty()) return false;
		y = scanline_list_.top();
		scanline_list_.pop();
		while (!scanline_list_.empty() && y == scanline_list_.top())
			scanline_list_.pop();  // Pop duplicates.
		return true;
	}


	bool ClipperBase::PopLocalMinima(int64_t y, LocalMinima*& local_minima)
	{
		if (current_locmin_iter_ == minima_list_.end() || (*current_locmin_iter_)->vertex->pt.y != y) return false;
		local_minima = (*current_locmin_iter_++);
		return true;
	}


	void ClipperBase::DisposeAllOutRecs()
	{
		for (auto outrec : outrec_list_)
		{
			if (outrec->pts) DisposeOutPts(*outrec);
			delete outrec;
		}
		outrec_list_.resize(0);
	}


	void ClipperBase::DisposeVerticesAndLocalMinima()
	{
		for (auto lm : minima_list_) delete lm;
		minima_list_.clear();
		for (auto v : vertex_lists_) delete[] v;
		vertex_lists_.clear();
	}


	void ClipperBase::AddLocMin(Vertex& vert, PathType polytype, bool is_open)
	{
		//make sure the vertex is added only once ...
		if ((VertexFlags::LocalMin & vert.flags) != VertexFlags::None) return;

		vert.flags = (vert.flags | VertexFlags::LocalMin);
		minima_list_.push_back(new LocalMinima(&vert, polytype, is_open));
	}

	bool ClipperBase::IsContributingClosed(const Active & e) const
	{
		switch (fillrule_)
		{
		case FillRule::EvenOdd:
			break;
		case FillRule::NonZero:
			if (abs(e.wind_cnt) != 1) return false;
			break;
		case FillRule::Positive:
			if (e.wind_cnt != 1) return false;
			break;
		case FillRule::Negative:
			if (e.wind_cnt != -1) return false;
			break;
		}

		switch (cliptype_)
		{
		case ClipType::None:
			return false;
		case ClipType::Intersection:
			switch (fillrule_)
			{
			case FillRule::Positive:
				return (e.wind_cnt2 > 0);
			case FillRule::Negative:
				return (e.wind_cnt2 < 0);
			default:
				return (e.wind_cnt2 != 0);
			}
			break;

		case ClipType::Union:
			switch (fillrule_)
			{
			case FillRule::Positive:
				return (e.wind_cnt2 <= 0);
			case FillRule::Negative:
				return (e.wind_cnt2 >= 0);
			default:
				return (e.wind_cnt2 == 0);
			}
			break;

		case ClipType::Difference:
			bool result;
			switch (fillrule_)
			{
			case FillRule::Positive:
				result = (e.wind_cnt2 <= 0);
				break;
			case FillRule::Negative:
				result = (e.wind_cnt2 >= 0);
				break;
			default:
				result = (e.wind_cnt2 == 0);
			}
			if (GetPolyType(e) == PathType::Subject)
				return result;
			else
				return !result;
			break;

		case ClipType::Xor: return true;  break;
		}
		return false;  // we should never get here
	}


	inline bool ClipperBase::IsContributingOpen(const Active& e) const
	{
		bool is_in_clip, is_in_subj;
		switch (fillrule_)
		{
		case FillRule::Positive:
			is_in_clip = e.wind_cnt2 > 0;
			is_in_subj = e.wind_cnt > 0;
			break;
		case FillRule::Negative:
			is_in_clip = e.wind_cnt2 < 0;
			is_in_subj = e.wind_cnt < 0;
			break;
		default:
			is_in_clip = e.wind_cnt2 != 0;
			is_in_subj = e.wind_cnt != 0;
		}

		switch (cliptype_)
		{
		case ClipType::Intersection: return is_in_clip;
		case ClipType::Union: return (!is_in_subj && !is_in_clip);
		default: return !is_in_clip;
		}
	}


	void ClipperBase::SetWindCountForClosedPathEdge(Active& e)
	{
		//Wind counts refer to polygon regions not edges, so here an edge's WindCnt
		//indicates the higher of the wind counts for the two regions touching the
		//edge. (NB Adjacent regions can only ever have their wind counts differ by
		//one. Also, open paths have no meaningful wind directions or counts.)

		Active* e2 = e.prev_in_ael;
		//find the nearest closed path edge of the same PolyType in AEL (heading left)
		PathType pt = GetPolyType(e);
		while (e2 && (GetPolyType(*e2) != pt || IsOpen(*e2))) e2 = e2->prev_in_ael;

		if (!e2)
		{
			e.wind_cnt = e.wind_dx;
			e2 = actives_;
		}
		else if (fillrule_ == FillRule::EvenOdd)
		{
			e.wind_cnt = e.wind_dx;
			e.wind_cnt2 = e2->wind_cnt2;
			e2 = e2->next_in_ael;
		}
		else
		{
			//NonZero, positive, or negative filling here ...
			//if e's WindCnt is in the SAME direction as its WindDx, then polygon
			//filling will be on the right of 'e'.
			//NB neither e2.WindCnt nor e2.WindDx should ever be 0.
			if (e2->wind_cnt * e2->wind_dx < 0)
			{
				//opposite directions so 'e' is outside 'e2' ...
				if (abs(e2->wind_cnt) > 1)
				{
					//outside prev poly but still inside another.
					if (e2->wind_dx * e.wind_dx < 0)
						//reversing direction so use the same WC
						e.wind_cnt = e2->wind_cnt;
					else
						//otherwise keep 'reducing' the WC by 1 (ie towards 0) ...
						e.wind_cnt = e2->wind_cnt + e.wind_dx;
				}
				else
					//now outside all polys of same polytype so set own WC ...
					e.wind_cnt = (IsOpen(e) ? 1 : e.wind_dx);
			}
			else
			{
				//'e' must be inside 'e2'
				if (e2->wind_dx * e.wind_dx < 0)
					//reversing direction so use the same WC
					e.wind_cnt = e2->wind_cnt;
				else
					//otherwise keep 'increasing' the WC by 1 (ie away from 0) ...
					e.wind_cnt = e2->wind_cnt + e.wind_dx;
			}
			e.wind_cnt2 = e2->wind_cnt2;
			e2 = e2->next_in_ael;  // ie get ready to calc WindCnt2
		}

		//update wind_cnt2 ...
		if (fillrule_ == FillRule::EvenOdd)
			while (e2 != &e)
			{
				if (GetPolyType(*e2) != pt && !IsOpen(*e2))
					e.wind_cnt2 = (e.wind_cnt2 == 0 ? 1 : 0);
				e2 = e2->next_in_ael;
			}
		else
			while (e2 != &e)
			{
				if (GetPolyType(*e2) != pt && !IsOpen(*e2))
					e.wind_cnt2 += e2->wind_dx;
				e2 = e2->next_in_ael;
			}
	}


	void ClipperBase::SetWindCountForOpenPathEdge(Active& e)
	{
		Active* e2 = actives_;
		if (fillrule_ == FillRule::EvenOdd)
		{
			int cnt1 = 0, cnt2 = 0;
			while (e2 != &e)
			{
				if (GetPolyType(*e2) == PathType::Clip)
					cnt2++;
				else if (!IsOpen(*e2))
					cnt1++;
				e2 = e2->next_in_ael;
			}
			e.wind_cnt = (IsOdd(cnt1) ? 1 : 0);
			e.wind_cnt2 = (IsOdd(cnt2) ? 1 : 0);
		}
		else
		{
			while (e2 != &e)
			{
				if (GetPolyType(*e2) == PathType::Clip)
					e.wind_cnt2 += e2->wind_dx;
				else if (!IsOpen(*e2))
					e.wind_cnt += e2->wind_dx;
				e2 = e2->next_in_ael;
			}
		}
	}


	bool IsValidAelOrder(const Active& resident, const Active& newcomer)
	{
		if (newcomer.curr_x != resident.curr_x)
			return newcomer.curr_x > resident.curr_x;

		//get the turning direction  a1.top, a2.bot, a2.top
		double d = CrossProduct(resident.top, newcomer.bot, newcomer.top);
		if (d != 0) return d < 0;

		//edges must be collinear to get here
		//for starting open paths, place them according to
		//the direction they're about to turn
		if (!IsMaxima(resident) && (resident.top.y > newcomer.top.y))
		{
			return CrossProduct(newcomer.bot,
				resident.top, NextVertex(resident)->pt) <= 0;
		}
		else if (!IsMaxima(newcomer) && (newcomer.top.y > resident.top.y))
		{
			return CrossProduct(newcomer.bot,
				newcomer.top, NextVertex(newcomer)->pt) >= 0;
		}

		int64_t y = newcomer.bot.y;
		bool newcomerIsLeft = newcomer.is_left_bound;

		if (resident.bot.y != y || resident.local_min->vertex->pt.y != y)
			return newcomer.is_left_bound;
		//resident must also have just been inserted
		else if (resident.is_left_bound != newcomerIsLeft)
			return newcomerIsLeft;
		else if (CrossProduct(PrevPrevVertex(resident)->pt,
			resident.bot, resident.top) == 0) return true;
		else
			//compare turning direction of the alternate bound
			return (CrossProduct(PrevPrevVertex(resident)->pt,
				newcomer.bot, PrevPrevVertex(newcomer)->pt) > 0) == newcomerIsLeft;
	}


	void ClipperBase::InsertLeftEdge(Active& e)
	{
		Active* e2;
		if (!actives_)
		{
			e.prev_in_ael = nullptr;
			e.next_in_ael = nullptr;
			actives_ = &e;
		}
		else if (!IsValidAelOrder(*actives_, e))
		{
			e.prev_in_ael = nullptr;
			e.next_in_ael = actives_;
			actives_->prev_in_ael = &e;
			actives_ = &e;
		}
		else
		{
			e2 = actives_;
			while (e2->next_in_ael && IsValidAelOrder(*e2->next_in_ael, e))
				e2 = e2->next_in_ael;
			e.next_in_ael = e2->next_in_ael;
			if (e2->next_in_ael) e2->next_in_ael->prev_in_ael = &e;
			e.prev_in_ael = e2;
			e2->next_in_ael = &e;
		}
	}


	void InsertRightEdge(Active& e, Active& e2)
	{
		e2.next_in_ael = e.next_in_ael;
		if (e.next_in_ael) e.next_in_ael->prev_in_ael = &e2;
		e2.prev_in_ael = &e;
		e.next_in_ael = &e2;
	}


	void ClipperBase::InsertLocalMinimaIntoAEL(int64_t bot_y)
	{
		LocalMinima* local_minima;
		Active* left_bound, * right_bound;
		//Add any local minima (if any) at BotY ...
		//nb: horizontal local minima edges should contain locMin.vertex.prev

		while (PopLocalMinima(bot_y, local_minima))
		{
			if ((local_minima->vertex->flags & VertexFlags::OpenStart) != VertexFlags::None)
			{
				left_bound = nullptr;
			}
			else
			{
				left_bound = new Active();
				left_bound->bot = local_minima->vertex->pt;
				left_bound->curr_x = left_bound->bot.x;
				left_bound->wind_cnt = 0,
				left_bound->wind_cnt2 = 0,
				left_bound->wind_dx = -1,
				left_bound->vertex_top = local_minima->vertex->prev;  // ie descending
				left_bound->top = left_bound->vertex_top->pt;
				left_bound->outrec = nullptr;
				left_bound->local_min = local_minima;
				SetDx(*left_bound);
			}

			if ((local_minima->vertex->flags & VertexFlags::OpenEnd) != VertexFlags::None)
			{
				right_bound = nullptr;
			}
			else
			{
				right_bound = new Active();
				right_bound->bot = local_minima->vertex->pt;
				right_bound->curr_x = right_bound->bot.x;
				right_bound->wind_cnt = 0,
				right_bound->wind_cnt2 = 0,
				right_bound->wind_dx = 1,
				right_bound->vertex_top = local_minima->vertex->next;  // ie ascending
				right_bound->top = right_bound->vertex_top->pt;
				right_bound->outrec = nullptr;
				right_bound->local_min = local_minima;
				SetDx(*right_bound);
			}

			//Currently LeftB is just the descending bound and RightB is the ascending.
			//Now if the LeftB isn't on the left of RightB then we need swap them.
			if (left_bound && right_bound)
			{
				if (IsHorizontal(*left_bound))
				{
					if (IsHeadingRightHorz(*left_bound)) SwapActives(left_bound, right_bound);
				}
				else if (IsHorizontal(*right_bound))
				{
					if (IsHeadingLeftHorz(*right_bound)) SwapActives(left_bound, right_bound);
				}
				else if (left_bound->dx < right_bound->dx)
					SwapActives(left_bound, right_bound);
			}
			else if (!left_bound)
			{
				left_bound = right_bound;
				right_bound = nullptr;
			}

			bool contributing;
			left_bound->is_left_bound = true;
			InsertLeftEdge(*left_bound);

			if (IsOpen(*left_bound))
			{
				SetWindCountForOpenPathEdge(*left_bound);
				contributing = IsContributingOpen(*left_bound);
			}
			else
			{
				SetWindCountForClosedPathEdge(*left_bound);
				contributing = IsContributingClosed(*left_bound);
			}

			if (right_bound)
			{
				right_bound->is_left_bound = false;
				right_bound->wind_cnt = left_bound->wind_cnt;
				right_bound->wind_cnt2 = left_bound->wind_cnt2;
				InsertRightEdge(*left_bound, *right_bound);  ///////
				if (contributing)
				{
					AddLocalMinPoly(*left_bound, *right_bound, left_bound->bot, true);
					if (!IsHorizontal(*left_bound) && TestJoinWithPrev1(*left_bound))
					{
						OutPt* op = AddOutPt(*left_bound->prev_in_ael, left_bound->bot);
						AddJoin(op, left_bound->outrec->pts);
					}
				}

				while (right_bound->next_in_ael &&
					IsValidAelOrder(*right_bound->next_in_ael, *right_bound))
				{
					IntersectEdges(*right_bound, *right_bound->next_in_ael, right_bound->bot);
					SwapPositionsInAEL(*right_bound, *right_bound->next_in_ael);
				}

				if (!IsHorizontal(*right_bound) &&
					TestJoinWithNext1(*right_bound))
				{
					OutPt* op = AddOutPt(*right_bound->next_in_ael, right_bound->bot);
					AddJoin(right_bound->outrec->pts, op);
				}

				if (IsHorizontal(*right_bound))
					PushHorz(*right_bound);
				else
					InsertScanline(right_bound->top.y);
			}
			else if (contributing)
			{
				StartOpenPath(*left_bound, left_bound->bot);
			}

			if (IsHorizontal(*left_bound))
				PushHorz(*left_bound);
			else
				InsertScanline(left_bound->top.y);
		}  // while (PopLocalMinima())
	}


	inline void ClipperBase::PushHorz(Active& e)
	{
		e.next_in_sel = (sel_ ? sel_ : nullptr);
		sel_ = &e;
	}


	inline bool ClipperBase::PopHorz(Active*& e)
	{
		e = sel_;
		if (!e) return false;
		sel_ = sel_->next_in_sel;
		return true;
	}


	OutPt* ClipperBase::AddLocalMinPoly(Active& e1, Active& e2,
		const Point64& pt, bool is_new)
	{
		OutRec* outrec = new OutRec();
		outrec->idx = (unsigned)outrec_list_.size();
		outrec_list_.push_back(outrec);
		outrec->pts = nullptr;
		outrec->polypath = nullptr;
		e1.outrec = outrec;
		e2.outrec = outrec;

		//Setting the owner and inner/outer states (above) is an essential
		//precursor to setting edge 'sides' (ie left and right sides of output
		//polygons) and hence the orientation of output paths ...

		if (IsOpen(e1))
		{
			outrec->owner = nullptr;
			outrec->is_open = true;
			if (e1.wind_dx > 0)
				SetSides(*outrec, e1, e2);
			else
				SetSides(*outrec, e2, e1);
		}
		else
		{
			Active* prevHotEdge = GetPrevHotEdge(e1);
			//e.windDx is the winding direction of the **input** paths
			//and unrelated to the winding direction of output polygons.
			//Output orientation is determined by e.outrec.frontE which is
			//the ascending edge (see AddLocalMinPoly).
			if (prevHotEdge)
			{
				outrec->owner = prevHotEdge->outrec;
				if (OutrecIsAscending(prevHotEdge) == is_new)
					SetSides(*outrec, e2, e1);
				else
					SetSides(*outrec, e1, e2);
			}
			else
			{
				outrec->owner = nullptr;
				if (is_new)
					SetSides(*outrec, e1, e2);
				else
					SetSides(*outrec, e2, e1);
			}
		}

		OutPt* op = new OutPt(pt, outrec);
		outrec->pts = op;
		return op;
	}


	OutPt* ClipperBase::AddLocalMaxPoly(Active& e1, Active& e2, const Point64& pt)
	{
		if (IsFront(e1) == IsFront(e2))
		{
			if (IsOpenEnd(e1))
				SwapFrontBackSides(*e1.outrec);
			else if (IsOpenEnd(e2))
				SwapFrontBackSides(*e2.outrec);
			else
			{
				succeeded_ = false;
				return nullptr;
			}
		}

		OutPt* result = AddOutPt(e1, pt);
		if (e1.outrec == e2.outrec)
		{
			OutRec& outrec = *e1.outrec;
			outrec.pts = result;

			UncoupleOutRec(e1);
			if (!IsOpen(e1)) CleanCollinear(&outrec);
			result = outrec.pts;
			if (using_polytree_ && outrec.owner && !outrec.owner->front_edge)
				outrec.owner = GetRealOutRec(outrec.owner->owner);
		}
		//and to preserve the winding orientation of outrec ...
		else if (IsOpen(e1))
		{
			if (e1.wind_dx < 0)
				JoinOutrecPaths(e1, e2);
			else
				JoinOutrecPaths(e2, e1);
		}
		else if (e1.outrec->idx < e2.outrec->idx)
			JoinOutrecPaths(e1, e2);
		else
			JoinOutrecPaths(e2, e1);

		return result;
	}

	void ClipperBase::JoinOutrecPaths(Active& e1, Active& e2)
	{
		//join e2 outrec path onto e1 outrec path and then delete e2 outrec path
		//pointers. (NB Only very rarely do the joining ends share the same coords.)
		OutPt* p1_st = e1.outrec->pts;
		OutPt* p2_st = e2.outrec->pts;
		OutPt* p1_end = p1_st->next;
		OutPt* p2_end = p2_st->next;
		if (IsFront(e1))
		{
			p2_end->prev = p1_st;
			p1_st->next = p2_end;
			p2_st->next = p1_end;
			p1_end->prev = p2_st;
			e1.outrec->pts = p2_st;
			e1.outrec->front_edge = e2.outrec->front_edge;
			if (e1.outrec->front_edge)
				e1.outrec->front_edge->outrec = e1.outrec;
		}
		else
		{
			p1_end->prev = p2_st;
			p2_st->next = p1_end;
			p1_st->next = p2_end;
			p2_end->prev = p1_st;
			e1.outrec->back_edge = e2.outrec->back_edge;
			if (e1.outrec->back_edge)
				e1.outrec->back_edge->outrec = e1.outrec;
		}

		//an owner must have a lower idx otherwise
		//it can't be a valid owner
		if (e2.outrec->owner && e2.outrec->owner->idx < e1.outrec->idx)
		{
			if (!e1.outrec->owner || e2.outrec->owner->idx < e1.outrec->owner->idx)
				e1.outrec->owner = e2.outrec->owner;
		}

		//after joining, the e2.OutRec must contains no vertices ...
		e2.outrec->front_edge = nullptr;
		e2.outrec->back_edge = nullptr;
		e2.outrec->pts = nullptr;
		e2.outrec->owner = e1.outrec;

		if (IsOpenEnd(e1))
		{
			e2.outrec->pts = e1.outrec->pts;
			e1.outrec->pts = nullptr;
		}

		//and e1 and e2 are maxima and are about to be dropped from the Actives list.
		e1.outrec = nullptr;
		e2.outrec = nullptr;
	}


	OutPt* ClipperBase::AddOutPt(const Active& e, const Point64& pt)
	{
		OutPt* new_op = nullptr;

		//Outrec.OutPts: a circular doubly-linked-list of POutPt where ...
		//op_front[.Prev]* ~~~> op_back & op_back == op_front.Next
		OutRec* outrec = e.outrec;
		bool to_front = IsFront(e);
		OutPt* op_front = outrec->pts;
		OutPt* op_back = op_front->next;

		if (to_front && (pt == op_front->pt))
			new_op = op_front;
		else if (!to_front && (pt == op_back->pt))
			new_op = op_back;
		else
		{
			new_op = new OutPt(pt, outrec);
			op_back->prev = new_op;
			new_op->prev = op_front;
			new_op->next = op_back;
			op_front->next = new_op;
			if (to_front) outrec->pts = new_op;
		}
		return new_op;
	}


	bool ClipperBase::ValidateClosedPathEx(OutPt*& outpt)
	{
		if (IsValidClosedPath(outpt)) return true;
		if (outpt) SafeDisposeOutPts(outpt);
		return false;
	}


	void ClipperBase::CleanCollinear(OutRec* outrec)
	{
		outrec = GetRealOutRec(outrec);
		if (!outrec || outrec->is_open ||
			outrec->front_edge || !ValidateClosedPathEx(outrec->pts)) return;

		OutPt* startOp = outrec->pts, * op2 = startOp;
		for (; ; )
		{
			if (op2->joiner) return;

			//NB if preserveCollinear == true, then only remove 180 deg. spikes
			if ((CrossProduct(op2->prev->pt, op2->pt, op2->next->pt) == 0) &&
				(op2->pt == op2->prev->pt ||
					op2->pt == op2->next->pt || !PreserveCollinear ||
					DotProduct(op2->prev->pt, op2->pt, op2->next->pt) < 0))
			{

				if (op2 == outrec->pts) outrec->pts = op2->prev;

				op2 = DisposeOutPt(op2);
				if (!ValidateClosedPathEx(op2))
				{
					outrec->pts = nullptr;
					return;
				}
				startOp = op2;
				continue;
			}
			op2 = op2->next;
			if (op2 == startOp) break;
		}
		FixSelfIntersects(outrec);
	}

	void ClipperBase::DoSplitOp(OutRec* outrec, OutPt* splitOp)
	{
		// splitOp.prev -> splitOp &&
		// splitOp.next -> splitOp.next.next are intersecting
		OutPt* prevOp = splitOp->prev;
		OutPt* nextNextOp = splitOp->next->next;
		outrec->pts = prevOp;
		PointD ipD;
		GetIntersectPoint(prevOp->pt,
			splitOp->pt, splitOp->next->pt, nextNextOp->pt, ipD);
		Point64 ip = Point64(ipD);
#ifdef USINGZ
		if (zCallback_)
			zCallback_(prevOp->pt, splitOp->pt, splitOp->next->pt, nextNextOp->pt, ip);
#endif
		double area1 = Area(outrec->pts);
		double absArea1 = std::fabs(area1);
		if (absArea1 < 2)
		{
			SafeDisposeOutPts(outrec->pts);
			// outrec.pts == nil; :)
			return;
		}

		// nb: area1 is the path's area *before* splitting, whereas area2 is
		// the area of the triangle containing splitOp & splitOp.next.
		// So the only way for these areas to have the same sign is if
		// the split triangle is larger than the path containing prevOp or
		// if there's more than one self=intersection.
		double area2 = AreaTriangle(ip, splitOp->pt, splitOp->next->pt);
		double absArea2 = std::fabs(area2);

		// de-link splitOp and splitOp.next from the path
		// while inserting the intersection point
		if (ip == prevOp->pt || ip == nextNextOp->pt)
		{
			nextNextOp->prev = prevOp;
			prevOp->next = nextNextOp;
		}
		else
		{
			OutPt* newOp2 = new OutPt(ip, prevOp->outrec);
			newOp2->prev = prevOp;
			newOp2->next = nextNextOp;
			nextNextOp->prev = newOp2;
			prevOp->next = newOp2;
		}

		SafeDeleteOutPtJoiners(splitOp->next);
		SafeDeleteOutPtJoiners(splitOp);

		if (absArea2 >= 1 &&
			(absArea2 > absArea1 || (area2 > 0) == (area1 > 0)))
		{
			OutRec* newOutRec = new OutRec();
			newOutRec->idx = outrec_list_.size();
			outrec_list_.push_back(newOutRec);
			newOutRec->owner = prevOp->outrec->owner;
			newOutRec->polypath = nullptr;
			splitOp->outrec = newOutRec;
			splitOp->next->outrec = newOutRec;

			OutPt* newOp = new OutPt(ip, newOutRec);
			newOp->prev = splitOp->next;
			newOp->next = splitOp;
			newOutRec->pts = newOp;
			splitOp->prev = newOp;
			splitOp->next->next = newOp;
		}
		else
		{
			delete splitOp->next;
			delete splitOp;
		}
	}

	void ClipperBase::FixSelfIntersects(OutRec* outrec)
	{
		OutPt* op2 = outrec->pts;
		for (; ; )
		{
			// triangles can't self-intersect
			if (op2->prev == op2->next->next) break;
			if (SegmentsIntersect(op2->prev->pt,
				op2->pt, op2->next->pt, op2->next->next->pt))
			{
				if (op2 == outrec->pts || op2->next == outrec->pts)
					outrec->pts = outrec->pts->prev;
				DoSplitOp(outrec, op2);
				if (!outrec->pts) break;
				op2 = outrec->pts;
				continue;
			}
			else
				op2 = op2->next;

			if (op2 == outrec->pts) break;
		}
	}


	inline void UpdateOutrecOwner(OutRec* outrec)
	{
		OutPt* opCurr = outrec->pts;
		for (; ; )
		{
			opCurr->outrec = outrec;
			opCurr = opCurr->next;
			if (opCurr == outrec->pts) return;
		}
	}


	void ClipperBase::SafeDisposeOutPts(OutPt*& op)
	{
		OutRec* outrec = GetRealOutRec(op->outrec);
		if (outrec->front_edge)
			outrec->front_edge->outrec = nullptr;
		if (outrec->back_edge)
			outrec->back_edge->outrec = nullptr;

		op->prev->next = nullptr;
		while (op)
		{
			SafeDeleteOutPtJoiners(op);
			OutPt* tmp = op->next;
			delete op;
			op = tmp;
		}
		outrec->pts = nullptr;
	}


	void ClipperBase::CompleteSplit(OutPt* op1, OutPt* op2, OutRec& outrec)
	{
		double area1 = Area(op1);
		double area2 = Area(op2);
		bool signs_change = (area1 > 0) == (area2 < 0);

		if (area1 == 0 || (signs_change && std::abs(area1) < 2))
		{
			SafeDisposeOutPts(op1);
			outrec.pts = op2;
		}
		else if (area2 == 0 || (signs_change && std::abs(area2) < 2))
		{
			SafeDisposeOutPts(op2);
			outrec.pts = op1;
		}
		else
		{
			OutRec* newOr = new OutRec();
			newOr->idx = outrec_list_.size();
			outrec_list_.push_back(newOr);
			newOr->polypath = nullptr;

			if (using_polytree_)
			{
				if (!outrec.splits) outrec.splits = new OutRecList();
				outrec.splits->push_back(newOr);
			}

			if (std::abs(area1) >= std::abs(area2))
			{
				outrec.pts = op1;
				newOr->pts = op2;
			}
			else
			{
				outrec.pts = op2;
				newOr->pts = op1;
			}

			if ((area1 > 0) == (area2 > 0))
				newOr->owner = outrec.owner;
			else
				newOr->owner = &outrec;

			UpdateOutrecOwner(newOr);
			CleanCollinear(newOr);
		}
	}


	OutPt* ClipperBase::StartOpenPath(Active& e, const Point64& pt)
	{
		OutRec* outrec = new OutRec();
		outrec->idx = outrec_list_.size();
		outrec_list_.push_back(outrec);
		outrec->owner = nullptr;
		outrec->is_open = true;
		outrec->pts = nullptr;
		outrec->polypath = nullptr;

		if (e.wind_dx > 0)
		{
			outrec->front_edge = &e;
			outrec->back_edge = nullptr;
		}
		else
		{
			outrec->front_edge = nullptr;
			outrec->back_edge =& e;
		}

		e.outrec = outrec;

		OutPt* op = new OutPt(pt, outrec);
		outrec->pts = op;
		return op;
	}


	inline void ClipperBase::UpdateEdgeIntoAEL(Active* e)
	{
		e->bot = e->top;
		e->vertex_top = NextVertex(*e);
		e->top = e->vertex_top->pt;
		e->curr_x = e->bot.x;
		SetDx(*e);
		if (IsHorizontal(*e)) return;
		InsertScanline(e->top.y);
		if (TestJoinWithPrev1(*e))
		{
			OutPt* op1 = AddOutPt(*e->prev_in_ael, e->bot);
			OutPt* op2 = AddOutPt(*e, e->bot);
			AddJoin(op1, op2);
		}
	}


	Active* FindEdgeWithMatchingLocMin(Active* e)
	{
		Active* result = e->next_in_ael;
		while (result)
		{
			if (result->local_min == e->local_min) return result;
			else if (!IsHorizontal(*result) && e->bot != result->bot) result = nullptr;
			else result = result->next_in_ael;
		}
		result = e->prev_in_ael;
		while (result)
		{
			if (result->local_min == e->local_min) return result;
			else if (!IsHorizontal(*result) && e->bot != result->bot) return nullptr;
			else result = result->prev_in_ael;
		}
		return result;
	}


	OutPt* ClipperBase::IntersectEdges(Active& e1, Active& e2, const Point64& pt)
	{
		//MANAGE OPEN PATH INTERSECTIONS SEPARATELY ...
		if (has_open_paths_ && (IsOpen(e1) || IsOpen(e2)))
		{
			if (IsOpen(e1) && IsOpen(e2)) return nullptr;

			Active* edge_o, * edge_c;
			if (IsOpen(e1))
			{
				edge_o = &e1;
				edge_c = &e2;
			}
			else
			{
				edge_o = &e2;
				edge_c = &e1;
			}

			if (abs(edge_c->wind_cnt) != 1) return nullptr;
			switch (cliptype_)
			{
			case ClipType::Union:
				if (!IsHotEdge(*edge_c)) return nullptr;
				break;
			default:
				if (edge_c->local_min->polytype == PathType::Subject)
					return nullptr;
			}

			switch (fillrule_)
			{
			case FillRule::Positive: if (edge_c->wind_cnt != 1) return nullptr; break;
			case FillRule::Negative: if (edge_c->wind_cnt != -1) return nullptr; break;
			default: if (std::abs(edge_c->wind_cnt) != 1) return nullptr; break;
			}

			//toggle contribution ...
			if (IsHotEdge(*edge_o))
			{
				OutPt* resultOp = AddOutPt(*edge_o, pt);
#ifdef USINGZ
				if (zCallback_) SetZ(e1, e2, resultOp->pt);
#endif
				if (IsFront(*edge_o)) edge_o->outrec->front_edge = nullptr;
				else edge_o->outrec->back_edge = nullptr;
				edge_o->outrec = nullptr;
				return resultOp;
			}

			//horizontal edges can pass under open paths at a LocMins
			else if (pt == edge_o->local_min->vertex->pt &&
				!IsOpenEnd(*edge_o->local_min->vertex))
			{
				//find the other side of the LocMin and
				//if it's 'hot' join up with it ...
				Active* e3 = FindEdgeWithMatchingLocMin(edge_o);
				if (e3 && IsHotEdge(*e3))
				{
					edge_o->outrec = e3->outrec;
					if (edge_o->wind_dx > 0)
						SetSides(*e3->outrec, *edge_o, *e3);
					else
						SetSides(*e3->outrec, *e3, *edge_o);
					return e3->outrec->pts;
				}
				else
					return StartOpenPath(*edge_o, pt);
			}
			else
				return StartOpenPath(*edge_o, pt);
		}


		//MANAGING CLOSED PATHS FROM HERE ON

		//UPDATE WINDING COUNTS...

		int old_e1_windcnt, old_e2_windcnt;
		if (e1.local_min->polytype == e2.local_min->polytype)
		{
			if (fillrule_ == FillRule::EvenOdd)
			{
				old_e1_windcnt = e1.wind_cnt;
				e1.wind_cnt = e2.wind_cnt;
				e2.wind_cnt = old_e1_windcnt;
			}
			else
			{
				if (e1.wind_cnt + e2.wind_dx == 0)
					e1.wind_cnt = -e1.wind_cnt;
				else
					e1.wind_cnt += e2.wind_dx;
				if (e2.wind_cnt - e1.wind_dx == 0)
					e2.wind_cnt = -e2.wind_cnt;
				else
					e2.wind_cnt -= e1.wind_dx;
			}
		}
		else
		{
			if (fillrule_ != FillRule::EvenOdd)
			{
				e1.wind_cnt2 += e2.wind_dx;
				e2.wind_cnt2 -= e1.wind_dx;
			}
			else
			{
				e1.wind_cnt2 = (e1.wind_cnt2 == 0 ? 1 : 0);
				e2.wind_cnt2 = (e2.wind_cnt2 == 0 ? 1 : 0);
			}
		}

		switch (fillrule_)
		{
		case FillRule::EvenOdd:
		case FillRule::NonZero:
			old_e1_windcnt = abs(e1.wind_cnt);
			old_e2_windcnt = abs(e2.wind_cnt);
			break;
		default:
			if (fillrule_ == fillpos)
			{
				old_e1_windcnt = e1.wind_cnt;
				old_e2_windcnt = e2.wind_cnt;
			}
			else
			{
				old_e1_windcnt = -e1.wind_cnt;
				old_e2_windcnt = -e2.wind_cnt;
			}
			break;
		}

		const bool e1_windcnt_in_01 = old_e1_windcnt == 0 || old_e1_windcnt == 1;
		const bool e2_windcnt_in_01 = old_e2_windcnt == 0 || old_e2_windcnt == 1;

		if ((!IsHotEdge(e1) && !e1_windcnt_in_01) || (!IsHotEdge(e2) && !e2_windcnt_in_01))
		{
			return nullptr;
		}

		//NOW PROCESS THE INTERSECTION ...
		OutPt* resultOp = nullptr;
		//if both edges are 'hot' ...
		if (IsHotEdge(e1) && IsHotEdge(e2))
		{
			if ((old_e1_windcnt != 0 && old_e1_windcnt != 1) || (old_e2_windcnt != 0 && old_e2_windcnt != 1) ||
				(e1.local_min->polytype != e2.local_min->polytype && cliptype_ != ClipType::Xor))
			{
				resultOp = AddLocalMaxPoly(e1, e2, pt);
#ifdef USINGZ
				if (zCallback_ && resultOp) SetZ(e1, e2, resultOp->pt);
#endif
			}
			else if (IsFront(e1) || (e1.outrec == e2.outrec))
			{
				//this 'else if' condition isn't strictly needed but
				//it's sensible to split polygons that ony touch at
				//a common vertex (not at common edges).

				resultOp = AddLocalMaxPoly(e1, e2, pt);
				OutPt* op2 = AddLocalMinPoly(e1, e2, pt);
#ifdef USINGZ
				if (zCallback_ && resultOp) SetZ(e1, e2, resultOp->pt);
				if (zCallback_) SetZ(e1, e2, op2->pt);
#endif
				if (resultOp && resultOp->pt == op2->pt &&
					!IsHorizontal(e1) && !IsHorizontal(e2) &&
					(CrossProduct(e1.bot, resultOp->pt, e2.bot) == 0))
					AddJoin(resultOp, op2);
			}
			else
			{
				resultOp = AddOutPt(e1, pt);
#ifdef USINGZ
				OutPt* op2 = AddOutPt(e2, pt);
				if (zCallback_)
				{
					SetZ(e1, e2, resultOp->pt);
					SetZ(e1, e2, op2->pt);
				}
#else
				AddOutPt(e2, pt);
#endif
				SwapOutrecs(e1, e2);
			}
		}
		else if (IsHotEdge(e1))
		{
			resultOp = AddOutPt(e1, pt);
#ifdef USINGZ
			if (zCallback_) SetZ(e1, e2, resultOp->pt);
#endif
			SwapOutrecs(e1, e2);
		}
		else if (IsHotEdge(e2))
		{
			resultOp = AddOutPt(e2, pt);
#ifdef USINGZ
			if (zCallback_) SetZ(e1, e2, resultOp->pt);
#endif
			SwapOutrecs(e1, e2);
		}
		else
		{
			int64_t e1Wc2, e2Wc2;
			switch (fillrule_)
			{
			case FillRule::EvenOdd:
			case FillRule::NonZero:
				e1Wc2 = abs(e1.wind_cnt2);
				e2Wc2 = abs(e2.wind_cnt2);
				break;
			default:
				if (fillrule_ == fillpos)
				{
					e1Wc2 = e1.wind_cnt2;
					e2Wc2 = e2.wind_cnt2;
				}
				else
				{
					e1Wc2 = -e1.wind_cnt2;
					e2Wc2 = -e2.wind_cnt2;
				}
				break;
			}

			if (!IsSamePolyType(e1, e2))
			{
				resultOp = AddLocalMinPoly(e1, e2, pt, false);
#ifdef USINGZ
				if (zCallback_) SetZ(e1, e2, resultOp->pt);
#endif
			}
			else if (old_e1_windcnt == 1 && old_e2_windcnt == 1)
			{
				resultOp = nullptr;
				switch (cliptype_)
				{
				case ClipType::Union:
					if (e1Wc2 <= 0 && e2Wc2 <= 0)
						resultOp = AddLocalMinPoly(e1, e2, pt, false);
					break;
				case ClipType::Difference:
					if (((GetPolyType(e1) == PathType::Clip) && (e1Wc2 > 0) && (e2Wc2 > 0)) ||
						((GetPolyType(e1) == PathType::Subject) && (e1Wc2 <= 0) && (e2Wc2 <= 0)))
					{
						resultOp = AddLocalMinPoly(e1, e2, pt, false);
					}
					break;
				case ClipType::Xor:
					resultOp = AddLocalMinPoly(e1, e2, pt, false);
					break;
				default:
					if (e1Wc2 > 0 && e2Wc2 > 0)
						resultOp = AddLocalMinPoly(e1, e2, pt, false);
					break;
				}
#ifdef USINGZ
				if (resultOp && zCallback_) SetZ(e1, e2, resultOp->pt);
#endif
			}
		}
		return resultOp;
	}


	inline void ClipperBase::DeleteFromAEL(Active& e)
	{
		Active* prev = e.prev_in_ael;
		Active* next = e.next_in_ael;
		if (!prev && !next && (&e != actives_)) return;  // already deleted
		if (prev)
			prev->next_in_ael = next;
		else
			actives_ = next;
		if (next) next->prev_in_ael = prev;
		delete& e;
	}


	inline void ClipperBase::AdjustCurrXAndCopyToSEL(const int64_t top_y)
	{
		Active* e = actives_;
		sel_ = e;
		while (e)
		{
			e->prev_in_sel = e->prev_in_ael;
			e->next_in_sel = e->next_in_ael;
			e->jump = e->next_in_sel;
			e->curr_x = TopX(*e, top_y);
			e = e->next_in_ael;
		}
	}


	bool ClipperBase::ExecuteInternal(ClipType ct, FillRule fillrule, bool use_polytrees)
	{
		cliptype_ = ct;
		fillrule_ = fillrule;
		using_polytree_ = use_polytrees;
		Reset();
		int64_t y;
		if (ct == ClipType::None || !PopScanline(y)) return true;

		while (succeeded_)
		{
			InsertLocalMinimaIntoAEL(y);
			Active* e;
			while (PopHorz(e)) DoHorizontal(*e);
			if (horz_joiners_) ConvertHorzTrialsToJoins();
			bot_y_ = y;  // bot_y_ == bottom of scanbeam
			if (!PopScanline(y)) break;  // y new top of scanbeam
			DoIntersections(y);
			DoTopOfScanbeam(y);
			while (PopHorz(e)) DoHorizontal(*e);
		}
		ProcessJoinerList();
		return succeeded_;
	}

	void ClipperBase::DoIntersections(const int64_t top_y)
	{
		if (BuildIntersectList(top_y))
		{
			ProcessIntersectList();
			intersect_nodes_.clear();
		}
	}

	void ClipperBase::AddNewIntersectNode(Active& e1, Active& e2, int64_t top_y)
	{
		Point64 pt = GetIntersectPoint(e1, e2);

		//rounding errors can occasionally place the calculated intersection
		//point either below or above the scanbeam, so check and correct ...
		if (pt.y > bot_y_)
		{
			//e.curr.y is still the bottom of scanbeam
			pt.y = bot_y_;
			//use the more vertical of the 2 edges to derive pt.x ...
			if (abs(e1.dx) < abs(e2.dx))
				pt.x = TopX(e1, bot_y_);
			else
				pt.x = TopX(e2, bot_y_);
		}
		else if (pt.y < top_y)
		{
			//top_y is at the top of the scanbeam
			pt.y = top_y;
			if (e1.top.y == top_y)
				pt.x = e1.top.x;
			else if (e2.top.y == top_y)
				pt.x = e2.top.x;
			else if (abs(e1.dx) < abs(e2.dx))
				pt.x = e1.curr_x;
			else
				pt.x = e2.curr_x;
		}

		intersect_nodes_.push_back(IntersectNode(&e1, &e2, pt));
	}


	bool ClipperBase::BuildIntersectList(const int64_t top_y)
	{
		if (!actives_ || !actives_->next_in_ael) return false;

		//Calculate edge positions at the top of the current scanbeam, and from this
		//we will determine the intersections required to reach these new positions.
		AdjustCurrXAndCopyToSEL(top_y);
		//Find all edge intersections in the current scanbeam using a stable merge
		//sort that ensures only adjacent edges are intersecting. Intersect info is
		//stored in FIntersectList ready to be processed in ProcessIntersectList.
		//Re merge sorts see https://stackoverflow.com/a/46319131/359538

		Active* left = sel_, * right, * l_end, * r_end, * curr_base, * tmp;

		while (left && left->jump)
		{
			Active* prev_base = nullptr;
			while (left && left->jump)
			{
				curr_base = left;
				right = left->jump;
				l_end = right;
				r_end = right->jump;
				left->jump = r_end;
				while (left != l_end && right != r_end)
				{
					if (right->curr_x < left->curr_x)
					{
						tmp = right->prev_in_sel;
						for (; ; )
						{
							AddNewIntersectNode(*tmp, *right, top_y);
							if (tmp == left) break;
							tmp = tmp->prev_in_sel;
						}

						tmp = right;
						right = ExtractFromSEL(tmp);
						l_end = right;
						Insert1Before2InSEL(tmp, left);
						if (left == curr_base)
						{
							curr_base = tmp;
							curr_base->jump = r_end;
							if (!prev_base) sel_ = curr_base;
							else prev_base->jump = curr_base;
						}
					}
					else left = left->next_in_sel;
				}
				prev_base = curr_base;
				left = r_end;
			}
			left = sel_;
		}
		return intersect_nodes_.size() > 0;
	}

	void ClipperBase::ProcessIntersectList()
	{
		//We now have a list of intersections required so that edges will be
		//correctly positioned at the top of the scanbeam. However, it's important
		//that edge intersections are processed from the bottom up, but it's also
		//crucial that intersections only occur between adjacent edges.

		//First we do a quicksort so intersections proceed in a bottom up order ...
		std::sort(intersect_nodes_.begin(), intersect_nodes_.end(), IntersectListSort);
		//Now as we process these intersections, we must sometimes adjust the order
		//to ensure that intersecting edges are always adjacent ...

		std::vector<IntersectNode>::iterator node_iter, node_iter2;
		for (node_iter = intersect_nodes_.begin();
			node_iter != intersect_nodes_.end();  ++node_iter)
		{
			if (!EdgesAdjacentInAEL(*node_iter))
			{
				node_iter2 = node_iter + 1;
				while (!EdgesAdjacentInAEL(*node_iter2)) ++node_iter2;
				std::swap(*node_iter, *node_iter2);
			}

			IntersectNode& node = *node_iter;
			IntersectEdges(*node.edge1, *node.edge2, node.pt);
			SwapPositionsInAEL(*node.edge1, *node.edge2);

			if (TestJoinWithPrev2(*node.edge2, node.pt))
			{
				OutPt* op1 = AddOutPt(*node.edge2->prev_in_ael, node.pt);
				OutPt* op2 = AddOutPt(*node.edge2, node.pt);
				if (op1 != op2) AddJoin(op1, op2);
			}
			else if (TestJoinWithNext2(*node.edge1, node.pt))
			{
				OutPt* op1 = AddOutPt(*node.edge1, node.pt);
				OutPt* op2 = AddOutPt(*node.edge1->next_in_ael, node.pt);
				if (op1 != op2) AddJoin(op1, op2);
			}
		}
	}


	void ClipperBase::SwapPositionsInAEL(Active& e1, Active& e2)
	{
		//preconditon: e1 must be immediately to the left of e2
		Active* next = e2.next_in_ael;
		if (next) next->prev_in_ael = &e1;
		Active* prev = e1.prev_in_ael;
		if (prev) prev->next_in_ael = &e2;
		e2.prev_in_ael = prev;
		e2.next_in_ael = &e1;
		e1.prev_in_ael = &e2;
		e1.next_in_ael = next;
		if (!e2.prev_in_ael) actives_ = &e2;
	}


	bool ClipperBase::ResetHorzDirection(const Active& horz,
		const Active* max_pair, int64_t& horz_left, int64_t& horz_right)
	{
		if (horz.bot.x == horz.top.x)
		{
			//the horizontal edge is going nowhere ...
			horz_left = horz.curr_x;
			horz_right = horz.curr_x;
			Active* e = horz.next_in_ael;
			while (e && e != max_pair) e = e->next_in_ael;
			return e != nullptr;
		}
		else if (horz.curr_x < horz.top.x)
		{
			horz_left = horz.curr_x;
			horz_right = horz.top.x;
			return true;
		}
		else
		{
			horz_left = horz.top.x;
			horz_right = horz.curr_x;
			return false;  // right to left
		}
	}

	inline bool HorzIsSpike(const Active& horzEdge)
	{
		Point64 nextPt = NextVertex(horzEdge)->pt;
		return (nextPt.y == horzEdge.bot.y) &&
			(horzEdge.bot.x < horzEdge.top.x) != (horzEdge.top.x < nextPt.x);
	}

	inline void TrimHorz(Active& horzEdge, bool preserveCollinear)
	{
		bool wasTrimmed = false;
		Point64 pt = NextVertex(horzEdge)->pt;
		while (pt.y == horzEdge.top.y)
		{
			//always trim 180 deg. spikes (in closed paths)
			//but otherwise break if preserveCollinear = true
			if (preserveCollinear &&
				((pt.x < horzEdge.top.x) != (horzEdge.bot.x < horzEdge.top.x)))
					break;

			horzEdge.vertex_top = NextVertex(horzEdge);
			horzEdge.top = pt;
			wasTrimmed = true;
			if (IsMaxima(horzEdge)) break;
			pt = NextVertex(horzEdge)->pt;
		}

		if (wasTrimmed) SetDx(horzEdge); // +/-infinity
	}


	void ClipperBase::DoHorizontal(Active& horz)
		/*******************************************************************************
				* Notes: Horizontal edges (HEs) at scanline intersections (ie at the top or    *
				* bottom of a scanbeam) are processed as if layered.The order in which HEs     *
				* are processed doesn't matter. HEs intersect with the bottom vertices of      *
				* other HEs[#] and with non-horizontal edges [*]. Once these intersections     *
				* are completed, intermediate HEs are 'promoted' to the next edge in their     *
				* bounds, and they in turn may be intersected[%] by other HEs.                 *
				*                                                                              *
				* eg: 3 horizontals at a scanline:    /   |                     /           /  *
				*              |                     /    |     (HE3)o ========%========== o   *
				*              o ======= o(HE2)     /     |         /         /                *
				*          o ============#=========*======*========#=========o (HE1)           *
				*         /              |        /       |       /                            *
				*******************************************************************************/
	{
		Point64 pt;
		bool horzIsOpen = IsOpen(horz);
		int64_t y = horz.bot.y;
		Vertex* vertex_max = nullptr;
		Active* max_pair = nullptr;

		if (!horzIsOpen)
		{
			vertex_max = GetCurrYMaximaVertex(horz);
			if (vertex_max)
			{
				max_pair = GetHorzMaximaPair(horz, vertex_max);
				//remove 180 deg.spikes and also simplify
				//consecutive horizontals when PreserveCollinear = true
				if (vertex_max != horz.vertex_top)
					TrimHorz(horz, PreserveCollinear);
			}
		}

		int64_t horz_left, horz_right;
		bool is_left_to_right =
			ResetHorzDirection(horz, max_pair, horz_left, horz_right);

		if (IsHotEdge(horz))
			AddOutPt(horz, Point64(horz.curr_x, y));

		OutPt* op;
		while (true) // loop through consec. horizontal edges
		{
			if (horzIsOpen && IsMaxima(horz) && !IsOpenEnd(horz))
			{
				vertex_max = GetCurrYMaximaVertex(horz);
				if (vertex_max)
					max_pair = GetHorzMaximaPair(horz, vertex_max);
			}

			Active* e;
			if (is_left_to_right) e = horz.next_in_ael;
			else e = horz.prev_in_ael;

			while (e)
			{

				if (e == max_pair)
				{
					if (IsHotEdge(horz))
					{
						while (horz.vertex_top != e->vertex_top)
						{
							AddOutPt(horz, horz.top);
							UpdateEdgeIntoAEL(&horz);
						}
						op = AddLocalMaxPoly(horz, *e, horz.top);
						if (op && !IsOpen(horz) && op->pt == horz.top)
							AddTrialHorzJoin(op);
					}
					DeleteFromAEL(*e);
					DeleteFromAEL(horz);
					return;
				}

				//if horzEdge is a maxima, keep going until we reach
				//its maxima pair, otherwise check for break conditions
				if (vertex_max != horz.vertex_top || IsOpenEnd(horz))
				{
					//otherwise stop when 'ae' is beyond the end of the horizontal line
					if ((is_left_to_right && e->curr_x > horz_right) ||
						(!is_left_to_right && e->curr_x < horz_left)) break;

					if (e->curr_x == horz.top.x && !IsHorizontal(*e))
					{
						pt = NextVertex(horz)->pt;
						if (is_left_to_right)
						{
							//with open paths we'll only break once past horz's end
							if (IsOpen(*e) && !IsSamePolyType(*e, horz) && !IsHotEdge(*e))
							{
								if (TopX(*e, pt.y) > pt.x) break;
							}
							//otherwise we'll only break when horz's outslope is greater than e's
							else if (TopX(*e, pt.y) >= pt.x) break;
						}
						else
						{
							if (IsOpen(*e) && !IsSamePolyType(*e, horz) && !IsHotEdge(*e))
							{
								if (TopX(*e, pt.y) < pt.x) break;
							}
							else if (TopX(*e, pt.y) <= pt.x) break;
						}
					}
				}

				pt = Point64(e->curr_x, horz.bot.y);

				if (is_left_to_right)
				{
					op = IntersectEdges(horz, *e, pt);
					SwapPositionsInAEL(horz, *e);
					// todo: check if op->pt == pt test is still needed
					// expect op != pt only after AddLocalMaxPoly when horz.outrec == nullptr
					if (IsHotEdge(horz) && op && !IsOpen(horz) && op->pt == pt)
						AddTrialHorzJoin(op);

					if (!IsHorizontal(*e) && TestJoinWithPrev1(*e))
					{
						op = AddOutPt(*e->prev_in_ael, pt);
						OutPt* op2 = AddOutPt(*e, pt);
						AddJoin(op, op2);
					}

					horz.curr_x = e->curr_x;
					e = horz.next_in_ael;
				}
				else
				{
					op = IntersectEdges(*e, horz, pt);
					SwapPositionsInAEL(*e, horz);

					if (IsHotEdge(horz) && op &&
						!IsOpen(horz) && op->pt == pt)
						AddTrialHorzJoin(op);

					if (!IsHorizontal(*e) && TestJoinWithNext1(*e))
					{
						op = AddOutPt(*e, pt);
						OutPt* op2 = AddOutPt(*e->next_in_ael, pt);
						AddJoin(op, op2);
					}

					horz.curr_x = e->curr_x;
					e = horz.prev_in_ael;
				}
			}

			//check if we've finished with (consecutive) horizontals ...
			if (horzIsOpen && IsOpenEnd(horz)) // ie open at top
			{
				if (IsHotEdge(horz))
				{
					AddOutPt(horz, horz.top);
					if (IsFront(horz))
						horz.outrec->front_edge = nullptr;
					else
						horz.outrec->back_edge = nullptr;
					horz.outrec = nullptr;
				}
				DeleteFromAEL(horz);
				return;
			}
			else if (NextVertex(horz)->pt.y != horz.top.y)
				break;

			//still more horizontals in bound to process ...
			if (IsHotEdge(horz))
				AddOutPt(horz, horz.top);
			UpdateEdgeIntoAEL(&horz);

			if (PreserveCollinear && !horzIsOpen && HorzIsSpike(horz))
				TrimHorz(horz, true);

			is_left_to_right =
				ResetHorzDirection(horz, max_pair, horz_left, horz_right);
		}

		if (IsHotEdge(horz))
		{
			op = AddOutPt(horz, horz.top);
			if (!IsOpen(horz))
				AddTrialHorzJoin(op);
		}
		else
			op = nullptr;

		if ((horzIsOpen && !IsOpenEnd(horz)) ||
			(!horzIsOpen && vertex_max != horz.vertex_top))
		{
			UpdateEdgeIntoAEL(&horz); // this is the end of an intermediate horiz.
			if (IsOpen(horz)) return;

			if (is_left_to_right && TestJoinWithNext1(horz))
			{
				OutPt* op2 = AddOutPt(*horz.next_in_ael, horz.bot);
				AddJoin(op, op2);
			}
			else if (!is_left_to_right && TestJoinWithPrev1(horz))
			{
				OutPt* op2 = AddOutPt(*horz.prev_in_ael, horz.bot);
				AddJoin(op2, op);
			}
		}
		else if (IsHotEdge(horz))
			AddLocalMaxPoly(horz, *max_pair, horz.top);
		else
		{
			DeleteFromAEL(*max_pair);
			DeleteFromAEL(horz);
		}
	}


	void ClipperBase::DoTopOfScanbeam(const int64_t y)
	{
		sel_ = nullptr;  // sel_ is reused to flag horizontals (see PushHorz below)
		Active* e = actives_;
		while (e)
		{
			//nb: 'e' will never be horizontal here
			if (e->top.y == y)
			{
				e->curr_x = e->top.x;
				if (IsMaxima(*e))
				{
					e = DoMaxima(*e);  // TOP OF BOUND (MAXIMA)
					continue;
				}
				else
				{
					//INTERMEDIATE VERTEX ...
					if (IsHotEdge(*e)) AddOutPt(*e, e->top);
					UpdateEdgeIntoAEL(e);
					if (IsHorizontal(*e))
						PushHorz(*e);  // horizontals are processed later
				}
			}
			else // i.e. not the top of the edge
				e->curr_x = TopX(*e, y);

			e = e->next_in_ael;
		}
	}


	Active* ClipperBase::DoMaxima(Active& e)
	{
		Active* next_e, * prev_e, * max_pair;
		prev_e = e.prev_in_ael;
		next_e = e.next_in_ael;
		if (IsOpenEnd(e))
		{
			if (IsHotEdge(e)) AddOutPt(e, e.top);
			if (!IsHorizontal(e))
			{
				if (IsHotEdge(e))
				{
					if (IsFront(e))
						e.outrec->front_edge = nullptr;
					else
						e.outrec->back_edge = nullptr;
					e.outrec = nullptr;
				}
				DeleteFromAEL(e);
			}
			return next_e;
		}
		else
		{
			max_pair = GetMaximaPair(e);
			if (!max_pair) return next_e;  // eMaxPair is horizontal
		}

		//only non-horizontal maxima here.
		//process any edges between maxima pair ...
		while (next_e != max_pair)
		{
			IntersectEdges(e, *next_e, e.top);
			SwapPositionsInAEL(e, *next_e);
			next_e = e.next_in_ael;
		}

		if (IsOpen(e))
		{
			if (IsHotEdge(e))
				AddLocalMaxPoly(e, *max_pair, e.top);
			DeleteFromAEL(*max_pair);
			DeleteFromAEL(e);
			return (prev_e ? prev_e->next_in_ael : actives_);
		}

		//here E.next_in_ael == ENext == EMaxPair ...
		if (IsHotEdge(e))
			AddLocalMaxPoly(e, *max_pair, e.top);

		DeleteFromAEL(e);
		DeleteFromAEL(*max_pair);
		return (prev_e ? prev_e->next_in_ael : actives_);
	}


	void ClipperBase::SafeDeleteOutPtJoiners(OutPt* op)
	{
		Joiner* joiner = op->joiner;
		if (!joiner) return;

		while (joiner)
		{
			if (joiner->idx < 0)
				DeleteTrialHorzJoin(op);
			else if (horz_joiners_)
			{
				if (OutPtInTrialHorzList(joiner->op1))
					DeleteTrialHorzJoin(joiner->op1);
				if (OutPtInTrialHorzList(joiner->op2))
					DeleteTrialHorzJoin(joiner->op2);
				DeleteJoin(joiner);
			}
			else
				DeleteJoin(joiner);
			joiner = op->joiner;
		}
	}


	Joiner* ClipperBase::GetHorzTrialParent(const OutPt* op)
	{
		Joiner* joiner = op->joiner;
		while (joiner)
		{
			if (joiner->op1 == op)
			{
				if (joiner->next1 && joiner->next1->idx < 0) return joiner;
				else joiner = joiner->next1;
			}
			else
			{
				if (joiner->next2 && joiner->next2->idx < 0) return joiner;
				else joiner = joiner->next1;
			}
		}
		return joiner;
	}


	bool ClipperBase::OutPtInTrialHorzList(OutPt* op)
	{
		return op->joiner && ((op->joiner->idx < 0) || GetHorzTrialParent(op));
	}


	void ClipperBase::AddTrialHorzJoin(OutPt* op)
	{
		//make sure 'op' isn't added more than once
		if (!op->outrec->is_open && !OutPtInTrialHorzList(op))
			horz_joiners_ = new Joiner(op, nullptr, horz_joiners_);
	}


	Joiner* FindTrialJoinParent(Joiner*& joiner, const OutPt* op)
	{
		Joiner* parent = joiner;
		while (parent)
		{
			if (op == parent->op1)
			{
				if (parent->next1 && parent->next1->idx < 0)
				{
					joiner = parent->next1;
					return parent;
				}
				parent = parent->next1;
			}
			else
			{
				if (parent->next2 && parent->next2->idx < 0)
				{
					joiner = parent->next2;
					return parent;
				}
				parent = parent->next2;
			}
		}
		return nullptr;
	}


	void ClipperBase::DeleteTrialHorzJoin(OutPt* op)
	{
		if (!horz_joiners_) return;

		Joiner* joiner = op->joiner;
		Joiner* parentH, * parentOp = nullptr;
		while (joiner)
		{
			if (joiner->idx < 0)
			{
				//first remove joiner from FHorzTrials
				if (joiner == horz_joiners_)
					horz_joiners_ = joiner->nextH;
				else
				{
					parentH = horz_joiners_;
					while (parentH->nextH != joiner)
						parentH = parentH->nextH;
					parentH->nextH = joiner->nextH;
				}

				//now remove joiner from op's joiner list
				if (!parentOp)
				{
					//joiner must be first one in list
					op->joiner = joiner->next1;
					delete joiner;
					joiner = op->joiner;
				}
				else
				{
					//the trial joiner isn't first
					if (op == parentOp->op1)
						parentOp->next1 = joiner->next1;
					else
						parentOp->next2 = joiner->next1;
					delete joiner;
					joiner = parentOp;
				}
			}
			else
			{
				//not a trial join so look further along the linked list
				parentOp = FindTrialJoinParent(joiner, op);
				if (!parentOp) break;
			}
			//loop in case there's more than one trial join
		}
	}


	inline bool GetHorzExtendedHorzSeg(OutPt*& op, OutPt*& op2)
	{
		OutRec* outrec = GetRealOutRec(op->outrec);
		op2 = op;
		if (outrec->front_edge)
		{
			while (op->prev != outrec->pts &&
				op->prev->pt.y == op->pt.y) op = op->prev;
			while (op2 != outrec->pts &&
				op2->next->pt.y == op2->pt.y) op2 = op2->next;
			return op2 != op;
		}
		else
		{
			while (op->prev != op2 && op->prev->pt.y == op->pt.y)
				op = op->prev;
			while (op2->next != op && op2->next->pt.y == op2->pt.y)
				op2 = op2->next;
			return op2 != op && op2->next != op;
		}
	}


	inline bool HorzEdgesOverlap(int64_t x1a, int64_t x1b, int64_t x2a, int64_t x2b)
	{
		const int64_t minOverlap = 2;
		if (x1a > x1b + minOverlap)
		{
			if (x2a > x2b + minOverlap)
				return !((x1a <= x2b) || (x2a <= x1b));
			else
				return !((x1a <= x2a) || (x2b <= x1b));
		}
		else if (x1b > x1a + minOverlap)
		{
			if (x2a > x2b + minOverlap)
				return !((x1b <= x2b) || (x2a <= x1a));
			else
				return !((x1b <= x2a) || (x2b <= x1a));
		}
		else
			return false;
	}


	inline bool ValueBetween(int64_t val, int64_t end1, int64_t end2)
	{
		//NB accommodates axis aligned between where end1 == end2
		return ((val != end1) == (val != end2)) &&
			((val > end1) == (val < end2));
	}


	inline bool ValueEqualOrBetween(int64_t val, int64_t end1, int64_t end2)
	{
		return (val == end1) || (val == end2) || ((val > end1) == (val < end2));
	}


	inline bool PointBetween(Point64 pt, Point64 corner1, Point64 corner2)
	{
		//NB points may not be collinear
		return ValueBetween(pt.x, corner1.x, corner2.x) &&
			ValueBetween(pt.y, corner1.y, corner2.y);
	}

	inline bool PointEqualOrBetween(Point64 pt, Point64 corner1, Point64 corner2)
	{
		//NB points may not be collinear
		return ValueEqualOrBetween(pt.x, corner1.x, corner2.x) &&
			ValueEqualOrBetween(pt.y, corner1.y, corner2.y);
	}


	Joiner* FindJoinParent(const Joiner* joiner, OutPt* op)
	{
		Joiner* result = op->joiner;
		for (; ; )
		{
			if (op == result->op1)
			{
				if (result->next1 == joiner) return result;
				else result = result->next1;
			}
			else
			{
				if (result->next2 == joiner) return result;
				else result = result->next2;
			}
		}
	}


	void ClipperBase::ConvertHorzTrialsToJoins()
	{
		while (horz_joiners_)
		{
			Joiner* joiner = horz_joiners_;
			horz_joiners_ = horz_joiners_->nextH;
			OutPt* op1a = joiner->op1;
			if (op1a->joiner == joiner)
			{
				op1a->joiner = joiner->next1;
			}
			else
			{
				Joiner* joinerParent = FindJoinParent(joiner, op1a);
				if (joinerParent->op1 == op1a)
					joinerParent->next1 = joiner->next1;
				else
					joinerParent->next2 = joiner->next1;
			}
			delete joiner;

			OutPt* op1b;
			if (!GetHorzExtendedHorzSeg(op1a, op1b))
			{
				CleanCollinear(op1a->outrec);
				continue;
			}

			bool joined = false;
			joiner = horz_joiners_;
			while (joiner)
			{
				OutPt* op2a = joiner->op1, * op2b;
				if (GetHorzExtendedHorzSeg(op2a, op2b) &&
					HorzEdgesOverlap(op1a->pt.x, op1b->pt.x, op2a->pt.x, op2b->pt.x))
				{
					//overlap found so promote to a 'real' join
					joined = true;
					if (op1a->pt == op2b->pt)
						AddJoin(op1a, op2b);
					else if (op1b->pt == op2a->pt)
						AddJoin(op1b, op2a);
					else if (op1a->pt == op2a->pt)
						AddJoin(op1a, op2a);
					else if (op1b->pt == op2b->pt)
						AddJoin(op1b, op2b);
					else if (ValueBetween(op1a->pt.x, op2a->pt.x, op2b->pt.x))
						AddJoin(op1a, InsertOp(op1a->pt, op2a));
					else if (ValueBetween(op1b->pt.x, op2a->pt.x, op2b->pt.x))
						AddJoin(op1b, InsertOp(op1b->pt, op2a));
					else if (ValueBetween(op2a->pt.x, op1a->pt.x, op1b->pt.x))
						AddJoin(op2a, InsertOp(op2a->pt, op1a));
					else if (ValueBetween(op2b->pt.x, op1a->pt.x, op1b->pt.x))
						AddJoin(op2b, InsertOp(op2b->pt, op1a));
					break;
				}
				joiner = joiner->nextH;
			}
			if (!joined)
				CleanCollinear(op1a->outrec);
		}
	}


	void ClipperBase::AddJoin(OutPt* op1, OutPt* op2)
	{
		if ((op1->outrec == op2->outrec) && ((op1 == op2) ||
			//unless op1.next or op1.prev crosses the start-end divide
			//don't waste time trying to join adjacent vertices
			((op1->next == op2) && (op1 != op1->outrec->pts)) ||
			((op2->next == op1) && (op2 != op1->outrec->pts)))) return;

		Joiner* j = new Joiner(op1, op2, nullptr);
		j->idx = static_cast<int>(joiner_list_.size());
		joiner_list_.push_back(j);
	}


	void ClipperBase::DeleteJoin(Joiner* joiner)
	{
		//This method deletes a single join, and it doesn't check for or
		//delete trial horz. joins. For that, use the following method.
		OutPt* op1 = joiner->op1, * op2 = joiner->op2;

		Joiner* parent_joiner;
		if (op1->joiner != joiner)
		{
			parent_joiner = FindJoinParent(joiner, op1);
			if (parent_joiner->op1 == op1)
				parent_joiner->next1 = joiner->next1;
			else
				parent_joiner->next2 = joiner->next1;
		}
		else
			op1->joiner = joiner->next1;

		if (op2->joiner != joiner)
		{
			parent_joiner = FindJoinParent(joiner, op2);
			if (parent_joiner->op1 == op2)
				parent_joiner->next1 = joiner->next2;
			else
				parent_joiner->next2 = joiner->next2;
		}
		else
			op2->joiner = joiner->next2;

		joiner_list_[joiner->idx] = nullptr;
		delete joiner;
	}


	void ClipperBase::ProcessJoinerList()
	{
		for (Joiner* j : joiner_list_)
		{
			if (!j) continue;
			if (succeeded_)
			{
				OutRec* outrec = ProcessJoin(j);
				CleanCollinear(outrec);
			}
			else
				delete j;
		}

		joiner_list_.resize(0);
	}


	bool CheckDisposeAdjacent(OutPt*& op, const OutPt* guard, OutRec& outRec)
	{
		bool result = false;
		while (op->prev != op)
		{
			if (op->pt == op->prev->pt && op != guard &&
				op->prev->joiner && !op->joiner)
			{
				if (op == outRec.pts) outRec.pts = op->prev;
				op = DisposeOutPt(op);
				op = op->prev;
			}
			else
				break;
		}

		while (op->next != op)
		{
			if (op->pt == op->next->pt && op != guard &&
				op->next->joiner && !op->joiner)
			{
				if (op == outRec.pts) outRec.pts = op->prev;
				op = DisposeOutPt(op);
				op = op->prev;
			}
			else
				break;
		}
		return result;
	}


	inline bool IsValidPath(OutPt* op)
	{
		return (op && op->next != op);
	}


	bool CollinearSegsOverlap(const Point64& seg1a, const Point64& seg1b,
		const Point64& seg2a, const Point64& seg2b)
	{
		//precondition: seg1 and seg2 are collinear
		if (seg1a.x == seg1b.x)
		{
			if (seg2a.x != seg1a.x || seg2a.x != seg2b.x) return false;
		}
		else if (seg1a.x < seg1b.x)
		{
			if (seg2a.x < seg2b.x)
			{
				if (seg2a.x >= seg1b.x || seg2b.x <= seg1a.x) return false;
			}
			else
			{
				if (seg2b.x >= seg1b.x || seg2a.x <= seg1a.x) return false;
			}
		}
		else
		{
			if (seg2a.x < seg2b.x)
			{
				if (seg2a.x >= seg1a.x || seg2b.x <= seg1b.x) return false;
			}
			else
			{
				if (seg2b.x >= seg1a.x || seg2a.x <= seg1b.x) return false;
			}
		}

		if (seg1a.y == seg1b.y)
		{
			if (seg2a.y != seg1a.y || seg2a.y != seg2b.y) return false;
		}
		else if (seg1a.y < seg1b.y)
		{
			if (seg2a.y < seg2b.y)
			{
				if (seg2a.y >= seg1b.y || seg2b.y <= seg1a.y) return false;
			}
			else
			{
				if (seg2b.y >= seg1b.y || seg2a.y <= seg1a.y) return false;
			}
		}
		else
		{
			if (seg2a.y < seg2b.y)
			{
				if (seg2a.y >= seg1a.y || seg2b.y <= seg1b.y) return false;
			}
			else
			{
				if (seg2b.y >= seg1a.y || seg2a.y <= seg1b.y) return false;
			}
		}
		return true;
	}

	OutRec* ClipperBase::ProcessJoin(Joiner* joiner)
	{
		OutPt* op1 = joiner->op1, * op2 = joiner->op2;
		OutRec* or1 = GetRealOutRec(op1->outrec);
		OutRec* or2 = GetRealOutRec(op2->outrec);
		DeleteJoin(joiner);

		if (or2->pts == nullptr) return or1;
		else if (!IsValidClosedPath(op2))
		{
			SafeDisposeOutPts(op2);
			return or1;
		}
		else if ((or1->pts == nullptr) || !IsValidClosedPath(op1))
		{
			SafeDisposeOutPts(op1);
			return or2;
		}
		else if (or1 == or2 &&
			((op1 == op2) || (op1->next == op2) || (op1->prev == op2))) return or1;

		CheckDisposeAdjacent(op1, op2, *or1);
		CheckDisposeAdjacent(op2, op1, *or2);
		if (op1->next == op2 || op2->next == op1) return or1;
		OutRec* result = or1;

		for (; ; )
		{
			if (!IsValidPath(op1) || !IsValidPath(op2) ||
				(or1 == or2 && (op1->prev == op2 || op1->next == op2))) return or1;

			if (op1->prev->pt == op2->next->pt ||
				((CrossProduct(op1->prev->pt, op1->pt, op2->next->pt) == 0) &&
					CollinearSegsOverlap(op1->prev->pt, op1->pt, op2->pt, op2->next->pt)))
			{
				if (or1 == or2)
				{
					//SPLIT REQUIRED
					//make sure op1.prev and op2.next match positions
					//by inserting an extra vertex if needed
					if (op1->prev->pt != op2->next->pt)
					{
						if (PointEqualOrBetween(op1->prev->pt, op2->pt, op2->next->pt))
							op2->next = InsertOp(op1->prev->pt, op2);
						else
							op1->prev = InsertOp(op2->next->pt, op1->prev);
					}

					//current              to     new
					//op1.p[opA] >>> op1   ...    opA \   / op1
					//op2.n[opB] <<< op2   ...    opB /   \ op2
					OutPt* opA = op1->prev, * opB = op2->next;
					opA->next = opB;
					opB->prev = opA;
					op1->prev = op2;
					op2->next = op1;
					CompleteSplit(op1, opA, *or1);
				}
				else
				{
					//JOIN, NOT SPLIT
					OutPt* opA = op1->prev, * opB = op2->next;
					opA->next = opB;
					opB->prev = opA;
					op1->prev = op2;
					op2->next = op1;

					//SafeDeleteOutPtJoiners(op2);
					//DisposeOutPt(op2);

					if (or1->idx < or2->idx)
					{
						or1->pts = op1;
						or2->pts = nullptr;
						if (or1->owner && (!or2->owner ||
							or2->owner->idx < or1->owner->idx))
								or1->owner = or2->owner;
						or2->owner = or1;
					}
					else
					{
						result = or2;
						or2->pts = op1;
						or1->pts = nullptr;
						if (or2->owner && (!or1->owner ||
							or1->owner->idx < or2->owner->idx))
								or2->owner = or1->owner;
						or1->owner = or2;
					}
				}
				break;
			}
			else if (op1->next->pt == op2->prev->pt ||
				((CrossProduct(op1->next->pt, op2->pt, op2->prev->pt) == 0) &&
					CollinearSegsOverlap(op1->next->pt, op1->pt, op2->pt, op2->prev->pt)))
			{
				if (or1 == or2)
				{
					//SPLIT REQUIRED
					//make sure op2.prev and op1.next match positions
					//by inserting an extra vertex if needed
					if (op2->prev->pt != op1->next->pt)
					{
						if (PointEqualOrBetween(op2->prev->pt, op1->pt, op1->next->pt))
							op1->next = InsertOp(op2->prev->pt, op1);
						else
							op2->prev = InsertOp(op1->next->pt, op2->prev);
					}

					//current              to     new
					//op2.p[opA] >>> op2   ...    opA \   / op2
					//op1.n[opB] <<< op1   ...    opB /   \ op1
					OutPt* opA = op2->prev, * opB = op1->next;
					opA->next = opB;
					opB->prev = opA;
					op2->prev = op1;
					op1->next = op2;
					CompleteSplit(op1, opA, *or1);
				}
				else
				{
					//JOIN, NOT SPLIT
					OutPt* opA = op1->next, * opB = op2->prev;
					opA->prev = opB;
					opB->next = opA;
					op1->next = op2;
					op2->prev = op1;

					//SafeDeleteOutPtJoiners(op2);
					//DisposeOutPt(op2);

					if (or1->idx < or2->idx)
					{
						or1->pts = op1;
						or2->pts = nullptr;
						if (or1->owner && (!or2->owner ||
							or2->owner->idx < or1->owner->idx))
								or1->owner = or2->owner;
						or2->owner = or1;
					}
					else
					{
						result = or2;
						or2->pts = op1;
						or1->pts = nullptr;
						if (or2->owner && (!or1->owner ||
							or1->owner->idx < or2->owner->idx))
								or2->owner = or1->owner;
						or1->owner = or2;
					}
				}
				break;
			}
			else if (PointBetween(op1->next->pt, op2->pt, op2->prev->pt) &&
				DistanceFromLineSqrd(op1->next->pt, op2->pt, op2->prev->pt) < 2.01)
			{
				InsertOp(op1->next->pt, op2->prev);
				continue;
			}
			else if (PointBetween(op2->next->pt, op1->pt, op1->prev->pt) &&
				DistanceFromLineSqrd(op2->next->pt, op1->pt, op1->prev->pt) < 2.01)
			{
				InsertOp(op2->next->pt, op1->prev);
				continue;
			}
			else if (PointBetween(op1->prev->pt, op2->pt, op2->next->pt) &&
				DistanceFromLineSqrd(op1->prev->pt, op2->pt, op2->next->pt) < 2.01)
			{
				InsertOp(op1->prev->pt, op2);
				continue;
			}
			else if (PointBetween(op2->prev->pt, op1->pt, op1->next->pt) &&
				DistanceFromLineSqrd(op2->prev->pt, op1->pt, op1->next->pt) < 2.01)
			{
				InsertOp(op2->prev->pt, op1);
				continue;
			}

			//something odd needs tidying up
			if (CheckDisposeAdjacent(op1, op2, *or1)) continue;
			else if (CheckDisposeAdjacent(op2, op1, *or1)) continue;
			else if (op1->prev->pt != op2->next->pt &&
				(DistanceSqr(op1->prev->pt, op2->next->pt) < 2.01))
			{
				op1->prev->pt = op2->next->pt;
				continue;
			}
			else if (op1->next->pt != op2->prev->pt &&
				(DistanceSqr(op1->next->pt, op2->prev->pt) < 2.01))
			{
				op2->prev->pt = op1->next->pt;
				continue;
			}
			else
			{
				//OK, there doesn't seem to be a way to join after all
				//so just tidy up the polygons
				or1->pts = op1;
				if (or2 != or1)
				{
					or2->pts = op2;
					CleanCollinear(or2);
				}
				break;
			}
		}
		return result;

	}

	inline bool Path1InsidePath2(const OutRec* or1, const OutRec* or2)
	{
		PointInPolygonResult result = PointInPolygonResult::IsOn;
		OutPt* op = or1->pts;
		do
		{
			result = PointInPolygon(op->pt, or2->path);
			if (result != PointInPolygonResult::IsOn) break;
			op = op->next;
		} while (op != or1->pts);
		if (result == PointInPolygonResult::IsOn)
			return Area(op) < Area(or2->pts);
		else
			return result == PointInPolygonResult::IsInside;
	}

	inline Rect64 GetBounds(const Path64& path)
	{
		if (path.empty()) return Rect64();
		Rect64 result = invalid_rect;
		for(const Point64& pt : path)
		{
			if (pt.x < result.left) result.left = pt.x;
			if (pt.x > result.right) result.right = pt.x;
			if (pt.y < result.top) result.top = pt.y;
			if (pt.y > result.bottom) result.bottom = pt.y;
		}
		return result;
	}

	bool BuildPath64(OutPt* op, bool reverse, bool isOpen, Path64& path)
	{
		if (op->next == op || (!isOpen && op->next == op->prev))
			return false;

		path.resize(0);
		Point64 lastPt;
		OutPt* op2;
		if (reverse)
		{
			lastPt = op->pt;
			op2 = op->prev;
		}
		else
		{
			op = op->next;
			lastPt = op->pt;
			op2 = op->next;
		}
		path.push_back(lastPt);

		while (op2 != op)
		{
			if (op2->pt != lastPt)
			{
				lastPt = op2->pt;
				path.push_back(lastPt);
			}
			if (reverse)
				op2 = op2->prev;
			else
				op2 = op2->next;
		}

		if (path.size() == 3 && IsVerySmallTriangle(*op2)) return false;
		else return true;
	}

	bool ClipperBase::DeepCheckOwner(OutRec* outrec, OutRec* owner)
	{
		if (owner->bounds.IsEmpty()) owner->bounds = GetBounds(owner->path);
		bool is_inside_owner_bounds = owner->bounds.Contains(outrec->bounds);

		// while looking for the correct owner, check the owner's
		// splits **before** checking the owner itself because
		// splits can occur internally, and checking the owner
		// first would miss the inner split's true ownership
		if (owner->splits)
		{
			for (OutRec* split : *owner->splits)
			{
				split = GetRealOutRec(split);
				if (!split || split->idx <= owner->idx || split == outrec) continue;

				if (split->splits && DeepCheckOwner(outrec, split)) return true;

				if (!split->path.size())
					BuildPath64(split->pts, ReverseSolution, false, split->path);
				if (split->bounds.IsEmpty()) split->bounds = GetBounds(split->path);

				if (split->bounds.Contains(outrec->bounds) &&
					Path1InsidePath2(outrec, split))
				{
					outrec->owner = split;
					return true;
				}
			}
		}

		// only continue past here when not inside recursion
		if (owner != outrec->owner) return false;

		for (;;)
		{
			if (is_inside_owner_bounds && Path1InsidePath2(outrec, outrec->owner))
				return true;
			// otherwise keep trying with owner's owner
			outrec->owner = outrec->owner->owner;
			if (!outrec->owner) return true; // true or false
			is_inside_owner_bounds = outrec->owner->bounds.Contains(outrec->bounds);
		}
	}

	void Clipper64::BuildPaths64(Paths64& solutionClosed, Paths64* solutionOpen)
	{
		solutionClosed.resize(0);
		solutionClosed.reserve(outrec_list_.size());
		if (solutionOpen)
		{
			solutionOpen->resize(0);
			solutionOpen->reserve(outrec_list_.size());
		}

		for (OutRec* outrec : outrec_list_)
		{
			if (outrec->pts == nullptr) continue;

			Path64 path;
			if (solutionOpen && outrec->is_open)
			{
				if (BuildPath64(outrec->pts, ReverseSolution, true, path))
					solutionOpen->emplace_back(std::move(path));
			}
			else
			{
				//closed paths should always return a Positive orientation
				if (BuildPath64(outrec->pts, ReverseSolution, false, path))
					solutionClosed.emplace_back(std::move(path));
			}
		}
	}

	void Clipper64::BuildTree64(PolyPath64& polytree, Paths64& open_paths)
	{
		polytree.Clear();
		open_paths.resize(0);
		if (has_open_paths_)
			open_paths.reserve(outrec_list_.size());

		for (OutRec* outrec : outrec_list_)
		{
			if (!outrec || !outrec->pts) continue;
			if (outrec->is_open)
			{
				Path64 path;
				if (BuildPath64(outrec->pts, ReverseSolution, true, path))
					open_paths.push_back(path);
				continue;
			}

			if (!BuildPath64(outrec->pts, ReverseSolution, false, outrec->path))
				continue;
			if (outrec->bounds.IsEmpty()) outrec->bounds = GetBounds(outrec->path);
			outrec->owner = GetRealOutRec(outrec->owner);
			if (outrec->owner) DeepCheckOwner(outrec, outrec->owner);

			// swap the order when a child preceeds its owner
			// (because owners must preceed children in polytrees)
			if (outrec->owner && outrec->idx < outrec->owner->idx)
			{
				OutRec* tmp = outrec->owner;
				outrec_list_[outrec->owner->idx] = outrec;
				outrec_list_[outrec->idx] = tmp;
				size_t tmp_idx = outrec->idx;
				outrec->idx = tmp->idx;
				tmp->idx = tmp_idx;
				outrec = tmp;
				outrec->owner = GetRealOutRec(outrec->owner);
				BuildPath64(outrec->pts, ReverseSolution, false, outrec->path);
				if (outrec->bounds.IsEmpty()) outrec->bounds = GetBounds(outrec->path);
				if (outrec->owner) DeepCheckOwner(outrec, outrec->owner);
			}

			PolyPath* owner_polypath;
			if (outrec->owner && outrec->owner->polypath)
				owner_polypath = outrec->owner->polypath;
			else
				owner_polypath = &polytree;
			outrec->polypath = owner_polypath->AddChild(outrec->path);
		}
	}

	bool BuildPathD(OutPt* op, bool reverse, bool isOpen, PathD& path, double inv_scale)
	{
		if (op->next == op || (!isOpen && op->next == op->prev)) return false;
		path.resize(0);
		Point64 lastPt;
		OutPt* op2;
		if (reverse)
		{
			lastPt = op->pt;
			op2 = op->prev;
		}
		else
		{
			op = op->next;
			lastPt = op->pt;
			op2 = op->next;
		}
		path.push_back(PointD(lastPt.x * inv_scale, lastPt.y * inv_scale));

		while (op2 != op)
		{
			if (op2->pt != lastPt)
			{
				lastPt = op2->pt;
				path.push_back(PointD(lastPt.x * inv_scale, lastPt.y * inv_scale));
			}
			if (reverse)
				op2 = op2->prev;
			else
				op2 = op2->next;
		}
		if (path.size() == 3 && IsVerySmallTriangle(*op2)) return false;
		return true;
	}

	void ClipperD::BuildPathsD(PathsD& solutionClosed, PathsD* solutionOpen)
	{
		solutionClosed.resize(0);
		solutionClosed.reserve(outrec_list_.size());
		if (solutionOpen)
		{
			solutionOpen->resize(0);
			solutionOpen->reserve(outrec_list_.size());
		}

		for (OutRec* outrec : outrec_list_)
		{
			if (outrec->pts == nullptr) continue;

			PathD path;
			if (solutionOpen && outrec->is_open)
			{
				if (BuildPathD(outrec->pts, ReverseSolution, true, path, invScale_))
					solutionOpen->emplace_back(std::move(path));
			}
			else
			{
				//closed paths should always return a Positive orientation
				if (BuildPathD(outrec->pts, ReverseSolution, false, path, invScale_))
					solutionClosed.emplace_back(std::move(path));
			}
		}
	}

	void ClipperD::BuildTreeD(PolyPathD& polytree, PathsD& open_paths)
	{
		polytree.Clear();
		open_paths.resize(0);
		if (has_open_paths_)
			open_paths.reserve(outrec_list_.size());

		for (OutRec* outrec : outrec_list_)
		{
			if (!outrec || !outrec->pts) continue;
			if (outrec->is_open)
			{
				PathD path;
				if (BuildPathD(outrec->pts, ReverseSolution, true, path, invScale_))
					open_paths.push_back(path);
				continue;
			}

			if (!BuildPath64(outrec->pts, ReverseSolution, false, outrec->path))
				continue;
			if (outrec->bounds.IsEmpty()) outrec->bounds = GetBounds(outrec->path);
			outrec->owner = GetRealOutRec(outrec->owner);
			if (outrec->owner) DeepCheckOwner(outrec, outrec->owner);

			// swap the order when a child preceeds its owner
			// (because owners must preceed children in polytrees)
			if (outrec->owner && outrec->idx < outrec->owner->idx)
			{
				OutRec* tmp = outrec->owner;
				outrec_list_[outrec->owner->idx] = outrec;
				outrec_list_[outrec->idx] = tmp;
				size_t tmp_idx = outrec->idx;
				outrec->idx = tmp->idx;
				tmp->idx = tmp_idx;
				outrec = tmp;
				outrec->owner = GetRealOutRec(outrec->owner);
				BuildPath64(outrec->pts, ReverseSolution, false, outrec->path);
				if (outrec->bounds.IsEmpty()) outrec->bounds = GetBounds(outrec->path);
				if (outrec->owner) DeepCheckOwner(outrec, outrec->owner);
			}

			PolyPath* owner_polypath;
			if (outrec->owner && outrec->owner->polypath)
				owner_polypath = outrec->owner->polypath;
			else
				owner_polypath = &polytree;
			outrec->polypath = owner_polypath->AddChild(outrec->path);
		}
	}

}  // namespace clipper2lib
