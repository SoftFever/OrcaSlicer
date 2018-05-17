#ifndef LIBNEST2D_CONFIG_HPP
#define LIBNEST2D_CONFIG_HPP

#if defined(_MSC_VER) &&  _MSC_VER <= 1800 || __cplusplus < 201103L
    #define BP2D_NOEXCEPT
    #define BP2D_CONSTEXPR
#elif __cplusplus >= 201103L
    #define BP2D_NOEXCEPT noexcept
    #define BP2D_CONSTEXPR constexpr
#endif

#include <stdexcept>
#include <string>
#include <cmath>

namespace libnest2d {

template< class T >
struct remove_cvref {
    using type = typename std::remove_cv<
        typename std::remove_reference<T>::type>::type;
};

template< class T >
using remove_cvref_t = typename remove_cvref<T>::type;

template<bool B, class T>
using enable_if_t = typename std::enable_if<B, T>::type;

/**
 * A useful little tool for triggering static_assert error messages e.g. when
 * a mandatory template specialization (implementation) is missing.
 *
 * \tparam T A template argument that may come from and outer template method.
 */
template<class T> struct always_false { enum { value = false }; };

const auto BP2D_CONSTEXPR Pi = 3.141592653589793238463; // 2*std::acos(0);

/**
 * @brief Only for the Radian and Degrees classes to behave as doubles.
 */
class Double {
  double val_;
public:
  Double(): val_(double{}) { }
  Double(double d) : val_(d) { }

  operator double() const BP2D_NOEXCEPT { return val_; }
  operator double&() BP2D_NOEXCEPT { return val_; }
};

class Degrees;

/**
 * @brief Data type representing radians. It supports conversion to degrees.
 */
class Radians: public Double {
public:
    Radians(double rads = Double() ): Double(rads) {}
    inline Radians(const Degrees& degs);

    inline operator Degrees();
    inline double toDegrees();
};

/**
 * @brief Data type representing degrees. It supports conversion to radians.
 */
class Degrees: public Double {
public:
    Degrees(double deg = Double()): Double(deg) {}
    Degrees(const Radians& rads): Double( rads * 180/Pi ) {}
    inline double toRadians() { return Radians(*this);}
};

inline bool operator==(const Degrees& deg, const Radians& rads) {
    Degrees deg2 = rads;
    auto diff = std::abs(deg - deg2);
    return diff < 0.0001;
}

inline bool operator==(const Radians& rads, const Degrees& deg) {
    return deg == rads;
}

inline Radians::operator Degrees() { return *this * 180/Pi; }

inline Radians::Radians(const Degrees &degs): Double( degs * Pi/180) {}

inline double Radians::toDegrees() { return operator Degrees(); }

}
#endif // LIBNEST2D_CONFIG_HPP
