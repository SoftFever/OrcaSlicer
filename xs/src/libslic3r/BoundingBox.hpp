#ifndef slic3r_BoundingBox_hpp_
#define slic3r_BoundingBox_hpp_

#include <myinit.h>
#include "Point.hpp"
#include "Polygon.hpp"

namespace Slic3r {

typedef Point   Size;
typedef Point3  Size3;
typedef Pointf  Sizef;
typedef Pointf3 Sizef3;

template <class PointClass>
class BoundingBoxBase
{
    public:
    PointClass min;
    PointClass max;
    bool defined;
    
    BoundingBoxBase() : defined(false) {};
    BoundingBoxBase(const std::vector<PointClass> &points);
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBoxBase<PointClass> &bb);
    void scale(double factor);
    PointClass size() const;
    void translate(coordf_t x, coordf_t y);
    void offset(coordf_t delta);
    PointClass center() const;
};

template <class PointClass>
class BoundingBox3Base : public BoundingBoxBase<PointClass>
{
    public:
    BoundingBox3Base() : BoundingBoxBase<PointClass>() {};
    BoundingBox3Base(const std::vector<PointClass> &points);
    void merge(const PointClass &point);
    void merge(const std::vector<PointClass> &points);
    void merge(const BoundingBox3Base<PointClass> &bb);
    PointClass size() const;
    void translate(coordf_t x, coordf_t y, coordf_t z);
    void offset(coordf_t delta);
    PointClass center() const;
};

class BoundingBox : public BoundingBoxBase<Point>
{
    public:
    void polygon(Polygon* polygon) const;
    Polygon polygon() const;
    
    BoundingBox() : BoundingBoxBase<Point>() {};
    BoundingBox(const Points &points) : BoundingBoxBase<Point>(points) {};
    BoundingBox(const Lines &lines);
};

/*
class BoundingBox3  : public BoundingBox3Base<Point3> {};
*/

class BoundingBoxf : public BoundingBoxBase<Pointf> {
    public:
    BoundingBoxf() : BoundingBoxBase<Pointf>() {};
    BoundingBoxf(const std::vector<Pointf> &points) : BoundingBoxBase<Pointf>(points) {};
};

class BoundingBoxf3 : public BoundingBox3Base<Pointf3> {
    public:
    BoundingBoxf3() : BoundingBox3Base<Pointf3>() {};
    BoundingBoxf3(const std::vector<Pointf3> &points) : BoundingBox3Base<Pointf3>(points) {};
};

}

#endif
