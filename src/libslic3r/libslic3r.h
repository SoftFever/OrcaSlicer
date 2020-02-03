#ifndef _libslic3r_h_
#define _libslic3r_h_

#include "libslic3r_version.h"

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <memory>
#include <algorithm>
#include <ostream>
#include <iostream>
#include <math.h>
#include <queue>
#include <sstream>
#include <cstdio>
#include <stdint.h>
#include <stdarg.h>
#include <vector>
#include <cassert>
#include <cmath>

#include "Technologies.hpp"
#include "Semver.hpp"

typedef int32_t coord_t;
typedef double  coordf_t;

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

#define SCALED_EPSILON scale_(EPSILON)

#define SLIC3R_DEBUG_OUT_PATH_PREFIX "out/"

#if defined(_MSC_VER) &&  _MSC_VER < 1900
# define SLIC3R_CONSTEXPR
# define SLIC3R_NOEXCEPT
#else
#define SLIC3R_CONSTEXPR constexpr
#define SLIC3R_NOEXCEPT  noexcept
#endif

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

extern Semver SEMVER;

template<typename T, typename Q>
inline T unscale(Q v) { return T(v) * T(SCALING_FACTOR); }

enum Axis { X=0, Y, Z, E, F, NUM_AXES };

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

// Casting an std::vector<> from one type to another type without warnings about a loss of accuracy.
template<typename T_TO, typename T_FROM>
std::vector<T_TO> cast(const std::vector<T_FROM> &src) 
{
    std::vector<T_TO> dst;
    dst.reserve(src.size());
    for (const T_FROM &a : src)
        dst.emplace_back((T_TO)a);
    return dst;
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

// Variant of std::lower_bound() with compare predicate, but without the key.
// This variant is very useful in case that the T type is large or it does not even have a public constructor.
template<class ForwardIt, class LowerThanKeyPredicate>
ForwardIt lower_bound_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_thank_key)
{
    ForwardIt it;
    typename std::iterator_traits<ForwardIt>::difference_type count, step;
    count = std::distance(first, last);
 
    while (count > 0) {
        it = first;
        step = count / 2;
        std::advance(it, step);
        if (lower_thank_key(*it)) {
            first = ++it;
            count -= step + 1;
        }
        else
            count = step;
    }
    return first;
}

// from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class T, class Compare=std::less<>>
ForwardIt binary_find(ForwardIt first, ForwardIt last, const T& value, Compare comp={})
{
    // Note: BOTH type T and the type after ForwardIt is dereferenced 
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare. 
    // This is stricter than lower_bound requirement (see above)
 
    first = std::lower_bound(first, last, value, comp);
    return first != last && !comp(value, *first) ? first : last;
}

// from https://en.cppreference.com/w/cpp/algorithm/lower_bound
template<class ForwardIt, class LowerThanKeyPredicate, class EqualToKeyPredicate>
ForwardIt binary_find_by_predicate(ForwardIt first, ForwardIt last, LowerThanKeyPredicate lower_thank_key, EqualToKeyPredicate equal_to_key)
{
    // Note: BOTH type T and the type after ForwardIt is dereferenced 
    // must be implicitly convertible to BOTH Type1 and Type2, used in Compare. 
    // This is stricter than lower_bound requirement (see above)
 
    first = lower_bound_by_predicate(first, last, lower_thank_key);
    return first != last && equal_to_key(*first) ? first : last;
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

template <typename T, typename Number>
static inline T lerp(const T& a, const T& b, Number t)
{
    assert((t >= Number(-EPSILON)) && (t <= Number(1) + Number(EPSILON)));
    return (Number(1) - t) * a + t * b;
}

template <typename Number>
static inline bool is_approx(Number value, Number test_value)
{
    return std::fabs(double(value) - double(test_value)) < double(EPSILON);
}

template<class...Args>
std::string string_printf(const char *const _fmt, Args &&...args)
{
    static const size_t INITIAL_LEN = 1024;
    std::vector<char> buffer(INITIAL_LEN, '\0');
    
    auto fmt = std::string("%s") + _fmt;
    int bufflen = snprintf(buffer.data(), INITIAL_LEN - 1, fmt.c_str(), "", std::forward<Args>(args)...);
    
    if (bufflen >= int(INITIAL_LEN)) {
        buffer.resize(size_t(bufflen) + 1);
        snprintf(buffer.data(), buffer.size(), fmt.c_str(), "", std::forward<Args>(args)...);
    }
    
    return std::string(buffer.begin(), buffer.begin() + bufflen);
}

} // namespace Slic3r

#endif
