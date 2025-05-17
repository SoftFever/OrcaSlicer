/*******************************************************************************
* Author    :  Angus Johnson                                                   *
* Date      :  4 November 2022                                                 *
* Website   :  http://www.angusj.com                                           *
* Copyright :  Angus Johnson 2010-2022                                         *
* Purpose   :  Core Clipper Library structures and functions                   *
* License   :  http://www.boost.org/LICENSE_1_0.txt                            *
*******************************************************************************/

#ifndef CLIPPER_CORE_H
#define CLIPPER_CORE_H

#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <climits>

//#define NO_EXCEPTIONS

namespace Clipper2Lib 
{

#ifndef NO_EXCEPTIONS
	static const char* precision_error =
		"Precision exceeds the permitted range";
#endif

	static double const PI = 3.141592653589793238;
	static int64_t const MAX_COORD = LLONG_MAX / 2;
	
	//By far the most widely used filling rules for polygons are EvenOdd
	//and NonZero, sometimes called Alternate and Winding respectively.
	//https://en.wikipedia.org/wiki/Nonzero-rule
	enum class FillRule { EvenOdd, NonZero, Positive, Negative };
	
// Point ------------------------------------------------------------------------

template <typename T>
struct Point {
	T x;
	T y;
#ifdef USINGZ
	int64_t z;

	template <typename T2>
	inline void Init(const T2 x_ = 0, const T2 y_ = 0, const int64_t z_ = 0)
	{
		if constexpr (std::numeric_limits<T>::is_integer &&
			!std::numeric_limits<T2>::is_integer)
		{
			x = static_cast<T>(std::round(x_));
			y = static_cast<T>(std::round(y_));
			z = z_;
		}
		else
		{
			x = static_cast<T>(x_);
			y = static_cast<T>(y_);
			z = z_;
		}
	}

	explicit Point() : x(0), y(0), z(0) {};

	template <typename T2>
	Point(const T2 x_, const T2 y_, const int64_t z_ = 0)
	{
		Init(x_, y_);
		z = z_;
	}

	template <typename T2>
	explicit Point<T>(const Point<T2>& p)
	{
		Init(p.x, p.y, p.z);
	}

	Point operator * (const double scale) const
	{
		return Point(x * scale, y * scale, z);
	}


	friend std::ostream& operator<<(std::ostream& os, const Point& point)
	{
		os << point.x << "," << point.y << "," << point.z;
		return os;
	}

#else

	template <typename T2>
	inline void Init(const T2 x_ = 0, const T2 y_ = 0)
	{
		if constexpr (std::numeric_limits<T>::is_integer &&
			!std::numeric_limits<T2>::is_integer)
		{
			x = static_cast<T>(std::round(x_));
			y = static_cast<T>(std::round(y_));
		}
		else
		{
			x = static_cast<T>(x_);
			y = static_cast<T>(y_);
		}
	}

	explicit Point() : x(0), y(0) {};

	template <typename T2>
	Point(const T2 x_, const T2 y_) { Init(x_, y_); }

	template <typename T2>
    explicit Point(const Point<T2>& p) { Init(p.x, p.y); }

	Point operator * (const double scale) const
	{
		return Point(x * scale, y * scale);
	}

	friend std::ostream& operator<<(std::ostream& os, const Point& point)
	{
		os << point.x << "," << point.y;
		return os;
	}
#endif

	friend bool operator==(const Point &a, const Point &b) 
	{
		return a.x == b.x && a.y == b.y;
	}

	friend bool operator!=(const Point& a, const Point& b)
	{
		return !(a == b);
	}

	inline Point<T> operator-() const
	{
		return Point<T>(-x,-y);
	}

	inline Point operator+(const Point &b) const
	{
		return Point(x+b.x, y+b.y);
	}

	inline Point operator-(const Point &b) const
	{
		return Point(x-b.x, y-b.y);
	}

	inline void Negate() { x = -x; y = -y; }

};

//nb: using 'using' here (instead of typedef) as they can be used in templates
using Point64 = Point<int64_t>;
using PointD = Point<double>;

template <typename T>
using Path = std::vector<Point<T>>;
template <typename T>
using Paths = std::vector<Path<T>>;

using Path64 = Path<int64_t>;
using PathD = Path<double>;
using Paths64 = std::vector< Path64>;
using PathsD = std::vector< PathD>;

template <typename T>
std::ostream& operator << (std::ostream& outstream, const Path<T>& path)
{
	if (!path.empty())
	{
		auto pt = path.cbegin(), last = path.cend() - 1;
		while (pt != last)
			outstream << *pt++ << ", ";
		outstream << *last << std::endl;
	}
	return outstream;
}

template <typename T>
std::ostream& operator << (std::ostream& outstream, const Paths<T>& paths)
{
	for (auto p : paths)
		outstream << p;
	return outstream;
}

template <typename T1, typename T2>
inline Path<T1> ScalePath(const Path<T2>& path, double scale)
{
	Path<T1> result;
	result.reserve(path.size());
#ifdef USINGZ
	for (const Point<T2>& pt : path)
		result.push_back(Point<T1>(pt.x * scale, pt.y * scale, pt.z));
#else
	for (const Point<T2>& pt : path)
		result.push_back(Point<T1>(pt.x * scale, pt.y * scale));
#endif
	return result;
}

template <typename T1, typename T2>
inline Paths<T1> ScalePaths(const Paths<T2>& paths, double scale)
{
	Paths<T1> result;
	result.reserve(paths.size());
	for (const Path<T2>& path : paths)
		result.push_back(ScalePath<T1, T2>(path, scale));
	return result;
}

template <typename T1, typename T2>
inline Path<T1> TransformPath(const Path<T2>& path)
{
	Path<T1> result;
	result.reserve(path.size());
	std::transform(path.cbegin(), path.cend(), std::back_inserter(result),
		[](const Point<T2>& pt) {return Point<T1>(pt); });
	return result;
}

template <typename T1, typename T2>
inline Paths<T1> TransformPaths(const Paths<T2>& paths)
{
	Paths<T1> result;
	std::transform(paths.cbegin(), paths.cend(), std::back_inserter(result),
		[](const Path<T2>& path) {return TransformPath<T1, T2>(path); });
	return result;
}

inline PathD Path64ToPathD(const Path64& path)
{
	return TransformPath<double, int64_t>(path);
}

inline PathsD Paths64ToPathsD(const Paths64& paths)
{
	return TransformPaths<double, int64_t>(paths);
}

inline Path64 PathDToPath64(const PathD& path)
{
	return TransformPath<int64_t, double>(path);
}

inline Paths64 PathsDToPaths64(const PathsD& paths)
{
	return TransformPaths<int64_t, double>(paths);
}

template<typename T>
inline double Sqr(T val)
{
	return static_cast<double>(val) * static_cast<double>(val);
}

template<typename T>
inline bool NearEqual(const Point<T>& p1,
	const Point<T>& p2, double max_dist_sqrd)
{
	return Sqr(p1.x - p2.x) + Sqr(p1.y - p2.y) < max_dist_sqrd;
}

template<typename T>
inline Path<T> StripNearEqual(const Path<T>& path, 
	double max_dist_sqrd, bool is_closed_path)
{
	if (path.size() == 0) return Path<T>();
	Path<T> result;
	result.reserve(path.size());
	typename Path<T>::const_iterator path_iter = path.cbegin();
	Point<T> first_pt = *path_iter++, last_pt = first_pt;
	result.push_back(first_pt);
	for (; path_iter != path.cend(); ++path_iter)
	{
		if (!NearEqual(*path_iter, last_pt, max_dist_sqrd))
		{
			last_pt = *path_iter;
			result.push_back(last_pt);
		}
	}
	if (!is_closed_path) return result;
	while (result.size() > 1 &&
		NearEqual(result.back(), first_pt, max_dist_sqrd)) result.pop_back();
	return result;
}

template<typename T>
inline Paths<T> StripNearEqual(const Paths<T>& paths, 
	double max_dist_sqrd, bool is_closed_path)
{
	Paths<T> result;
	result.reserve(paths.size());
	for (typename Paths<T>::const_iterator paths_citer = paths.cbegin();
		paths_citer != paths.cend(); ++paths_citer)
	{
		result.push_back(StripNearEqual(*paths_citer, max_dist_sqrd, is_closed_path));
	}
	return result;
}

template<typename T>
inline Path<T> StripDuplicates(const Path<T>& path, bool is_closed_path)
{
	if (path.size() == 0) return Path<T>();
	Path<T> result;
	result.reserve(path.size());
	typename Path<T>::const_iterator path_iter = path.cbegin();
	Point<T> first_pt = *path_iter++, last_pt = first_pt;
	result.push_back(first_pt);
	for (; path_iter != path.cend(); ++path_iter)
	{
		if (*path_iter != last_pt)
		{
			last_pt = *path_iter;
			result.push_back(last_pt);
		}
	}
	if (!is_closed_path) return result;
	while (result.size() > 1 && result.back() == first_pt) result.pop_back();
	return result;
}

template<typename T>
inline Paths<T> StripDuplicates(const Paths<T>& paths, bool is_closed_path)
{
	Paths<T> result;
	result.reserve(paths.size());
	for (typename Paths<T>::const_iterator paths_citer = paths.cbegin();
		paths_citer != paths.cend(); ++paths_citer)
	{
		result.push_back(StripDuplicates(*paths_citer, is_closed_path));
	}
	return result;
}

// Rect ------------------------------------------------------------------------

template <typename T>
struct Rect;

using Rect64 = Rect<int64_t>;
using RectD = Rect<double>;

template <typename T>
struct Rect {
	T left;
	T top;
	T right;
	T bottom;

	Rect() :
		left(0),
		top(0),
		right(0),
		bottom(0) {}

	Rect(T l, T t, T r, T b) :
		left(l),
		top(t),
		right(r),
		bottom(b) {}


	T Width() const { return right - left; }
	T Height() const { return bottom - top; }
	void Width(T width) { right = left + width; }
	void Height(T height) { bottom = top + height; }

	Point<T> MidPoint() const
	{
		return Point<T>((left + right) / 2, (top + bottom) / 2);
	}

	Path<T> AsPath() const
	{
		Path<T> result;
		result.reserve(4);
		result.push_back(Point<T>(left, top));
		result.push_back(Point<T>(right, top));
		result.push_back(Point<T>(right, bottom));
		result.push_back(Point<T>(left, bottom));
		return result;
	}

	bool Contains(const Point<T>& pt) const
	{
		return pt.x > left && pt.x < right&& pt.y > top && pt.y < bottom;
	}

	bool Contains(const Rect<T>& rec) const
	{
		return rec.left >= left && rec.right <= right && 
			rec.top >= top && rec.bottom <= bottom;
	}

	void Scale(double scale) {
		left *= scale; 
		top *= scale;
		right *= scale;
		bottom *= scale;
	}

	bool IsEmpty() const { return bottom <= top || right <= left; };

	bool Intersects(const Rect<T>& rec) const 
	{ 
		return (std::max(left, rec.left) < std::min(right, rec.right)) &&
			(std::max(top, rec.top) < std::min(bottom, rec.bottom));
	};

	friend std::ostream &operator<<(std::ostream &os, const Rect<T> &rect) {
		os << "("
		   << rect.left << "," << rect.top << "," << rect.right << "," << rect.bottom
		   << ")";
		return os;
	}
};

template <typename T1, typename T2>
inline Rect<T1> ScaleRect(const Rect<T2>& rect, double scale)
{
	Rect<T1> result;

	if constexpr (std::numeric_limits<T1>::is_integer &&
		!std::numeric_limits<T2>::is_integer)
	{
		result.left = static_cast<T1>(std::round(rect.left * scale));
		result.top = static_cast<T1>(std::round(rect.top * scale));
		result.right = static_cast<T1>(std::round(rect.right * scale));
		result.bottom = static_cast<T1>(std::round(rect.bottom * scale));
	}
	else
	{
		result.left = rect.left * scale;
		result.top = rect.top * scale;
		result.right = rect.right * scale;
		result.bottom = rect.bottom * scale;
	}
	return result;
}

// clipper2Exception ---------------------------------------------------------

#ifndef NO_EXCEPTIONS
class Clipper2Exception : public std::exception {
public:
	explicit Clipper2Exception(const char *description) :
		m_descr(description) {}
	virtual const char *what() const throw() { return m_descr.c_str(); }

private:
	std::string m_descr;
};
#endif

// Miscellaneous ------------------------------------------------------------

inline void CheckPrecision(int& precision)
{
	if (precision >= -8 && precision <= 8) return;
#ifdef NO_EXCEPTIONS
	precision = precision > 8 ? 8 : -8;
#else
	throw Clipper2Exception(precision_error);
#endif
}

template <typename T>
inline double CrossProduct(const Point<T>& pt1, const Point<T>& pt2, const Point<T>& pt3) {
	return (static_cast<double>(pt2.x - pt1.x) * static_cast<double>(pt3.y - 
		pt2.y) - static_cast<double>(pt2.y - pt1.y) * static_cast<double>(pt3.x - pt2.x));
}

template <typename T>
inline double CrossProduct(const Point<T>& vec1, const Point<T>& vec2)
{
	return static_cast<double>(vec1.y * vec2.x) - static_cast<double>(vec2.y * vec1.x);
}

template <typename T>
inline double DotProduct(const Point<T>& pt1, const Point<T>& pt2, const Point<T>& pt3) {
	return (static_cast<double>(pt2.x - pt1.x) * static_cast<double>(pt3.x - pt2.x) + 
		static_cast<double>(pt2.y - pt1.y) * static_cast<double>(pt3.y - pt2.y));
}

template <typename T>
inline double DotProduct(const Point<T>& vec1, const Point<T>& vec2)
{
	return static_cast<double>(vec1.x * vec2.x) + static_cast<double>(vec1.y * vec2.y);
}

template <typename T>
inline double DistanceSqr(const Point<T> pt1, const Point<T> pt2)
{
	return Sqr(pt1.x - pt2.x) + Sqr(pt1.y - pt2.y);
}

template <typename T>
inline double DistanceFromLineSqrd(const Point<T>& pt, const Point<T>& ln1, const Point<T>& ln2)
{
	//perpendicular distance of point (x³,y³) = (Ax³ + By³ + C)/Sqrt(A² + B²)
	//see http://en.wikipedia.org/wiki/Perpendicular_distance
	double A = static_cast<double>(ln1.y - ln2.y);
	double B = static_cast<double>(ln2.x - ln1.x);
	double C = A * ln1.x + B * ln1.y;
	C = A * pt.x + B * pt.y - C;
	return (C * C) / (A * A + B * B);
}

template <typename T>
inline double Area(const Path<T>& path)
{
	size_t cnt = path.size();
	if (cnt < 3) return 0.0;
	double a = 0.0;
	typename Path<T>::const_iterator it1, it2 = path.cend() - 1, stop = it2;
	if (!(cnt & 1)) ++stop;
	for (it1 = path.cbegin(); it1 != stop;)
	{
		a += static_cast<double>(it2->y + it1->y) * (it2->x - it1->x);
		it2 = it1 + 1;
		a += static_cast<double>(it1->y + it2->y) * (it1->x - it2->x);
		it1 += 2;
	}
	if (cnt & 1)
		a += static_cast<double>(it2->y + it1->y) * (it2->x - it1->x);
	return a * 0.5;
}

template <typename T>
inline double Area(const Paths<T>& paths)
{
	double a = 0.0;
	for (typename Paths<T>::const_iterator paths_iter = paths.cbegin();
		paths_iter != paths.cend(); ++paths_iter)
	{
		a += Area<T>(*paths_iter);
	}
	return a;
}

template <typename T>
inline bool IsPositive(const Path<T>& poly)
{
	// A curve has positive orientation [and area] if a region 'R' 
	// is on the left when traveling around the outside of 'R'.
	//https://mathworld.wolfram.com/CurveOrientation.html
	//nb: This statement is premised on using Cartesian coordinates
	return Area<T>(poly) >= 0;
}

inline bool SegmentsIntersect(const Point64& seg1a, const Point64& seg1b,
	const Point64& seg2a, const Point64& seg2b, bool inclusive = false)
{
	if (inclusive) 
	{
		double res1 = CrossProduct(seg1a, seg2a, seg2b);
		double res2 = CrossProduct(seg1b, seg2a, seg2b);
		if (res1 * res2 > 0) return false;
		double res3 = CrossProduct(seg2a, seg1a, seg1b);
		double res4 = CrossProduct(seg2b, seg1a, seg1b);
		if (res3 * res4 > 0) return false;
		return (res1 || res2 || res3 || res4); // ensures not collinear
	}
	else {
		double dx1 = static_cast<double>(seg1a.x - seg1b.x);
		double dy1 = static_cast<double>(seg1a.y - seg1b.y);
		double dx2 = static_cast<double>(seg2a.x - seg2b.x);
		double dy2 = static_cast<double>(seg2a.y - seg2b.y);
		return (((dy1 * (seg2a.x - seg1a.x) - dx1 * (seg2a.y - seg1a.y)) *
			(dy1 * (seg2b.x - seg1a.x) - dx1 * (seg2b.y - seg1a.y)) < 0) &&
			((dy2 * (seg1a.x - seg2a.x) - dx2 * (seg1a.y - seg2a.y)) *
				(dy2 * (seg1b.x - seg2a.x) - dx2 * (seg1b.y - seg2a.y)) < 0));
	}
}


enum class PointInPolygonResult { IsOn, IsInside, IsOutside };

template <typename T>
inline PointInPolygonResult PointInPolygon(const Point<T>& pt, const Path<T>& polygon)
{
	if (polygon.size() < 3)
		return PointInPolygonResult::IsOutside;

	int val = 0;
	typename Path<T>::const_iterator start = polygon.cbegin(), cit = start;
	typename Path<T>::const_iterator cend = polygon.cend(), pit = cend - 1;

	while (pit->y == pt.y)
	{
		if (pit == start) return PointInPolygonResult::IsOutside;
		--pit;
	}
	bool is_above = pit->y < pt.y;

	while (cit != cend)
	{
		if (is_above)
		{
			while (cit != cend && cit->y < pt.y) ++cit;
			if (cit == cend) break;
		}
		else
		{
			while (cit != cend && cit->y > pt.y) ++cit;
			if (cit == cend) break;
		}

		if (cit == start) pit = cend - 1;
		else  pit = cit - 1;

		if (cit->y == pt.y)
		{
			if (cit->x == pt.x || (cit->y == pit->y &&
				((pt.x < pit->x) != (pt.x < cit->x))))
				return PointInPolygonResult::IsOn;
			++cit;
			continue;
		}

		if (pt.x < cit->x && pt.x < pit->x)
		{
			// we're only interested in edges crossing on the left
		}
		else if (pt.x > pit->x && pt.x > cit->x)
			val = 1 - val; // toggle val
		else
		{
			double d = CrossProduct(*pit, *cit, pt);
			if (d == 0) return PointInPolygonResult::IsOn;
			if ((d < 0) == is_above) val = 1 - val;
		}
		is_above = !is_above;
		++cit;
	}
	return (val == 0) ? 
		PointInPolygonResult::IsOutside :
		PointInPolygonResult::IsInside;
}

}  // namespace

#endif  // CLIPPER_CORE_H
