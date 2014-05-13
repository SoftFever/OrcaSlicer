/**
 * \file visilibity.hpp
 * \authors Karl J. Obermeyer
 * \date March 20, 2008
 *
VisiLibity:  A Floating-Point Visibility Algorithms Library,
Copyright (C) 2008  Karl J. Obermeyer (karl.obermeyer [ at ] gmail.com)

This file is part of VisiLibity.

VisiLibity is free software: you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation, either version 3 of the License, or (at your
option) any later version.

VisiLibity is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public
License along with VisiLibity.  If not, see <http://www.gnu.org/licenses/>.
*/

/** 
 * \mainpage
 * <center> 
 * see also the <a href="../index.html">VisiLibity Project Page</a>
 * </center>
 * <b>Authors</b>:  Karl J. Obermeyer
 * <hr>
 * \section developers For Developers
 * <a href="../VisiLibity.coding_standards.html">Coding Standards</a>
 * <hr>
 * \section release_notes Release Notes
 * <b>Current Functionality</b>
 *   <ul>
 *      <li>visibility polygons in polygonal environments with holes</li>
 *      <li>visibility graphs</li>
 *      <li>shortest path planning for a point</li>
 *   </ul>
 */

#ifndef VISILIBITY_H
#define VISILIBITY_H


//Uncomment these lines when compiling under 
//Microsoft Visual Studio
/*
#include <limits>
#define NAN std::numeric_limits<double>::quiet_NaN()
#define INFINITY std::numeric_limits<double>::infinity()
#define M_PI 3.141592653589793238462643
#define and &&
#define or ||
*/

#include <cmath>      //math functions in std namespace
#include <vector>
#include <queue>      //queue and priority_queue.
#include <set>        //priority queues with iteration, 
                      //integrated keys
#include <list>
#include <algorithm>  //sorting, min, max, reverse
#include <cstdlib>    //rand and srand
#include <ctime>      //Unix time
#include <fstream>    //file I/O
#include <iostream>
#include <cstring>    //C-string manipulation
#include <string>     //string class
#include <cassert>    //assertions


/// VisiLibity's sole namespace
namespace VisiLibity
{

  //Fwd declaration of all classes and structs serves as index.
  struct Bounding_Box;
  class Point;
  class Line_Segment;
  class Angle;
  class Ray;
  class Polar_Point;
  class Polyline;
  class Polygon;
  class Environment;
  class Guards;
  class Visibility_Polygon;
  class Visibility_Graph;


  /** \brief  floating-point display precision.
   *
   * This is the default precision with which floating point
   * numbers are displayed or written to files for classes with a
   * write_to_file() method.
   */
  const int FIOS_PRECISION = 10;


  /** \brief  get a uniform random sample from an (inclusive) interval
   *          on the real line
   *
   * \author  Karl J. Obermeyer
   * \param lower_bound  lower bound of the real interval
   * \param upper_bound  upper bound of the real interval
   * \pre  \a lower_bound <= \a upper_bound 
   * \return  a random sample from a uniform probability distribution
   * on the real interval [\a lower_bound, \a upper_bound]
   * \remarks  Uses the Standard Library's rand() function. rand()
   * should be seeded (only necessary once at the beginning of the
   * program) using the command 
   * std::srand( std::time( NULL ) ); rand();
   * \warning  performance degrades as upper_bound - lower_bound
   * approaches RAND_MAX.
   */  
  double uniform_random_sample(double lower_bound, double upper_bound);


  /** \brief  rectangle with sides parallel to the x- and y-axes
   *
   * \author  Karl J. Obermeyer
   * Useful for enclosing other geometric objects.
   */
  struct Bounding_Box { double x_min, x_max, y_min, y_max; };


  /// Point in the plane represented by Cartesian coordinates
  class Point
  {
  public:
    //Constructors
    /** \brief  default
     *
     * \remarks Data defaults to NAN so that checking whether the
     * data are numbers can be used as a precondition in functions.
     */
    Point() : x_(NAN) , y_(NAN) { }
    /// costruct from raw coordinates
    Point(double x_temp, double y_temp)
    { x_=x_temp; y_=y_temp; }
    //Accessors
    /// get x coordinate
    double x () const { return x_; }
    /// get y coordinate
    double y () const { return y_; }
    /** \brief closest Point on \a line_segment_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers
     *       and \a line_segment_temp is nonempty
     * \return  the Point on \a line_segment_temp which is the smallest
     * Euclidean distance from the calling Point
     */
    Point projection_onto(const Line_Segment& line_segment_temp) const;
    /** \brief closest Point on \a ray_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point and \a ray_temp data are numbers
     * \return  the Point on \a ray_temp which is the smallest
     * Euclidean distance from the calling Point
     */
    Point projection_onto(const Ray& ray_temp) const;
    /** \brief  closest Point on \a polyline_temp
     *
     * \pre  the calling Point data are numbers and \a polyline_temp 
     * is nonempty
     * \return  the Point on \a polyline_temp which is the smallest
     * Euclidean distance from the calling Point
     */
    Point projection_onto(const Polyline& polyline_temp) const;
    /** \brief  closest vertex of \a polygon_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a polygon_temp
     * is nonempty
     * \return  the vertex of \a polygon_temp which is the
     * smallest Euclidean distance from the calling Point
     */
    Point projection_onto_vertices_of(const Polygon& polygon_temp) const;
    /** \brief  closest vertex of \a environment_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a environment_temp
     * is nonempty
     * \return  the vertex of \a environment_temp which is
     * the smallest Euclidean distance from the calling Point
     */
    Point projection_onto_vertices_of(const Environment& 
				      enviroment_temp) const;
    /** \brief  closest Point on boundary of \a polygon_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a polygon_temp
     * is nonempty
     * \return  the Point on the boundary of \a polygon_temp which is the
     * smallest Euclidean distance from the calling Point
     */
    Point projection_onto_boundary_of(const Polygon& polygon_temp) const;
    /** \brief  closest Point on boundary of \a environment_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a environment_temp
     * is nonempty
     * \return  the Point on the boundary of \a environment_temp which is
     * the smalles Euclidean distance from the calling Point
     */
    Point projection_onto_boundary_of(const Environment&
				      enviroment_temp) const;
    /** \brief  true iff w/in \a epsilon of boundary of \a polygon_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a polygon_temp
     * is nonempty
     * \return true iff the calling Point is within Euclidean distance 
     * \a epsilon of \a polygon_temp 's boundary
     * \remarks  O(n) time complexity, where n is the number
     * of vertices of \a polygon_temp
     */
    bool on_boundary_of(const Polygon& polygon_temp,
			double epsilon=0.0) const;
    /** \brief  true iff w/in \a epsilon of boundary of \a environment_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a environment_temp
     * is nonempty
     * \return true iff the calling Point is within Euclidean distance 
     * \a epsilon of \a environment_temp 's boundary
     * \remarks  O(n) time complexity, where n is the number
     * of vertices of \a environment_temp
     */   
    bool on_boundary_of(const Environment& environment_temp,
			double epsilon=0.0) const;
    /** \brief true iff w/in \a epsilon of \a line_segment_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a line_segment_temp
     * is nonempty
     * \return  true iff the calling Point is within distance 
     * \a epsilon of the (closed) Line_Segment \a line_segment_temp
     */
    bool in(const Line_Segment& line_segment_temp,
	    double epsilon=0.0) const;
    /** \brief true iff w/in \a epsilon of interior but greater than 
     *         \a espilon away from endpoints of \a line_segment_temp
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a line_segment_temp 
     * is nonempty
     * \return  true iff the calling Point is within distance \a
     * epsilon of \line_segment_temp, but distance (strictly) greater
     * than epsilon from \a line_segment_temp 's endpoints.
     */
    bool in_relative_interior_of(const Line_Segment& line_segment_temp,
				 double epsilon=0.0) const;
    /** \brief  true iff w/in \a epsilon of \a polygon_temp
     *
     * \author  Karl J. Obermeyer
     *
     * \pre the calling Point data are numbers and \a polygon_temp is
     * \a epsilon -simple. Test simplicity with
     * Polygon::is_simple(epsilon)
     *
     * \return  true iff the calling Point is a Euclidean distance no greater
     * than \a epsilon from the (closed) Polygon (with vertices listed
     * either cw or ccw) \a polygon_temp.
     * \remarks  O(n) time complexity, where n is the number of vertices
     * in \a polygon_temp
     */
    bool in(const Polygon& polygon_temp,
	    double epsilon=0.0) const;
    /** \brief  true iff w/in \a epsilon of \a environment_temp
     *
     * \author  Karl J. Obermeyer
     *
     * \pre the calling Point data are numbers and \a environment_temp
     * is nonempty and \a epsilon -valid. Test validity with
     * Enviroment::is_valid(epsilon)
     *
     * \return  true iff the calling Point is a Euclidean distance no greater
     * than \a epsilon from the in the (closed) Environment \a environment_temp
     * \remarks  O(n) time complexity, where n is the number of
     * vertices in \a environment_temp
     */
    bool in(const Environment& environment_temp,
	    double epsilon=0.0) const;
    /** \brief  true iff w/in \a epsilon of some endpoint 
     *          of \a line_segment_temp
     *
     * \pre  the calling Point data are numbers and \a line_segment_temp
     * is nonempty
     * \return  true iff calling Point is a Euclidean distance no greater
     * than \a epsilon from some endpoint of \a line_segment_temp
     */
    bool is_endpoint_of(const Line_Segment& line_segment_temp,
			double epsilon=0.0) const;
    //Mutators
    /// change x coordinate
    void set_x(double x_temp) { x_ = x_temp;}
    /// change y coordinate
    void set_y(double y_temp) { y_ = y_temp;}
    /** \brief relocate to closest vertex if w/in \a epsilon of some
     *         vertex (of \a polygon_temp)
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a polygon_temp 
     * is nonempty
     * \post If the calling Point was a Euclidean distance no greater
     * than \a epsilon from any vertex of \a polygon_temp, then it
     * will be repositioned to coincide with the closest such vertex
     * \remarks O(n) time complexity, where n is the number of
     * vertices in \a polygon_temp.
     */
    void snap_to_vertices_of(const Polygon& polygon_temp,
			     double epsilon=0.0);
    /** \brief relocate to closest vertex if w/in \a epsilon of some
     *         vertex (of \a environment_temp)
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a environment_temp 
     * is nonempty
     * \post If the calling Point was a Euclidean distance no greater
     * than \a epsilon from any vertex of \a environment_temp, then it
     * will be repositioned to coincide with the closest such vertex
     * \remarks O(n) time complexity, where n is the number of
     * vertices in \a environment_temp.
     */
    void snap_to_vertices_of(const Environment& environment_temp,
			     double epsilon=0.0);
    /** \brief relocate to closest Point on boundary if w/in \a epsilon
     *	       of the boundary (of \a polygon_temp)
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a polygon_temp 
     * is nonempty
     * \post if the calling Point was a Euclidean distance no greater
     * than \a epsilon from the boundary of \a polygon_temp, then it
     * will be repositioned to it's projection onto that boundary
     * \remarks O(n) time complexity, where n is the number of
     * vertices in \a polygon_temp.
     */
    void snap_to_boundary_of(const Polygon& polygon_temp,
			     double epsilon=0.0);
    /** \brief relocate to closest Point on boundary if w/in \a epsilon
     *	       of the boundary (of \a environment_temp)
     *
     * \author  Karl J. Obermeyer
     * \pre  the calling Point data are numbers and \a environment_temp
     * is nonempty
     * \post if the calling Point was a Euclidean distance no greater
     * than \a epsilon from the boundary of \a environment_temp, then it
     * will be repositioned to it's projection onto that boundary
     * \remarks O(n) time complexity, where n is the number of
     * vertices in \a environment_temp.
     */
    void snap_to_boundary_of(const Environment& environment_temp,
			     double epsilon=0.0);
  protected:
    double x_;
    double y_;
  };
  
  
  /** \brief True iff Points' coordinates are identical.
   *
   * \remarks  NAN==NAN returns false, so if either point has 
   * not been assigned real number coordinates, they will not be ==
   */
  bool  operator == (const Point& point1, const Point& point2);  
  /// True iff Points' coordinates are not identical.
  bool  operator != (const Point& point1, const Point& point2);
  
  
  /** \brief  compare lexicographic order of points
   *
   * For Points p1 and p2, p1 < p2 iff either p1.x() < p2.x() or
   * p1.x()==p2.x() and p1.y()<p2.y().  False if any member data have
   * not been assigned (numbers).
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a line parallel to one of the axes
   */
  bool  operator <  (const Point& point1, const Point& point2);
  /** \brief  compare lexicographic order of points
   *
   * For Points p1 and p2, p1 < p2 iff either p1.x() < p2.x() or
   * p1.x()==p2.x() and p1.y()<p2.y().  False if any member data have
   * not been assigned (numbers).
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a line parallel to one of the axes
   */
  bool  operator >  (const Point& point1, const Point& point2);
  /** \brief  compare lexicographic order of points
   *
   * For Points p1 and p2, p1 < p2 iff either p1.x() < p2.x() or
   * p1.x()==p2.x() and p1.y()<p2.y().  False if any member data have
   * not been assigned (numbers).
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a line parallel to one of the axes
   */
  bool  operator >= (const Point& point1, const Point& point2);
  /** \brief  compare lexicographic order of points
   *
   * For Points p1 and p2, p1 < p2 iff either p1.x() < p2.x() or
   * p1.x()==p2.x() and p1.y()<p2.y().  False if any member data have
   * not been assigned (numbers).
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a line parallel to one of the axes
   */
  bool  operator <= (const Point& point1, const Point& point2);


  /// vector addition of Points
  Point operator +  (const Point& point1, const Point& point2);
  /// vector subtraction of Points
  Point operator -  (const Point& point1, const Point& point2);


  //// dot (scalar) product treats the Points as vectors
  Point operator *  (const Point& point1, const Point& point2);
 

  /// simple scaling treats the Point as a vector
  Point operator *  (double scalar, const Point& point2); 
  /// simple scaling treats the Point as a vector
  Point operator *  (const Point&  point1, double scalar);


  /** \brief  cross product (signed) magnitude treats the Points as vectors
   *
   * \author  Karl J. Obermeyer
   * \pre  Points' data are numbers
   * \remarks This is equal to the (signed) area of the parallelogram created
   * by the Points viewed as vectors.
   */
  double cross(const Point& point1, const Point& point2);


  /** \brief  Euclidean distance between Points
   *
   * \pre  Points' data are numbers
   */
  double distance(const Point& point1, const Point& point2);


  /** \brief  Euclidean distance between a Point and a Line_Segment
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Line_Segment is nonempty
   */
  double distance(const Point& point_temp,
		  const Line_Segment& line_segment_temp);
  /** \brief  Euclidean distance between a Point and a Line_Segment
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Line_Segment is nonempty
   */
  double distance(const Line_Segment& line_segment_temp,
		  const Point& point_temp);


  /** \brief  Euclidean distance between a Point and a Ray
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's and Ray's data are numbers
   */
  double distance(const Point& point_temp,
		  const Ray& ray_temp);
  /** \brief  Euclidean distance between a Point and a Ray
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's and Ray's data are numbers
   */
  double distance(const Ray& ray_temp,
		  const Point& point_temp);


  /** \brief  Euclidean distance between a Point and a Polyline
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Polyline is nonempty
   */
  double distance(const Point& point_temp,
		  const Polyline& polyline_temp);
  /** \brief  Euclidean distance between a Point and a Polyline
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Polyline is nonempty
   */
  double distance(const Polyline& polyline_temp,
		  const Point& point_temp);


  /** \brief  Euclidean distance between a Point and a Polygon's boundary
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Polygon is nonempty
   */
  double boundary_distance(const Point& point_temp,
			   const Polygon& polygon_temp);
  /** \brief  Euclidean distance between a Point and a Polygon's boundary
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Polygon is nonempty
   */
  double boundary_distance(const Polygon& polygon_temp,
			   const Point& point_temp);


  /** \brief  Euclidean distance between a Point and a Environment's boundary
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Environment is nonempty
   */
  double boundary_distance(const Point& point_temp,
			   const Environment& environment_temp);
  /** \brief  Euclidean distance between a Point and a Environment's boundary
   *
   * \author  Karl J. Obermeyer
   * \pre  Point's data are numbers and the Environment is nonempty
   */
  double boundary_distance(const Environment& environment_temp,
			   const Point& point_temp);


  /// print a Point
  std::ostream& operator << (std::ostream& outs, const Point& point_temp);


  /** \brief line segment in the plane represented by its endpoints
   *
   * Closed and oriented line segment in the plane represented by its endpoints.
   * \remarks  may be degenerate (colocated endpoints) or empty
   */
  class Line_Segment
  {
  public:
    //Constructors
    /// default to empty
    Line_Segment();
    /// copy constructor
    Line_Segment(const Line_Segment& line_segment_temp);
    /// construct degenerate segment from a single Point
    Line_Segment(const Point& point_temp);
    /// Line_Segment pointing from first_point_temp to second_point_temp
    Line_Segment(const Point& first_point_temp,
		 const Point& second_point_temp, double epsilon=0);
    //Accessors 
    /** \brief  first endpoint
     *
     * \pre size() > 0
     * \return  the first Point of the Line_Segment
     * \remarks  If size() == 1, then both first() and second() are valid 
     * and will return the same Point
     */
    Point first()  const;
    /** \brief  second endpoint
     *
     * \pre size() > 0
     * \return  the second Point of the Line_Segment
     * \remarks  If size() == 1, then both first() and second() are valid 
     * and will return the same Point
     */
    Point second() const;
    /** \brief  number of distinct endpoints
     *
     * \remarks
     * size 0 => empty line segment;
     * size 1 => degenerate (single point) line segment;
     * size 2 => full-fledged (bona fide) line segment
     */
    unsigned size() const { return size_; }
    /** \brief midpoint
     *
     * \pre size() > 0
     */
    Point midpoint() const;
    /** \brief Euclidean length
     *
     * \pre size() > 0
     */
    double length() const;
    /** \brief  true iff vertices in lex. order
     *
     * \pre  size() > 0
     * \return  true iff vertices are listed beginning with the vertex 
     * which is lexicographically smallest (lowest x, then lowest y)
     * \remarks  lex. comparison is very sensitive to perturbations if
     * two Points nearly define a line parallel to one of the axes
     */
    bool is_in_standard_form() const;
    //Mutators
    /// assignment operator
    Line_Segment& operator = (const Line_Segment& line_segment_temp);
    /** \brief set first endpoint
     *
     * \remarks  if \a point_temp is w/in a distance \a epsilon of an existing 
     * endpoint, the coordinates of \a point_temp are used and size is set to 
     * 1 as appropriate
     */
    void set_first(const Point& point_temp, double epsilon=0.0);
    /** \brief set second endpoint
     *
     * \remarks  if \a point_temp is w/in a distance \a epsilon of an existing 
     * endpoint, the coordinates of \a point_temp are used and size is set to 
     * 1 as appropriate
     */
    void set_second(const Point& point_temp, double epsilon=0.0);
    /** \brief  reverse order of endpoints
     *
     * \post order of endpoints is reversed.
     */
    void reverse();
    /** \brief  enforce that lex. smallest endpoint first
     *
     * \post the lexicographically smallest endpoint (lowest x, then lowest y)
     * is first
     * \remarks  lex. comparison is very sensitive to perturbations if
     * two Points nearly define a line parallel to one of the axes
     */
    void enforce_standard_form();
    /// erase both endpoints and set line segment empty (size 0)
    void clear();
    /// destructor
    virtual ~Line_Segment();
  protected:
    //Pointer to dynamic array of endpoints.
    Point *endpoints_;
    //See size() comments.
    unsigned    size_;
  };
  

  /** \brief  true iff endpoint coordinates are exactly equal, but
   *          false if either Line_Segment has size 0
   *
   * \remarks  respects ordering of vertices, i.e., even if the line segments 
   * overlap exactly, they are not considered == unless the orientations are 
   * the same
   */
  bool  operator == (const Line_Segment& line_segment1,
		     const Line_Segment& line_segment2);
  /// true iff endpoint coordinates are not ==
  bool  operator != (const Line_Segment& line_segment1,
		     const Line_Segment& line_segment2);
  

  /** \brief  true iff line segments' endpoints match up w/in a (closed)
   *          \a epsilon ball of each other, but false if either
   *          Line_Segment has size 0
   *
   * \author  Karl J. Obermeyer
   * \remarks  this function will return true even if it has to flip
   * the orientation of one of the segments to get the vertices to
   * match up
   */
  bool equivalent(Line_Segment line_segment1,
		  Line_Segment line_segment2, double epsilon=0);


  /** \brief  Euclidean distance between Line_Segments
   *
   * \author  Karl J. Obermeyer
   * \pre  \a line_segment1.size() > 0 and \a line_segment2.size() > 0
   */
  double distance(const Line_Segment& line_segment1,
		  const Line_Segment& line_segment2);


  /** \brief  Euclidean distance between a Line_Segment and the
   *          boundary of a Polygon
   *
   * \author  Karl J. Obermeyer
   * \pre  \a line_segment.size() > 0 and \a polygon.n() > 0
   */
  double boundary_distance(const Line_Segment& line_segment,
			   const Polygon& polygon);


  /** \brief  Euclidean distance between a Line_Segment and the
   *          boundary of a Polygon
   *
   * \author  Karl J. Obermeyer
   * \pre  \a line_segment.size() > 0 and \a polygon.n() > 0
   */
  double boundary_distance(const Polygon& polygon,
			   const Line_Segment& line_segment);


  /** \brief  true iff the Euclidean distance between Line_Segments is 
   *          no greater than \a epsilon, false if either line segment
   *          has size 0
   *
   * \author  Karl J. Obermeyer
   */
  bool intersect(const Line_Segment& line_segment1,
		 const Line_Segment& line_segment2,
		 double epsilon=0.0);


  /** \brief  true iff line segments intersect properly w/in epsilon,
   *          false if either line segment has size 0
   *
   * \author  Karl J. Obermeyer
   * \return true iff Line_Segments intersect exactly at a single
   * point in their relative interiors.  For robustness, here the
   * relative interior of a Line_Segment is consider to be any Point
   * in the Line_Segment which is a distance greater than \a epsilon
   * from both endpoints.
   */
  bool intersect_proper(const Line_Segment& line_segment1,
			const Line_Segment& line_segment2,
			double epsilon=0.0);


  /** \brief  intersection of Line_Segments
   *
   * \author  Karl J. Obermeyer
   * \return a Line_Segment of size 0, 1, or 2 
   * \remarks  size 0 results if the distance (or at least the
   * floating-point computed distance) between line_segment1 and
   * line_segment2 is (strictly) greater than epsilon. size 1 results
   * if the segments intersect poperly, form a T intersection, or --
   * intersection.  size 2 results when two or more endpoints are a
   * Euclidean distance no greater than \a epsilon from the opposite
   * segment, and the overlap of the segments has a length greater
   * than \a epsilon.
   */
  Line_Segment intersection(const Line_Segment& line_segment1,
			    const Line_Segment& line_segment2,
			    double epsilon=0.0);


  /// print a Line_Segment
  std::ostream& operator << (std::ostream& outs, 
			     const Line_Segment& line_segment_temp);
  

  /** \brief  angle in radians represented by a value in 
   *          the interval [0,2*M_PI]
   *
   * \remarks  the intended interpretation is that angles 0 and 2*M_PI
   * correspond to the positive x-axis of the coordinate system
   */
  class Angle
  {
  public:
    //Constructors
    /** \brief  default
     *
     * \remarks  data defaults to NAN so that checking whether the
     * data are numbers can be used as a precondition in functions
     */
    Angle() : angle_radians_(NAN) { }
    /// construct from real value, mod into interval [0, 2*M_PI)
    Angle(double data_temp);
    /** \brief construct using 4 quadrant inverse tangent into [0, 2*M_PI),
     *         where 0 points along the x-axis
     */ 
    Angle(double rise_temp, double run_temp);
    //Accessors
    /// get radians
    double get() const { return angle_radians_; }
    //Mutators
    /// set angle, mod into interval [0, 2*PI)
    void set(double data_temp);
    /** \brief  set angle data to 2*M_PI
     *
     * \remarks  sometimes it is necessary to set the angle value to
     * 2*M_PI instead of 0, so that the lex. inequalities behave
     * appropriately during a radial line sweep
    */
    void set_to_2pi() { angle_radians_=2*M_PI; }
    /// set to new random angle in [0, 2*M_PI)
    void randomize();
  private:
    double angle_radians_;
  };


  /// compare angle radians
  bool operator  == (const Angle& angle1, const Angle& angle2);
  /// compare angle radians
  bool operator  != (const Angle& angle1, const Angle& angle2);


  /// compare angle radians
  bool operator  >  (const Angle& angle1, const Angle& angle2);
  /// compare angle radians
  bool operator  <  (const Angle& angle1, const Angle& angle2);
  /// compare angle radians
  bool operator  >= (const Angle& angle1, const Angle& angle2);
  /// compare angle radians
  bool operator  <= (const Angle& angle1, const Angle& angle2);


  /// add angles' radians and mod into [0, 2*M_PI)
  Angle operator +  (const Angle& angle1, const Angle& angle2);
  /// subtract angles' radians and mod into [0, 2*M_PI)
  Angle operator -  (const Angle& angle1, const Angle& angle2);


  /** \brief  geodesic distance in radians between Angles
   *
   * \author  Karl J. Obermeyer
   * \pre  \a angle1 and \a angle2 data are numbers
   */
  double geodesic_distance(const Angle& angle1, const Angle& angle2);


  /** \brief  1.0 => geodesic path from angle1 to angle2 
   *          is couterclockwise, -1.0 => clockwise
   *
   * \author  Karl J. Obermeyer
   * \pre  \a angle1 and \a angle2 data are numbers
   */
  double geodesic_direction(const Angle& angle1, const Angle& angle2); 


  /// print Angle
  std::ostream& operator << (std::ostream& outs, const Angle& angle_temp);


  /** \brief  Point in the plane packaged together with polar
   *          coordinates w.r.t. specified origin
   *
   * The origin of the polar coordinate system is stored with the
   * Polar_Point (in \a polar_origin_) and bearing is measured ccw from the
   * positive x-axis.
   * \remarks  used, e.g., for radial line sweeps
   */
  class Polar_Point : public Point
  {
  public:
    //Constructors
    /** \brief  default
     *
     * \remarks  Data defaults to NAN so that checking whether the
     * data are numbers can be used as a precondition in functions.
     */
    Polar_Point() : Point(), range_(NAN), bearing_(NAN) { }
    /** \brief  construct from (Cartesian) Points
     *
     * \pre  member data of \a polar_origin_temp and \a point_temp have
     * been assigned (numbers)
     * \param polar_origin_temp  the origin of the polar coordinate system
     * \param point_temp  the point to be represented
     * \remarks  if polar_origin_temp == point_temp, the default
     * bearing is Angle(0.0)
     */
    Polar_Point(const Point& polar_origin_temp,
		const Point& point_temp,
		double epsilon=0.0);
    //Accessors
    /** \brief  origin of the polar coordinate system in which the point is
     *          represented
     */
    Point polar_origin() const { return polar_origin_; }
    /// Euclidean distance from the point represented to the origin of
    /// the polar coordinate system
    double       range() const { return range_; }
    /// bearing from polar origin w.r.t. direction parallel to x-axis
    Angle      bearing() const { return bearing_; }
    //Mutators
    /** \brief  set the origin of the polar coordinate system
     *
     * \remarks x and y held constant, bearing and range modified
     * accordingly
     */
    void set_polar_origin(const Point& polar_origin_temp);
    /** \brief  set x
     *
     * \remarks  polar_origin held constant, bearing and range modified
     * accordingly
     */
    void set_x(double x_temp);
    /** \brief  set y
     *
     * \remarks  polar_origin held constant, bearing and range modified
     * accordingly
     */
    void set_y(double y_temp);
    /** \brief set range
     *
     * \remarks  polar_origin held constant, x and y modified
     * accordingly
     */
    void set_range(double range_temp);
    /** \brief set bearing
     *
     * \remarks  polar_origin and range held constant, x and y modified
     * accordingly
     */
    void set_bearing(const Angle& bearing_temp);
    /** \brief  set bearing Angle data to 2*M_PI
     *
     * \remarks  Special function for use in computations involving a
     * radial line sweep; sometimes it is necessary to set the angle
     * value to 2*PI instead of 0, so that the lex. inequalities
     * behave appropriately
     */
    void set_bearing_to_2pi() { bearing_.set_to_2pi(); }
  protected:
    //Origin of the polar coordinate system in world coordinates.
    Point  polar_origin_;
    //Polar coordinates where radius always positive, and angle
    //measured ccw from the world coordinate system's x-axis.
    double range_;
    Angle  bearing_;
  };


  /** \brief  compare member data
   *
   * \remarks  returns false if any member data are NaN
   */
  bool operator == (const Polar_Point& polar_point1,
		    const Polar_Point& polar_point2);
  bool operator != (const Polar_Point& polar_point1,
		    const Polar_Point& polar_point2);


  /** \brief  compare according to polar lexicographic order 
   *          (smaller bearing, then smaller range)
   *
   *  false if any member data have not been assigned (numbers)
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a radial line
   */
  bool operator >  (const Polar_Point& polar_point1,
			   const Polar_Point& polar_point2);
  /** \brief  compare according to polar lexicographic order 
   *          (smaller bearing, then smaller range)
   *
   *  false if any member data have not been assigned (numbers)
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a radial line
   */ 
  bool operator <  (const Polar_Point& polar_point1,
			   const Polar_Point& polar_point2);
  /** \brief  compare according to polar lexicographic order 
   *          (smaller bearing, then smaller range)
   *
   *  false if any member data have not been assigned (numbers)
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a radial line
   */ 
  bool operator >= (const Polar_Point& polar_point1,
			   const Polar_Point& polar_point2);
  /** \brief  compare according to polar lexicographic order 
   *          (smaller bearing, then smaller range)
   *
   *  false if any member data have not been assigned (numbers)
   * \remarks  lex. comparison is very sensitive to perturbations if
   * two Points nearly define a radial line
   */ 
  bool operator <= (const Polar_Point& polar_point1,
			   const Polar_Point& polar_point2);


  /// print Polar_Point
  std::ostream& operator << (std::ostream& outs,
			     const Polar_Point& polar_point_temp);


  /// ray in the plane represented by base Point and bearing Angle
  class Ray
  {
  public:
    //Constructors
    /** \brief  default
     *
     * \remarks data defaults to NAN so that checking whether the data
     * are numbers can be used as a precondition in functions
     */
    Ray() { }
    /// construct ray emanating from \a base_point_temp in the direction
    /// \a bearing_temp
    Ray(Point base_point_temp, Angle bearing_temp) : 
    base_point_(base_point_temp) , bearing_(bearing_temp) {}
    /// construct ray emanating from \a base_point_temp towards
    /// \a bearing_point
    Ray(Point base_point_temp, Point bearing_point);
    //Accessors
    /// get base point
    Point base_point() const { return base_point_; }
    /// get bearing
    Angle bearing() const { return bearing_; }
    //Mutators
    /// set base point
    void set_base_point(const Point& point_temp)
    { base_point_ = point_temp; }
    /// set bearing
    void set_bearing(const Angle& angle_temp)
    { bearing_ = angle_temp; } 
  private:
    Point base_point_;
    Angle bearing_;
  };


  /** \brief  compare member data
   *
   * \remarks  returns false if any member data are NaN
   */
  bool operator == (const Ray& ray1,
		    const Ray& ray2);
  /** \brief  compare member data
   *
   * \remarks  negation of ==
   */
  bool operator != (const Ray& ray1,
		    const Ray& ray2);


  /** \brief compute the intersection of a Line_Segment with a Ray
   *
   * \author  Karl J. Obermeyer
   * \pre  member data of \a ray_temp has been assigned (numbers) and
   * \a line_segment_temp has size greater than 0
   * \remarks  as a convention, if the intersection has positive
   * length, the Line_Segment returned has the first point closest to
   * the Ray's base point
   */
  Line_Segment intersection(const Ray ray_temp,
			    const Line_Segment& line_segment_temp,
			    double epsilon=0.0);
  /** \brief compute the intersection of a Line_Segment with a Ray
   *
   * \author  Karl J. Obermeyer
   * \pre  member data of \a ray_temp has been assigned (numbers) and
   * \a line_segment_temp has size greater than 0
   * \remarks  as a convention, if the intersection has positive
   * length, the Line_Segment returned has the first point closest to
   * the Ray's base point
   */
  Line_Segment intersection(const Line_Segment& line_segment_temp,
			    const Ray& ray_temp,
			    double epsilon=0.0);


  ///oriented polyline in the plane represented by list of vertices
  class Polyline
  {
  public:
    friend class Point;
    //Constructors
    /// default to empty
    Polyline() { }
    /// construct from vector of vertices
    Polyline(const std::vector<Point>& vertices_temp)
    { vertices_ = vertices_temp; }
    //Accessors
    /** \brief  raw access
     *
     * \remarks for efficiency, no bounds checks; usually trying to
     * access out of bounds causes a bus error
     */
    Point operator [] (unsigned i) const
    { return vertices_[i]; }
    /// vertex count
    unsigned size() const
    { return vertices_.size(); }
    /// Euclidean length of the Polyline
    double length() const;
    /** \brief  Euclidean diameter
     *
     * \pre  Polyline has greater than 0 vertices
     * \return  the maximum Euclidean distance between all pairs of
     * vertices
     * \remarks  time complexity O(n^2), where n is the number of
     * vertices representing the Polyline
     */
    double diameter() const;
    //a box which fits snugly around the Polyline
    Bounding_Box bbox() const;
    //Mutators
    /** \brief  raw access
     *
     * \remarks for efficiency, no bounds checks; usually trying to
     * access out of bounds causes a bus error
     */
    Point& operator [] (unsigned i)
    { return vertices_[i]; }
    /// erase all points
    void clear()
    { vertices_.clear(); }
    /// add a vertex to the back (end) of the list
    void push_back(const Point& point_temp)
    { vertices_.push_back(point_temp); }
    /// delete a vertex to the back (end) of the list
    void pop_back()
    { vertices_.pop_back(); }
    /// reset the whole list of vertices at once
    void set_vertices(const std::vector<Point>& vertices_temp)
    { vertices_ = vertices_temp; }
    /** \brief  eliminates vertices which are (\a epsilon) - colinear
     *          with their respective neighbors
     *
     * \author  Karl J. Obermeyer
     * \post  the Euclidean distance between each vertex and the line
     * segment connecting its neighbors is at least \a epsilon
     * \remarks time complexity O(n), where n is the number of
     * vertices representing the Polyline.  
     */
    void eliminate_redundant_vertices(double epsilon=0.0);
    //Reduce number of vertices in representation...
    //void smooth(double epsilon);
    /// reverse order of vertices
    void reverse();
    /// append the points from another polyline
    void append( const Polyline& polyline );
  private:
    std::vector<Point> vertices_;
  };


  //print Polyline
  std::ostream& operator << (std::ostream& outs,
			     const Polyline& polyline_temp);


  /** \brief simple polygon in the plane represented by list of vertices
   *
   * Simple here means non-self-intersecting.  More precisely, edges
   * should not (i) intersect with an edge not adjacent to it, nor
   * (ii) intersect at more than one Point with an adjacent edge.
   * \remarks  vertices may be listed cw or ccw
   */
  class Polygon
  {
  public:
    friend class Point;
    //Constructors
    ///default to empty
    Polygon() { }
    /** \brief  construct from *.polygon file
     *
     * \author  Karl J. Obermeyer
     * \remarks  for efficiency, simplicity check not called here
     */
    Polygon(const std::string& filename);
    /** \brief  construct from vector of vertices
     *
     * \remarks  for efficiency, simplicity check not called here
     */
    Polygon(const std::vector<Point>& vertices_temp);
    /** \brief  construct triangle from 3 Points
     *
     * \remarks  for efficiency, simplicity check not called here
     */
    Polygon(const Point& point0, const Point& point1, const Point& point2);
    //Accessors
    /** \brief  access with automatic wrap-around in forward direction
     *
     * \remarks  For efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    const Point& operator [] (unsigned i) const 
    { return vertices_[i % vertices_.size()]; }
    /** \brief  vertex count
     *
     * \remarks  O(1) time complexity
     */
    unsigned n() const { return vertices_.size(); }
    /** \brief  reflex vertex count (nonconvex vertices)
     *  
     * \author  Karl J. Obermeyer
     * \remarks  Works regardless of polygon orientation (ccw vs cw),
     * but assumes no redundant vertices.  Time complexity O(n), where
     * n is the number of vertices representing the Polygon
     */
    unsigned r() const;
    /** \brief  true iff Polygon is (\a epsilon) simple 
     *
     * \author  Karl J. Obermeyer
     *
     * \remarks A Polygon is considered \a epsilon -simple iff (i) the
     * Euclidean distance between nonadjacent edges is no greater than
     * \a epsilon, (ii) adjacent edges intersect only at their single
     * shared Point, (iii) and it has at least 3 vertices.  One
     * consequence of these conditions is that there can be no
     * redundant vertices.
     */
    bool is_simple(double epsilon=0.0) const;
    /** \brief true iff lexicographically smallest vertex is first in 
     *         the list of vertices representing the Polygon
     *
     * \author  Karl J. Obermeyer
     * \remarks  lex. comparison is very sensitive to perturbations if
     * two Points nearly define a line parallel to one of the axes
     */
    bool is_in_standard_form() const;
    /// perimeter length
    double boundary_length() const;
    /** oriented area of the Polygon
     *
     * \author  Karl J. Obermeyer
     * \pre Polygon is simple, but for efficiency simplicity is not asserted.
     * area > 0 => vertices listed ccw, 
     * area < 0 => cw
     * \remarks O(n) time complexity, where n is the number
     * of vertices representing the Polygon
     */
    double area() const;
    /** \brief  Polygon's centroid (center of mass)
     *
     * \author  Karl J. Obermeyer
     * \pre Polygon has greater than 0 vertices and is simple,
     * but for efficiency simplicity is not asserted
     */
    Point  centroid() const;
    /** \brief  Euclidean diameter
     *
     * \pre Polygon has greater than 0 vertices
     * \return  maximum Euclidean distance between all pairs of
     * vertices
     * \remarks  time complexity O(n^2), where n is the number of
     * vertices representing the Polygon
     */
    double diameter() const;
    /** \brief box which fits snugly around the Polygon
     *
     * \author  Karl J. Obermeyer
     * \pre Polygon has greater than 0 vertices
     */
    Bounding_Box bbox() const;
    // Returns a vector of n pts randomly situated in the polygon.
    std::vector<Point> random_points(const unsigned& count,
				     double epsilon=0.0) const;
    /** \brief  write list of vertices to *.polygon file 
     *
     * \author  Karl J. Obermeyer
     * Uses intuitive human and computer readable decimal format with
     * display precision \a fios_precision_temp.
     * \pre  \a fios_precision_temp >=1
     */
    void write_to_file(const std::string& filename,
		       int fios_precision_temp=FIOS_PRECISION);
    //Mutators
    /** \brief  access with automatic wrap-around in forward direction
     *
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    Point& operator [] (unsigned i) { return vertices_[i % vertices_.size()]; }
    /// set vertices using STL vector of Points
    void set_vertices(const std::vector<Point>& vertices_temp)
    { vertices_ = vertices_temp; }
    /// push a Point onto the back of the vertex list
    void push_back(const Point& vertex_temp )
    { vertices_.push_back( vertex_temp ); }
    /// erase all vertices
    void clear()
    { vertices_.clear(); }
    /** \brief enforces that the lexicographically smallest vertex is first
     *         in the list of vertices representing the Polygon
     *
     * \author  Karl J. Obermeyer
     * \remarks  O(n) time complexity, where n is the number of
     * vertices representing the Polygon.  Lex. comparison is very
     * sensitive to perturbations if two Points nearly define a line
     * parallel to one of the axes
     */
    void enforce_standard_form();
    /** \brief  eliminates vertices which are (\a epsilon) - colinear
     *          with their respective neighbors
     *
     * \author  Karl J. Obermeyer
     * \post  the Euclidean distance between each vertex and the line
     * segment connecting its neighbors is at least \a epsilon, and the
     * Polygon is in standard form
     * \remarks  time complexity O(n), where n is the number of
     * vertices representing the the Polygon  
     */
    void eliminate_redundant_vertices(double epsilon=0.0);
    /** \brief  reverse (cyclic) order of vertices
     *
     * \remarks  vertex first in list is held first
     */
    void reverse();
  protected:
    std::vector<Point> vertices_;
  };


  /** \brief  true iff vertex lists are identical, but false if either
   *          Polygon has size 0
   *
   * \remarks returns false if either Polygon has size 0
   * \remarks  O(n) time complexity
   */
  bool operator == (Polygon polygon1, Polygon polygon2);
  bool operator != (Polygon polygon1, Polygon polygon2);
  /** \brief true iff the Polygon's vertices match up w/in a (closed)
   *         epsilon ball of each other, but false if either Polygon
   *         has size 0
   *
   * Respects number, ordering, and orientation of vertices, i.e.,
   * even if the (conceptual) polygons represented by two Polygons are
   * identical, they are not considered \a epsilon - equivalent unless
   * the number of vertices is the same, the orientations are the same
   * (cw vs. ccw list), and the Points of the vertex lists match up
   * within epsilon.  This function does attempt to match the polygons
   * for all possible cyclic permutations, hence the quadratic time
   * complexity.
   * \author  Karl J. Obermeyer 
   * \remarks  O(n^2) time complexity, where n is the number of
   * vertices representing the polygon
   */
  bool equivalent(Polygon polygon1, Polygon polygon2,
		  double epsilon=0.0);


  /** \brief  Euclidean distance between Polygons' boundaries
   *
   * \author  Karl J. Obermeyer
   * \pre  \a polygon1 and \a polygon2 each have greater than 0 vertices
   */
  double boundary_distance( const Polygon& polygon1,
			    const Polygon& polygon2 );

  
  //print Polygon
  std::ostream& operator << (std::ostream& outs,
			     const Polygon& polygon_temp);

  
  /** \brief  environment represented by simple polygonal outer boundary
   *          with simple polygonal holes
   *
   * \remarks  For methods to work correctly, the outer boundary vertices must
   * be listed ccw and the hole vertices cw
   */
  class Environment
  {
  public:
    friend class Point;
    //Constructors
    /// default to empty
    Environment() { }
    /** \brief  construct Environment without holes
     *
     * \remarks  time complexity O(n), where n is the number of vertices
     * representing the Environment
     */
    Environment(const Polygon& polygon_temp)
    { outer_boundary_=polygon_temp; update_flattened_index_key(); }
    /** \brief  construct Environment with holes from STL vector of Polygons
     *
     *  the first Polygon in the vector becomes the outer boundary,
     *  the rest become the holes
     * \remarks  time complexity O(n), where n is the number of vertices
     * representing the Environment
     */
    Environment(const std::vector<Polygon>& polygons);
    /** construct from *.environment file.
     *
     * \author  Karl J. Obermeyer
     * \remarks  time complexity O(n), where n is the number of vertices
     * representing the Environment
     */
    Environment(const std::string& filename);
    //Accessors
    /** \brief  raw access to Polygons
     *
     * An argument of 0 accesses the outer boundary, 1 and above
     * access the holes.
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    const Polygon& operator [] (unsigned i) const
    { if(i==0){return outer_boundary_;} else{return holes_[i-1];} }
    /** \brief  raw access to vertices via flattened index
     *
     * By flattened index is intended the label given to a vertex if
     * you were to label all the vertices from 0 to n-1 (where n is
     * the number of vertices representing the Environment) starting
     * with the first vertex of the outer boundary and progressing in
     * order through all the remaining vertices of the outer boundary
     * and holes.
     *
     * \remarks  Time complexity O(1).  For efficiency, no bounds
     * check; usually trying to access out of bounds causes a bus
     * error.
     */
    const Point& operator () (unsigned k) const;
    /// hole count
    unsigned h() const { return holes_.size(); }
    /** \brief  vertex count
     *
     * \remarks  time complexity O(h)
     */
    unsigned n() const;
    /** \brief  total reflex vertex count (nonconvex vertices)
     *  
     * \author  Karl J. Obermeyer
     * \remarks  time complexity O(n), where n is the number of
     * vertices representing the Environment
     */
    unsigned r() const;
    /** \brief  true iff lexicographically smallest vertex is first in 
     *          each list of vertices representing a Polygon of the
     *          Environment
     *
     * \author  Karl J. Obermeyer
     * \remarks  lex. comparison is very sensitive to perturbations if
     * two Points nearly define a line parallel to one of the axes
     */    
    bool is_in_standard_form() const;
    /** \brief  true iff \a epsilon -valid
     *
     * \a epsilon -valid means (i) the outer boundary and holes are
     * pairwise \a epsilon -disjoint (no two features should come
     * within \a epsilon of each other) simple polygons, (ii) outer
     * boundary is oriented ccw, and (iii) holes are oriented cw.
     *
     * \author  Karl J. Obermeyer
     *
     * \pre  Environment has greater than 2 vertices
     * (otherwise it can't even have nonzero area)
     * \remarks time complexity O(h^2*n^2), where h is the number of
     * holes and n is the number of vertices representing the
     * Environment
     */
    bool is_valid(double epsilon=0.0) const;
    /** \brief  sum of perimeter lengths of outer boundary and holes
     *
     * \author  Karl J. Obermeyer
     * \remarks O(n) time complexity, where n is the number of
     * vertices representing the Environment
     */
    double boundary_length() const;
    /** \brief (obstacle/hole free) area of the Environment
     *
     * \author  Karl J. Obermeyer
     * \remarks  O(n) time complexity, where n is the number of
     * vertices representing the Environment
     */
    double area() const;
    /** \brief  Euclidean diameter
     *
     * \author  Karl J. Obermeyer
     * \pre Environment has greater than 0 vertices
     * \return  maximum Euclidean distance between all pairs of
     * vertices
     * \remarks  time complexity O(n^2), where n is the number of
     * vertices representing the Environment
     */
    double diameter() const { return outer_boundary_.diameter(); }
    /** \brief box which fits snugly around the Environment
     *
     * \author  Karl J. Obermeyer
     * \pre Environment has greater than 0 vertices
     */
    Bounding_Box bbox() const { return outer_boundary_.bbox(); }
    /** \brief get STL vector of \a count Points randomly situated
     *         within \a epsilon of the Environment
     *
     * \author  Karl J. Obermeyer
     * \pre  the Environment has positive area
     */
    std::vector<Point> random_points(const unsigned& count,
				     double epsilon=0.0) const;
    /** \brief  compute a shortest path between 2 Points
     *
     * Uses the classical visibility graph method as described, e.g.,
     * in ``Robot Motion Planning" (Ch. 4 Sec. 1) by J.C. Latombe.
     * Specifically, an A* search is performed on the visibility graph
     * using the Euclidean distance as the heuristic function.
     *
     * \author  Karl J. Obermeyer
     *
     * \pre \a start and \a finish must be in the environment.
     * Environment must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \remarks  If multiple shortest path queries are made for the
     * same Envrionment, it is better to precompute the
     * Visibility_Graph. For a precomputed Visibility_Graph, the time
     * complexity of a shortest_path() query is O(n^2), where n is the
     * number of vertices representing the Environment.
     *
     * \todo  return not just one, but all shortest paths (w/in
     * epsilon), e.g., returning a std::vector<Polyline>)
     */
    Polyline shortest_path(const Point& start,
			   const Point& finish,
			   const Visibility_Graph& visibility_graph,
			   double epsilon=0.0);
    /** \brief  compute shortest path between 2 Points
     *
     * \author  Karl J. Obermeyer
     *
     * \pre \a start and \a finish must be in the environment.
     * Environment must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \remarks  For single shortest path query, visibility graph is
     * not precomputed.  Time complexity O(n^3), where n is the number
     * of vertices representing the Environment.
     */
    Polyline shortest_path(const Point& start,
			   const Point& finish,
			   double epsilon=0.0);
    /** \brief  compute the faces (partition cells) of an arrangement
     *          of Line_Segments inside the Environment
     *
     * \author  Karl J. Obermeyer
     * \todo  finish this
     */
    std::vector<Polygon> compute_partition_cells( std::vector<Line_Segment> 
						  partition_inducing_segments,
						  double epsilon=0.0 )
    {
      std::vector<Polygon> cells;
      return cells;
    }
    /** \brief  write lists of vertices to *.environment file 
     *
     * uses intuitive human and computer readable decimal format with
     * display precision \a fios_precision_temp
     * \author  Karl J. Obermeyer
     * \pre  \a fios_precision_temp >=1
     */
    void write_to_file(const std::string& filename,
		       int fios_precision_temp=FIOS_PRECISION);
    //Mutators
    /** \brief  raw access to Polygons
     *
     * An argument of 0 accesses the outer boundary, 1 and above
     * access the holes.
     * \author  Karl J. Obermeyer
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    Polygon& operator [] (unsigned i) 
    { if(i==0){return outer_boundary_;} else{return holes_[i-1];} }
    //Mutators
    /** \brief  raw access to vertices via flattened index
     *
     * By flattened index is intended the label given to a vertex if
     * you were to label all the vertices from 0 to n-1 (where n is
     * the number of vertices representing the Environment) starting
     * with the first vertex of the outer boundary and progressing in
     * order through all the remaining vertices of the outer boundary
     * and holes.
     * \author  Karl J. Obermeyer
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error.
     */
    Point& operator () (unsigned k);
    /// set outer boundary
    void set_outer_boundary(const Polygon& polygon_temp)
    { outer_boundary_ = polygon_temp; update_flattened_index_key(); }
    /// add hole
    void add_hole(const Polygon& polygon_temp)
    { holes_.push_back(polygon_temp); update_flattened_index_key(); }
    /** \brief enforces outer boundary vertices are listed ccw and
     *         holes listed cw, and that these lists begin with respective
     *         lexicographically smallest vertex
     *
     * \author  Karl J. Obermeyer
     * \remarks  O(n) time complexity, where n is the number of
     * vertices representing the Environment.  Lex. comparison is very
     * sensitive to perturbations if two Points nearly define a line
     * parallel to one of the axes.
     */
    void enforce_standard_form();
    /** \brief eliminates vertices which are (\a epsilon) - colinear
     *         with their respective neighbors
     *
     * \author  Karl J. Obermeyer
     * \post  the Euclidean distance between each vertex and the line
     * segment connecting its neighbors is at least \a epsilon
     * \remarks time complexity O(n), where n is the number of
     * vertices representing the the Environment
     */
    void eliminate_redundant_vertices(double epsilon=0.0);
    /** \brief  reverse (cyclic) order of vertices belonging to holes
     *          only
     *
     * \remarks  vertex first in each hole's list is held first
     */
    void reverse_holes();
  private:    
    Polygon outer_boundary_;
    //obstacles
    std::vector<Polygon> holes_;
    //allows constant time access to vertices via operator () with
    //flattened index as argument
    std::vector< std::pair<unsigned,unsigned> > flattened_index_key_;
    //Must call if size of outer_boundary and/or holes_ changes.  Time
    //complexity O(n), where n is the number of vertices representing
    //the Environment.
    void update_flattened_index_key();
    //converts flattened index to index pair (hole #, vertex #) in
    //time O(n), where n is the number of vertices representing the
    //Environment
    std::pair<unsigned,unsigned> one_to_two(unsigned k) const;
    //node used for search tree of A* search in shortest_path() method
    class Shortest_Path_Node
    {
    public:
      //flattened index of corresponding Environment vertex
      //convention vertex_index = n() => corresponds to start Point
      //vertex_index = n() + 1 => corresponds to finish Point
      unsigned vertex_index;
      //pointer to self in search tree.
      std::list<Shortest_Path_Node>::iterator search_tree_location;
      //pointer to parent in search tree.
      std::list<Shortest_Path_Node>::iterator parent_search_tree_location;
      //Geodesic distance from start Point.
      double cost_to_come;
      //Euclidean distance to finish Point.
      double estimated_cost_to_go;
      //std::vector<Shortest_Path_Node> expand();
      bool operator < (const Shortest_Path_Node& spn2) const
      { 
	double f1 = this->cost_to_come + this->estimated_cost_to_go;
	double f2 = spn2.cost_to_come + spn2.estimated_cost_to_go;
	if( f1 < f2 )
	  return true;
	else if( f2 < f1 )
	  return false;
	else if( this->vertex_index < spn2.vertex_index )
	  return true;
	else if( this->vertex_index > spn2.vertex_index )
	  return false;
	else if( &(*(this->parent_search_tree_location)) 
		 < &(*(spn2.parent_search_tree_location)) ) 
	  return true;
	else
	  return false;
      }
      // print member data for debugging
      void print() const
      {
	std::cout << "         vertex_index = " << vertex_index << std::endl
		  << "parent's vertex_index = " 
		  << parent_search_tree_location->vertex_index 
		  << std::endl
		  << "         cost_to_come = " << cost_to_come << std::endl
		  << " estimated_cost_to_go = " 
		  << estimated_cost_to_go << std::endl;	  
      }
    };
  };
  
  
  /// printing Environment
  std::ostream& operator << (std::ostream& outs,
			     const Environment& environment_temp);


  /** \brief  set of Guards represented by a list of Points
   */
  class Guards
  {
  public:
    friend class Visibility_Graph;
    //Constructors
    /// default to empty
    Guards() { }
    /** \brief  construct from *.guards file
     *
     * \author  Karl J. Obermeyer
     */
    Guards(const std::string& filename);
    /// construct from STL vector of Points
    Guards(const std::vector<Point>& positions)
    { positions_ = positions; }
    //Accessors
    /** \brief  raw access to guard position Points
     *
     * \author  Karl J. Obermeyer
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    const Point& operator [] (unsigned i) const { return positions_[i]; }
    /// guard count
    unsigned N() const { return positions_.size(); }
    /// true iff positions are lexicographically ordered
    bool are_lex_ordered() const;
    /// true iff no two guards are w/in epsilon of each other
    bool noncolocated(double epsilon=0.0) const;
    /// true iff all guards are located in \a polygon_temp
    bool in(const Polygon& polygon_temp, double epsilon=0.0) const;
    /// true iff all guards are located in \a environment_temp
    bool in(const Environment& environment_temp, double epsilon=0.0) const;
    /** \brief  Euclidean diameter
     *
     * \author  Karl J. Obermeyer
     * \pre  greater than 0 guards
     * \return  maximum Euclidean distance between all pairs of
     * vertices
     * \remarks  time complexity O(N^2), where N is the number of
     * guards
     */
    double diameter() const;
    /** \brief box which fits snugly around the Guards
     *
     * \author  Karl J. Obermeyer
     * \pre  greater than 0 guards
     */
    Bounding_Box bbox() const;
    /** \brief  write list of positions to *.guards file 
     *
     * Uses intuitive human and computer readable decimal format with
     * display precision \a fios_precision_temp.
     * \author  Karl J. Obermeyer
     * \pre  \a fios_precision_temp >=1
     */
    void write_to_file(const std::string& filename,
		       int fios_precision_temp=FIOS_PRECISION);
    //Mutators
    /** \brief  raw access to guard position Points
     *
     * \author  Karl J. Obermeyer
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    Point& operator [] (unsigned i) { return positions_[i]; }
    /// add a guard
    void push_back(const Point& point_temp)
    { positions_.push_back(point_temp); } 
    /// set positions with STL vector of Points
    void set_positions(const std::vector<Point>& positions_temp)
    { positions_ = positions_temp; }
    /** \brief sort positions in lexicographic order
     *
     * from (lowest x, then lowest y) to (highest x, then highest y)
     * \author  Karl J. Obermeyer
     * \remarks time complexity O(N logN), where N is the guard count.
     * Lex. comparison is very sensitive to perturbations if two
     * Points nearly define a line parallel to one of the axes.
     */
    void enforce_lex_order();
    /// reverse order of positions
    void reverse();
    /** \brief relocate each guard to closest vertex if within
     *         \a epsilon of some vertex (of \a environment_temp)
     *   
     * \author  Karl J. Obermeyer
     * \pre  the guards' position data are numbers and \a environment_temp 
     * is nonempty
     * \post if a guard was a Euclidean distance no greater
     * than \a epsilon from any vertex of \a environment_temp, then it
     * will be repositioned to coincide with the closest such vertex
     * \remarks O(N*n) time complexity, where N is the guard count
     * and n is the number of vertices in \a environment_temp.
     */
    void snap_to_vertices_of(const Environment& environment_temp,
			     double epsilon=0.0);

    /** \brief relocate each guard to closest vertex if within
     *         \a epsilon of some vertex (of \a environment_temp)
     *   
     * \author  Karl J. Obermeyer
     * \pre  the guards' position data are numbers and \a polygon_temp 
     * is nonempty
     * \post if a guard was a Euclidean distance no greater
     * than \a epsilon from any vertex of \a polygon_temp, then it
     * will be repositioned to coincide with the closest such vertex
     * \remarks O(N*n) time complexity, where N is the guard count
     * and n is the number of vertices in \a polygon_temp
     */
    void snap_to_vertices_of(const Polygon& polygon_temp,
			     double epsilon=0.0);
    /** \brief  relocate each guard to closest Point on boundary if
     *	        within \a epsilon of the boundary (of \a environment_temp)
     *
     * \author  Karl J. Obermeyer
     * \pre  the guards' position data are numbers and \a environment_temp
     * is nonempty
     * \post If the calling Point was a Euclidean distance no greater
     * than \a epsilon from the boundary of \a environment_temp, then it
     * will be repositioned to it's projection onto that boundary
     * \remarks O(N*n) time complexity, where N is the guard count and
     * n is the number of vertices in \a environment_temp
     */
    void snap_to_boundary_of(const Environment& environment_temp,
			     double epsilon=0.0);
    /** \brief  relocate each guard to closest Point on boundary if
     *	        within \a epsilon of the boundary (of \a polygon_temp)
     *
     * \author  Karl J. Obermeyer
     * \pre  the guards' position data are numbers and \a polygon_temp
     * is nonempty
     * \post If the calling Point was a Euclidean distance no greater
     * than \a epsilon from the boundary of \a polygon_temp, then it
     * will be repositioned to it's projection onto that boundary
     * \remarks O(N*n) time complexity, where N is the guard count and
     * n is the number of vertices in \a polygon_temp
     */
    void snap_to_boundary_of(const Polygon& polygon_temp,
			     double epsilon=0.0);
  private:
    std::vector<Point> positions_;
  };


  /// print Guards
  std::ostream& operator << (std::ostream& outs,
			     const Guards& guards);


  /** \brief visibility polygon of a Point in an Environment or Polygon
   *
   * A Visibility_Polygon represents the closure of the set of all
   * points in an environment which are {\it clearly visible} from a
   * point (the observer).  Two Points p1 and p2 are (mutually) {\it
   * clearly visible} in an environment iff the relative interior of
   * the line segment connecting p1 and p2 does not intersect the
   * boundary of the environment.
   *
   * \remarks  average case time complexity O(n log(n)), where n is the
   * number of vertices in the Evironment (resp. Polygon).  Note the
   * Standard Library's sort() function performs O(n log(n))
   * comparisons (both average and worst-case) and the sort() member
   * function of an STL list performs "approximately O(n log(n))
   * comparisons".  For robustness, any Point (observer) should be \a
   * epsilon -snapped to the environment boundary and vertices before
   * computing its Visibility_Polygon (use the Point methods
   * snap_to_vertices_of(...) and snap_to_boundary_of(...) ).
   */
  class Visibility_Polygon : public Polygon
  {
  public:
    //Constructors
    /// default to empty
    Visibility_Polygon() { }
    //:TRICKY:
    /** \brief visibility set of a Point in an Environment
     *
     * \author  Karl J. Obermeyer
     *
     * \pre \a observer is in \a environment_temp (w/in \a epsilon )
     * and has been epsilon-snapped to the Environment using the
     * method Point::snap_to_boundary_of() followed by (order is
     * important) Point::snap_to_vertices_of(). \a environment_temp
     * must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \remarks  O(n log(n)) average case time complexity, where n is the
     * number of vertices in the Evironment (resp. Polygon).
     */
    Visibility_Polygon(const Point& observer,
		       const Environment& environment_temp,
		       double epsilon=0.0); 
    /** \brief visibility set of a Point in a Polygon
     *
     * \pre \a observer is in \a polygon_temp (w/in \a epsilon ) and
     * has been epsilon-snapped to the Polygon using the methods
     * Point::snap_to_vertices_of() and Point::snap_to_boundary_of().
     * \a environment_temp must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \remarks  less efficient because constructs an Environment from
     * a Polygon and then calls the other Visibility_Polygon constructor.
     * O(n log(n)) average case time complexity, where n is the
     * number of vertices in the Evironment (resp. Polygon).
     */
    Visibility_Polygon(const Point& observer, 
		       const Polygon& polygon_temp,
		       double epsilon=0.0); 
    //Accessors
    //std::vector<bool> get_gap_edges(double epsilon=0.0) { return gap_edges_; }
    /// location of observer which induced the visibility polygon
    Point observer() const
    { return observer_; }
    //Mutators 
  private:
    //ith entry of gap_edges is true iff the edge following ith vertex
    //is a gap edge (not solid).
    //std::vector<bool> gap_edges_;
    Point observer_;

    struct Polar_Edge
    {
      Polar_Point first;
      Polar_Point second;
      Polar_Edge() { }
      Polar_Edge(const Polar_Point& ppoint1,
		 const Polar_Point& ppoint2) :
	first(ppoint1), second(ppoint2) {}
    };    
 
    class Polar_Point_With_Edge_Info : public Polar_Point
    {
    public:      
      std::list<Polar_Edge>::iterator incident_edge;
      bool is_first;  //True iff polar_point is the first_point of the
		      //Polar_Edge pointed to by
		      //incident_edge.      
      void set_polar_point(const Polar_Point& ppoint_temp)
      {
	set_polar_origin( ppoint_temp.polar_origin() );	
	set_x( ppoint_temp.x() );
	set_y( ppoint_temp.y() );
	set_range( ppoint_temp.range() );
	set_bearing( ppoint_temp.bearing() );
      }
      //The operator < is the same as for Polar_Point with one
      //exception.  If two vertices have equal coordinates, but one is
      //the first point of its respecitve edge and the other is the
      //second point of its respective edge, then the vertex which is
      //the second point of its respective edge is considered
      //lexicographically smaller.
      friend bool operator < (const Polar_Point_With_Edge_Info& ppwei1,
		       const Polar_Point_With_Edge_Info& ppwei2)
      {
	if( Polar_Point(ppwei1) == Polar_Point(ppwei2) 
	    and !ppwei1.is_first and ppwei2.is_first )
	  return true;
	else
	  return Polar_Point(ppwei1) < Polar_Point(ppwei2);
      }
    };

    //Strict weak ordering (acts like <) for pointers to Polar_Edges.
    //Used to sort the priority_queue q2 used in the radial line sweep
    //of Visibility_Polygon constructors.  Let p1 be a pointer to
    //Polar_Edge e1 and p2 be a pointer to Polar_Edge e2.  Then p1 is
    //considered greater (higher priority) than p2 if the distance
    //from the observer (pointed to by observer_pointer) to e1 along
    //the direction to current_vertex is smaller than the distance
    //from the observer to e2 along the direction to current_vertex.
    class Incident_Edge_Compare
    {
      const Point *const observer_pointer;
      const Polar_Point_With_Edge_Info *const current_vertex_pointer;
      double epsilon;
    public:
      Incident_Edge_Compare(const Point& observer,
			    const Polar_Point_With_Edge_Info& current_vertex,
			    double epsilon_temp) :
	observer_pointer(&observer), 
	current_vertex_pointer(&current_vertex),
	epsilon(epsilon_temp) { }
      bool operator () (std::list<Polar_Edge>::iterator e1,
			std::list<Polar_Edge>::iterator e2) const
      {
	Polar_Point k1, k2;
	Line_Segment xing1 = intersection( Ray(*observer_pointer, 
					   current_vertex_pointer->bearing()), 
					   Line_Segment(e1->first,
							e1->second),
					   epsilon);
	Line_Segment xing2 = intersection( Ray(*observer_pointer, 
					   current_vertex_pointer->bearing()), 
					   Line_Segment(e2->first,
							e2->second),
					   epsilon);
	if( xing1.size() > 0 and xing2.size() > 0 ){
	  k1 = Polar_Point( *observer_pointer,
			    xing1.first() );
	  k2 = Polar_Point( *observer_pointer,
			    xing2.first() );
	  if( k1.range() <= k2.range() )
	    return false;
	  return true;
	}
	//Otherwise infeasible edges are given higher priority, so they
	//get pushed out the top of the priority_queue's (q2's)
	//heap.
	else if( xing1.size() == 0 and xing2.size() > 0 )
	  return false;
	else if( xing1.size() > 0 and xing2.size() == 0 )
	  return true;
	else
	  return true;
      }
    };
    
    bool is_spike( const Point& observer,
		   const Point& point1,
		   const Point& point2,
		   const Point& point3, 
		   double epsilon=0.0 ) const;
    
    //For eliminating spikes as they appear.  In the
    //Visibility_Polygon constructors, these are checked every time a
    //Point is added to vertices.
    void chop_spikes_at_back(const Point& observer,
			     double epsilon);
    void chop_spikes_at_wrap_around(const Point& observer,
				    double epsilon);
    void chop_spikes(const Point& observer,
		     double epsilon);
    //For debugging Visibility_Polygon constructors.
    //Prints current_vertex and active_edge data to screen.
    void print_cv_and_ae(const Polar_Point_With_Edge_Info& current_vertex,
			 const std::list<Polar_Edge>::iterator&
			 active_edge);
  };


  /** \brief  visibility graph of points in an Environment,
   *          represented by adjacency matrix
   *
   * \remarks  used for shortest path planning in the
   * Environment::shortest_path() method
   *
   * \todo Add method to prune edges for faster shortest path
   * calculation, e.g., exclude concave vertices and only include
   * tangent edges as described in ``Robot Motion Planning" (Ch. 4
   * Sec. 1) by J.C. Latombe.
   */
  class  Visibility_Graph
  {
  public:
    //Constructors
    /// default to empty 
    Visibility_Graph() { n_=0; adjacency_matrix_ = NULL; }
    /// copy 
    Visibility_Graph( const Visibility_Graph& vg2 );
    /** \brief  construct the visibility graph of Environment vertices
     *
     * \author  Karl J. Obermeyer
     *
     * \pre \a environment must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \remarks  Currently this constructor simply computes the
     * Visibility_Polygon of each vertex and checks inclusion of the
     * other vertices, taking time complexity O(n^3), where n is the
     * number of vertices representing the Environment.  This time
     * complexity is not optimal. As mentioned in ``Robot Motion
     * Planning" by J.C. Latombe p.157, there are algorithms able to
     * construct a visibility graph for a polygonal environment with
     * holes in time O(n^2).  The nonoptimal algorithm is being used
     * temporarily because of (1) its ease to implement using the
     * Visibility_Polygon class, and (2) its apparent robustness.
     * Implementing the optimal algorithm robustly is future work.
     */
    Visibility_Graph(const Environment& environment, double epsilon=0.0);
    //Constructors
    /** \brief  construct the visibility graph of Points in an Environment
     *
     * \pre \a environment must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \author Karl J. Obermeyer \remarks Currently this constructor
     * simply computes the Visibility_Polygon of each Point and checks
     * inclusion of the other Points, taking time complexity
     * O(N n log(n) + N^2 n), where N is the number of Points and n is
     * the number of vertices representing the Environment.  This time
     * complexity is not optimal, but has been used for
     * simplicity. More efficient algorithms are discussed in ``Robot
     * Motion Planning" by J.C. Latombe p.157.
     */
    Visibility_Graph(const std::vector<Point> points,
		     const Environment& environment, double epsilon=0.0);
    //Constructors
    /** \brief  construct the visibility graph of Guards in an Environment
     *
     * \pre \a start and \a finish must be in the environment.
     * Environment must be \a epsilon -valid.  Test with
     * Environment::is_valid(epsilon).
     *
     * \author  Karl J. Obermeyer 
     * \remarks  Currently this constructor simply computes the
     * Visibility_Polygon of each guard and checks inclusion of the
     * other guards, taking time complexity O(N n log(n) + N^2 n),
     * where N is the number of guards and n is the number of vertices
     * representing the Environment.  This time complexity is not
     * optimal, but has been used for simplicity. More efficient
     * algorithms are discussed in ``Robot Motion Planning" by
     * J.C. Latombe p.157.
     */
    Visibility_Graph(const Guards& guards,
		     const Environment& environment, double epsilon=0.0);
    //Accessors
    /** \brief  raw access to adjacency matrix data
     *
     * \author  Karl J. Obermeyer
     * \param i1  Polygon index of first vertex
     * \param j1  index of first vertex within its Polygon
     * \param i2  Polygon index of second vertex
     * \param j2  index of second vertex within its Polygon
     * \return  true iff first vertex is visible from second vertex
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    bool operator () (unsigned i1,
		      unsigned j1,
		      unsigned i2,
		      unsigned j2) const;
    /** \brief  raw access to adjacency matrix data via flattened
     *          indices
     *
     * By flattened index is intended the label given to a vertex if
     * you were to label all the vertices from 0 to n-1 (where n is
     * the number of vertices representing the Environment) starting
     * with the first vertex of the outer boundary and progressing in
     * order through all the remaining vertices of the outer boundary
     * and holes.
     * \author  Karl J. Obermeyer
     * \param k1  flattened index of first vertex
     * \param k1  flattened index of second vertex
     * \return  true iff first vertex is visible from second vertex
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    bool operator () (unsigned k1,
		      unsigned k2) const;
    /// \brief  total number of vertices in corresponding Environment
    unsigned n() const { return n_; }
    //Mutators
    /** \brief  raw access to adjacency matrix data
     *
     * \author  Karl J. Obermeyer
     * \param i1  Polygon index of first vertex
     * \param j1  index of first vertex within its Polygon
     * \param i2  Polygon index of second vertex
     * \param j2  index of second vertex within its Polygon
     * \return  true iff first vertex is visible from second vertex
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    bool& operator () (unsigned i1,
		       unsigned j1,
		       unsigned i2,
		       unsigned j2);
    /** \brief  raw access to adjacency matrix data via flattened
     *          indices
     *
     * By flattened index is intended the label given to a vertex if
     * you were to label all the vertices from 0 to n-1 (where n is
     * the number of vertices representing the Environment) starting
     * with the first vertex of the outer boundary and progressing in
     * order through all the remaining vertices of the outer boundary
     * and holes.
     * \author  Karl J. Obermeyer
     * \param k1  flattened index of first vertex
     * \param k1  flattened index of second vertex
     * \return  true iff first vertex is visible from second vertex
     * \remarks  for efficiency, no bounds check; usually trying to
     * access out of bounds causes a bus error
     */
    bool& operator () (unsigned k1,
		       unsigned k2);
    /// assignment operator
    Visibility_Graph& operator = 
    (const Visibility_Graph& visibility_graph_temp);
    /// destructor
    virtual ~Visibility_Graph();
  private:
    //total number of vertices of corresponding Environment
    unsigned n_;
    //the number of vertices in each Polygon of corresponding Environment
    std::vector<unsigned> vertex_counts_;
    // n_-by-n_ adjacency matrix data stored as 2D dynamic array
    bool **adjacency_matrix_;
    //converts vertex pairs (hole #, vertex #) to flattened index
    unsigned two_to_one(unsigned i,
			unsigned j) const;
  };


  /// print Visibility_Graph adjacency matrix
  std::ostream& operator << (std::ostream& outs,
			     const Visibility_Graph& visibility_graph);

}
  
#endif //VISILIBITY_H
