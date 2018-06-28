#ifndef LIBNEST2D_CONFIG_HPP
#define LIBNEST2D_CONFIG_HPP

#ifndef NDEBUG
#include <iostream>
#endif

#include <stdexcept>
#include <string>
#include <cmath>
#include <type_traits>

#if defined(_MSC_VER) &&  _MSC_VER <= 1800 || __cplusplus < 201103L
    #define BP2D_NOEXCEPT
    #define BP2D_CONSTEXPR
#elif __cplusplus >= 201103L
    #define BP2D_NOEXCEPT noexcept
    #define BP2D_CONSTEXPR constexpr
#endif


/*
 * Debugging output dout and derr definition
 */
//#ifndef NDEBUG
//#   define dout std::cout
//#   define derr std::cerr
//#else
//#   define dout 0 && std::cout
//#   define derr 0 && std::cerr
//#endif

namespace libnest2d {

struct DOut {
#ifndef NDEBUG
    std::ostream& out = std::cout;
#endif
};

struct DErr {
#ifndef NDEBUG
    std::ostream& out = std::cerr;
#endif
};

template<class T>
inline DOut&& operator<<( DOut&& out, T&& d) {
#ifndef NDEBUG
    out.out << d;
#endif
    return std::move(out);
}

template<class T>
inline DErr&& operator<<( DErr&& out, T&& d) {
#ifndef NDEBUG
    out.out << d;
#endif
    return std::move(out);
}
inline DOut dout() { return DOut(); }
inline DErr derr() { return DErr(); }

template< class T >
struct remove_cvref {
    using type = typename std::remove_cv<
        typename std::remove_reference<T>::type>::type;
};

template< class T >
using remove_cvref_t = typename remove_cvref<T>::type;

template< class T >
using remove_ref_t = typename std::remove_reference<T>::type;

template<bool B, class T>
using enable_if_t = typename std::enable_if<B, T>::type;

template<class F, class...Args>
struct invoke_result {
    using type = typename std::result_of<F(Args...)>::type;
};

template<class F, class...Args>
using invoke_result_t = typename invoke_result<F, Args...>::type;

/* ************************************************************************** */
/* C++14 std::index_sequence implementation:                                  */
/* ************************************************************************** */

/**
 * \brief C++11 conformant implementation of the index_sequence type from C++14
 */
template<size_t...Ints> struct index_sequence {
    using value_type = size_t;
    BP2D_CONSTEXPR value_type size() const { return sizeof...(Ints); }
};

// A Help structure to generate the integer list
template<size_t...Nseq> struct genSeq;

// Recursive template to generate the list
template<size_t I, size_t...Nseq> struct genSeq<I, Nseq...> {
    // Type will contain a genSeq with Nseq appended by one element
    using Type = typename genSeq< I - 1, I - 1, Nseq...>::Type;
};

// Terminating recursion
template <size_t ... Nseq> struct genSeq<0, Nseq...> {
    // If I is zero, Type will contain index_sequence with the fuly generated
    // integer list.
    using Type = index_sequence<Nseq...>;
};

/// Helper alias to make an index sequence from 0 to N
template<size_t N> using make_index_sequence = typename genSeq<N>::Type;

/// Helper alias to make an index sequence for a parameter pack
template<class...Args>
using index_sequence_for = make_index_sequence<sizeof...(Args)>;


/* ************************************************************************** */

/**
 * A useful little tool for triggering static_assert error messages e.g. when
 * a mandatory template specialization (implementation) is missing.
 *
 * \tparam T A template argument that may come from and outer template method.
 */
template<class T> struct always_false { enum { value = false }; };

const double BP2D_CONSTEXPR Pi = 3.141592653589793238463; // 2*std::acos(0);
const double BP2D_CONSTEXPR Pi_2 = 2*Pi;

/**
 * @brief Only for the Radian and Degrees classes to behave as doubles.
 */
class Double {
protected:
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
    mutable double sin_ = std::nan(""), cos_ = std::nan("");
public:
    Radians(double rads = Double() ): Double(rads) {}
    inline Radians(const Degrees& degs);

    inline operator Degrees();
    inline double toDegrees();

    inline double sin() const {
        if(std::isnan(sin_)) {
            cos_ = std::cos(val_);
            sin_ = std::sin(val_);
        }
        return sin_;
    }

    inline double cos() const {
        if(std::isnan(cos_)) {
            cos_ = std::cos(val_);
            sin_ = std::sin(val_);
        }
        return cos_;
    }
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
