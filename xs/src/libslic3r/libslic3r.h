#ifndef _libslic3r_h_
#define _libslic3r_h_

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <ostream>
#include <iostream>
#include <math.h>
#include <queue>
#include <sstream>
#include <cstdio>
#include <stdint.h>
#include <stdarg.h>
#include <vector>
#include <boost/thread.hpp>

#define SLIC3R_FORK_NAME "Slic3r Prusa Edition"
#define SLIC3R_VERSION "1.38.3"
#define SLIC3R_BUILD "UNKNOWN"

typedef long coord_t;
typedef double coordf_t;

//FIXME This epsilon value is used for many non-related purposes:
// For a threshold of a squared Euclidean distance,
// for a trheshold in a difference of radians,
// for a threshold of a cross product of two non-normalized vectors etc.
#define EPSILON 1e-4
// Scaling factor for a conversion from coord_t to coordf_t: 10e-6
// This scaling generates a following fixed point representation with for a 32bit integer:
// 0..4294mm with 1nm resolution
// int32_t fits an interval of (-2147.48mm, +2147.48mm)
#define SCALING_FACTOR 0.000001
// RESOLUTION, SCALED_RESOLUTION: Used as an error threshold for a Douglas-Peucker polyline simplification algorithm.
#define RESOLUTION 0.0125
#define SCALED_RESOLUTION (RESOLUTION / SCALING_FACTOR)
#define PI 3.141592653589793238
// When extruding a closed loop, the loop is interrupted and shortened a bit to reduce the seam.
#define LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER 0.15
// Maximum perimeter length for the loop to apply the small perimeter speed. 
#define SMALL_PERIMETER_LENGTH (6.5 / SCALING_FACTOR) * 2 * PI
#define INSET_OVERLAP_TOLERANCE 0.4
// 3mm ring around the top / bottom / bridging areas.
//FIXME This is quite a lot.
#define EXTERNAL_INFILL_MARGIN 3.
//FIXME Better to use an inline function with an explicit return type.
//inline coord_t scale_(coordf_t v) { return coord_t(floor(v / SCALING_FACTOR + 0.5f)); }
#define scale_(val) ((val) / SCALING_FACTOR)
#define unscale(val) ((val) * SCALING_FACTOR)
#define SCALED_EPSILON scale_(EPSILON)
/* Implementation of CONFESS("foo"): */
#ifdef _MSC_VER
	#define CONFESS(...) confess_at(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
	#define CONFESS(...) confess_at(__FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
void confess_at(const char *file, int line, const char *func, const char *pat, ...);
/* End implementation of CONFESS("foo"): */

// Which C++ version is supported?
// For example, could optimized functions with move semantics be used?
#if __cplusplus==201402L
	#define SLIC3R_CPPVER 14
	#define STDMOVE(WHAT) std::move(WHAT)
#elif __cplusplus==201103L
	#define SLIC3R_CPPVER 11
	#define STDMOVE(WHAT) std::move(WHAT)
#else
	#define SLIC3R_CPPVER 0
	#define STDMOVE(WHAT) (WHAT)
#endif

#define SLIC3R_DEBUG_OUT_PATH_PREFIX "out/"

inline std::string debug_out_path(const char *name, ...)
{
	char buffer[2048];
	va_list args;
	va_start(args, name);
	std::vsprintf(buffer, name, args);
	va_end(args);
	return std::string(SLIC3R_DEBUG_OUT_PATH_PREFIX) + std::string(buffer);
}

#ifdef _MSC_VER
	// Visual Studio older than 2015 does not support the prinf type specifier %zu. Use %Iu instead.
	#define PRINTF_ZU "%Iu"
#else
	#define PRINTF_ZU "%zu"
#endif

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif /* UNUSED */

// Detect whether the compiler supports C++11 noexcept exception specifications.
#if defined(_MSC_VER) && _MSC_VER < 1900
    #define noexcept throw()
#endif

// Write slices as SVG images into out directory during the 2D processing of the slices.
// #define SLIC3R_DEBUG_SLICE_PROCESSING

namespace Slic3r {

enum Axis { X=0, Y, Z };

template <class T>
inline void append_to(std::vector<T> &dst, const std::vector<T> &src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

template <typename T>
inline void append(std::vector<T>& dest, const std::vector<T>& src)
{
    if (dest.empty())
        dest = src;
    else
        dest.insert(dest.end(), src.begin(), src.end());
}

template <typename T>
inline void append(std::vector<T>& dest, std::vector<T>&& src)
{
    if (dest.empty())
        dest = std::move(src);
    else
        std::move(std::begin(src), std::end(src), std::back_inserter(dest));
    src.clear();
    src.shrink_to_fit();
}

template <typename T>
inline void remove_nulls(std::vector<T*> &vec)
{
	vec.erase(
    	std::remove_if(vec.begin(), vec.end(), [](const T *ptr) { return ptr == nullptr; }),
    	vec.end());
}

template <typename T>
inline void sort_remove_duplicates(std::vector<T> &vec)
{
	std::sort(vec.begin(), vec.end());
	vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
}

// Older compilers do not provide a std::make_unique template. Provide a simple one.
template<typename T, typename... Args>
inline std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
static inline T sqr(T x)
{
    return x * x;
}

template <typename T>
static inline T clamp(const T low, const T high, const T value)
{
    return std::max(low, std::min(high, value));
}

template <typename T>
static inline T lerp(const T a, const T b, const T t)
{
    assert(t >= T(-EPSILON) && t <= T(1.+EPSILON));
    return (1. - t) * a + t * b;
}

} // namespace Slic3r

#endif
