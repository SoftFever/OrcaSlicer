#ifndef NFP_HPP_
#define NFP_HPP_

#include <iostream>
#include <list>
#include <string>
#include <fstream>
#include <streambuf>
#include <vector>
#include <set>
#include <exception>
#include <random>
#include <limits>

#if defined(_MSC_VER) &&  _MSC_VER <= 1800 || __cplusplus < 201103L
    #define LIBNFP_NOEXCEPT
    #define LIBNFP_CONSTEXPR
#elif __cplusplus >= 201103L
    #define LIBNFP_NOEXCEPT noexcept
    #define LIBNFP_CONSTEXPR constexpr
#endif

#ifdef LIBNFP_USE_RATIONAL
#include <boost/multiprecision/gmp.hpp>
#include <boost/multiprecision/number.hpp>
#endif
#include <boost/geometry.hpp>
#include <boost/geometry/util/math.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/linestring.hpp>
#include <boost/geometry/io/svg/svg_mapper.hpp>
#include <boost/geometry/algorithms/intersects.hpp>
#include <boost/geometry/geometries/register/point.hpp>

#ifdef LIBNFP_USE_RATIONAL
namespace bm = boost::multiprecision;
#endif
namespace bg = boost::geometry;
namespace trans = boost::geometry::strategy::transform;


namespace libnfporb {
#ifdef NFP_DEBUG
#define DEBUG_VAL(x) std::cerr << x << std::endl;
#define DEBUG_MSG(title, value) std::cerr << title << ":" << value << std::endl;
#else
#define DEBUG_VAL(x)
#define DEBUG_MSG(title, value)
#endif

using std::string;

static LIBNFP_CONSTEXPR long double NFP_EPSILON=0.00000001;

class LongDouble {
private:
	long double val_;
public:
	LongDouble() : val_(0) {
	}

	LongDouble(const long double& val) : val_(val) {
	}

	void setVal(const long double& v) {
		val_ = v;
	}

	long double val() const {
		return val_;
	}

	LongDouble operator/(const LongDouble& other) const {
		return this->val_ / other.val_;
	}

	LongDouble operator*(const LongDouble& other) const {
		return this->val_ * other.val_;
	}

	LongDouble operator-(const LongDouble& other) const {
		return this->val_ - other.val_;
	}

	LongDouble operator-() const {
		return this->val_ * -1;
	}

	LongDouble operator+(const LongDouble& other) const {
		return this->val_ + other.val_;
	}

	void operator/=(const LongDouble& other) {
		this->val_ = this->val_ / other.val_;
	}

	void operator*=(const LongDouble& other) {
		this->val_ = this->val_ * other.val_;
	}

	void operator-=(const LongDouble& other) {
		this->val_ = this->val_ - other.val_;
	}

	void operator+=(const LongDouble& other) {
		this->val_ = this->val_ + other.val_;
	}

	bool operator==(const int& other) const {
		return this->operator ==(static_cast<long double>(other));
	}

	bool operator==(const LongDouble& other) const {
		return this->operator ==(other.val());
	}

	bool operator==(const long double& other) const {
		return this->val() == other;
	}

	bool operator!=(const int& other) const {
		return !this->operator ==(other);
	}

	bool operator!=(const LongDouble& other) const {
		return !this->operator ==(other);
	}

	bool operator!=(const long double& other) const {
		return !this->operator ==(other);
	}

	bool operator<(const int& other) const {
		return this->operator <(static_cast<long double>(other));
	}

	bool operator<(const LongDouble& other) const {
		return this->operator <(other.val());
	}

	bool operator<(const long double& other) const {
		return this->val() < other;
	}

	bool operator>(const int& other) const {
		return this->operator >(static_cast<long double>(other));
	}

	bool operator>(const LongDouble& other) const {
		return this->operator >(other.val());
	}

	bool operator>(const long double& other) const {
		return this->val() > other;
	}

	bool operator>=(const int& other) const {
		return this->operator >=(static_cast<long double>(other));
	}

	bool operator>=(const LongDouble& other) const {
		return this->operator >=(other.val());
	}

	bool operator>=(const long double& other) const {
		return this->val() >= other;
	}

	bool operator<=(const int& other) const {
		return this->operator <=(static_cast<long double>(other));
	}

	bool operator<=(const LongDouble& other) const {
		return this->operator <=(other.val());
	}

	bool operator<=(const long double& other) const {
		return this->val() <= other;
	}
};
}


namespace std {
template<>
   struct numeric_limits<libnfporb::LongDouble>
   {
     static const LIBNFP_CONSTEXPR bool is_specialized = true;

     static const LIBNFP_CONSTEXPR long double
     min() LIBNFP_NOEXCEPT { return std::numeric_limits<long double>::min(); }

     static LIBNFP_CONSTEXPR long double
     max() LIBNFP_NOEXCEPT { return std::numeric_limits<long double>::max(); }

#if __cplusplus >= 201103L
     static LIBNFP_CONSTEXPR long double
     lowest() LIBNFP_NOEXCEPT { return -std::numeric_limits<long double>::lowest(); }
#endif

     static const LIBNFP_CONSTEXPR int digits = std::numeric_limits<long double>::digits;
     static const LIBNFP_CONSTEXPR int digits10 = std::numeric_limits<long double>::digits10;
#if __cplusplus >= 201103L
     static const LIBNFP_CONSTEXPR int max_digits10
	 = std::numeric_limits<long double>::max_digits10;
#endif
     static const LIBNFP_CONSTEXPR bool is_signed = true;
     static const LIBNFP_CONSTEXPR bool is_integer = false;
     static const LIBNFP_CONSTEXPR bool is_exact = false;
     static const LIBNFP_CONSTEXPR int radix = std::numeric_limits<long double>::radix;

     static const LIBNFP_CONSTEXPR long double
     epsilon() LIBNFP_NOEXCEPT { return libnfporb::NFP_EPSILON; }

     static const LIBNFP_CONSTEXPR long double
     round_error() LIBNFP_NOEXCEPT { return 0.5L; }

     static const LIBNFP_CONSTEXPR int min_exponent = std::numeric_limits<long double>::min_exponent;
     static const LIBNFP_CONSTEXPR int min_exponent10 = std::numeric_limits<long double>::min_exponent10;
     static const LIBNFP_CONSTEXPR int max_exponent = std::numeric_limits<long double>::max_exponent;
     static const LIBNFP_CONSTEXPR int max_exponent10 = std::numeric_limits<long double>::max_exponent10;

	 
     static const LIBNFP_CONSTEXPR bool has_infinity = std::numeric_limits<long double>::has_infinity;
     static const LIBNFP_CONSTEXPR bool has_quiet_NaN = std::numeric_limits<long double>::has_quiet_NaN;
     static const LIBNFP_CONSTEXPR bool has_signaling_NaN = has_quiet_NaN;
     static const LIBNFP_CONSTEXPR float_denorm_style has_denorm
		 = std::numeric_limits<long double>::has_denorm;
     static const LIBNFP_CONSTEXPR bool has_denorm_loss
	     = std::numeric_limits<long double>::has_denorm_loss;
	 

     static const LIBNFP_CONSTEXPR long double
        infinity() LIBNFP_NOEXCEPT { return std::numeric_limits<long double>::infinity(); }

     static const LIBNFP_CONSTEXPR long double
         quiet_NaN() LIBNFP_NOEXCEPT { return std::numeric_limits<long double>::quiet_NaN(); }

     static const LIBNFP_CONSTEXPR long double
         signaling_NaN() LIBNFP_NOEXCEPT { return std::numeric_limits<long double>::signaling_NaN(); }

	 
     static const LIBNFP_CONSTEXPR long double
         denorm_min() LIBNFP_NOEXCEPT { return std::numeric_limits<long double>::denorm_min(); }

     static const LIBNFP_CONSTEXPR bool is_iec559
	 = has_infinity && has_quiet_NaN && has_denorm == denorm_present;
	 
     static const LIBNFP_CONSTEXPR bool is_bounded = true;
     static const LIBNFP_CONSTEXPR bool is_modulo = false;

     static const LIBNFP_CONSTEXPR bool traps = std::numeric_limits<long double>::traps;
     static const LIBNFP_CONSTEXPR bool tinyness_before =
    		 std::numeric_limits<long double>::tinyness_before;
     static const LIBNFP_CONSTEXPR float_round_style round_style =
						      round_to_nearest;
   };
}

namespace boost {
namespace numeric {
	template<>
	struct raw_converter<boost::numeric::conversion_traits<double, libnfporb::LongDouble>>
	{
        typedef boost::numeric::conversion_traits<double, libnfporb::LongDouble>::result_type   result_type   ;
        typedef boost::numeric::conversion_traits<double, libnfporb::LongDouble>::argument_type argument_type ;

		static result_type low_level_convert ( argument_type s ) { return s.val() ; }
	} ;
}
}

namespace libnfporb {

#ifndef LIBNFP_USE_RATIONAL
typedef LongDouble coord_t;
#else
typedef bm::number<bm::gmp_rational, bm::et_off> rational_t;
typedef rational_t coord_t;
#endif

bool equals(const LongDouble& lhs, const LongDouble& rhs);
#ifdef LIBNFP_USE_RATIONAL
bool equals(const rational_t& lhs, const rational_t& rhs);
#endif
bool equals(const long double& lhs, const long double& rhs);

const coord_t MAX_COORD = 999999999999999999.0;
const coord_t MIN_COORD = std::numeric_limits<coord_t>::min();

class point_t {
public:
	point_t() : x_(0), y_(0) {
	}
	point_t(coord_t x, coord_t y) : x_(x), y_(y) {
	}
	bool marked_ = false;
	coord_t x_;
	coord_t y_;

	point_t operator-(const point_t& other) const {
		point_t result = *this;
		bg::subtract_point(result, other);
		return result;
	}

	point_t operator+(const point_t& other) const {
		point_t result = *this;
		bg::add_point(result, other);
		return result;
	}

	bool operator==(const point_t&  other) const {
			return bg::equals(this, other);
  }

  bool operator!=(const point_t&  other) const {
      return !this->operator ==(other);
  }

	bool operator<(const point_t&  other) const {
      return  boost::geometry::math::smaller(this->x_, other.x_) || (equals(this->x_, other.x_) && boost::geometry::math::smaller(this->y_, other.y_));
  }
};




inline long double toLongDouble(const LongDouble& c) {
	return c.val();
}

#ifdef LIBNFP_USE_RATIONAL
inline long double toLongDouble(const rational_t& c) {
	return bm::numerator(c).convert_to<long double>() / bm::denominator(c).convert_to<long double>();
}
#endif

std::ostream& operator<<(std::ostream& os, const coord_t& p) {
	os << toLongDouble(p);
	return os;
}

std::istream& operator>>(std::istream& is, LongDouble& c) {
	long double val;
	is >> val;
	c.setVal(val);
	return is;
}

std::ostream& operator<<(std::ostream& os, const point_t& p) {
	os << "{" << toLongDouble(p.x_) << "," << toLongDouble(p.y_) << "}";
	return os;
}
const point_t INVALID_POINT = {MAX_COORD, MAX_COORD};

typedef bg::model::segment<point_t> segment_t;
}

#ifdef LIBNFP_USE_RATIONAL
inline long double acos(const libnfporb::rational_t& r) {
	return acos(libnfporb::toLongDouble(r));
}
#endif

inline long double acos(const libnfporb::LongDouble& ld) {
	return acos(libnfporb::toLongDouble(ld));
}

#ifdef LIBNFP_USE_RATIONAL
inline long double sqrt(const libnfporb::rational_t& r) {
	return sqrt(libnfporb::toLongDouble(r));
}
#endif

inline long double sqrt(const libnfporb::LongDouble& ld) {
	return sqrt(libnfporb::toLongDouble(ld));
}

BOOST_GEOMETRY_REGISTER_POINT_2D(libnfporb::point_t, libnfporb::coord_t, cs::cartesian, x_, y_)


namespace boost {
namespace geometry {
namespace math {
namespace detail {

template <>
struct square_root<libnfporb::LongDouble>
{
  typedef libnfporb::LongDouble return_type;

	static inline libnfporb::LongDouble apply(libnfporb::LongDouble const& a)
  {
        return std::sqrt(a.val());
  }
};

#ifdef LIBNFP_USE_RATIONAL
template <>
struct square_root<libnfporb::rational_t>
{
  typedef libnfporb::rational_t return_type;

	static inline libnfporb::rational_t apply(libnfporb::rational_t const& a)
  {
        return std::sqrt(libnfporb::toLongDouble(a));
  }
};
#endif

template<>
struct abs<libnfporb::LongDouble>
	{
	static libnfporb::LongDouble apply(libnfporb::LongDouble const& value)
			{
				libnfporb::LongDouble const zero = libnfporb::LongDouble();
					return value.val() < zero.val() ? -value.val() : value.val();
			}
	};

template <>
struct equals<libnfporb::LongDouble, false>
{
	template<typename Policy>
	static inline bool apply(libnfporb::LongDouble const& lhs, libnfporb::LongDouble const& rhs, Policy const& policy)
  {
		if(lhs.val() == rhs.val())
			return true;

	  return bg::math::detail::abs<libnfporb::LongDouble>::apply(lhs.val() - rhs.val()) <=  policy.apply(lhs.val(), rhs.val()) * libnfporb::NFP_EPSILON;
  }
};

template <>
struct smaller<libnfporb::LongDouble>
{
	static inline bool apply(libnfporb::LongDouble const& lhs, libnfporb::LongDouble const& rhs)
  {
		if(lhs.val() == rhs.val() || bg::math::detail::abs<libnfporb::LongDouble>::apply(lhs.val() - rhs.val()) <=  libnfporb::NFP_EPSILON * std::max(lhs.val(), rhs.val()))
			return false;

	  return lhs < rhs;
  }
};
}
}
}
}

namespace libnfporb {
inline bool smaller(const LongDouble& lhs, const LongDouble& rhs) {
	return boost::geometry::math::detail::smaller<LongDouble>::apply(lhs, rhs);
}

inline bool larger(const LongDouble& lhs, const LongDouble& rhs) {
  return smaller(rhs, lhs);
}

bool equals(const LongDouble& lhs, const LongDouble& rhs) {
	if(lhs.val() == rhs.val())
		return true;

  return bg::math::detail::abs<libnfporb::LongDouble>::apply(lhs.val() - rhs.val()) <=  libnfporb::NFP_EPSILON * std::max(lhs.val(), rhs.val());
}

#ifdef LIBNFP_USE_RATIONAL
inline bool smaller(const rational_t& lhs, const rational_t& rhs) {
	return lhs < rhs;
}

inline bool larger(const rational_t& lhs, const rational_t& rhs) {
  return smaller(rhs, lhs);
}

bool equals(const rational_t& lhs, const rational_t& rhs) {
	return lhs == rhs;
}
#endif

inline bool smaller(const long double& lhs, const long double& rhs) {
	return lhs < rhs;
}

inline bool larger(const long double& lhs, const long double& rhs) {
  return smaller(rhs, lhs);
}


bool equals(const long double& lhs, const long double& rhs) {
	return lhs == rhs;
}

typedef bg::model::polygon<point_t, false, true> polygon_t;
typedef std::vector<polygon_t::ring_type> nfp_t;
typedef bg::model::linestring<point_t> linestring_t;

typedef polygon_t::ring_type::size_type psize_t;

typedef bg::model::d2::point_xy<long double> pointf_t;
typedef bg::model::segment<pointf_t> segmentf_t;
typedef bg::model::polygon<pointf_t, false, true> polygonf_t;

polygonf_t::ring_type convert(const polygon_t::ring_type& r) {
	polygonf_t::ring_type rf;
	for(const auto& pt : r) {
		rf.push_back(pointf_t(toLongDouble(pt.x_), toLongDouble(pt.y_)));
	}
	return rf;
}

polygonf_t convert(polygon_t p) {
	polygonf_t pf;
	pf.outer() = convert(p.outer());

	for(const auto& r : p.inners()) {
		pf.inners().push_back(convert(r));
	}

	return pf;
}

polygon_t nfpRingsToNfpPoly(const nfp_t& nfp) {
	polygon_t nfppoly;
	for (const auto& pt : nfp.front()) {
		nfppoly.outer().push_back(pt);
	}

	for (size_t i = 1; i < nfp.size(); ++i) {
		nfppoly.inners().push_back({});
		for (const auto& pt : nfp[i]) {
			nfppoly.inners().back().push_back(pt);
		}
	}

	return nfppoly;
}

void write_svg(std::string const& filename,const std::vector<segment_t>& segments) {
    std::ofstream svg(filename.c_str());

    boost::geometry::svg_mapper<pointf_t> mapper(svg, 100, 100, "width=\"200mm\" height=\"200mm\" viewBox=\"-250 -250 500 500\"");
    for(const auto& seg : segments) {
    	segmentf_t segf({toLongDouble(seg.first.x_), toLongDouble(seg.first.y_)}, {toLongDouble(seg.second.x_), toLongDouble(seg.second.y_)});
    	mapper.add(segf);
    	mapper.map(segf, "fill-opacity:0.5;fill:rgb(153,204,0);stroke:rgb(153,204,0);stroke-width:2");
    }
}

void write_svg(std::string const& filename,	const polygon_t& p, const polygon_t::ring_type& ring) {
	std::ofstream svg(filename.c_str());

	boost::geometry::svg_mapper<pointf_t> mapper(svg, 100, 100,	"width=\"200mm\" height=\"200mm\" viewBox=\"-250 -250 500 500\"");
	auto pf = convert(p);
	auto rf = convert(ring);

	mapper.add(pf);
	mapper.map(pf, "fill-opacity:0.5;fill:rgb(153,204,0);stroke:rgb(153,204,0);stroke-width:2");
	mapper.add(rf);
	mapper.map(rf, "fill-opacity:0.5;fill:rgb(153,204,0);stroke:rgb(153,204,0);stroke-width:2");
}

void write_svg(std::string const& filename, std::vector<polygon_t> const& polygons) {
	std::ofstream svg(filename.c_str());

	boost::geometry::svg_mapper<pointf_t> mapper(svg, 100, 100, "width=\"200mm\" height=\"200mm\" viewBox=\"-250 -250 500 500\"");
	for (auto p : polygons) {
		auto pf  = convert(p);
		mapper.add(pf);
		mapper.map(pf, "fill-opacity:0.5;fill:rgb(153,204,0);stroke:rgb(153,204,0);stroke-width:2");
	}
}

void write_svg(std::string const& filename, std::vector<polygon_t> const& polygons, const nfp_t& nfp) {
	polygon_t nfppoly;
	for (const auto& pt : nfp.front()) {
		nfppoly.outer().push_back(pt);
	}

	for (size_t i = 1; i < nfp.size(); ++i) {
		nfppoly.inners().push_back({});
		for (const auto& pt : nfp[i]) {
			nfppoly.inners().back().push_back(pt);
		}
	}
	std::ofstream svg(filename.c_str());

	boost::geometry::svg_mapper<pointf_t> mapper(svg, 100, 100,	"width=\"200mm\" height=\"200mm\" viewBox=\"-250 -250 500 500\"");
	for (auto p : polygons) {
		auto pf  = convert(p);
		mapper.add(pf);
		mapper.map(pf, "fill-opacity:0.5;fill:rgb(153,204,0);stroke:rgb(153,204,0);stroke-width:2");
	}
	bg::correct(nfppoly);
	auto nfpf = convert(nfppoly);
	mapper.add(nfpf);
	mapper.map(nfpf, "fill-opacity:0.5;fill:rgb(204,153,0);stroke:rgb(204,153,0);stroke-width:2");

	for(auto& r: nfpf.inners()) {
		if(r.size() == 1) {
			mapper.add(r.front());
			mapper.map(r.front(), "fill-opacity:0.5;fill:rgb(204,153,0);stroke:rgb(204,153,0);stroke-width:2");
		} else if(r.size() == 2) {
			segmentf_t seg(r.front(), *(r.begin()+1));
			mapper.add(seg);
			mapper.map(seg, "fill-opacity:0.5;fill:rgb(204,153,0);stroke:rgb(204,153,0);stroke-width:2");
		}
	}
}

std::ostream& operator<<(std::ostream& os, const segment_t& seg) {
	os << "{" << seg.first << "," << seg.second << "}";
	return os;
}

bool operator<(const segment_t& lhs, const segment_t& rhs) {
	return lhs.first < rhs.first || ((lhs.first == rhs.first) && (lhs.second < rhs.second));
}

bool operator==(const segment_t& lhs, const segment_t& rhs) {
	return (lhs.first == rhs.first && lhs.second == rhs.second) || (lhs.first == rhs.second && lhs.second == rhs.first);
}

bool operator!=(const segment_t& lhs, const segment_t& rhs) {
	return !operator==(lhs,rhs);
}

enum Alignment {
	LEFT,
	RIGHT,
	ON
};

point_t normalize(const point_t& pt) {
	point_t norm = pt;
	coord_t len = bg::length(segment_t{{0,0},pt});

	if(len == 0.0L)
		return {0,0};

	norm.x_ /= len;
	norm.y_ /= len;

	return norm;
}

Alignment get_alignment(const segment_t& seg, const point_t& pt){
	coord_t res = ((seg.second.x_ - seg.first.x_)*(pt.y_ - seg.first.y_)
			- (seg.second.y_ - seg.first.y_)*(pt.x_ - seg.first.x_));

	if(equals(res, 0)) {
		return ON;
	} else	if(larger(res,0)) {
		return LEFT;
	} else {
		return RIGHT;
	}
}

long double get_inner_angle(const point_t& joint, const point_t& end1, const point_t& end2) {
	coord_t dx21 = end1.x_-joint.x_;
	coord_t dx31 = end2.x_-joint.x_;
	coord_t dy21 = end1.y_-joint.y_;
	coord_t dy31 = end2.y_-joint.y_;
	coord_t m12 = sqrt((dx21*dx21 + dy21*dy21));
	coord_t m13 = sqrt((dx31*dx31 + dy31*dy31));
	if(m12 == 0.0L || m13 == 0.0L)
		return 0;
	return acos( (dx21*dx31 + dy21*dy31) / (m12 * m13) );
}

struct TouchingPoint {
	enum Type {
		VERTEX,
		A_ON_B,
		B_ON_A
	};
	Type type_;
	psize_t A_;
	psize_t B_;
};

struct TranslationVector {
	point_t vector_;
	segment_t edge_;
	bool fromA_;
	string name_;

	bool operator<(const TranslationVector& other) const {
		return this->vector_ < other.vector_ || ((this->vector_ == other.vector_) && (this->edge_ < other.edge_));
	}
};

std::ostream& operator<<(std::ostream& os, const TranslationVector& tv) {
	os << "{" << tv.edge_ << " -> " << tv.vector_ << "} = " << tv.name_;
	return os;
}


void read_wkt_polygon(const string& filename, polygon_t& p) {
	std::ifstream t(filename);

	std::string str;
	t.seekg(0, std::ios::end);
	str.reserve(t.tellg());
	t.seekg(0, std::ios::beg);

	str.assign((std::istreambuf_iterator<char>(t)),
							std::istreambuf_iterator<char>());

	str.pop_back();
	bg::read_wkt(str, p);
	bg::correct(p);
}

std::vector<psize_t> find_minimum_y(const polygon_t& p) {
	std::vector<psize_t> result;
	coord_t min = MAX_COORD;
	auto& po = p.outer();
	for(psize_t i = 0; i < p.outer().size() - 1; ++i) {
		if(smaller(po[i].y_, min)) {
			result.clear();
			min = po[i].y_;
			result.push_back(i);
		} else if (equals(po[i].y_, min)) {
			result.push_back(i);
		}
	}
	return result;
}

std::vector<psize_t> find_maximum_y(const polygon_t& p) {
	std::vector<psize_t> result;
	coord_t max = MIN_COORD;
	auto& po = p.outer();
	for(psize_t i = 0; i < p.outer().size() - 1; ++i) {
		if(larger(po[i].y_, max)) {
			result.clear();
			max = po[i].y_;
			result.push_back(i);
		} else if (equals(po[i].y_, max)) {
			result.push_back(i);
		}
	}
	return result;
}

psize_t find_point(const polygon_t::ring_type& ring, const point_t& pt) {
	for(psize_t i = 0; i < ring.size(); ++i) {
		if(ring[i] == pt)
			return i;
	}
	return std::numeric_limits<psize_t>::max();
}

std::vector<TouchingPoint> findTouchingPoints(const polygon_t::ring_type& ringA, const polygon_t::ring_type& ringB) {
	std::vector<TouchingPoint> touchers;
	for(psize_t i = 0; i < ringA.size() - 1; i++) {
		psize_t nextI = i+1;
		for(psize_t j = 0; j < ringB.size() - 1; j++) {
			psize_t nextJ = j+1;
			if(ringA[i] == ringB[j]) {
				touchers.push_back({TouchingPoint::VERTEX, i, j});
			} else if (ringA[nextI] != ringB[j] && bg::intersects(segment_t(ringA[i],ringA[nextI]), ringB[j])) {
				touchers.push_back({TouchingPoint::B_ON_A, nextI, j});
			} else if (ringB[nextJ] != ringA[i] && bg::intersects(segment_t(ringB[j],ringB[nextJ]), ringA[i])) {
				touchers.push_back({TouchingPoint::A_ON_B, i, nextJ});
			}
		}
	}
	return touchers;
}

//TODO deduplicate code
TranslationVector trimVector(const polygon_t::ring_type& rA, const polygon_t::ring_type& rB, const TranslationVector& tv) {
	coord_t shortest = bg::length(tv.edge_);
	TranslationVector trimmed = tv;
	for(const auto& ptA : rA) {
		point_t translated;
		//for polygon A we invert the translation
		trans::translate_transformer<coord_t, 2, 2> translate(-tv.vector_.x_, -tv.vector_.y_);
		boost::geometry::transform(ptA, translated, translate);
		linestring_t projection;
		segment_t segproj(ptA, translated);
		projection.push_back(ptA);
		projection.push_back(translated);
		std::vector<point_t> intersections;
		bg::intersection(rB, projection, intersections);
		if(bg::touches(projection, rB) && intersections.size() < 2) {
			continue;
		}

		//find shortest intersection
		coord_t len;
		segment_t segi;
		for(const auto& pti : intersections) {
			segi = segment_t(ptA,pti);
			len = bg::length(segi);
			if(smaller(len, shortest)) {
				trimmed.vector_ = ptA - pti;
				trimmed.edge_ = segi;
				shortest = len;
			}
		}
	}

	for(const auto& ptB : rB) {
		point_t translated;

		trans::translate_transformer<coord_t, 2, 2> translate(tv.vector_.x_, tv.vector_.y_);
		boost::geometry::transform(ptB, translated, translate);
		linestring_t projection;
		segment_t segproj(ptB, translated);
		projection.push_back(ptB);
		projection.push_back(translated);
		std::vector<point_t> intersections;
		bg::intersection(rA, projection, intersections);
		if(bg::touches(projection, rA) && intersections.size() < 2) {
			continue;
		}

		//find shortest intersection
		coord_t len;
		segment_t segi;
		for(const auto& pti : intersections) {

			segi = segment_t(ptB,pti);
			len = bg::length(segi);
			if(smaller(len, shortest)) {
				trimmed.vector_ = pti - ptB;
				trimmed.edge_ = segi;
				shortest = len;
			}
		}
	}
 	return trimmed;
}

std::vector<TranslationVector> findFeasibleTranslationVectors(polygon_t::ring_type& ringA, polygon_t::ring_type& ringB, const std::vector<TouchingPoint>& touchers) {
	//use a set to automatically filter duplicate vectors
	std::vector<TranslationVector> potentialVectors;
	std::vector<std::pair<segment_t,segment_t>> touchEdges;

	for (psize_t i = 0; i < touchers.size(); i++) {
		point_t& vertexA = ringA[touchers[i].A_];
		vertexA.marked_ = true;

		// adjacent A vertices
        auto prevAindex = static_cast<signed long>(touchers[i].A_ - 1);
        auto nextAindex = static_cast<signed long>(touchers[i].A_ + 1);

        prevAindex = (prevAindex < 0) ? static_cast<signed long>(ringA.size() - 2) : prevAindex; // loop
		nextAindex = (static_cast<psize_t>(nextAindex) >= ringA.size()) ? 1 : nextAindex; // loop

		point_t& prevA = ringA[prevAindex];
		point_t& nextA = ringA[nextAindex];

		// adjacent B vertices
		point_t& vertexB = ringB[touchers[i].B_];

        auto prevBindex = static_cast<signed long>(touchers[i].B_ - 1);
        auto nextBindex = static_cast<signed long>(touchers[i].B_ + 1);

        prevBindex = (prevBindex < 0) ? static_cast<signed long>(ringB.size() - 2) : prevBindex; // loop
		nextBindex = (static_cast<psize_t>(nextBindex) >= ringB.size()) ? 1 : nextBindex; // loop

		point_t& prevB = ringB[prevBindex];
		point_t& nextB = ringB[nextBindex];

		if (touchers[i].type_ == TouchingPoint::VERTEX) {
			segment_t a1 = { vertexA, nextA };
			segment_t a2 = { vertexA, prevA };
			segment_t b1 = { vertexB, nextB };
			segment_t b2 = { vertexB, prevB };

			//swap the segment elements so that always the first point is the touching point
			//also make the second segment always a segment of ringB
			touchEdges.push_back({a1, b1});
			touchEdges.push_back({a1, b2});
			touchEdges.push_back({a2, b1});
			touchEdges.push_back({a2, b2});
#ifdef NFP_DEBUG
			write_svg("touchersV" + std::to_string(i) + ".svg", {a1,a2,b1,b2});
#endif

			//TODO test parallel edges for floating point stability
			Alignment al;
			//a1 and b1 meet at start vertex
			al = get_alignment(a1, b1.second);
			if(al == LEFT) {
				potentialVectors.push_back({b1.first - b1.second, b1, false, "vertex1"});
			} else if(al == RIGHT) {
				potentialVectors.push_back({a1.second - a1.first, a1, true, "vertex2"});
			} else {
				potentialVectors.push_back({a1.second - a1.first, a1, true, "vertex3"});
			}

			//a1 and b2 meet at start and end
			al = get_alignment(a1, b2.second);
			if(al == LEFT) {
				//no feasible translation
			} else if(al == RIGHT) {
				potentialVectors.push_back({a1.second - a1.first, a1, true, "vertex4"});
			} else {
				potentialVectors.push_back({a1.second - a1.first, a1, true, "vertex5"});
			}

			//a2 and b1 meet at end and start
			al = get_alignment(a2, b1.second);
			if(al == LEFT) {
				//no feasible translation
			} else if(al == RIGHT) {
				potentialVectors.push_back({b1.first - b1.second, b1, false, "vertex6"});
			} else {
				potentialVectors.push_back({b1.first - b1.second, b1, false, "vertex7"});
			}
		} else if (touchers[i].type_ == TouchingPoint::B_ON_A) {
			segment_t a1 = {vertexB, vertexA};
			segment_t a2 = {vertexB, prevA};
			segment_t b1 = {vertexB, prevB};
			segment_t b2 = {vertexB, nextB};

			touchEdges.push_back({a1, b1});
			touchEdges.push_back({a1, b2});
			touchEdges.push_back({a2, b1});
			touchEdges.push_back({a2, b2});
#ifdef NFP_DEBUG
			write_svg("touchersB" + std::to_string(i) + ".svg", {a1,a2,b1,b2});
#endif
			potentialVectors.push_back({vertexA - vertexB, {vertexB, vertexA}, true, "bona"});
		} else if (touchers[i].type_ == TouchingPoint::A_ON_B) {
			//TODO testme
			segment_t a1 = {vertexA, prevA};
			segment_t a2 = {vertexA, nextA};
			segment_t b1 = {vertexA, vertexB};
			segment_t b2 = {vertexA, prevB};
#ifdef NFP_DEBUG
			write_svg("touchersA" + std::to_string(i) + ".svg", {a1,a2,b1,b2});
#endif
			touchEdges.push_back({a1, b1});
			touchEdges.push_back({a2, b1});
			touchEdges.push_back({a1, b2});
			touchEdges.push_back({a2, b2});
			potentialVectors.push_back({vertexA - vertexB, {vertexA, vertexB}, false, "aonb"});
		}
	}

	//discard immediately intersecting translations
	std::vector<TranslationVector> vectors;
	for(const auto& v : potentialVectors) {
		bool discarded = false;
		for(const auto& sp : touchEdges) {
			point_t normEdge = normalize(v.edge_.second - v.edge_.first);
			point_t normFirst = normalize(sp.first.second - sp.first.first);
			point_t normSecond = normalize(sp.second.second - sp.second.first);

			Alignment a1 = get_alignment({{0,0},normEdge}, normFirst);
			Alignment a2 = get_alignment({{0,0},normEdge}, normSecond);

			if(a1 == a2 && a1 != ON) {
				long double df = get_inner_angle({0,0},normEdge, normFirst);
				long double ds = get_inner_angle({0,0},normEdge, normSecond);

				point_t normIn = normalize(v.edge_.second - v.edge_.first);
				if (equals(df, ds)) {
					TranslationVector trimmed = trimVector(ringA,ringB, v);
					polygon_t::ring_type translated;
					trans::translate_transformer<coord_t, 2, 2> translate(trimmed.vector_.x_,	trimmed.vector_.y_);
					boost::geometry::transform(ringB, translated, translate);
					if (!(bg::intersects(translated, ringA) && !bg::overlaps(translated, ringA) && !bg::covered_by(translated, ringA) && !bg::covered_by(ringA, translated))) {
						discarded = true;
						break;
					}
				} else {

					if (normIn == normalize(v.vector_)) {
						if (larger(ds, df)) {
							discarded = true;
							break;
						}
					} else {
						if (smaller(ds, df)) {
							discarded = true;
							break;
						}
					}
				}
			}
		}
		if(!discarded)
			vectors.push_back(v);
	}
	return vectors;
}

bool find(const std::vector<TranslationVector>& h, const TranslationVector& tv) {
	for(const auto& htv : h) {
		if(htv.vector_ == tv.vector_)
			return true;
	}
	return false;
}

TranslationVector getLongest(const std::vector<TranslationVector>& tvs) {
	coord_t len;
	coord_t maxLen = MIN_COORD;
	TranslationVector longest;
	longest.vector_ = INVALID_POINT;

	for(auto& tv : tvs) {
		len = bg::length(segment_t{{0,0},tv.vector_});
		if(larger(len, maxLen)) {
			maxLen = len;
			longest = tv;
		}
	}
	return longest;
}

TranslationVector selectNextTranslationVector(const polygon_t& pA, const polygon_t::ring_type& rA,	const polygon_t::ring_type& rB, const std::vector<TranslationVector>& tvs, const std::vector<TranslationVector>& history) {
	if(!history.empty()) {
		TranslationVector last = history.back();
		std::vector<TranslationVector> historyCopy = history;
		if(historyCopy.size() >= 2) {
			historyCopy.erase(historyCopy.end() - 1);
			historyCopy.erase(historyCopy.end() - 1);
			if(historyCopy.size() > 4) {
				historyCopy.erase(historyCopy.begin(), historyCopy.end() - 4);
			}

		} else {
			historyCopy.clear();
		}
		DEBUG_MSG("last", last);

		psize_t laterI = std::numeric_limits<psize_t>::max();
		point_t previous = rA[0];
		point_t next;

		if(last.fromA_) {
			for (psize_t i = 1; i < rA.size() + 1; ++i) {
				if (i >= rA.size())
					next = rA[i % rA.size()];
				else
					next = rA[i];

				segment_t candidate( previous, next );
				if(candidate == last.edge_) {
					laterI = i;
					break;
				}
				previous = next;
			}

			if (laterI == std::numeric_limits<psize_t>::max()) {
				point_t later;
				if (last.vector_ == (last.edge_.second - last.edge_.first)) {
					later = last.edge_.second;
				} else {
					later = last.edge_.first;
				}

				laterI = find_point(rA, later);
			}
		} else {
			point_t later;
			if (last.vector_ == (last.edge_.second - last.edge_.first)) {
				later = last.edge_.second;
			} else {
				later = last.edge_.first;
			}

			laterI = find_point(rA, later);
		}

		if (laterI == std::numeric_limits<psize_t>::max()) {
			throw std::runtime_error(
					"Internal error: Can't find later point of last edge");
		}

		std::vector<segment_t> viableEdges;
		previous = rA[laterI];
		for(psize_t i = laterI + 1; i < rA.size() + laterI + 1; ++i) {
			if(i >= rA.size())
				next = rA[i % rA.size()];
			else
				next = rA[i];

			viableEdges.push_back({previous, next});
			previous = next;
		}

//		auto rng = std::default_random_engine {};
//		std::shuffle(std::begin(viableEdges), std::end(viableEdges), rng);

		//search with consulting the history to prevent oscillation
		std::vector<TranslationVector> viableTrans;
		for(const auto& ve: viableEdges) {
			for(const auto& tv : tvs) {
				if((tv.fromA_ && (normalize(tv.vector_) == normalize(ve.second - ve.first))) && (tv.edge_ != last.edge_ || tv.vector_.x_ != -last.vector_.x_ || tv.vector_.y_ != -last.vector_.y_) && !find(historyCopy, tv)) {
					viableTrans.push_back(tv);
				}
			}
			for (const auto& tv : tvs) {
				if (!tv.fromA_) {
					point_t later;
					if (tv.vector_ == (tv.edge_.second - tv.edge_.first) && (tv.edge_ != last.edge_ || tv.vector_.x_ != -last.vector_.x_ || tv.vector_.y_ != -last.vector_.y_) && !find(historyCopy, tv)) {
						later = tv.edge_.second;
					} else if (tv.vector_ == (tv.edge_.first - tv.edge_.second)) {
						later = tv.edge_.first;
					} else
						continue;

					if (later == ve.first || later == ve.second) {
						viableTrans.push_back(tv);
					}
				}
			}
		}

		if(!viableTrans.empty())
			return getLongest(viableTrans);

		//search again without the history
		for(const auto& ve: viableEdges) {
			for(const auto& tv : tvs) {
				if((tv.fromA_ && (normalize(tv.vector_) == normalize(ve.second - ve.first))) && (tv.edge_ != last.edge_ || tv.vector_.x_ != -last.vector_.x_ || tv.vector_.y_ != -last.vector_.y_)) {
					viableTrans.push_back(tv);
				}
			}
			for (const auto& tv : tvs) {
				if (!tv.fromA_) {
					point_t later;
					if (tv.vector_ == (tv.edge_.second - tv.edge_.first) && (tv.edge_ != last.edge_ || tv.vector_.x_ != -last.vector_.x_ || tv.vector_.y_ != -last.vector_.y_)) {
						later = tv.edge_.second;
					} else if (tv.vector_ == (tv.edge_.first - tv.edge_.second)) {
						later = tv.edge_.first;
					} else
						continue;

					if (later == ve.first || later == ve.second) {
						viableTrans.push_back(tv);
					}
				}
			}
		}
		if(!viableTrans.empty())
			return getLongest(viableTrans);

		/*
		//search again without the history and without checking last edge
		for(const auto& ve: viableEdges) {
			for(const auto& tv : tvs) {
				if((tv.fromA_ && (normalize(tv.vector_) == normalize(ve.second - ve.first)))) {
					return tv;
				}
			}
			for (const auto& tv : tvs) {
				if (!tv.fromA_) {
					point_t later;
					if (tv.vector_ == (tv.edge_.second - tv.edge_.first)) {
						later = tv.edge_.second;
					} else if (tv.vector_ == (tv.edge_.first - tv.edge_.second)) {
						later = tv.edge_.first;
					} else
						continue;

					if (later == ve.first || later == ve.second) {
						return tv;
					}
				}
			}
		}*/

		if(tvs.size() == 1)
			return *tvs.begin();

		TranslationVector tv;
		tv.vector_ = INVALID_POINT;
		return tv;
	} else {
		return getLongest(tvs);
	}
}

bool inNfp(const point_t& pt, const nfp_t& nfp) {
	for(const auto& r : nfp) {
		if(bg::touches(pt, r))
			return true;
	}

	return false;
}

enum SearchStartResult {
	FIT,
	FOUND,
	NOT_FOUND
};

SearchStartResult searchStartTranslation(polygon_t::ring_type& rA, const polygon_t::ring_type& rB, const nfp_t& nfp,const bool& inside, point_t& result) {
	for(psize_t i = 0; i < rA.size() - 1; i++) {
		psize_t index;
		if (i >= rA.size())
			index = i % rA.size() + 1;
		else
			index = i;

		auto& ptA = rA[index];

		if(ptA.marked_)
			continue;

		ptA.marked_ = true;

		for(const auto& ptB: rB) {
			point_t testTranslation = ptA - ptB;
			polygon_t::ring_type translated;
			boost::geometry::transform(rB, translated, trans::translate_transformer<coord_t, 2, 2>(testTranslation.x_, testTranslation.y_));

			//check if the translated rB is identical to rA
			bool identical = false;
			for(const auto& ptT: translated) {
				identical = false;
				for(const auto& ptA: rA) {
					if(ptT == ptA) {
						identical = true;
						break;
					}
				}
				if(!identical)
					break;
			}

			if(identical) {
				result = testTranslation;
				return FIT;
			}

			bool bInside = false;
			for(const auto& ptT: translated) {
				if(bg::within(ptT, rA)) {
					bInside = true;
					break;
				} else if(!bg::touches(ptT, rA)) {
					bInside = false;
					break;
				}
			}

			if(((bInside && inside) || (!bInside && !inside)) && (!bg::overlaps(translated, rA) && !bg::covered_by(translated, rA) && !bg::covered_by(rA, translated)) && !inNfp(translated.front(), nfp)){
				result = testTranslation;
				return FOUND;
			}

			point_t nextPtA = rA[index + 1];
			TranslationVector slideVector;
			slideVector.vector_ = nextPtA - ptA;
			slideVector.edge_ = {ptA, nextPtA};
			slideVector.fromA_ = true;
			TranslationVector trimmed = trimVector(rA, translated, slideVector);
			polygon_t::ring_type translated2;
			trans::translate_transformer<coord_t, 2, 2> trans(trimmed.vector_.x_, trimmed.vector_.y_);
			boost::geometry::transform(translated, translated2, trans);

			//check if the translated rB is identical to rA
			identical = false;
			for(const auto& ptT: translated) {
				identical = false;
				for(const auto& ptA: rA) {
					if(ptT == ptA) {
						identical = true;
						break;
					}
				}
				if(!identical)
					break;
			}

			if(identical) {
				result = trimmed.vector_ + testTranslation;
				return FIT;
			}

			bInside = false;
			for(const auto& ptT: translated2) {
				if(bg::within(ptT, rA)) {
					bInside = true;
					break;
				} else if(!bg::touches(ptT, rA)) {
					bInside = false;
					break;
				}
			}

			if(((bInside && inside) || (!bInside && !inside)) && (!bg::overlaps(translated2, rA) && !bg::covered_by(translated2, rA) && !bg::covered_by(rA, translated2)) && !inNfp(translated2.front(), nfp)){
				result = trimmed.vector_ + testTranslation;
				return FOUND;
			}
		}
	}
	return NOT_FOUND;
}

enum SlideResult {
	LOOP,
	NO_LOOP,
	NO_TRANSLATION
};

SlideResult slide(polygon_t& pA, polygon_t::ring_type& rA, polygon_t::ring_type& rB, nfp_t& nfp, const point_t& transB, bool inside) {
	polygon_t::ring_type rifsB;
	boost::geometry::transform(rB, rifsB, trans::translate_transformer<coord_t, 2, 2>(transB.x_, transB.y_));
	rB = std::move(rifsB);

#ifdef NFP_DEBUG
	write_svg("ifs.svg", pA, rB);
#endif

	bool startAvailable = true;
	psize_t cnt = 0;
	point_t referenceStart = rB.front();
	std::vector<TranslationVector> history;

	//generate the nfp for the ring
	while(startAvailable) {
		DEBUG_VAL(cnt);
		//use first point of rB as reference
		nfp.back().push_back(rB.front());
		if(cnt == 15)
			std::cerr << "";

		std::vector<TouchingPoint> touchers = findTouchingPoints(rA, rB);

#ifdef NFP_DEBUG
		DEBUG_MSG("touchers", touchers.size());
		for(auto t : touchers) {
			DEBUG_VAL(t.type_);
		}
#endif
		if(touchers.empty()) {
			throw std::runtime_error("Internal error: No touching points found");
		}
		std::vector<TranslationVector> transVectors = findFeasibleTranslationVectors(rA, rB, touchers);

#ifdef NFP_DEBUG
		DEBUG_MSG("collected vectors", transVectors.size());
		for(auto pt : transVectors) {
			DEBUG_VAL(pt);
		}
#endif

		if(transVectors.empty()) {
			return NO_LOOP;
		}

		TranslationVector next = selectNextTranslationVector(pA, rA, rB, transVectors, history);

		if(next.vector_ == INVALID_POINT)
			return NO_TRANSLATION;

		DEBUG_MSG("next", next);

		TranslationVector trimmed = trimVector(rA, rB, next);
		DEBUG_MSG("trimmed", trimmed);

		history.push_back(next);

		polygon_t::ring_type nextRB;
		boost::geometry::transform(rB, nextRB, trans::translate_transformer<coord_t, 2, 2>(trimmed.vector_.x_, trimmed.vector_.y_));
		rB = std::move(nextRB);

#ifdef NFP_DEBUG
		write_svg("next" + std::to_string(cnt) + ".svg", pA,rB);
#endif

		++cnt;
		if(referenceStart == rB.front() || (inside && bg::touches(rB.front(), nfp.front()))) {
			startAvailable = false;
		}
	}
	return LOOP;
}

void removeCoLinear(polygon_t::ring_type& r) {
	assert(r.size() > 2);
	psize_t nextI;
	psize_t prevI = 0;
	segment_t segment(r[r.size() - 2], r[0]);
	polygon_t::ring_type newR;

	for (psize_t i = 1; i < r.size() + 1; ++i) {
		if (i >= r.size())
			nextI = i % r.size() + 1;
		else
			nextI = i;

		if (get_alignment(segment, r[nextI]) != ON) {
			newR.push_back(r[prevI]);
		}
		segment = {segment.second, r[nextI]};
		prevI = nextI;
	}

	r = newR;
}

void removeCoLinear(polygon_t& p) {
	removeCoLinear(p.outer());
	for (auto& r : p.inners())
		removeCoLinear(r);
}

nfp_t generateNFP(polygon_t& pA, polygon_t& pB, const bool checkValidity = true) {
	removeCoLinear(pA);
	removeCoLinear(pB);

	if(checkValidity)  {
		std::string reason;
		if(!bg::is_valid(pA, reason))
			throw std::runtime_error("Polygon A is invalid: " + reason);

		if(!bg::is_valid(pB, reason))
			throw std::runtime_error("Polygon B is invalid: " + reason);
	}

	nfp_t nfp;

#ifdef NFP_DEBUG
	write_svg("start.svg", {pA, pB});
#endif

	DEBUG_VAL(bg::wkt(pA))
	DEBUG_VAL(bg::wkt(pB));

	//prevent double vertex connections at start because we might come back the same way we go which would end the nfp prematurely
	std::vector<psize_t> ptyaminI = find_minimum_y(pA);
	std::vector<psize_t> ptybmaxI = find_maximum_y(pB);

	point_t pAstart;
	point_t pBstart;

	if(ptyaminI.size() > 1 || ptybmaxI.size() > 1) {
		//find right-most of A and left-most of B to prevent double connection at start
		coord_t maxX = MIN_COORD;
		psize_t iRightMost = 0;
		for(psize_t& ia : ptyaminI) {
			const point_t& candidateA = pA.outer()[ia];
			if(larger(candidateA.x_, maxX)) {
				maxX = candidateA.x_;
				iRightMost = ia;
			}
		}

		coord_t minX = MAX_COORD;
		psize_t iLeftMost = 0;
		for(psize_t& ib : ptybmaxI) {
			const point_t& candidateB = pB.outer()[ib];
			if(smaller(candidateB.x_, minX)) {
				minX = candidateB.x_;
				iLeftMost = ib;
			}
		}
		pAstart = pA.outer()[iRightMost];
		pBstart = pB.outer()[iLeftMost];
	} else {
		pAstart = pA.outer()[ptyaminI.front()];
		pBstart = pB.outer()[ptybmaxI.front()];
	}

	nfp.push_back({});
	point_t transB = {pAstart - pBstart};

	if(slide(pA, pA.outer(), pB.outer(), nfp, transB, false) != LOOP) {
			throw std::runtime_error("Unable to complete outer nfp loop");
	}

	DEBUG_VAL("##### outer #####");
	point_t startTrans;
  while(true) {
  	SearchStartResult res = searchStartTranslation(pA.outer(), pB.outer(), nfp, false, startTrans);
  	if(res == FOUND) {
			nfp.push_back({});
			DEBUG_VAL("##### interlock start #####")
			polygon_t::ring_type rifsB;
			boost::geometry::transform(pB.outer(), rifsB, trans::translate_transformer<coord_t, 2, 2>(startTrans.x_, startTrans.y_));
			if(inNfp(rifsB.front(), nfp)) {
				continue;
			}
			SlideResult sres = slide(pA, pA.outer(), pB.outer(), nfp, startTrans, true);
			if(sres != LOOP) {
				if(sres == NO_TRANSLATION) {
					//no initial slide found -> jiggsaw
					if(!inNfp(pB.outer().front(),nfp)) {
						nfp.push_back({});
						nfp.back().push_back(pB.outer().front());
					}
				}
			}
			DEBUG_VAL("##### interlock end #####");
  	} else if(res == FIT) {
  		point_t reference = pB.outer().front();
  		point_t translated;
			trans::translate_transformer<coord_t, 2, 2> translate(startTrans.x_, startTrans.y_);
			boost::geometry::transform(reference, translated, translate);
			if(!inNfp(translated,nfp)) {
				nfp.push_back({});
				nfp.back().push_back(translated);
			}
			break;
  	} else
  		break;
	}


	for(auto& rA : pA.inners()) {
		while(true) {
			SearchStartResult res = searchStartTranslation(rA, pB.outer(), nfp, true, startTrans);
			if(res == FOUND) {
				nfp.push_back({});
				DEBUG_VAL("##### hole start #####");
				slide(pA, rA, pB.outer(), nfp, startTrans, true);
				DEBUG_VAL("##### hole end #####");
			} else if(res == FIT) {
	  		point_t reference = pB.outer().front();
	  		point_t translated;
				trans::translate_transformer<coord_t, 2, 2> translate(startTrans.x_, startTrans.y_);
				boost::geometry::transform(reference, translated, translate);
				if(!inNfp(translated,nfp)) {
					nfp.push_back({});
					nfp.back().push_back(translated);
				}
				break;
			} else
				break;
		}
  }

#ifdef NFP_DEBUG
  write_svg("nfp.svg", {pA,pB}, nfp);
#endif

  return nfp;
}
}
#endif
