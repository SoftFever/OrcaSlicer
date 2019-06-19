#ifndef CLIPPER_POLYGON_HPP
#define CLIPPER_POLYGON_HPP

#include <clipper.hpp>

namespace ClipperLib {

struct Polygon {
    Path Contour;
    Paths Holes;

    inline Polygon() = default;

    inline explicit Polygon(const Path& cont): Contour(cont) {}
//    inline explicit Polygon(const Paths& holes):
//        Holes(holes) {}
    inline Polygon(const Path& cont, const Paths& holes):
        Contour(cont), Holes(holes) {}

    inline explicit Polygon(Path&& cont): Contour(std::move(cont)) {}
//    inline explicit Polygon(Paths&& holes): Holes(std::move(holes)) {}
    inline Polygon(Path&& cont, Paths&& holes):
        Contour(std::move(cont)), Holes(std::move(holes)) {}
};

inline IntPoint& operator +=(IntPoint& p, const IntPoint& pa ) {
    // This could be done with SIMD
    p.X += pa.X;
    p.Y += pa.Y;
    return p;
}

inline IntPoint operator+(const IntPoint& p1, const IntPoint& p2) {
    IntPoint ret = p1;
    ret += p2;
    return ret;
}

inline IntPoint& operator -=(IntPoint& p, const IntPoint& pa ) {
    p.X -= pa.X;
    p.Y -= pa.Y;
    return p;
}

inline IntPoint operator -(const IntPoint& p ) {
    IntPoint ret = p;
    ret.X = -ret.X;
    ret.Y = -ret.Y;
    return ret;
}

inline IntPoint operator-(const IntPoint& p1, const IntPoint& p2) {
    IntPoint ret = p1;
    ret -= p2;
    return ret;
}

inline IntPoint& operator *=(IntPoint& p, const IntPoint& pa ) {
    p.X *= pa.X;
    p.Y *= pa.Y;
    return p;
}

inline IntPoint operator*(const IntPoint& p1, const IntPoint& p2) {
    IntPoint ret = p1;
    ret *= p2;
    return ret;
}

}

#endif // CLIPPER_POLYGON_HPP
