/*******************************************************************************
* Author    :  Angus Johnson                                                   *
* Date      :  4 November 2022                                                 *
* Website   :  http://www.angusj.com                                           *
* Copyright :  Angus Johnson 2010-2022                                         *
* Purpose   :  This is the main polygon clipping module                        *
* License   :  http://www.boost.org/LICENSE_1_0.txt                            *
*******************************************************************************/

#ifndef CLIPPER_ENGINE_H
#define CLIPPER_ENGINE_H

constexpr auto CLIPPER2_VERSION = "1.0.6";

#include <cstdlib>
#include <queue>
#include <stdexcept>
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>
#include "clipper.core.h"

namespace Clipper2Lib {

	struct Scanline;
	struct IntersectNode;
	struct Active;
	struct Vertex;
	struct LocalMinima;
	struct OutRec;
	struct Joiner;

	//Note: all clipping operations except for Difference are commutative.
	enum class ClipType { None, Intersection, Union, Difference, Xor };

	enum class PathType { Subject, Clip };

	enum class VertexFlags : uint32_t {
		None = 0, OpenStart = 1, OpenEnd = 2, LocalMax = 4, LocalMin = 8
	};

	constexpr enum VertexFlags operator &(enum VertexFlags a, enum VertexFlags b)
	{
		return (enum VertexFlags)(uint32_t(a) & uint32_t(b));
	}

	constexpr enum VertexFlags operator |(enum VertexFlags a, enum VertexFlags b)
	{
		return (enum VertexFlags)(uint32_t(a) | uint32_t(b));
	}

	struct Vertex {
		Point64 pt;
		Vertex* next = nullptr;
		Vertex* prev = nullptr;
		VertexFlags flags = VertexFlags::None;
	};

	struct OutPt {
		Point64 pt;
		OutPt*	next = nullptr;
		OutPt*	prev = nullptr;
		OutRec* outrec;
		Joiner* joiner = nullptr;

		OutPt(const Point64& pt_, OutRec* outrec_): pt(pt_), outrec(outrec_) {
			next = this;
			prev = this;
		}
	};

	class PolyPath;
	class PolyPath64;
	class PolyPathD;
	using PolyTree64 = PolyPath64;
	using PolyTreeD = PolyPathD;

	struct OutRec;
	typedef std::vector<OutRec*> OutRecList;

	//OutRec: contains a path in the clipping solution. Edges in the AEL will
	//have OutRec pointers assigned when they form part of the clipping solution.
	struct OutRec {
		size_t idx = 0;
		OutRec* owner = nullptr;
		OutRecList* splits = nullptr;
		Active* front_edge = nullptr;
		Active* back_edge = nullptr;
		OutPt* pts = nullptr;
		PolyPath* polypath = nullptr;
		Rect64 bounds = {};
		Path64 path;
		bool is_open = false;
		~OutRec() { if (splits) delete splits; };
	};

	///////////////////////////////////////////////////////////////////
	//Important: UP and DOWN here are premised on Y-axis positive down
	//displays, which is the orientation used in Clipper's development.
	///////////////////////////////////////////////////////////////////

	struct Active {
		Point64 bot;
		Point64 top;
		int64_t curr_x = 0;		//current (updated at every new scanline)
		double dx = 0.0;
		int wind_dx = 1;			//1 or -1 depending on winding direction
		int wind_cnt = 0;
		int wind_cnt2 = 0;		//winding count of the opposite polytype
		OutRec* outrec = nullptr;
		//AEL: 'active edge list' (Vatti's AET - active edge table)
		//     a linked list of all edges (from left to right) that are present
		//     (or 'active') within the current scanbeam (a horizontal 'beam' that
		//     sweeps from bottom to top over the paths in the clipping operation).
		Active* prev_in_ael = nullptr;
		Active* next_in_ael = nullptr;
		//SEL: 'sorted edge list' (Vatti's ST - sorted table)
		//     linked list used when sorting edges into their new positions at the
		//     top of scanbeams, but also (re)used to process horizontals.
		Active* prev_in_sel = nullptr;
		Active* next_in_sel = nullptr;
		Active* jump = nullptr;
		Vertex* vertex_top = nullptr;
		LocalMinima* local_min = nullptr;  // the bottom of an edge 'bound' (also Vatti)
		bool is_left_bound = false;
	};

	struct LocalMinima {
		Vertex* vertex;
		PathType polytype;
		bool is_open;
		LocalMinima(Vertex* v, PathType pt, bool open) :
			vertex(v), polytype(pt), is_open(open){}
	};

	struct IntersectNode {
		Point64 pt;
		Active* edge1;
		Active* edge2;
		IntersectNode() : pt(Point64(0, 0)), edge1(NULL), edge2(NULL) {}
			IntersectNode(Active* e1, Active* e2, Point64& pt_) :
			pt(pt_), edge1(e1), edge2(e2)
		{
		}
	};

#ifdef USINGZ
		typedef std::function<void(const Point64& e1bot, const Point64& e1top,
		const Point64& e2bot, const Point64& e2top, Point64& pt)> ZCallback64;

	typedef std::function<void(const PointD& e1bot, const PointD& e1top,
		const PointD& e2bot, const PointD& e2top, PointD& pt)> ZCallbackD;
#endif

	// ClipperBase -------------------------------------------------------------

	class ClipperBase {
	private:
		ClipType cliptype_ = ClipType::None;
		FillRule fillrule_ = FillRule::EvenOdd;
		FillRule fillpos = FillRule::Positive;
		int64_t bot_y_ = 0;
		bool minima_list_sorted_ = false;
		bool using_polytree_ = false;
		Active* actives_ = nullptr;
		Active *sel_ = nullptr;
		Joiner *horz_joiners_ = nullptr;
		std::vector<LocalMinima*> minima_list_;		//pointers in case of memory reallocs
		std::vector<LocalMinima*>::iterator current_locmin_iter_;
		std::vector<Vertex*> vertex_lists_;
		std::priority_queue<int64_t> scanline_list_;
		std::vector<IntersectNode> intersect_nodes_;
		std::vector<Joiner*> joiner_list_;				//pointers in case of memory reallocs
		void Reset();
		void InsertScanline(int64_t y);
		bool PopScanline(int64_t &y);
		bool PopLocalMinima(int64_t y, LocalMinima *&local_minima);
		void DisposeAllOutRecs();
		void DisposeVerticesAndLocalMinima();
		void DeleteEdges(Active*& e);
		void AddLocMin(Vertex &vert, PathType polytype, bool is_open);
		bool IsContributingClosed(const Active &e) const;
		inline bool IsContributingOpen(const Active &e) const;
		void SetWindCountForClosedPathEdge(Active &edge);
		void SetWindCountForOpenPathEdge(Active &e);
		void InsertLocalMinimaIntoAEL(int64_t bot_y);
		void InsertLeftEdge(Active &e);
		inline void PushHorz(Active &e);
		inline bool PopHorz(Active *&e);
		inline OutPt* StartOpenPath(Active &e, const Point64& pt);
		inline void UpdateEdgeIntoAEL(Active *e);
		OutPt* IntersectEdges(Active &e1, Active &e2, const Point64& pt);
		inline void DeleteFromAEL(Active &e);
		inline void AdjustCurrXAndCopyToSEL(const int64_t top_y);
		void DoIntersections(const int64_t top_y);
		void AddNewIntersectNode(Active &e1, Active &e2, const int64_t top_y);
		bool BuildIntersectList(const int64_t top_y);
		void ProcessIntersectList();
		void SwapPositionsInAEL(Active& edge1, Active& edge2);
		OutPt* AddOutPt(const Active &e, const Point64& pt);
		OutPt* AddLocalMinPoly(Active &e1, Active &e2,
			const Point64& pt, bool is_new = false);
		OutPt* AddLocalMaxPoly(Active &e1, Active &e2, const Point64& pt);
		void DoHorizontal(Active &horz);
		bool ResetHorzDirection(const Active &horz, const Active *max_pair,
			int64_t &horz_left, int64_t &horz_right);
		void DoTopOfScanbeam(const int64_t top_y);
		Active *DoMaxima(Active &e);
		void JoinOutrecPaths(Active &e1, Active &e2);
		void CompleteSplit(OutPt* op1, OutPt* op2, OutRec& outrec);
		bool ValidateClosedPathEx(OutPt*& outrec);
		void CleanCollinear(OutRec* outrec);
		void FixSelfIntersects(OutRec* outrec);
		void DoSplitOp(OutRec* outRec, OutPt* splitOp);
		Joiner* GetHorzTrialParent(const OutPt* op);
		bool OutPtInTrialHorzList(OutPt* op);
		void SafeDisposeOutPts(OutPt*& op);
		void SafeDeleteOutPtJoiners(OutPt* op);
		void AddTrialHorzJoin(OutPt* op);
		void DeleteTrialHorzJoin(OutPt* op);
		void ConvertHorzTrialsToJoins();
		void AddJoin(OutPt* op1, OutPt* op2);
		void DeleteJoin(Joiner* joiner);
		void ProcessJoinerList();
		OutRec* ProcessJoin(Joiner* joiner);
	protected:
		bool has_open_paths_ = false;
		bool succeeded_ = true;
		std::vector<OutRec*> outrec_list_; //pointers in case list memory reallocated
		bool ExecuteInternal(ClipType ct, FillRule ft, bool use_polytrees);
		bool DeepCheckOwner(OutRec* outrec, OutRec* owner);
#ifdef USINGZ
		ZCallback64 zCallback_ = nullptr;
		void SetZ(const Active& e1, const Active& e2, Point64& pt);
#endif
		void CleanUp();  // unlike Clear, CleanUp preserves added paths
		void AddPath(const Path64& path, PathType polytype, bool is_open);
		void AddPaths(const Paths64& paths, PathType polytype, bool is_open);
	public:
		virtual ~ClipperBase();
		bool PreserveCollinear = true;
		bool ReverseSolution = false;
		void Clear();
	};

	// PolyPath / PolyTree --------------------------------------------------------

	//PolyTree: is intended as a READ-ONLY data structure for CLOSED paths returned
	//by clipping operations. While this structure is more complex than the
	//alternative Paths structure, it does preserve path 'ownership' - ie those
	//paths that contain (or own) other paths. This will be useful to some users.

	class PolyPath {
	protected:
		PolyPath* parent_;
	public:
		PolyPath(PolyPath* parent = nullptr): parent_(parent){}
		virtual ~PolyPath() { Clear(); };
		//https://en.cppreference.com/w/cpp/language/rule_of_three
		PolyPath(const PolyPath&) = delete;
		PolyPath& operator=(const PolyPath&) = delete;

		unsigned Level() const
		{
			unsigned result = 0;
			const PolyPath* p = parent_;
			while (p) { ++result; p = p->parent_; }
			return result;
		}

		virtual PolyPath* AddChild(const Path64& path) = 0;

		virtual void Clear() {};
		virtual size_t Count() const { return 0; }

		const PolyPath* Parent() const { return parent_; }

		bool IsHole() const
		{
			const PolyPath* pp = parent_;
			bool is_hole = pp;
			while (pp) {
				is_hole = !is_hole;
				pp = pp->parent_;
			}
			return is_hole;
		}
	};

	class PolyPath64 : public PolyPath {
	private:
		std::vector<PolyPath64*> childs_;
		Path64 polygon_;
		typedef typename std::vector<PolyPath64*>::const_iterator pp64_itor;
	public:
		PolyPath64(PolyPath64* parent = nullptr) : PolyPath(parent) {}
		PolyPath64* operator [] (size_t index) { return static_cast<PolyPath64*>(childs_[index]); }
		pp64_itor begin() const { return childs_.cbegin(); }
		pp64_itor end() const { return childs_.cend(); }

		PolyPath64* AddChild(const Path64& path) override
		{
			PolyPath64* result = new PolyPath64(this);
			childs_.push_back(result);
			result->polygon_ = path;
			return result;
		}

		void Clear() override
		{
			for (const PolyPath64* child : childs_) delete child;
			childs_.resize(0);
		}

		size_t Count() const  override
		{
			return childs_.size();
		}

		const Path64& Polygon() const { return polygon_; };

		double Area() const
		{
			double result = Clipper2Lib::Area<int64_t>(polygon_);
			for (const PolyPath64* child : childs_)
				result += child->Area();
			return result;
		}

		friend std::ostream& operator << (std::ostream& outstream, const PolyPath64& polypath)
		{
			const size_t level_indent = 4;
			const size_t coords_per_line = 4;
			const size_t last_on_line = coords_per_line - 1;
			unsigned level = polypath.Level();
			if (level > 0)
			{
				std::string level_padding;
				level_padding.insert(0, (level - 1) * level_indent, ' ');
				std::string caption = polypath.IsHole() ? "Hole " : "Outer Polygon ";
				std::string childs = polypath.Count() == 1 ? " child" : " children";
				outstream << level_padding.c_str() << caption << "with " << polypath.Count() << childs << std::endl;
				outstream << level_padding;
				size_t i = 0, highI = polypath.Polygon().size() - 1;
				for (; i < highI; ++i)
				{
					outstream << polypath.Polygon()[i] << ' ';
					if ((i % coords_per_line) == last_on_line)
						outstream << std::endl << level_padding;
				}
				if (highI > 0) outstream << polypath.Polygon()[i];
				outstream << std::endl;
			}
			for (auto child : polypath)
				outstream << *child;
			return outstream;
		}

	};

	class PolyPathD : public PolyPath {
	private:
		std::vector<PolyPathD*> childs_;
		double inv_scale_;
		PathD polygon_;
		typedef typename std::vector<PolyPathD*>::const_iterator ppD_itor;
	public:
		PolyPathD(PolyPathD* parent = nullptr) : PolyPath(parent)
		{
			inv_scale_ = parent ? parent->inv_scale_ : 1.0;
		}
		PolyPathD* operator [] (size_t index)
		{
			return static_cast<PolyPathD*>(childs_[index]);
		}
		ppD_itor begin() const { return childs_.cbegin(); }
		ppD_itor end() const { return childs_.cend(); }

		void SetInvScale(double value) { inv_scale_ = value; }
		double InvScale() { return inv_scale_; }
		PolyPathD* AddChild(const Path64& path) override
		{
			PolyPathD* result = new PolyPathD(this);
			childs_.push_back(result);
			result->polygon_ = ScalePath<double, int64_t>(path, inv_scale_);
			return result;
		}

		void Clear() override
		{
			for (const PolyPathD* child : childs_) delete child;
			childs_.resize(0);
		}

		size_t Count() const  override
		{
			return childs_.size();
		}

		const PathD& Polygon() const { return polygon_; };

		double Area() const
		{
			double result = Clipper2Lib::Area<double>(polygon_);
			for (const PolyPathD* child : childs_)
				result += child->Area();
			return result;
		}
	};

	class Clipper64 : public ClipperBase
	{
	private:
		void BuildPaths64(Paths64& solutionClosed, Paths64* solutionOpen);
		void BuildTree64(PolyPath64& polytree, Paths64& open_paths);
	public:
#ifdef USINGZ
		void SetZCallback(ZCallback64 cb) { zCallback_ = cb; }
#endif

		void AddSubject(const Paths64& subjects)
		{
			AddPaths(subjects, PathType::Subject, false);
		}
		void AddOpenSubject(const Paths64& open_subjects)
		{
			AddPaths(open_subjects, PathType::Subject, true);
		}
		void AddClip(const Paths64& clips)
		{
			AddPaths(clips, PathType::Clip, false);
		}

		bool Execute(ClipType clip_type,
			FillRule fill_rule, Paths64& closed_paths)
		{
			Paths64 dummy;
			return Execute(clip_type, fill_rule, closed_paths, dummy);
		}

		bool Execute(ClipType clip_type, FillRule fill_rule,
			Paths64& closed_paths, Paths64& open_paths)
		{
			closed_paths.clear();
			open_paths.clear();
			if (ExecuteInternal(clip_type, fill_rule, false))
				BuildPaths64(closed_paths, &open_paths);
			CleanUp();
			return succeeded_;
		}

		bool Execute(ClipType clip_type, FillRule fill_rule, PolyTree64& polytree)
		{
			Paths64 dummy;
			return Execute(clip_type, fill_rule, polytree, dummy);
		}

		bool Execute(ClipType clip_type,
			FillRule fill_rule, PolyTree64& polytree, Paths64& open_paths)
		{
			if (ExecuteInternal(clip_type, fill_rule, true))
			{
				open_paths.clear();
				polytree.Clear();
				BuildTree64(polytree, open_paths);
			}
			CleanUp();
			return succeeded_;
		}
	};

	class ClipperD : public ClipperBase {
	private:
		double scale_ = 1.0, invScale_ = 1.0;
#ifdef USINGZ
		ZCallbackD zCallback_ = nullptr;
#endif
		void BuildPathsD(PathsD& solutionClosed, PathsD* solutionOpen);
		void BuildTreeD(PolyPathD& polytree, PathsD& open_paths);
	public:
		explicit ClipperD(int precision = 2) : ClipperBase()
		{
			CheckPrecision(precision);
			// to optimize scaling / descaling precision
			// set the scale to a power of double's radix (2) (#25)
			scale_ = std::pow(std::numeric_limits<double>::radix,
				std::ilogb(std::pow(10, precision)) + 1);
			invScale_ = 1 / scale_;
		}

#ifdef USINGZ
		void SetZCallback(ZCallbackD cb) { zCallback_ = cb; };

		void ZCB(const Point64& e1bot, const Point64& e1top,
			const Point64& e2bot, const Point64& e2top, Point64& pt)
		{
			// de-scale (x & y)
			// temporarily convert integers to their initial float values
			// this will slow clipping marginally but will make it much easier
			// to understand the coordinates passed to the callback function
			PointD tmp = PointD(pt) * invScale_;
			PointD e1b = PointD(e1bot) * invScale_;
			PointD e1t = PointD(e1top) * invScale_;
			PointD e2b = PointD(e2bot) * invScale_;
			PointD e2t = PointD(e2top) * invScale_;
			zCallback_(e1b,e1t, e2b, e2t, tmp);
			pt.z = tmp.z; // only update 'z'
		};

		void CheckCallback()
		{
			if(zCallback_)
				// if the user defined float point callback has been assigned
				// then assign the proxy callback function
				ClipperBase::zCallback_ =
					std::bind(&ClipperD::ZCB, this, std::placeholders::_1,
					std::placeholders::_2, std::placeholders::_3,
					std::placeholders::_4, std::placeholders::_5);
			else
				ClipperBase::zCallback_ = nullptr;
		}

#endif

		void AddSubject(const PathsD& subjects)
		{
			AddPaths(ScalePaths<int64_t, double>(subjects, scale_), PathType::Subject, false);
		}

		void AddOpenSubject(const PathsD& open_subjects)
		{
			AddPaths(ScalePaths<int64_t, double>(open_subjects, scale_), PathType::Subject, true);
		}

		void AddClip(const PathsD& clips)
		{
			AddPaths(ScalePaths<int64_t, double>(clips, scale_), PathType::Clip, false);
		}

		bool Execute(ClipType clip_type, FillRule fill_rule, PathsD& closed_paths)
		{
			PathsD dummy;
			return Execute(clip_type, fill_rule, closed_paths, dummy);
		}

		bool Execute(ClipType clip_type,
			FillRule fill_rule, PathsD& closed_paths, PathsD& open_paths)
		{
#ifdef USINGZ
			CheckCallback();
#endif
			if (ExecuteInternal(clip_type, fill_rule, false))
			{
				BuildPathsD(closed_paths, &open_paths);
			}
			CleanUp();
			return succeeded_;
		}

		bool Execute(ClipType clip_type, FillRule fill_rule, PolyTreeD& polytree)
		{
			PathsD dummy;
			return Execute(clip_type, fill_rule, polytree, dummy);
		}

		bool Execute(ClipType clip_type,
			FillRule fill_rule, PolyTreeD& polytree, PathsD& open_paths)
		{
#ifdef USINGZ
			CheckCallback();
#endif
			if (ExecuteInternal(clip_type, fill_rule, true))
			{
				polytree.Clear();
				polytree.SetInvScale(invScale_);
				open_paths.clear();
				BuildTreeD(polytree, open_paths);
			}
			CleanUp();
			return succeeded_;
		}

	};

}  // namespace

#endif  // CLIPPER_ENGINE_H
