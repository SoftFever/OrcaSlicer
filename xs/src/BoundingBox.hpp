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
    
    BoundingBoxBase();
    BoundingBoxBase(const std::vector<PointClass> points);
    void merge(const BoundingBoxBase<PointClass> &bb);
    void scale(double factor);
};

template <class PointClass>
class BoundingBox2Base : public BoundingBoxBase<PointClass>
{
    public:
    BoundingBox2Base();
    BoundingBox2Base(const std::vector<PointClass> points) : BoundingBoxBase<PointClass>(points) {};
    PointClass size() const;
    void translate(coordf_t x, coordf_t y);
    PointClass center() const;
};

template <class PointClass>
class BoundingBox3Base : public BoundingBoxBase<PointClass>
{
    public:
    BoundingBox3Base();
    BoundingBox3Base(const std::vector<PointClass> points);
    void merge(const BoundingBox3Base<PointClass> &bb);
    PointClass size() const;
    void translate(coordf_t x, coordf_t y, coordf_t z);
    PointClass center() const;
};

class BoundingBox : public BoundingBox2Base<Point>
{
    public:
    void polygon(Polygon* polygon) const;
    
    BoundingBox() {};
    BoundingBox(const Points points) : BoundingBox2Base<Point>(points) {};
};

class BoundingBoxf  : public BoundingBox2Base<Pointf> {};
class BoundingBox3  : public BoundingBox3Base<Point3> {};

class BoundingBoxf3 : public BoundingBox3Base<Pointf3> {
    public:
    BoundingBoxf3() {};
    BoundingBoxf3(const std::vector<Pointf3> points) : BoundingBox3Base<Pointf3>(points) {};
};

}

#endif
