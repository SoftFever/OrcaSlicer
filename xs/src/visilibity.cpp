/**
 * \file visilibity.cpp
 * \author Karl J. Obermeyer
 * \date March 20, 2008
 *
\remarks
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


#include "visilibity.hpp"  //VisiLibity header
#include <cmath>         //math functions in std namespace
#include <vector>
#include <queue>         //queue and priority_queue
#include <set>           //priority queues with iteration,
                         //integrated keys
#include <list>
#include <algorithm>     //sorting, min, max, reverse
#include <cstdlib>       //rand and srand
#include <ctime>         //Unix time
#include <fstream>       //file I/O
#include <iostream>
#include <cstring>       //gives C-string manipulation
#include <string>        //string class
#include <cassert>       //assertions


///Hide helping functions in unnamed namespace (local to .C file).
namespace
{
  
}


/// VisiLibity's sole namespace
namespace VisiLibity
{

  double uniform_random_sample(double lower_bound, double upper_bound)
  {
    assert( lower_bound <= upper_bound );
    if( lower_bound == upper_bound )
      return lower_bound;
    double sample_point;
    double span = upper_bound - lower_bound;
    sample_point = lower_bound 
                   + span * static_cast<double>( std::rand() )
                   / static_cast<double>( RAND_MAX );
    return sample_point;
  }


  //Point


  Point Point::projection_onto(const Line_Segment& line_segment_temp) const
  {
    assert( *this == *this
	    and line_segment_temp.size() > 0 );

    if(line_segment_temp.size() == 1)
      return line_segment_temp.first();
    //The projection of point_temp onto the line determined by
    //line_segment_temp can be represented as an affine combination
    //expressed in the form projection of Point =
    //theta*line_segment_temp.first +
    //(1.0-theta)*line_segment_temp.second.  if theta is outside
    //the interval [0,1], then one of the Line_Segment's endpoints
    //must be closest to calling Point.
    double theta = 
      ( (line_segment_temp.second().x()-x())
	*(line_segment_temp.second().x()
	  -line_segment_temp.first().x()) 
	+ (line_segment_temp.second().y()-y())
	*(line_segment_temp.second().y()
	  -line_segment_temp.first().y()) ) 
      / ( pow(line_segment_temp.second().x()
	      -line_segment_temp.first().x(),2)
	  + pow(line_segment_temp.second().y()
		-line_segment_temp.first().y(),2) );
    //std::cout << "\E[1;37;40m" << "Theta is: " << theta << "\x1b[0m"
    //<< std::endl;
    if( (0.0<=theta) and (theta<=1.0) )
      return theta*line_segment_temp.first() 
	+ (1.0-theta)*line_segment_temp.second();
    //Else pick closest endpoint.
    if( distance(*this, line_segment_temp.first()) 
	< distance(*this, line_segment_temp.second()) )
      return line_segment_temp.first();
    return line_segment_temp.second();
  }


  Point Point::projection_onto(const Ray& ray_temp) const
  {
    assert( *this == *this
	    and ray_temp == ray_temp );

    //Construct a Line_Segment parallel with the Ray which is so long,
    //that the projection of the the calling Point onto that
    //Line_Segment must be the same as the projection of the calling
    //Point onto the Ray.
    double R = distance( *this , ray_temp.base_point() );
    Line_Segment seg_approx =
      Line_Segment(  ray_temp.base_point(), ray_temp.base_point() +
		     Point( R*std::cos(ray_temp.bearing().get()),
			    R*std::sin(ray_temp.bearing().get()) )  );
    return projection_onto( seg_approx );
  }


 Point Point::projection_onto(const Polyline& polyline_temp) const
  {
    assert( *this == *this
	    and polyline_temp.size() > 0 );

    Point running_projection = polyline_temp[0];
    double running_min = distance(*this, running_projection);    
    Point point_temp;
    for(unsigned i=0; i<=polyline_temp.size()-1; i++){
      point_temp = projection_onto( Line_Segment(polyline_temp[i],
						 polyline_temp[i+1]) );
      if( distance(*this, point_temp) < running_min ){
	running_projection = point_temp;
	running_min = distance(*this, running_projection);
      }
    }
    return running_projection;
  }


  Point Point::projection_onto_vertices_of(const Polygon& polygon_temp) const
  {
    assert(*this == *this 
	   and polygon_temp.vertices_.size() > 0 );

    Point running_projection = polygon_temp[0];
    double running_min = distance(*this, running_projection);
    for(unsigned i=1; i<=polygon_temp.n()-1; i++){
      if( distance(*this, polygon_temp[i]) < running_min ){
	running_projection = polygon_temp[i];
	running_min = distance(*this, running_projection);
      }
    }
    return running_projection;
  }


  Point Point::projection_onto_vertices_of(const Environment&
					   environment_temp) const
  {
    assert(*this == *this 
	   and environment_temp.n() > 0 );

    Point running_projection 
      = projection_onto_vertices_of(environment_temp.outer_boundary_);
    double running_min = distance(*this, running_projection);   
    Point point_temp;
    for(unsigned i=0; i<environment_temp.h(); i++){
      point_temp = projection_onto_vertices_of(environment_temp.holes_[i]);
      if( distance(*this, point_temp) < running_min ){
	running_projection = point_temp;
	running_min = distance(*this, running_projection);
      }
    }
    return running_projection;
  }


  Point Point::projection_onto_boundary_of(const Polygon& polygon_temp) const
  {
    assert( *this == *this
	    and polygon_temp.n() > 0 );

    Point running_projection = polygon_temp[0];
    double running_min = distance(*this, running_projection);    
    Point point_temp;
    for(unsigned i=0; i<=polygon_temp.n()-1; i++){
      point_temp = projection_onto( Line_Segment(polygon_temp[i],
						 polygon_temp[i+1]) );
      if( distance(*this, point_temp) < running_min ){
	running_projection = point_temp;
	running_min = distance(*this, running_projection);
      }
    }
    return running_projection;
  }


  Point Point::projection_onto_boundary_of(const Environment&
					   environment_temp) const
  {
    assert( *this == *this
	    and environment_temp.n() > 0 );

    Point running_projection 
      = projection_onto_boundary_of(environment_temp.outer_boundary_);
    double running_min = distance(*this, running_projection);
    Point point_temp;
    for(unsigned i=0; i<environment_temp.h(); i++){
      point_temp = projection_onto_boundary_of(environment_temp.holes_[i]);
      if( distance(*this, point_temp) < running_min ){
	running_projection = point_temp;
	running_min = distance(*this, running_projection);
      }
    }
    return running_projection;
  }

  
  bool Point::on_boundary_of(const Polygon& polygon_temp,
			     double epsilon) const
  {
    assert( *this == *this
	    and polygon_temp.vertices_.size() > 0 );

    if( distance(*this, projection_onto_boundary_of(polygon_temp) ) 
	<= epsilon ){
      return true;
    }
    return false;
  }
 

  bool Point::on_boundary_of(const Environment& environment_temp,
			     double epsilon) const
  {
    assert( *this == *this
	    and environment_temp.outer_boundary_.n() > 0 );

    if( distance(*this, projection_onto_boundary_of(environment_temp) ) 
	<= epsilon ){
      return true;
    }
    return false;
  }    


  bool Point::in(const Line_Segment& line_segment_temp,
		 double epsilon) const
  {
    assert( *this == *this
	    and line_segment_temp.size() > 0 );

    if( distance(*this, line_segment_temp) < epsilon )
      return true;
    return false;
  }


  bool Point::in_relative_interior_of(const Line_Segment& line_segment_temp,
				      double epsilon) const
  {
    assert( *this == *this
	    and line_segment_temp.size() > 0 );
      
    return in(line_segment_temp, epsilon) 
      and distance(*this, line_segment_temp.first()) > epsilon
      and distance(*this, line_segment_temp.second()) > epsilon;
  }


  bool Point::in(const Polygon& polygon_temp,
		 double epsilon) const
  {
    assert( *this == *this
	    and polygon_temp.vertices_.size() > 0 );

    int n = polygon_temp.vertices_.size();
    if( on_boundary_of(polygon_temp, epsilon) )
      return true;
    // Then check the number of times a ray emanating from the Point
    // crosses the boundary of the Polygon.  An odd number of
    // crossings indicates the Point is in the interior of the
    // Polygon.  Based on
    // http://www.ecse.rpi.edu/Homepages/wrf/Research/Short_Notes/pnpoly.html
    int i, j; bool c = false;
    for (i = 0, j = n-1; i < n; j = i++){
      if ( (((polygon_temp[i].y() <= y()) 
	     and (y() < polygon_temp[j].y())) 
	    or ((polygon_temp[j].y() <= y()) 
		and (y() < polygon_temp[i].y()))) 
	   and ( x() < (polygon_temp[j].x() 
		       - polygon_temp[i].x()) 
		* (y() - polygon_temp[i].y()) 
		/ (polygon_temp[j].y() 
		   - polygon_temp[i].y()) 
		   + polygon_temp[i].x()) )     
	c = !c;
    }
    return c;
  }
 

  bool Point::in(const Environment& environment_temp, double epsilon) const
  {
    assert( *this == *this
	    and environment_temp.outer_boundary_.n() > 0 );

    //On outer boundary?
    if( on_boundary_of(environment_temp, epsilon) )
      return true;
    //Not in outer boundary?
    if( !in(environment_temp.outer_boundary_, epsilon) )
      return false;
    //In hole?
    for(unsigned i=0; i<environment_temp.h(); i++)
      if( in(environment_temp.holes_[i]) )
	return false;
    //Must be in interior.
    return true;
  }


  bool Point::is_endpoint_of(const Line_Segment& line_segment_temp,
			     double epsilon) const
  {
    assert( *this == *this
	    and line_segment_temp.size() > 0 );

    if( distance(line_segment_temp.first(), *this)<=epsilon 
	or distance(line_segment_temp.second(), *this)<=epsilon )
      return true;
    return false;
  }


  void Point::snap_to_vertices_of(const Polygon& polygon_temp,
				  double epsilon)
  {
    assert( *this == *this
	    and polygon_temp.n() > 0 );

    Point point_temp( this->projection_onto_vertices_of(polygon_temp) );
    if(  distance( *this , point_temp ) <= epsilon )
      *this = point_temp;
  }
  void Point::snap_to_vertices_of(const Environment& environment_temp,
				  double epsilon)
  {
    assert( *this == *this
	    and environment_temp.n() > 0 );

    Point point_temp( this->projection_onto_vertices_of(environment_temp) );
    if(  distance( *this , point_temp ) <= epsilon )
      *this = point_temp;
  }


  void Point::snap_to_boundary_of(const Polygon& polygon_temp,
				  double epsilon)
  {
    assert( *this == *this
	    and polygon_temp.n() > 0 );

    Point point_temp( this->projection_onto_boundary_of(polygon_temp) );
    if(  distance( *this , point_temp ) <= epsilon )
      *this = point_temp;
  }
  void Point::snap_to_boundary_of(const Environment& environment_temp,
				  double epsilon)
  {
    assert( *this == *this
	    and environment_temp.n() > 0 );

    Point point_temp( this->projection_onto_boundary_of(environment_temp) );
    if(  distance( *this , point_temp ) <= epsilon )
      *this = point_temp;
  }


  bool operator == (const Point& point1, const Point& point2)
  { return (  ( point1.x() == point2.x() ) 
	      and ( point1.y() == point2.y() )  ); }
  bool operator != (const Point& point1, const Point& point2)
  { return !( point1 == point2 ); }


  bool operator < (const Point& point1, const Point& point2)
  {
    if( point1 != point1 or point2 != point2 )
      return false;
    if(point1.x() < point2.x())
      return true;
    else if(  ( point1.x() == point2.x() ) 
	      and ( point1.y() < point2.y() )  )
      return true;
    return false;
  }
  bool operator > (const Point& point1, const Point& point2)
  {
    if( point1 != point1 or point2 != point2 )
      return false;
    if( point1.x() > point2.x() )
      return true;
    else if(  ( point1.x() == point2.x() ) 
	      and ( point1.y() > point2.y() )  )
      return true;
    return false;
  }
  bool operator >= (const Point& point1, const Point& point2)
  {
    if( point1 != point1 or point2 != point2 )
      return false;
    return !( point1 < point2 );
  }
  bool operator <= (const Point& point1, const Point& point2)
  {
    if( point1 != point1 or point2 != point2 )
      return false;
    return !( point1 > point2 );
  }


  Point operator + (const Point& point1, const Point& point2)
  {
    return Point( point1.x() + point2.x(),
		  point1.y() + point2.y() );
  }
  Point operator - (const Point& point1, const Point& point2)
  {
    return Point( point1.x() - point2.x(),
		  point1.y() - point2.y() );
  }


  Point operator * (const Point& point1, const Point& point2)
  {
    return Point( point1.x()*point2.x(),
		  point1.y()*point2.y() ); 
  }


  Point operator * (double scalar, const Point& point2)
  {
    return Point( scalar*point2.x(),
		  scalar*point2.y()); 
  }
  Point operator * (const Point& point1, double scalar)
  {
    return Point( scalar*point1.x(),
		  scalar*point1.y()); 
  }


  double cross(const Point& point1, const Point& point2)
  {
    assert( point1 == point1
	    and point2 == point2 );

    //The area of the parallelogram created by the Points viewed as vectors.
    return point1.x()*point2.y() - point2.x()*point1.y();
  }


  double distance(const Point& point1, const Point& point2)
  {
    assert( point1 == point1
	    and point2 == point2 );

    return sqrt(  pow( point1.x() - point2.x() , 2 )
		  + pow( point1.y() - point2.y() , 2 )  );
  }


  double distance(const Point& point_temp,
		  const Line_Segment& line_segment_temp)
  {
    assert( point_temp == point_temp
	    and line_segment_temp.size() > 0 );

    return distance( point_temp, 
		     point_temp.projection_onto(line_segment_temp) );
  }
  double distance(const Line_Segment& line_segment_temp,
		  const Point& point_temp)
  {
    return distance( point_temp,
		     line_segment_temp );
  }


  double distance(const Point& point_temp,
		  const Ray& ray_temp)
  {
    assert( point_temp == point_temp
	    and ray_temp == ray_temp );
    return distance( point_temp, 
		     point_temp.projection_onto(ray_temp) );
  }
  double distance(const Ray& ray_temp,
		  const Point& point_temp)
  {
    return distance( point_temp,
		     point_temp.projection_onto(ray_temp) );
  }


  double distance(const Point& point_temp,
		  const Polyline& polyline_temp)
  {
    assert( point_temp == point_temp
	    and polyline_temp.size() > 0 );

    double running_min = distance(point_temp, polyline_temp[0]);
    double distance_temp;
    for(unsigned i=0; i<polyline_temp.size()-1; i++){
      distance_temp = distance(point_temp, Line_Segment(polyline_temp[i],
							polyline_temp[i+1]) );
      if(distance_temp < running_min)
	running_min = distance_temp;
    }
    return running_min;
  }
  double distance(const Polyline& polyline_temp,
		  const Point& point_temp)
  {
    return distance(point_temp, polyline_temp);
  }


  double boundary_distance(const Point& point_temp,
			   const Polygon& polygon_temp)
  {
    assert( point_temp == point_temp
	    and polygon_temp.n() > 0);

    double running_min = distance(point_temp, polygon_temp[0]);
    double distance_temp;
    for(unsigned i=0; i<=polygon_temp.n(); i++){
      distance_temp = distance(point_temp, Line_Segment(polygon_temp[i],
							polygon_temp[i+1]) );
      if(distance_temp < running_min)
	running_min = distance_temp;
    }
    return running_min;
  }
  double boundary_distance(const Polygon& polygon_temp, const Point& point_temp)
  {
    return boundary_distance(point_temp, polygon_temp);
  } 

 
  double boundary_distance(const Point& point_temp,
			   const Environment& environment_temp)
  {
    assert( point_temp == point_temp
	    and environment_temp.n() > 0 );

    double running_min = distance(point_temp, environment_temp[0][0]);
    double distance_temp;
    for(unsigned i=0; i <= environment_temp.h(); i++){
      distance_temp = boundary_distance(point_temp, environment_temp[i]);
      if(distance_temp < running_min)
	running_min = distance_temp;
    }
    return running_min;
  }  
  double boundary_distance(const Environment& environment_temp,
			   const Point& point_temp)
  {
    return boundary_distance(point_temp, environment_temp);
  }


  std::ostream& operator << (std::ostream& outs, const Point& point_temp)
  {
    outs << point_temp.x() << "  " << point_temp.y();
    return outs;
  }


  //Line_Segment


  Line_Segment::Line_Segment()
  {
    endpoints_ = NULL;
    size_ = 0;
  }


  Line_Segment::Line_Segment(const Line_Segment& line_segment_temp)
  {
    switch(line_segment_temp.size_){
    case 0:
      endpoints_ = NULL;
      size_ = 0;
      break;
    case 1:
      endpoints_ = new Point[1];
      endpoints_[0] = line_segment_temp.endpoints_[0];
      size_ = 1;
      break;
    case 2:
      endpoints_ = new Point[2];
      endpoints_[0] = line_segment_temp.endpoints_[0];
      endpoints_[1] = line_segment_temp.endpoints_[1];
      size_ = 2;
    }
  }


  Line_Segment::Line_Segment(const Point& point_temp)
  {
    endpoints_ = new Point[1];
    endpoints_[0] = point_temp;
    size_ = 1;
  }


  Line_Segment::Line_Segment(const Point& first_point_temp,
			     const Point& second_point_temp, double epsilon)
  {
    if( distance(first_point_temp, second_point_temp) <= epsilon ){
      endpoints_ = new Point[1];
      endpoints_[0] = first_point_temp;
      size_ = 1;
    }
    else{
      endpoints_ = new Point[2];
      endpoints_[0] = first_point_temp;
      endpoints_[1] = second_point_temp;
      size_ = 2;
    }
  }


  Point Line_Segment::first() const
  {
    assert( size() > 0 );

    return endpoints_[0];
  }


  Point Line_Segment::second() const
  {
    assert( size() > 0 );

    if(size_==2)
      return endpoints_[1];
    else
      return endpoints_[0];
  }


  Point Line_Segment::midpoint() const
  {
    assert( size_ > 0 );

    return 0.5*( first() + second() );
  }


  double Line_Segment::length() const
  {
    assert( size_ > 0 );

    return distance(first(), second());
  }


  bool Line_Segment::is_in_standard_form() const
  {
    assert( size_ > 0);

    if(size_<2)
      return true;
    return first() <= second();
  }


  Line_Segment& Line_Segment::operator = (const Line_Segment& line_segment_temp)
  {
    //Makes sure not to delete dynamic vars before they're copied.
    if(this==&line_segment_temp)
      return *this;
    delete [] endpoints_;
    switch(line_segment_temp.size_){
    case 0:
      endpoints_ = NULL;
      size_ = 0;
      break;
    case 1:
      endpoints_ = new Point[1];
      endpoints_[0] = line_segment_temp.endpoints_[0];
      size_ = 1;
      break;
    case 2:
      endpoints_ = new Point[2];
      endpoints_[0] = line_segment_temp.endpoints_[0];
      endpoints_[1] = line_segment_temp.endpoints_[1];
      size_ = 2;
    }
    return *this;
  }


  void Line_Segment::set_first(const Point& point_temp, double epsilon)
  {
    Point second_point_temp;
    switch(size_){
    case 0:
      endpoints_ = new Point[1];
      endpoints_[0] = point_temp;
      size_ = 1;
      break;
    case 1:
      if( distance(endpoints_[0], point_temp) <= epsilon )
	{ endpoints_[0] = point_temp; return; }
      second_point_temp = endpoints_[0];
      delete [] endpoints_;
      endpoints_ = new Point[2];
      endpoints_[0] = point_temp;
      endpoints_[1] = second_point_temp;
      size_ = 2;
      break;
    case 2:
      if( distance(point_temp, endpoints_[1]) > epsilon )
	{ endpoints_[0] = point_temp; return; }
      delete [] endpoints_;
      endpoints_ = new Point[1];
      endpoints_[0] = point_temp;
      size_ = 1;
    }
  }


  void Line_Segment::set_second(const Point& point_temp, double epsilon)
  {
    Point first_point_temp;
    switch(size_){
    case 0:
      endpoints_ = new Point[1];
      endpoints_[0] = point_temp;
      size_ = 1;
      break;
    case 1:
      if( distance(endpoints_[0], point_temp) <= epsilon )
	{ endpoints_[0] = point_temp; return; }
      first_point_temp = endpoints_[0];
      delete [] endpoints_;
      endpoints_ = new Point[2];
      endpoints_[0] = first_point_temp;
      endpoints_[1] = point_temp;
      size_ = 2;
      break;
    case 2:
      if( distance(endpoints_[0], point_temp) > epsilon )
	{ endpoints_[1] = point_temp; return; }
      delete [] endpoints_;
      endpoints_ = new Point[1];
      endpoints_[0] = point_temp;
      size_ = 1;
    }
  }


  void Line_Segment::reverse()
  {
    if(size_<2)
      return;
    Point point_temp(first());
    endpoints_[0] = second();
    endpoints_[1] = point_temp;
  }


  void Line_Segment::enforce_standard_form()
  {
    if(first() > second())
      reverse();
  }


  void Line_Segment::clear()
  {
    delete [] endpoints_;
    endpoints_ = NULL;
    size_ = 0;
  }


  Line_Segment::~Line_Segment()
  {
    delete [] endpoints_;
  }


  bool  operator == (const Line_Segment& line_segment1,
		     const Line_Segment& line_segment2)
  {
    if( line_segment1.size() != line_segment2.size() 
	or line_segment1.size() == 0
	or line_segment2.size() == 0 )
      return false;
    else if( line_segment1.first() == line_segment2.first()
	     and line_segment1.second() == line_segment2.second() )
      return true;
    else
      return false;
  } 


  bool  operator != (const Line_Segment& line_segment1,
		     const Line_Segment& line_segment2)
  {
    return !( line_segment1 == line_segment2 );
  } 


  bool equivalent(Line_Segment line_segment1, 
		  Line_Segment line_segment2, double epsilon)
  {
    if( line_segment1.size() != line_segment2.size()
	or line_segment1.size() == 0
	or line_segment2.size() == 0 )
      return false;
    else if(   (  distance( line_segment1.first(), 
			    line_segment2.first() ) <= epsilon  
		  and  distance( line_segment1.second(), 
				 line_segment2.second() ) <= epsilon  )
	    or (  distance( line_segment1.first(), 
			    line_segment2.second() ) <= epsilon  
		  and  distance( line_segment1.second(), 
				 line_segment2.first() ) <= epsilon  )   )
      return true;
    else
      return false;
  }


  double distance(const Line_Segment& line_segment1,
		  const Line_Segment& line_segment2)
  {
    assert( line_segment1.size() > 0  and  line_segment2.size() > 0 );
    
    if(intersect_proper(line_segment1, line_segment2))
      return 0;
    //But if two line segments intersect improperly, the distance
    //between them is equal to the minimum of the distances between
    //all 4 endpoints_ and their respective projections onto the line
    //segment they don't belong to.
    double running_min, distance_temp;
    running_min = distance(line_segment1.first(), line_segment2);
    distance_temp = distance(line_segment1.second(), line_segment2);
    if(distance_temp<running_min)
      running_min = distance_temp;
    distance_temp = distance(line_segment2.first(), line_segment1);
    if(distance_temp<running_min)
      running_min = distance_temp;
    distance_temp = distance(line_segment2.second(), line_segment1);
    if(distance_temp<running_min)
      return distance_temp;
    return running_min;
  }


  double boundary_distance(const Line_Segment& line_segment,
			   const Polygon& polygon)
  {
    assert( line_segment.size() > 0 and polygon.n() > 0 );

    double running_min = distance( line_segment , polygon[0] );
    if( polygon.n() > 1 )
      for(unsigned i=0; i<polygon.n(); i++){
	double d = distance(  line_segment, 
			      Line_Segment( polygon[i] , polygon[i+1] )  );
	if( running_min > d )
	  running_min = d;
      }
    return running_min;
  }
  double boundary_distance(const Polygon& polygon,
			   const Line_Segment& line_segment)
  { return boundary_distance( line_segment , polygon ); }


  bool intersect(const Line_Segment& line_segment1,
		 const Line_Segment& line_segment2, double epsilon)
  {
    if( line_segment1.size() == 0
	or line_segment2.size() == 0 )
      return false;
    if( distance(line_segment1, line_segment2) <= epsilon )
      return true;
    return false;
  }

  
  bool intersect_proper(const Line_Segment& line_segment1,
			const Line_Segment& line_segment2, double epsilon)
  {
    if( line_segment1.size() == 0
	or line_segment2.size() == 0 )
      return false;

    //Declare new vars just for readability.
    Point a( line_segment1.first()  );
    Point b( line_segment1.second() );
    Point c( line_segment2.first()  );
    Point d( line_segment2.second() );
    //First find the minimum of the distances between all 4 endpoints_
    //and their respective projections onto the opposite line segment.
    double running_min, distance_temp;
    running_min = distance(a, line_segment2);
    distance_temp = distance(b, line_segment2);
    if(distance_temp<running_min)
      running_min = distance_temp;
    distance_temp = distance(c, line_segment1);
    if(distance_temp<running_min)
      running_min = distance_temp;
    distance_temp = distance(d, line_segment1);
    if(distance_temp<running_min)
      running_min = distance_temp;
    //If an endpoint is close enough to the other segment, the
    //intersection is not considered proper.
    if(running_min <= epsilon)
      return false; 
    //This test is from O'Rourke's "Computational Geometry in C",
    //p.30.  Checks left and right turns.
    if(  cross(b-a, c-b) * cross(b-a, d-b) < 0   
	 and   cross(d-c, b-d) * cross(d-c, a-d) < 0  )
      return true;
    return false;
  }

  
  Line_Segment intersection(const Line_Segment& line_segment1,
			    const Line_Segment& line_segment2, double epsilon)
  {
    //Initially empty.
    Line_Segment line_segment_temp;

    if( line_segment1.size() == 0
	or line_segment2.size() == 0 )
      return line_segment_temp;

    //No intersection => return empty segment.
    if( !intersect(line_segment1, line_segment2, epsilon) )
       return line_segment_temp;
    //Declare new vars just for readability.
    Point a( line_segment1.first()  );
    Point b( line_segment1.second() );
    Point c( line_segment2.first()  );
    Point d( line_segment2.second() );
    if( intersect_proper(line_segment1, line_segment2, epsilon) ){
      //Use formula from O'Rourke's "Computational Geometry in C", p. 221.
      //Note D=0 iff the line segments are parallel.
      double D = a.x()*( d.y() - c.y() ) 
	+ b.x()*( c.y() - d.y() ) 
	+ d.x()*( b.y() - a.y() ) 
	+ c.x()*( a.y() - b.y() );
      double s = (  a.x()*( d.y() - c.y() ) 
		    + c.x()*( a.y() - d.y() ) 
		    + d.x()*( c.y() - a.y() )  ) / D;
      line_segment_temp.set_first( a + s * ( b - a ) );
      return line_segment_temp;
      }
    //Otherwise if improper...
    double distance_temp_a = distance(a,  line_segment2);
    double distance_temp_b = distance(b, line_segment2);
    double distance_temp_c = distance(c,  line_segment1);
    double distance_temp_d = distance(d, line_segment1);
    //Check if the intersection is nondegenerate segment.
    if( distance_temp_a <= epsilon  and  distance_temp_b <= epsilon ){
      line_segment_temp.set_first(a, epsilon);
      line_segment_temp.set_second(b, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_c <= epsilon  and  distance_temp_d <= epsilon ){
      line_segment_temp.set_first(c, epsilon);
      line_segment_temp.set_second(d, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_a <= epsilon  and  distance_temp_c <= epsilon ){
      line_segment_temp.set_first(a, epsilon);
      line_segment_temp.set_second(c, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_a <= epsilon  and  distance_temp_d <= epsilon ){
      line_segment_temp.set_first(a, epsilon);
      line_segment_temp.set_second(d, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_b <= epsilon  and  distance_temp_c <= epsilon ){
      line_segment_temp.set_first(b, epsilon);
      line_segment_temp.set_second(c, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_b <= epsilon  and  distance_temp_d <= epsilon ){
      line_segment_temp.set_first(b, epsilon);
      line_segment_temp.set_second(d, epsilon);
      return line_segment_temp;
    }
    //Check if the intersection is a single point.
    else if( distance_temp_a <= epsilon ){
      line_segment_temp.set_first(a, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_b <= epsilon ){
      line_segment_temp.set_first(b, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_c <= epsilon ){
      line_segment_temp.set_first(c, epsilon);
      return line_segment_temp;
    }
    else if( distance_temp_d <= epsilon ){
      line_segment_temp.set_first(d, epsilon);
      return line_segment_temp;
    }
    return line_segment_temp;
  }


  std::ostream& operator << (std::ostream& outs,
			     const Line_Segment& line_segment_temp)
  {
    switch(line_segment_temp.size()){
    case 0:
      return outs;
      break;
    case 1:
      outs << line_segment_temp.first() << std::endl 
	   << line_segment_temp.second() << std::endl;
      return outs;
      break;
    case 2:
      outs << line_segment_temp.first() << std::endl 
	   << line_segment_temp.second() << std::endl;
      return outs;
    }
    return outs;
  }


  //Angle


  Angle::Angle(double data_temp)
  {
    if(data_temp >= 0)
      angle_radians_ = fmod(data_temp, 2*M_PI);
    else{
      angle_radians_ = 2*M_PI + fmod(data_temp, -2*M_PI);
      if(angle_radians_ == 2*M_PI)
	angle_radians_ = 0;
    }
  }


  Angle::Angle(double rise_temp, double run_temp)
  {
    if( rise_temp == 0 and run_temp == 0 )
      angle_radians_ = 0;
    //First calculate 4 quadrant inverse tangent into [-pi,+pi].
    angle_radians_ = std::atan2(rise_temp, run_temp);
    //Correct so angles specified in [0, 2*PI).
    if(angle_radians_ < 0)
      angle_radians_ = 2*M_PI + angle_radians_;
  }


  void Angle::set(double data_temp)
  { 
    *this = Angle(data_temp);
  }


  void Angle::randomize()
  {
    angle_radians_ = fmod( uniform_random_sample(0, 2*M_PI), 2*M_PI );
  }


  bool operator == (const Angle& angle1, const Angle& angle2)
  {
    return (angle1.get() == angle2.get());
  }
  bool operator != (const Angle& angle1, const Angle& angle2)
  {
    return !(angle1.get() == angle2.get());
  }


  bool operator >  (const Angle& angle1, const Angle& angle2)
  {
    return angle1.get() > angle2.get();
  }
  bool operator <  (const Angle& angle1, const Angle& angle2)
  {
    return angle1.get() < angle2.get();
  }
  bool operator >= (const Angle& angle1, const Angle& angle2)
  {
    return angle1.get() >= angle2.get();
  }
  bool operator <= (const Angle& angle1, const Angle& angle2)
  {
    return angle1.get() <= angle2.get();
  }


  Angle operator +  (const Angle& angle1, const Angle& angle2)
  {
    return Angle( angle1.get()  + angle2.get() );
  }
  Angle operator -  (const Angle& angle1, const Angle& angle2)
  {
    return Angle( angle1.get() - angle2.get() );
  }


  double geodesic_distance(const Angle& angle1, const Angle& angle2)
  {
    assert( angle1.get() == angle1.get()
	    and angle2.get() == angle2.get() );

    double distance1 = std::fabs( angle1.get() 
				  - angle2.get() );
    double distance2 = 2*M_PI - distance1;
    if(distance1 < distance2)
      return distance1;
    return distance2;
  }


  double geodesic_direction(const Angle& angle1, const Angle& angle2)
  {
    assert( angle1.get() == angle1.get()
	    and angle2.get() == angle2.get() );

    double distance1 = std::fabs( angle1.get() 
				  - angle2.get() );
    double distance2 = 2*M_PI - distance1;
    if(angle1 <= angle2){
      if(distance1 < distance2)
	return 1.0;
      return -1.0;
    }
    //Otherwise angle1 > angle2.
    if(distance1 < distance2)
      return -1.0;
    return 1.0;
  }


  std::ostream& operator << (std::ostream& outs, const Angle& angle_temp)
  {
    outs << angle_temp.get();
    return outs;
  }


  //Polar_Point


  Polar_Point::Polar_Point(const Point& polar_origin_temp,
			   const Point& point_temp,
			   double epsilon) : Point(point_temp)
  {
    polar_origin_ = polar_origin_temp;
    if( polar_origin_==polar_origin_
	and point_temp==point_temp
	and distance(polar_origin_, point_temp) <= epsilon ){
      bearing_ = Angle(0.0);
      range_ = 0.0;
    }
    else if( polar_origin_==polar_origin_
	     and point_temp==point_temp){
      bearing_ = Angle(  point_temp.y()-polar_origin_temp.y(), 
			 point_temp.x()-polar_origin_temp.x()  );
      range_ = distance(polar_origin_temp, point_temp);
    }
  }


  void Polar_Point::set_polar_origin(const Point& polar_origin_temp)
  {
    *this = Polar_Point( polar_origin_temp, Point(x(), y()) );
  }


  void Polar_Point::set_x(double x_temp)
  {
    *this = Polar_Point( polar_origin_, Point(x_temp, y()) );
  }


  void Polar_Point::set_y(double y_temp)
  {
    *this = Polar_Point( polar_origin_, Point(x(), y_temp) );
  }


  void Polar_Point::set_range(double range_temp)
  {
    range_ = range_temp;
    x_ = polar_origin_.x() 
      + range_*std::cos( bearing_.get() );
    y_ = polar_origin_.y() 
      + range_*std::sin( bearing_.get() );
  }


  void Polar_Point::set_bearing(const Angle& bearing_temp)
  {
    bearing_ = bearing_temp;
    x_ = polar_origin_.x() 
      + range_*std::cos( bearing_.get() );
    y_ = polar_origin_.y() 
             + range_*std::sin( bearing_.get() );
  }

  
  bool operator == (const Polar_Point& polar_point1,
		    const Polar_Point& polar_point2)
  {
    if( polar_point1.polar_origin() == polar_point2.polar_origin()
	and polar_point1.range() == polar_point2.range()
	and polar_point1.bearing() == polar_point2.bearing()
	)
      return true;
    return false;
  }
  bool operator != (const Polar_Point& polar_point1,
		    const Polar_Point& polar_point2)
  {
    return !( polar_point1 == polar_point2 );
  }


  bool operator > (const Polar_Point& polar_point1,
		   const Polar_Point& polar_point2)
  {
    if( polar_point1.polar_origin() != polar_point1.polar_origin()
	or polar_point1.range() != polar_point1.range()
	or polar_point1.bearing() != polar_point1.bearing() 
	or polar_point2.polar_origin() != polar_point2.polar_origin()
	or polar_point2.range() != polar_point2.range()
	or polar_point2.bearing() != polar_point2.bearing() 
	)
      return false;

    if( polar_point1.bearing() > polar_point2.bearing() )
      return true;
    else if( polar_point1.bearing() == polar_point2.bearing() 
	     and  polar_point1.range() > polar_point2.range() )
      return true;
    return false;
  }
  bool operator < (const Polar_Point& polar_point1,
		   const Polar_Point& polar_point2)
  {
    if( polar_point1.polar_origin() != polar_point1.polar_origin()
	or polar_point1.range() != polar_point1.range()
	or polar_point1.bearing() != polar_point1.bearing() 
	or polar_point2.polar_origin() != polar_point2.polar_origin()
	or polar_point2.range() != polar_point2.range()
	or polar_point2.bearing() != polar_point2.bearing() 
	)
      return false;

    if( polar_point1.bearing() < polar_point2.bearing() )
      return true;
    else if( polar_point1.bearing() == polar_point2.bearing() 
	     and  polar_point1.range() < polar_point2.range() )
      return true;
    return false;

  }
  bool operator >= (const Polar_Point& polar_point1,
		    const Polar_Point& polar_point2)
  {
    if( polar_point1.polar_origin() != polar_point1.polar_origin()
	or polar_point1.range() != polar_point1.range()
	or polar_point1.bearing() != polar_point1.bearing() 
	or polar_point2.polar_origin() != polar_point2.polar_origin()
	or polar_point2.range() != polar_point2.range()
	or polar_point2.bearing() != polar_point2.bearing() 
	)
      return false;

    return !(polar_point1<polar_point2);
  }
  bool operator <= (const Polar_Point& polar_point1,
		    const Polar_Point& polar_point2) 
  {
    if( polar_point1.polar_origin() != polar_point1.polar_origin()
	or polar_point1.range() != polar_point1.range()
	or polar_point1.bearing() != polar_point1.bearing() 
	or polar_point2.polar_origin() != polar_point2.polar_origin()
	or polar_point2.range() != polar_point2.range()
	or polar_point2.bearing() != polar_point2.bearing() 
	)
      return false;

    return !(polar_point1>polar_point2);
  }


  std::ostream& operator << (std::ostream& outs,
			     const Polar_Point& polar_point_temp)
  {
    outs << polar_point_temp.bearing() << "  " << polar_point_temp.range();
    return outs;
  }


  //Ray


  Ray::Ray(Point base_point_temp, Point bearing_point)
  { 
      assert(  !( base_point_temp == bearing_point )  ); 

      base_point_ = base_point_temp;
      bearing_ = Angle( bearing_point.y()-base_point_temp.y(),
			    bearing_point.x()-base_point_temp.x() );
    }

  bool operator == (const Ray& ray1,
		    const Ray& ray2)
  {
    if( ray1.base_point() == ray2.base_point()
	and ray1.bearing() == ray2.bearing() )
      return true;
    else
      return false;
  }


  bool operator != (const Ray& ray1,
		    const Ray& ray2)
  {
    return !( ray1 == ray2 );
  }


  Line_Segment intersection(const Ray ray_temp,
			    const Line_Segment& line_segment_temp,
			    double epsilon)
  {
    assert( ray_temp == ray_temp
	    and line_segment_temp.size() > 0 );

    //First construct a Line_Segment parallel with the Ray which is so
    //long, that it's intersection with line_segment_temp will be
    //equal to the intersection of ray_temp with line_segment_temp.
    double R = distance(ray_temp.base_point(), line_segment_temp) 
               + line_segment_temp.length();
    Line_Segment seg_approx =
      Line_Segment(  ray_temp.base_point(), ray_temp.base_point() +
		     Point( R*std::cos(ray_temp.bearing().get()),
			    R*std::sin(ray_temp.bearing().get()) )  );
    Line_Segment intersect_seg = intersection(line_segment_temp,
					      seg_approx,
					      epsilon);
    //Make sure point closer to ray_temp's base_point is listed first.
    if( intersect_seg.size() == 2
	and distance( intersect_seg.first(), ray_temp.base_point() ) >
	distance( intersect_seg.second(), ray_temp.base_point() )  ){
      intersect_seg.reverse();
    }
    return intersect_seg;
  }


  Line_Segment intersection(const Line_Segment& line_segment_temp,
			    const Ray& ray_temp,
			    double epsilon)
  {
    return intersection( ray_temp , line_segment_temp , epsilon );
  }


  //Polyline


  double Polyline::length() const
  {
    double length_temp = 0;
    for(unsigned i=1; i <= vertices_.size()-1; i++)
      length_temp += distance( vertices_[i-1] , vertices_[i] );
    return length_temp;
  }


  double Polyline::diameter() const
  {
    //Precondition:  nonempty Polyline.
    assert( size() > 0 );

    double running_max=0;
    for(unsigned i=0; i<size()-1; i++){
    for(unsigned j=i+1; j<size(); j++){
      if( distance( (*this)[i] , (*this)[j] ) > running_max )
	running_max = distance( (*this)[i] , (*this)[j] );
    }}
    return running_max;
  }


  Bounding_Box Polyline::bbox () const
  {
    //Precondition:  nonempty Polyline.
    assert( vertices_.size() > 0 );

    Bounding_Box bounding_box;
    double x_min=vertices_[0].x(), x_max=vertices_[0].x(),
      y_min=vertices_[0].y(), y_max=vertices_[0].y();
    for(unsigned i = 1; i <  vertices_.size(); i++){
      if(x_min > vertices_[i].x())  { x_min=vertices_[i].x(); }
      if(x_max < vertices_[i].x())  { x_max=vertices_[i].x(); }
      if(y_min > vertices_[i].y())  { y_min=vertices_[i].y(); }
      if(y_max < vertices_[i].y())  { y_max=vertices_[i].y(); }
    }
    bounding_box.x_min=x_min; bounding_box.x_max=x_max;
    bounding_box.y_min=y_min; bounding_box.y_max=y_max;
    return bounding_box;
  }


  void Polyline::eliminate_redundant_vertices(double epsilon)
  {
    //Trivial case
    if(vertices_.size() < 3)
      return;

    //Store new minimal length list of vertices
    std::vector<Point> vertices_temp;
    vertices_temp.reserve(vertices_.size());

    //Place holders
    unsigned first  = 0;
    unsigned second = 1;
    unsigned third  = 2;

    //Add first vertex
    vertices_temp.push_back((*this)[first]);

    while( third < vertices_.size() ){
      //if second redundant
      if(   distance(  Line_Segment( (*this)[first], 
				     (*this)[third] ) ,
		       (*this)[second]  ) 
	    <= epsilon   ){
	//=>skip it
	second = third;
	third++;
      }
      //else second not redundant
      else{
	//=>add it.
	vertices_temp.push_back((*this)[second]);
	first = second;
	second = third;
	third++;
      }
    }

    //Add last vertex
    vertices_temp.push_back(vertices_.back());

    //Update list of vertices
    vertices_ = vertices_temp;
  }


  void Polyline::reverse()
  {
    std::reverse( vertices_.begin() , vertices_.end() );
  }


  std::ostream& operator << (std::ostream& outs,
			     const Polyline& polyline_temp)
  {
    for(unsigned i=0; i<polyline_temp.size(); i++)
      outs << polyline_temp[i] << std::endl;
    return outs;
  }


  void Polyline::append( const Polyline& polyline ){
    vertices_.reserve( vertices_.size() + polyline.vertices_.size() );
    for(unsigned i=0; i<polyline.vertices_.size(); i++){
      vertices_.push_back( polyline.vertices_[i] );
    }
  }


  //Polygon


  Polygon::Polygon (const std::string& filename)
  {
    std::ifstream fin(filename.c_str());
    //if(fin.fail()) { std::cerr << "\x1b[5;31m" << "Input file
    //opening failed." << "\x1b[0m\n" << "\a \n"; exit(1);}
    assert( !fin.fail() );

    Point point_temp;
    double x_temp, y_temp;
    while (fin >> x_temp and fin >> y_temp){
      point_temp.set_x(x_temp);
      point_temp.set_y(y_temp);
      vertices_.push_back(point_temp);
    }
    fin.close();
  }


  Polygon::Polygon(const std::vector<Point>& vertices_temp)
  {
    vertices_ = vertices_temp; 
  }


  Polygon::Polygon(const Point& point0,
		   const Point& point1,
		   const Point& point2)
  {
    vertices_.push_back(point0);
    vertices_.push_back(point1);
    vertices_.push_back(point2);
  }


  unsigned  Polygon::r () const
  {
    int r_count = 0;
    if( vertices_.size() > 1 ){
      //Use cross product to count right turns.
      for(unsigned i=0; i<=n()-1; i++)
	if( ((*this)[i+1].x()-(*this)[i].x())
	    *((*this)[i+2].y()-(*this)[i].y()) 
	    - ((*this)[i+1].y()-(*this)[i].y())
	    *((*this)[i+2].x()-(*this)[i].x()) < 0 )
	  r_count++;
      if( area() < 0 ){ 
	r_count = n() - r_count;
      }
    }
    return r_count;
  }


  bool Polygon::is_simple(double epsilon) const
  {
    
    if(n()==0 or n()==1 or n()==2)
      return false;

    //Make sure adjacent edges only intersect at a single point.
    for(unsigned i=0; i<=n()-1; i++)
      if(  intersection( Line_Segment((*this)[i],(*this)[i+1]) , 
			 Line_Segment((*this)[i+1],(*this)[i+2]) , 
			 epsilon ).size() > 1  )
	return false;

    //Make sure nonadjacent edges do not intersect.
    for(unsigned i=0; i<n()-2; i++)
      for(unsigned j=i+2; j<=n()-1; j++)
	if( 0!=(j+1)%vertices_.size()  
	    and distance( Line_Segment((*this)[i],(*this)[i+1]) , 
			  Line_Segment((*this)[j],(*this)[j+1]) ) <= epsilon  )
	  return false;
    
    return true;
  }


  bool Polygon::is_in_standard_form() const
  {
    if(vertices_.size() > 1)  //if more than one point in the polygon.
      for(unsigned i=1; i<vertices_.size(); i++)
	if(vertices_[0] > vertices_[i])
	  return false;
    return true;
  }


  double Polygon::boundary_length() const
  {
    double length_temp=0;
    if(n()==0 or n()==1)
      return 0;
    for(unsigned i=0; i<n()-1; i++)
      length_temp += distance( vertices_[i] , vertices_[i+1] );
    length_temp += distance( vertices_[n()-1] , 
			     vertices_[0] );
    return length_temp;
  }


  double Polygon::area() const
  {
    double area_temp = 0;
    if(n()==0)
      return 0;
    for(unsigned i=0; i<=n()-1; i++)
      area_temp += (*this)[i].x()*(*this)[i+1].y() 
	- (*this)[i+1].x()*(*this)[i].y();
    return area_temp/2.0;
  }


  Point Polygon::centroid() const
  {
    assert( vertices_.size() > 0 );

    double area_temp=area();
    if(area_temp==0)
      { std::cerr << "\x1b[5;31m" 
	 << "Warning:  tried to compute centoid of polygon with zero area!" 
	 << "\x1b[0m\n" << "\a \n"; exit(1); } 
    double x_temp=0;
    for(unsigned i=0; i<=n()-1; i++)
      x_temp += ( (*this)[i].x() + (*this)[i+1].x() ) 
	* ( (*this)[i].x()*(*this)[i+1].y() 
	    - (*this)[i+1].x()*(*this)[i].y() );
    double y_temp=0;
    for(unsigned i=0; i<=n()-1; i++)
      y_temp += ( (*this)[i].y() + (*this)[i+1].y() ) 
	* ( (*this)[i].x()*(*this)[i+1].y() 
	    - (*this)[i+1].x()*(*this)[i].y() );
    return Point(x_temp/(6*area_temp), y_temp/(6*area_temp));
  }


  double Polygon::diameter() const
  {
    //Precondition:  nonempty Polygon.
    assert( n() > 0 );

    double running_max=0;
    for(unsigned i=0; i<n()-1; i++){
    for(unsigned j=i+1; j<n(); j++){
      if( distance( (*this)[i] , (*this)[j] ) > running_max )
	running_max = distance( (*this)[i] , (*this)[j] );
    }}
    return running_max;
  }


  Bounding_Box Polygon::bbox () const
  {
    //Precondition:  nonempty Polygon.
    assert( vertices_.size() > 0 );

    Bounding_Box bounding_box;
    double x_min=vertices_[0].x(), x_max=vertices_[0].x(),
      y_min=vertices_[0].y(), y_max=vertices_[0].y();
    for(unsigned i = 1; i <  vertices_.size(); i++){
      if(x_min > vertices_[i].x())  { x_min=vertices_[i].x(); }
      if(x_max < vertices_[i].x())  { x_max=vertices_[i].x(); }
      if(y_min > vertices_[i].y())  { y_min=vertices_[i].y(); }
      if(y_max < vertices_[i].y())  { y_max=vertices_[i].y(); }
    }
    bounding_box.x_min=x_min; bounding_box.x_max=x_max;
    bounding_box.y_min=y_min; bounding_box.y_max=y_max;
    return bounding_box;
  }


  std::vector<Point> Polygon::random_points(const unsigned& count,
					    double epsilon) const
  {
    //Precondition:  nonempty Polygon.
    assert( vertices_.size() > 0 );

    Bounding_Box bounding_box = bbox();
    std::vector<Point> pts_in_polygon; pts_in_polygon.reserve(count);
    Point pt_temp( uniform_random_sample(bounding_box.x_min, 
					 bounding_box.x_max),
		   uniform_random_sample(bounding_box.y_min, 
					 bounding_box.y_max) );
    while(pts_in_polygon.size() < count){
      while(!pt_temp.in(*this, epsilon)){
	pt_temp.set_x( uniform_random_sample(bounding_box.x_min, 
					     bounding_box.x_max) );
	pt_temp.set_y( uniform_random_sample(bounding_box.y_min, 
					     bounding_box.y_max) );
      }
      pts_in_polygon.push_back(pt_temp);
      pt_temp.set_x( uniform_random_sample(bounding_box.x_min, 
					   bounding_box.x_max) );
      pt_temp.set_y( uniform_random_sample(bounding_box.y_min, 
					   bounding_box.y_max) );
    }
    return pts_in_polygon;
  }


  void Polygon::write_to_file(const std::string& filename,
			      int fios_precision_temp)
  {
    assert( fios_precision_temp >= 1 );

    std::ofstream fout( filename.c_str() );
    //fout.open( filename.c_str() );  //Alternatives.
    //fout << *this;
    fout.setf(std::ios::fixed);
    fout.setf(std::ios::showpoint);
    fout.precision(fios_precision_temp);
    for(unsigned i=0; i<n(); i++)
      fout << vertices_[i] << std::endl;
    fout.close();
  }


  void Polygon::enforce_standard_form()
  {
    int point_count=vertices_.size();
    if(point_count > 1){ //if more than one point in the polygon.
      std::vector<Point> vertices_temp;
      vertices_temp.reserve(point_count);
      //Find index of lexicographically smallest point.
      int index_of_smallest=0;
      int i; //counter.
      for(i=1; i<point_count; i++)
	if(vertices_[i]<vertices_[index_of_smallest])
	  index_of_smallest=i;
      //Fill vertices_temp starting with lex. smallest.
      for(i=index_of_smallest; i<point_count; i++)
	vertices_temp.push_back(vertices_[i]);
      for(i=0; i<index_of_smallest; i++)
	vertices_temp.push_back(vertices_[i]);
      vertices_=vertices_temp;
    }
  }


  void Polygon::eliminate_redundant_vertices(double epsilon)
  {
    //Degenerate case.
    if( vertices_.size() < 4 )
      return;

    //Store new minimal length list of vertices.
    std::vector<Point> vertices_temp;
    vertices_temp.reserve( vertices_.size() );
 
    //Place holders.
    unsigned first  = 0;
    unsigned second = 1;
    unsigned third  = 2;

    while( third <= vertices_.size() ){
      //if second is redundant
      if(   distance(  Line_Segment( (*this)[first], 
				     (*this)[third] ) ,
		       (*this)[second]  ) 
	    <= epsilon   ){
	//=>skip it
	second = third;
	third++;
      }
      //else second not redundant
      else{
	//=>add it
	vertices_temp.push_back( (*this)[second] );
	first = second;
	second = third;
	third++;
      }
    }

    //decide whether to add original first point
    if(   distance(  Line_Segment( vertices_temp.front(), 
				   vertices_temp.back() ) ,
		     vertices_.front()  ) 
	  > epsilon   )
      vertices_temp.push_back( vertices_.front() );
    
    //Update list of vertices.
    vertices_ = vertices_temp;   
  }


  void Polygon::reverse()
  {
    if( n() > 2 )
      std::reverse( ++vertices_.begin() , vertices_.end() );
  }


  bool operator == (Polygon polygon1, Polygon polygon2)
  {
    if( polygon1.n() != polygon2.n() 
	or polygon1.n() == 0
	or polygon2.n() == 0 )
      return false;
    for(unsigned i=0; i<polygon1.n(); i++)
      if(  !(polygon1[i] == polygon2[i])  )
	return false;
    return true;
  }
  bool operator != (Polygon polygon1, Polygon polygon2)
  {
    return !( polygon1 == polygon2 );
  }
  bool equivalent(Polygon polygon1, Polygon polygon2, double epsilon)
  {
    if( polygon1.n() == 0 or polygon2.n() == 0 )
      return false;
    if( polygon1.n() != polygon2.n() )
      return false;
    //Try all cyclic matches
    unsigned n = polygon1.n();//=polygon2.n()
    for( unsigned offset = 0 ; offset < n ; offset++ ){
      bool successful_match = true;
      for(unsigned i=0; i<n; i++){
	if(  distance( polygon1[ i ] , polygon2[ i + offset ] ) > epsilon  )
	  { successful_match = false; break; }
      }
      if( successful_match )
	return true;
    }
    return false;
  }


  double boundary_distance(const Polygon& polygon1, const Polygon& polygon2)
  {
    assert( polygon1.n() > 0  and  polygon2.n() > 0 );

    //Handle single point degeneracy.
    if(polygon1.n() == 1)
      return boundary_distance(polygon1[0], polygon2);
    else if(polygon2.n() == 1)
      return boundary_distance(polygon2[0], polygon1);
    //Handle cases where each polygon has at least 2 points.
    //Initialize to an upper bound.
    double running_min = boundary_distance(polygon1[0], polygon2);
    double distance_temp;
    //Loop over all possible pairs of line segments.
    for(unsigned i=0; i<=polygon1.n()-1; i++){
    for(unsigned j=0; j<=polygon2.n()-1; j++){
      distance_temp = distance( Line_Segment(polygon1[i], polygon1[i+1]) ,
				Line_Segment(polygon2[j], polygon2[j+1]) );
      if(distance_temp < running_min)
	running_min = distance_temp;
    }}
    return running_min;
  }


  std::ostream& operator << (std::ostream& outs,
			     const Polygon& polygon_temp)
  {
    for(unsigned i=0; i<polygon_temp.n(); i++)
      outs << polygon_temp[i] << std::endl;
    return outs;
  }


  //Environment
  
  
  Environment::Environment(const std::vector<Polygon>& polygons)
  {
    outer_boundary_ = polygons[0];
    for(unsigned i=1; i<polygons.size(); i++)
      holes_.push_back( polygons[i] );
    update_flattened_index_key();
  }
  Environment::Environment(const std::string& filename)
  {
    std::ifstream fin(filename.c_str());
    //if(fin.fail()) { std::cerr << "\x1b[5;31m" << "Input file
    //opening failed." << "\x1b[0m\n" << "\a \n"; exit(1);}
    assert( !fin.fail() );

    //Temporary vars for numbers to be read from file.
    double x_temp, y_temp;  
    std::vector<Point> vertices_temp;

    //Skip comments
    while( fin.peek() == '/' ) 
      fin.ignore(200,'\n');

    //Read outer_boundary.
    while ( fin.peek() != '/' ){
      fin >> x_temp >> y_temp;
      //Skip to next line.
      fin.ignore(1);
      if( fin.eof() )
	{ 
	  outer_boundary_.set_vertices(vertices_temp);
	  fin.close(); 
	  update_flattened_index_key(); return;
	}      
      vertices_temp.push_back( Point(x_temp, y_temp) );
    }
    outer_boundary_.set_vertices(vertices_temp);
    vertices_temp.clear();
    
    //Read holes.
    Polygon polygon_temp;
    while(1){
      //Skip comments
      while( fin.peek() == '/' )
	fin.ignore(200,'\n');
      if( fin.eof() )
	{ fin.close(); update_flattened_index_key(); return; }
      while( fin.peek() != '/' ){	
	fin >> x_temp >> y_temp;
	if( fin.eof() )
	  { 
	    polygon_temp.set_vertices(vertices_temp);
	    holes_.push_back(polygon_temp);
	    fin.close(); 
	    update_flattened_index_key(); return;
	  }
	vertices_temp.push_back( Point(x_temp, y_temp) );
	//Skips to next line.
	fin.ignore(1);
      }
      polygon_temp.set_vertices(vertices_temp);
      holes_.push_back(polygon_temp);
      vertices_temp.clear();
    }

    update_flattened_index_key();
  }


  const Point& Environment::operator () (unsigned k) const
  {
    //std::pair<unsigned,unsigned> ij(one_to_two(k));
    std::pair<unsigned,unsigned> ij( flattened_index_key_[k] );
    return (*this)[ ij.first ][ ij.second ];
  }


  unsigned Environment::n() const
  {
    int n_count = 0;
    n_count = outer_boundary_.n();
    for(unsigned i=0; i<h(); i++)
      n_count += holes_[i].n();
    return n_count;
  }


  unsigned Environment::r() const
  {
    int r_count = 0;
    Polygon polygon_temp;
    r_count = outer_boundary_.r();
    for(unsigned i=0; i<h(); i++){
      r_count +=  holes_[i].n() - holes_[i].r();
    }
    return r_count;
  }


  bool Environment::is_in_standard_form() const
  {
    if( outer_boundary_.is_in_standard_form() == false 
	or outer_boundary_.area() < 0 )
      return false;
    for(unsigned i=0; i<holes_.size(); i++)
      if( holes_[i].is_in_standard_form() == false 
	  or holes_[i].area() > 0 )
	return false;
    return true;
  }

  
  bool Environment::is_valid(double epsilon) const
  {
    if( n() <= 2 )
      return false;

    //Check all Polygons are simple.
    if( !outer_boundary_.is_simple(epsilon) ){
      std::cerr << std::endl << "\x1b[31m" 
		<< "The outer boundary is not simple." 
		<<  "\x1b[0m" << std::endl;
      return false;
    }
    for(unsigned i=0; i<h(); i++)
      if( !holes_[i].is_simple(epsilon) ){
	std::cerr << std::endl << "\x1b[31m" 
		  << "Hole " << i << " is not simple." 
		  <<  "\x1b[0m" << std::endl;
	return false; 
      }

    //Check none of the Polygons' boundaries intersect w/in epsilon.
    for(unsigned i=0; i<h(); i++)
      if( boundary_distance(outer_boundary_, holes_[i]) <= epsilon ){
	std::cerr << std::endl << "\x1b[31m" 
	  << "The outer boundary intersects the boundary of hole " << i << "." 
	  <<  "\x1b[0m" << std::endl;
	return false; 
      }
    for(unsigned i=0; i<h(); i++)
      for(unsigned j=i+1; j<h(); j++)
	if( boundary_distance(holes_[i], holes_[j]) <= epsilon ){
	  std::cerr << std::endl << "\x1b[31m" 
		    << "The boundary of hole " << i 
		    << " intersects the boundary of hole " << j << "." 
		    <<  "\x1b[0m" << std::endl;
	  return false;
	}

    //Check that the vertices of each hole are in the outside_boundary
    //and not in any other holes.
    //Loop over holes.
    for(unsigned i=0; i<h(); i++){
      //Loop over vertices of a hole
      for(unsigned j=0; j<holes_[i].n(); j++){
	if( !holes_[i][j].in(outer_boundary_, epsilon) ){
	  std::cerr << std::endl << "\x1b[31m" 
		    << "Vertex " << j << " of hole " << i 
		    << " is not within the outer boundary." 
		    <<  "\x1b[0m" << std::endl;
	  return false; 
	}
	//Second loop over holes.
	for(unsigned k=0; k<h(); k++)
	  if( i!=k and holes_[i][j].in(holes_[k], epsilon) ){
	    std::cerr << std::endl << "\x1b[31m" 
		      << "Vertex " << j 
		      << " of hole " << i 
		      << " is in hole " << k << "." 
		      <<  "\x1b[0m" << std::endl;
	    return false;
	  }
      }
    }

    //Check outer_boundary is ccw and holes are cw.
    if( outer_boundary_.area() <= 0 ){
      std::cerr << std::endl << "\x1b[31m" 
		<< "The outer boundary vertices are not listed ccw." 
		<<  "\x1b[0m" << std::endl;
      return false; 
    }
    for(unsigned i=0; i<h(); i++)
      if( holes_[i].area() >= 0 ){
	std::cerr << std::endl << "\x1b[31m" 
		  << "The vertices of hole " << i << " are not listed cw." 
		  <<  "\x1b[0m" << std::endl;
	return false; 
      }

    return true; 
  } 


  double Environment::boundary_length() const
  {
    //Precondition:  nonempty Environment.
    assert( outer_boundary_.n() > 0 );

    double length_temp = outer_boundary_.boundary_length();
    for(unsigned i=0; i<h(); i++)
      length_temp += holes_[i].boundary_length();
    return length_temp;
  }


  double Environment::area() const
  {
    double area_temp = outer_boundary_.area();
    for(unsigned i=0; i<h(); i++)
      area_temp += holes_[i].area();
    return area_temp;
  }


  std::vector<Point> Environment::random_points(const unsigned& count,
						double epsilon) const
  {
    assert( area() > 0 );

    Bounding_Box bounding_box = bbox();
    std::vector<Point> pts_in_environment; 
    pts_in_environment.reserve(count);
    Point pt_temp( uniform_random_sample(bounding_box.x_min,
					 bounding_box.x_max),
		   uniform_random_sample(bounding_box.y_min,
					 bounding_box.y_max) );
    while(pts_in_environment.size() < count){
      while(!pt_temp.in(*this, epsilon)){
	pt_temp.set_x( uniform_random_sample(bounding_box.x_min, 
					     bounding_box.x_max) );
	pt_temp.set_y( uniform_random_sample(bounding_box.y_min,
					     bounding_box.y_max) );	  
      }
      pts_in_environment.push_back(pt_temp);
      pt_temp.set_x( uniform_random_sample(bounding_box.x_min, 
					   bounding_box.x_max) );
      pt_temp.set_y( uniform_random_sample(bounding_box.y_min,
					   bounding_box.y_max) );
    }
    return pts_in_environment;
  }


  Polyline Environment::shortest_path(const Point& start,
				      const Point& finish,
				      const Visibility_Graph& visibility_graph,
				      double epsilon)
  {
    //true  => data printed to terminal
    //false => silent
    const bool PRINTING_DEBUG_DATA = false;

    //For now, just find one shortest path, later change this to a
    //vector to find all shortest paths (w/in epsilon).
    Polyline shortest_path_output;
    Visibility_Polygon start_visibility_polygon(start, *this, epsilon);

    //Trivial cases
    if( distance(start,finish) <= epsilon ){
      shortest_path_output.push_back(start);
      return shortest_path_output;
    }
    else if( finish.in(start_visibility_polygon, epsilon) ){
      shortest_path_output.push_back(start);
      shortest_path_output.push_back(finish);
      return shortest_path_output;
    }

    Visibility_Polygon finish_visibility_polygon(finish, *this, epsilon);

    //Connect start and finish Points to the visibility graph
    bool *start_visible;  //start row of visibility graph
    bool *finish_visible; //finish row of visibility graph
    start_visible = new bool[n()];
    finish_visible = new bool[n()];
    for(unsigned k=0; k<n(); k++){
      if(  (*this)(k).in( start_visibility_polygon , epsilon )  )
	start_visible[k] = true;
      else
	start_visible[k] = false;
      if(  (*this)(k).in( finish_visibility_polygon , epsilon )  )
	finish_visible[k] = true;
      else
	finish_visible[k] = false;
    }

    //Initialize search tree of visited nodes
    std::list<Shortest_Path_Node> T;
    //:WARNING:
    //If T is a vector it is crucial to make T large enough that it
    //will not be resized.  If T were resized, any iterators pointing
    //to its contents would be invalidated, thus causing the program
    //to fail.
    //T.reserve( n() + 3 );

    //Initialize priority queue of unexpanded nodes
    std::set<Shortest_Path_Node> Q;

    //Construct initial node
    Shortest_Path_Node current_node;
    //convention vertex_index == n() => corresponds to start Point
    //vertex_index == n() + 1 => corresponds to finish Point
    current_node.vertex_index = n();
    current_node.cost_to_come = 0;
    current_node.estimated_cost_to_go = distance( start , finish );
    //Put in T and on Q
    T.push_back( current_node );
    T.begin()->search_tree_location = T.begin();
    current_node.search_tree_location = T.begin();
    T.begin()->parent_search_tree_location = T.begin();
    current_node.parent_search_tree_location = T.begin();
    Q.insert( current_node );

    //Initialize temporary variables
    Shortest_Path_Node child; //children of current_node
    std::vector<Shortest_Path_Node> children;
    //flags
    bool solution_found = false;
    bool child_already_visited = false;
    //-----------Begin Main Loop-----------
    while( !Q.empty() ){

      //Pop top element off Q onto current_node
      current_node = *Q.begin(); Q.erase( Q.begin() );

      if(PRINTING_DEBUG_DATA){
	std::cout << std::endl
		  <<"=============="
		  <<" current_node just poped off of Q "
		  <<"=============="
		  << std::endl;
		  current_node.print();
		  std::cout << std::endl;
      }
      
      //Check for goal state
      //(if current node corresponds to finish)
      if( current_node.vertex_index == n() + 1 ){
	
	if( PRINTING_DEBUG_DATA ){
	  std::cout <<"solution found!" 
		    << std::endl
		    << std::endl;
	}

	solution_found = true;
	break;
      }

      //Expand current_node (compute children)
      children.clear();

      if( PRINTING_DEBUG_DATA ){
	std::cout << "-------------------------------------------"
		  << std::endl
		  << "Expanding Current Node (Computing Children)"
		  << std::endl
		  << "current size of search tree T = " 
		  << T.size()
		  << std::endl
		  << "-------------------------------------------"
		  << std::endl;      
      }

      //if current_node corresponds to start
      if( current_node.vertex_index == n() ){
	//loop over environment vertices
	for(unsigned i=0; i < n(); i++){
	  if( start_visible[i] ){
	    child.vertex_index = i;
	    child.parent_search_tree_location 
	      = current_node.search_tree_location;
	    child.cost_to_come = distance( start , (*this)(i) );
	    child.estimated_cost_to_go = distance( (*this)(i) , finish );	    
	    children.push_back( child );
	    
	    if( PRINTING_DEBUG_DATA ){
	      std::cout << std::endl << "computed child: " 
			<< std::endl;
	      child.print();
	    }

	  }
	}
      }
      //else current_node corresponds to a vertex of the environment
      else{
	//check which environment vertices are visible
	for(unsigned i=0; i < n(); i++){
	  if( current_node.vertex_index != i )
	    if( visibility_graph( current_node.vertex_index , i ) ){
	      child.vertex_index = i;
	      child.parent_search_tree_location 
		= current_node.search_tree_location;
	      child.cost_to_come = current_node.cost_to_come
		+ distance( (*this)(current_node.vertex_index),
			    (*this)(i) );
	      child.estimated_cost_to_go = distance( (*this)(i) , finish );
	      children.push_back( child );

	      if( PRINTING_DEBUG_DATA ){
		std::cout << std::endl << "computed child: " 
			  << std::endl;
		child.print();
	      }
	      
	    }
	}
	//check if finish is visible
	if( finish_visible[ current_node.vertex_index ] ){
	  child.vertex_index = n() + 1;
	  child.parent_search_tree_location 
	    = current_node.search_tree_location;
	  child.cost_to_come = current_node.cost_to_come
	    + distance( (*this)(current_node.vertex_index) , finish );
	  child.estimated_cost_to_go = 0;	    
	  children.push_back( child );

	  if( PRINTING_DEBUG_DATA ){
	    std::cout << std::endl << "computed child: " 
		      << std::endl;
	    child.print();
	  }

	}
      }
      
      if( PRINTING_DEBUG_DATA ){
	std::cout << std::endl
		  <<"-----------------------------------------"
		  << std::endl
		  << "Processing " << children.size() 
		  << " children" << std::endl
		  << "-----------------------------------------"
		  << std::endl;      
      }

      //Process children
      for( std::vector<Shortest_Path_Node>::iterator
	     children_itr = children.begin();
	   children_itr != children.end();
	   children_itr++ ){
	child_already_visited = false;

	if( PRINTING_DEBUG_DATA ){
	  std::cout << std::endl << "current child being processed: " 
		    << std::endl;
	  children_itr->print();	  
	}

	//Check if child state has already been visited 
	//(by looking in search tree T) 
	for( std::list<Shortest_Path_Node>::iterator T_itr = T.begin();
	     T_itr != T.end(); T_itr++ ){
	  if( children_itr->vertex_index 
	      == T_itr->vertex_index ){
	    children_itr->search_tree_location = T_itr;
	    child_already_visited = true;
	    break;
	  }    
	}	

	if( !child_already_visited ){
	  //Add child to search tree T
	  T.push_back( *children_itr );
	  (--T.end())->search_tree_location = --T.end();
	  children_itr->search_tree_location = --T.end();
	  Q.insert( *children_itr );
	}
	else if( children_itr->search_tree_location->cost_to_come > 
		 children_itr->cost_to_come ){
	  //redirect parent pointer in search tree
	  children_itr->search_tree_location->parent_search_tree_location
	    = children_itr->parent_search_tree_location;
	  //and update cost data
	  children_itr->search_tree_location->cost_to_come
	    = children_itr->cost_to_come;
	  //update Q
	  for(std::set<Shortest_Path_Node>::iterator 
		Q_itr = Q.begin();
	      Q_itr!= Q.end();
	      Q_itr++){
	    if( children_itr->vertex_index == Q_itr->vertex_index ){
	      Q.erase( Q_itr );
	      break;
	    }	  
	  }
	  Q.insert( *children_itr );	  
	}

	//If not already visited, insert into Q
	if( !child_already_visited )
	  Q.insert( *children_itr );

	if( PRINTING_DEBUG_DATA ){
	  std::cout << "child already visited? "
		    << child_already_visited
		    << std::endl;      
	}
      
      }
    }
    //-----------End Main Loop-----------

    //Recover solution
    if( solution_found ){
      shortest_path_output.push_back( finish );
      std::list<Shortest_Path_Node>::iterator
	backtrace_itr = current_node.parent_search_tree_location;
      Point waypoint;

      if( PRINTING_DEBUG_DATA ){
	std::cout << "----------------------------" << std::endl
		  << "backtracing to find solution" << std::endl
		  << "----------------------------" << std::endl;

      }

      while( true ){

	if( PRINTING_DEBUG_DATA ){
	  std::cout << "backtrace node is "
		    << std::endl;
	  backtrace_itr->print();
	  std::cout << std::endl;      
	}

	if( backtrace_itr->vertex_index < n() )
	  waypoint = (*this)( backtrace_itr->vertex_index );
	else if( backtrace_itr->vertex_index == n() )
	  waypoint = start;
	//Add vertex if not redundant
	if( shortest_path_output.size() > 0
	    and distance( shortest_path_output[ shortest_path_output.size()
						- 1 ],
			  waypoint ) > epsilon )
	  shortest_path_output.push_back( waypoint );
	if( backtrace_itr->cost_to_come == 0 )
	  break;
	backtrace_itr = backtrace_itr->parent_search_tree_location;
      }
      shortest_path_output.reverse();
    }

    //free memory
    delete [] start_visible;
    delete [] finish_visible;

    //shortest_path_output.eliminate_redundant_vertices( epsilon );
    //May not be desirable to eliminate redundant vertices, because
    //those redundant vertices can make successive waypoints along the
    //shortest path robustly visible (and thus easier for a robot to
    //navigate)

    return shortest_path_output;
  }
  Polyline Environment::shortest_path(const Point& start,
				      const Point& finish,
				      double epsilon)
  {
    return shortest_path( start,
			  finish,
			  Visibility_Graph(*this, epsilon),
			  epsilon );
  }


  void Environment::write_to_file(const std::string& filename,
				  int fios_precision_temp)
  {
    assert( fios_precision_temp >= 1 );

    std::ofstream fout( filename.c_str() );
    //fout.open( filename.c_str() );  //Alternatives.
    //fout << *this;
    fout.setf(std::ios::fixed);
    fout.setf(std::ios::showpoint);
    fout.precision(fios_precision_temp);
    fout << "//Environment Model" << std::endl;
    fout << "//Outer Boundary" << std::endl << outer_boundary_; 
    for(unsigned i=0; i<h(); i++)
      {
	fout << "//Hole" << std::endl << holes_[i];
      }
    //fout << "//EOF marker";
    fout.close();
  }


  Point& Environment::operator () (unsigned k)
  {
    //std::pair<unsigned,unsigned> ij( one_to_two(k) );
    std::pair<unsigned,unsigned> ij( flattened_index_key_[k] );
    return (*this)[ ij.first ][ ij.second ];
  }


  void Environment::enforce_standard_form()
  {
    if( outer_boundary_.area() < 0 )
      outer_boundary_.reverse();
    outer_boundary_.enforce_standard_form();
    for(unsigned i=0; i<h(); i++){
      if( holes_[i].area() > 0 )
	holes_[i].reverse();
      holes_[i].enforce_standard_form();
    }
  }


  void Environment::eliminate_redundant_vertices(double epsilon)
  {
    outer_boundary_.eliminate_redundant_vertices(epsilon);
    for(unsigned i=0; i<holes_.size(); i++)
      holes_[i].eliminate_redundant_vertices(epsilon);

    update_flattened_index_key();
  }


  void Environment::reverse_holes()
  {
    for(unsigned i=0; i < holes_.size(); i++)
      holes_[i].reverse();
  }


  void Environment::update_flattened_index_key()
  {
    flattened_index_key_.clear();
    std::pair<unsigned, unsigned> pair_temp;
    for(unsigned i=0; i<=h(); i++){
    for(unsigned j=0; j<(*this)[i].n(); j++){
      pair_temp.first = i;
      pair_temp.second = j;
      flattened_index_key_.push_back( pair_temp );
    }}
  }


  std::pair<unsigned,unsigned> Environment::one_to_two(unsigned k) const
  {
    std::pair<unsigned,unsigned> two(0,0);
    //Strategy: add up vertex count of each Polygon (outer boundary +
    //holes) until greater than k
    unsigned current_polygon_index = 0;
    unsigned vertex_count_up_to_current_polygon = (*this)[0].n();
    unsigned vertex_count_up_to_last_polygon = 0;

    while( k >= vertex_count_up_to_current_polygon
	   and current_polygon_index < (*this).h() ){
      current_polygon_index++;
      two.first = two.first + 1;
      vertex_count_up_to_last_polygon = vertex_count_up_to_current_polygon;
      vertex_count_up_to_current_polygon += (*this)[current_polygon_index].n();
    }
    two.second = k - vertex_count_up_to_last_polygon;
    
    return two;
  }


  std::ostream& operator << (std::ostream& outs, 
			     const Environment& environment_temp)
  {
    outs << "//Environment Model" << std::endl;
    outs << "//Outer Boundary" << std::endl << environment_temp[0]; 
    for(unsigned i=1; i<=environment_temp.h(); i++){
      outs << "//Hole" << std::endl << environment_temp[i];
    }
    //outs << "//EOF marker";
    return outs;
  }
  

  //Guards


  Guards::Guards(const std::string& filename)
  {
    std::ifstream fin(filename.c_str());
    //if(fin.fail()) { std::cerr << "\x1b[5;31m" << "Input file
    //opening failed." << "\x1b[0m\n" << "\a \n"; exit(1);}
    assert( !fin.fail() );

    //Temp vars for numbers to be read from file.
    double x_temp, y_temp;  
    
    //Skip comments
    while( fin.peek() == '/' ) 
      fin.ignore(200,'\n');

    //Read positions.
    while (1){
      fin >> x_temp >> y_temp;
      if( fin.eof() )
	{ fin.close(); return; }
      positions_.push_back( Point(x_temp, y_temp) );
      //Skip to next line.
      fin.ignore(1);
      //Skip comments
      while( fin.peek() == '/' )
	fin.ignore(200,'\n');
    }
  }


  bool Guards::are_lex_ordered() const
  {
    //if more than one guard.
    if(positions_.size() > 1)
      for(unsigned i=0; i<positions_.size()-1; i++)
	if(positions_[i] > positions_[i+1])
	  return false;
    return true;
  }


  bool Guards::noncolocated(double epsilon) const
  {
    for(unsigned i=0; i<positions_.size(); i++)
      for(unsigned j=i+1; j<positions_.size(); j++)
	if( distance(positions_[i], positions_[j]) <= epsilon )
	  return false;
    return true;
  }


  bool Guards::in(const Polygon& polygon_temp, double epsilon) const
  {
    for(unsigned i=0; i<positions_.size(); i++)
      if(!positions_[i].in(polygon_temp, epsilon))
	return false;
    return true;
  }


  bool Guards::in(const Environment& environment_temp, double epsilon) const
  {
    for(unsigned i=0; i<positions_.size(); i++)
      if(!positions_[i].in(environment_temp, epsilon))
	return false;
    return true;
  }


  double Guards::diameter() const
  {
    //Precondition:  more than 0 guards
    assert( N() > 0 );

    double running_max=0;
    for(unsigned i=0; i<N()-1; i++){
    for(unsigned j=i+1; j<N(); j++){
      if( distance( (*this)[i] , (*this)[j] ) > running_max )
	running_max = distance( (*this)[i] , (*this)[j] );
    }}
    return running_max;
  }


  Bounding_Box Guards::bbox() const
  {
    //Precondition:  nonempty Guard set
    assert( positions_.size() > 0 );

    Bounding_Box bounding_box;
    double x_min=positions_[0].x(), x_max=positions_[0].x(),
      y_min=positions_[0].y(), y_max=positions_[0].y();
    for(unsigned i = 1; i <  positions_.size(); i++){
      if(x_min > positions_[i].x())  { x_min=positions_[i].x(); }
      if(x_max < positions_[i].x())  { x_max=positions_[i].x(); }
      if(y_min > positions_[i].y())  { y_min=positions_[i].y(); }
      if(y_max < positions_[i].y())  { y_max=positions_[i].y(); }
    }
    bounding_box.x_min=x_min; bounding_box.x_max=x_max;
    bounding_box.y_min=y_min; bounding_box.y_max=y_max;
    return bounding_box;
  }


  void Guards::write_to_file(const std::string& filename,
			     int fios_precision_temp)
  {
    assert( fios_precision_temp >= 1 );

    std::ofstream fout( filename.c_str() );
    //fout.open( filename.c_str() );  //Alternatives.
    //fout << *this;
    fout.setf(std::ios::fixed);
    fout.setf(std::ios::showpoint);
    fout.precision(fios_precision_temp);
    fout << "//Guard Positions" << std::endl; 
    for(unsigned i=0; i<positions_.size(); i++)
      fout << positions_[i].x() << "  " << positions_[i].y() << std::endl;
    //fout << "//EOF marker";
    fout.close();
  }


  void Guards::enforce_lex_order()
  {
    //std::stable_sort(positions_.begin(), positions_.end());
    std::sort(positions_.begin(), positions_.end());
  }


  void Guards::reverse()
  {
    std::reverse( positions_.begin() , positions_.end() );
  }


  void Guards::snap_to_vertices_of(const Environment& environment_temp,
				   double epsilon)
  {
    for(unsigned i=0; i<positions_.size(); i++)
      positions_[i].snap_to_vertices_of(environment_temp);
  }


  void Guards::snap_to_vertices_of(const Polygon& polygon_temp,
				   double epsilon)
  {
    for(unsigned i=0; i<positions_.size(); i++)
      positions_[i].snap_to_vertices_of(polygon_temp);
  }


  void Guards::snap_to_boundary_of(const Environment& environment_temp,
				   double epsilon)
  {
    for(unsigned i=0; i<positions_.size(); i++)
      positions_[i].snap_to_boundary_of(environment_temp);
  }


  void Guards::snap_to_boundary_of(const Polygon& polygon_temp,
				   double epsilon)
  {
    for(unsigned i=0; i<positions_.size(); i++)
      positions_[i].snap_to_boundary_of(polygon_temp);
  }


  std::ostream& operator << (std::ostream& outs, const Guards& guards)
  {
    outs << "//Guard Positions" << std::endl; 
    for(unsigned i=0; i<guards.N(); i++)
      outs << guards[i].x() << "  " << guards[i].y() << std::endl;
    //outs << "//EOF marker";
    return outs;
  }


  //Visibility_Polygon


  bool Visibility_Polygon::is_spike( const Point& observer,
				     const Point& point1,
				     const Point& point2,
				     const Point& point3, 
				     double epsilon) const
  {

    return(  
         //Make sure observer not colocated with any of the points.
	   distance( observer , point1 ) > epsilon
	   and distance( observer , point2 ) > epsilon
	   and distance( observer , point3 ) > epsilon
	 //Test whether there is a spike with point2 as the tip
	   and (  ( distance(observer,point2) 
		    >= distance(observer,point1)
		    and distance(observer,point2) 
		    >= distance(observer,point3) ) 
		  or ( distance(observer,point2) 
		       <= distance(observer,point1)
		       and distance(observer,point2) 
		       <= distance(observer,point3) )  )
	 //and the pike is sufficiently sharp,
	   and std::max(  distance( Ray(observer, point1), point2 ), 
			  distance( Ray(observer, point3), point2 )  )
	   <= epsilon  
	     );
    //Formerly used
    //std::fabs( Polygon(point1, point2, point3).area() ) < epsilon
  }

  
  void Visibility_Polygon::chop_spikes_at_back(const Point& observer,
					       double epsilon)
  {
    //Eliminate "special case" vertices of the visibility polygon.
    //While the top three vertices form a spike.
    while(  vertices_.size() >= 3
	    and is_spike( observer, 
			  vertices_[vertices_.size()-3],
			  vertices_[vertices_.size()-2],
			  vertices_[vertices_.size()-1], epsilon )  ){
      vertices_[vertices_.size()-2] = vertices_[vertices_.size()-1];
      vertices_.pop_back();
    }
  }


  void Visibility_Polygon::chop_spikes_at_wrap_around(const Point& observer,
						      double epsilon)
  {
    //Eliminate "special case" vertices of the visibility polygon at
    //wrap-around.  While the there's a spike at the wrap-around,
    while(  vertices_.size() >= 3 
	    and is_spike( observer,
			  vertices_[vertices_.size()-2],
			  vertices_[vertices_.size()-1],
			  vertices_[0], epsilon )  ){
      //Chop off the tip of the spike.
      vertices_.pop_back();
    }
  }


  void Visibility_Polygon::chop_spikes(const Point& observer,
				       double epsilon)
  {    
    std::set<Point> spike_tips;
    std::vector<Point> vertices_temp;
    //Middle point is potentially the tip of a spike
    for(unsigned i=0; i<vertices_.size(); i++)
      if(   distance(  (*this)[i+2], 
		       Line_Segment( (*this)[i], (*this)[i+1] )  )
	    <= epsilon
	    or
	    distance(  (*this)[i], 
		       Line_Segment( (*this)[i+1], (*this)[i+2] )  )
	    <= epsilon   )
	spike_tips.insert( (*this)[i+1] );
    
    for(unsigned i=0; i<vertices_.size(); i++)
      if( spike_tips.find(vertices_[i]) == spike_tips.end() )
	vertices_temp.push_back( vertices_[i] );
    vertices_.swap( vertices_temp );
  }


  void Visibility_Polygon::
  print_cv_and_ae(const Polar_Point_With_Edge_Info& current_vertex,
		  const std::list<Polar_Edge>::iterator&
		  active_edge)
  {
    std::cout << "           current_vertex [x  y  bearing  range is_first] = ["
	      << current_vertex.x() << "  "
	      << current_vertex.y() << "  "
	      << current_vertex.bearing() << "  "
	      << current_vertex.range() << "  "
	      << current_vertex.is_first << "]" << std::endl;
    std::cout << "1st point of current_vertex's edge [x  y  bearing  range] = ["
	      << (current_vertex.incident_edge->first).x() << "  " 
	      << (current_vertex.incident_edge->first).y() << "  "
	      << (current_vertex.incident_edge->first).bearing() << "  "
	      << (current_vertex.incident_edge->first).range() << "]" 
	      << std::endl;
    std::cout << "2nd point of current_vertex's edge [x  y  bearing  range] = [" 
	      << (current_vertex.incident_edge->second).x() << "  " 
	      << (current_vertex.incident_edge->second).y() << "  "
	      << (current_vertex.incident_edge->second).bearing() << "  "
	      << (current_vertex.incident_edge->second).range() << "]" 
	      << std::endl;
    std::cout << "          1st point of active_edge [x  y  bearing  range] = ["
	      << (active_edge->first).x() << "  " 
	      << (active_edge->first).y() << "  "
	      << (active_edge->first).bearing() << "  "
	      << (active_edge->first).range() << "]" << std::endl;
    std::cout << "          2nd point of active_edge [x  y  bearing  range] = [" 
	      << (active_edge->second).x() << "  " 
	      << (active_edge->second).y() << "  "
	      << (active_edge->second).bearing() << "  "
	      << (active_edge->second).range() << "]" << std::endl;
  }


  Visibility_Polygon::Visibility_Polygon(const Point& observer,
					 const Environment& environment_temp,
					 double epsilon)
    : observer_(observer)
  {
    //Visibility polygon algorithm for environments with holes 
    //Radial line (AKA angular plane) sweep technique.
    //
    //Based on algorithms described in 
    //
    //[1] "Automated Camera Layout to Satisfy Task-Specific and
    //Floorplan-Specific Coverage Requirements" by Ugur Murat Erdem
    //and Stan Scarloff, April 15, 2004
    //available at BUCS Technical Report Archive:
    //http://www.cs.bu.edu/techreports/pdf/2004-015-camera-layout.pdf
    //
    //[2] "Art Gallery Theorems and Algorithms" by Joseph O'Rourke
    //
    //[3] "Visibility Algorithms in the Plane" by Ghosh
    //

    //We define a k-point is a point seen on the other side of a
    //visibility occluding corner.  This name is appropriate because
    //the vertical line in the letter "k" is like a line-of-sight past
    //the corner of the "k".

    //
    //Preconditions:
    //(1)  the Environment is epsilon-valid,
    //(2)  the Point observer is actually in the Environment
    //     environment_temp,
    //(3)  the guard has been epsilon-snapped to the boundary, followed
    //     by vertices of the environment (the order of the snapping
    //     is important).
    //
    //:WARNING: 
    //For efficiency, the assertions corresponding to these
    //preconditions have been excluded.
    //
    //assert( environment_temp.is_valid(epsilon) );
    //assert( environment_temp.is_in_standard_form() );
    //assert( observer.in(environment_temp, epsilon) );

    //true  => data printed to terminal
    //false => silent
    const bool PRINTING_DEBUG_DATA = false;

    //The visibility polygon cannot have more vertices than the environment.
    vertices_.reserve( environment_temp.n() );

    //
    //--------PREPROCESSING--------
    //
     
    //Construct a POLAR EDGE LIST from environment_temp's outer
    //boundary and holes.  During this construction, those edges are
    //split which either (1) cross the ray emanating from the observer
    //parallel to the x-axis (of world coords), or (2) contain the
    //observer in their relative interior (w/in epsilon).  Also, edges
    //having first vertex bearing >= second vertex bearing are
    //eliminated because they cannot possibly contribute to the
    //visibility polygon.
    std::list<Polar_Edge> elp;
    Polar_Point ppoint1, ppoint2;
    Polar_Point split_bottom, split_top;
    double t;
    //If the observer is standing on the Enviroment boundary with its
    //back to the wall, these will be the bearings of the next vertex
    //to the right and to the left, respectively.
    Angle right_wall_bearing;
    Angle left_wall_bearing;
    for(unsigned i=0; i<=environment_temp.h(); i++){
    for(unsigned j=0; j<environment_temp[i].n(); j++){      
      ppoint1 = Polar_Point( observer, environment_temp[i][j] );
      ppoint2 = Polar_Point( observer, environment_temp[i][j+1] );

      //If the observer is in the relative interior of the edge.
      if(  observer.in_relative_interior_of( Line_Segment(ppoint1, ppoint2),
					     epsilon )  ){
	//Split the edge at the observer and add the resulting two
	//edges to elp (the polar edge list).
	split_bottom = Polar_Point(observer, observer);
	split_top = Polar_Point(observer, observer);

	if( ppoint2.bearing() == Angle(0.0) )
	  ppoint2.set_bearing_to_2pi();
	
	left_wall_bearing = ppoint1.bearing();
	right_wall_bearing = ppoint2.bearing();

	elp.push_back(  Polar_Edge( ppoint1   , split_bottom )  );
	elp.push_back(  Polar_Edge( split_top , ppoint2      )  );
	continue;
      }

      //Else if the observer is on first vertex of edge.
      else if( distance(observer, ppoint1) <= epsilon ){
	if( ppoint2.bearing() == Angle(0.0) )
	  ppoint2.set_bearing_to_2pi();
	//Get right wall bearing.
	right_wall_bearing = ppoint2.bearing();
	elp.push_back(  Polar_Edge( Polar_Point(observer, observer),
				    ppoint2 )  );
	continue;
      }
      //Else if the observer is on second vertex of edge.
      else if( distance(observer, ppoint2) <= epsilon ){
	//Get left wall bearing.
	left_wall_bearing = ppoint1.bearing();
	elp.push_back(  Polar_Edge( ppoint1,
				    Polar_Point(observer, observer) )  );	
	continue;
      }

      //Otherwise the observer is not on the edge.

      //If edge not horizontal (w/in epsilon).
      else if(  std::fabs( ppoint1.y() - ppoint2.y() ) > epsilon  ){
	//Possible source of numerical instability?
	t = ( observer.y() - ppoint2.y() ) 
	  / ( ppoint1.y() - ppoint2.y() );
	//If edge crosses the ray emanating horizontal and right of
	//the observer.
	if( 0 < t and t < 1 and 
	    observer.x() < t*ppoint1.x() + (1-t)*ppoint2.x() ){ 
	  //If first point is above, omit edge because it runs
	  //'against the grain'.
	  if( ppoint1.y() > observer.y() )
	    continue;
	  //Otherwise split the edge, making sure angles are assigned
	  //correctly on each side of the split point.
	  split_bottom = split_top 
	    = Polar_Point(  observer,
			    Point( t*ppoint1.x() + (1-t)*ppoint2.x(),
				   observer.y() )  );
	  split_top.set_bearing( Angle(0.0) );
	  split_bottom.set_bearing_to_2pi();
	  elp.push_back(  Polar_Edge( ppoint1   , split_bottom )  );
	  elp.push_back(  Polar_Edge( split_top , ppoint2      )  );
	  continue;
	}
	//If the edge is not horizontal and doesn't cross the ray
	//emanating horizontal and right of the observer.
	else if( ppoint1.bearing() >= ppoint2.bearing()
		 and ppoint2.bearing() == Angle(0.0) 
		 and ppoint1.bearing() > Angle(M_PI) )
	  ppoint2.set_bearing_to_2pi();
	//Filter out edges which run 'against the grain'.
	else if(  ( ppoint1.bearing() == Angle(0,0)
		    and ppoint2.bearing() > Angle(M_PI) )
		  or  ppoint1.bearing() >= ppoint2.bearing()  )
	  continue;
	elp.push_back(  Polar_Edge( ppoint1, ppoint2 )  );
	continue;
      }    
      //If edge is horizontal (w/in epsilon).
      else{
	//Filter out edges which run 'against the grain'.
	if( ppoint1.bearing() >= ppoint2.bearing() ) 
	  continue;
	elp.push_back(  Polar_Edge( ppoint1, ppoint2 )  );
      }
    }}
  
    //Construct a SORTED LIST, q1, OF VERTICES represented by
    //Polar_Point_With_Edge_Info objects.  A
    //Polar_Point_With_Edge_Info is a derived class of Polar_Point
    //which includes (1) a pointer to the corresponding edge
    //(represented as a Polar_Edge) in the polar edge list elp, and
    //(2) a boolean (is_first) which is true iff that vertex is the
    //first Point of the respective edge (is_first == false => it's
    //second Point).  q1 is sorted according to lex. order of polar
    //coordinates just as Polar_Points are, but with the additional
    //requirement that if two vertices have equal polar coordinates,
    //the vertex which is the first point of its respective edge is
    //considered greater.  q1 will serve as an event point queue for
    //the radial sweep.
    std::list<Polar_Point_With_Edge_Info> q1;
    Polar_Point_With_Edge_Info ppoint_wei1, ppoint_wei2;
    std::list<Polar_Edge>::iterator elp_iterator;
    for(elp_iterator=elp.begin(); 
	elp_iterator!=elp.end(); 
	elp_iterator++){
      ppoint_wei1.set_polar_point( elp_iterator->first );
      ppoint_wei1.incident_edge = elp_iterator;
      ppoint_wei1.is_first = true;
      ppoint_wei2.set_polar_point( elp_iterator->second );
      ppoint_wei2.incident_edge = elp_iterator;
      ppoint_wei2.is_first = false;
      //If edge contains the observer, then adjust the bearing of
      //the Polar_Point containing the observer.
      if( distance(observer, ppoint_wei1) <= epsilon ){
	if( right_wall_bearing > left_wall_bearing ){
	  ppoint_wei1.set_bearing( right_wall_bearing );
	  (elp_iterator->first).set_bearing( right_wall_bearing );
	}
	else{
	  ppoint_wei1.set_bearing( Angle(0.0) );
	  (elp_iterator->first).set_bearing( Angle(0.0) );
	}
      } 
      else if( distance(observer, ppoint_wei2) <= epsilon ){
	if( right_wall_bearing > left_wall_bearing ){
	  ppoint_wei2.set_bearing(right_wall_bearing);
	  (elp_iterator->second).set_bearing( right_wall_bearing );
	}
	else{
	  ppoint_wei2.set_bearing_to_2pi();
	  (elp_iterator->second).set_bearing_to_2pi();
	}
      }
      q1.push_back(ppoint_wei1);
      q1.push_back(ppoint_wei2);
    }
    //Put event point in correct order.
    //STL list's sort method is a stable sort. 
    q1.sort();

    if(PRINTING_DEBUG_DATA){
      std::cout << std::endl 
		<< "\E[1;37;40m" 
		<< "COMPUTING VISIBILITY POLYGON " << std::endl
		<<  "for an observer located at [x y] = [" 
		<< observer << "]" 
		<< "\x1b[0m" 
		<< std::endl << std::endl
		<< "\E[1;37;40m" <<"PREPROCESSING" << "\x1b[0m" 
		<< std::endl << std::endl 
		<< "q1 is" << std::endl;
      std::list<Polar_Point_With_Edge_Info>::iterator q1_itr;
      for(q1_itr=q1.begin(); q1_itr!=q1.end(); q1_itr++){
	std::cout << "[x  y  bearing  range is_first] = [" 
		  << q1_itr->x() << "  " 
		  << q1_itr->y() << "  " 
		  << q1_itr->bearing() << "  " 
		  << q1_itr->range() << "  " 
		  << q1_itr->is_first << "]" 
		  << std::endl;
      }
    }

    //
    //-------PREPARE FOR MAIN LOOP-------
    //

    //current_vertex is used to hold the event point (from q1)
    //considered at iteration of the main loop.
    Polar_Point_With_Edge_Info current_vertex;
    //Note active_edge and e are not actually edges themselves, but
    //iterators pointing to edges.  active_edge keeps track of the
    //current edge visibile during the sweep.  e is an auxiliary
    //variable used in calculation of k-points
    std::list<Polar_Edge>::iterator active_edge, e;
    //More aux vars for computing k-points.
    Polar_Point k;
    double k_range;
    Line_Segment xing;

    //Priority queue of edges, where higher priority indicates closer
    //range to observer along current ray (of ray sweep).
    Incident_Edge_Compare my_iec(observer, current_vertex, epsilon);
    std::priority_queue<std::list<Polar_Edge>::iterator,
                        std::vector<std::list<Polar_Edge>::iterator>,
                        Incident_Edge_Compare> q2(my_iec);
   
    //Initialize main loop.
    current_vertex = q1.front(); q1.pop_front();
    active_edge = current_vertex.incident_edge;

    if(PRINTING_DEBUG_DATA){
      std::cout << std::endl
		<< "\E[1;37;40m" 
		<< "INITIALIZATION" 
		<< "\x1b[0m" 
		<< std::endl << std::endl
		<< "\x1b[35m" 
		<< "Pop first vertex off q1" 
		<< "\x1b[0m"
		<< ", set as current_vertex, \n"
		<< "and set active_edge to the corresponding "
		<< "incident edge."
		<< std::endl;
      print_cv_and_ae(current_vertex, active_edge); 
    }

    //Insert e into q2 as long as it doesn't contain the
    //observer.
    if( distance(observer,active_edge->first) > epsilon
	and distance(observer,active_edge->second) > epsilon ){
     
      if(PRINTING_DEBUG_DATA){
	std::cout << std::endl
		  << "Push current_vertex's edge onto q2."
		  << std::endl;
      }

      q2.push(active_edge);
    }

    if(PRINTING_DEBUG_DATA){
      std::cout << std::endl
		<< "\E[32m" 
		<< "Add current_vertex to visibility polygon." 
		<< "\x1b[0m" 
		<< std::endl << std::endl
		<< "\E[1;37;40m" 
		<< "MAIN LOOP" 
		<< "\x1b[0m" 
		<< std::endl;
    }

    vertices_.push_back(current_vertex);

    //-------BEGIN MAIN LOOP-------//
    //
    //Perform radial sweep by sequentially considering each vertex
    //(event point) in q1.
    while( !q1.empty() ){

      //Pop current_vertex from q1.
      current_vertex = q1.front(); q1.pop_front();

      if(PRINTING_DEBUG_DATA){
	std::cout << std::endl
		  << "\x1b[35m" 
		  << "Pop next vertex off q1" << "\x1b[0m"
		  << " and set as current_vertex."
		  << std::endl;
	print_cv_and_ae(current_vertex, active_edge);
      }

      //---Handle Event Point---

      //TYPE 1: current_vertex is the _second_vertex_ of active_edge.
      if( current_vertex.incident_edge == active_edge 
	  and !current_vertex.is_first ){

	if(PRINTING_DEBUG_DATA){
	  std::cout << std::endl
		    << "\E[36m" << "TYPE 1:" << "\x1b[0m"
		    << " current_vertex is the second vertex of active_edge."
		    << std::endl;
	}

	if( !q1.empty() ){ 
	  //If the next vertex in q1 is contiguous.
	  if( distance( current_vertex, q1.front() ) <= epsilon ){

	    if(PRINTING_DEBUG_DATA){
	      std::cout << std::endl
			<< "current_vertex is contiguous "
			<< "with the next vertex in q1."
			<< std::endl;
	    }

	    continue;
	  }
	}

	if(PRINTING_DEBUG_DATA){
	  std::cout << std::endl
		    << "\E[32m" << "Add current_vertex to visibility polygon." 
		    << "\x1b[0m" << std::endl;
	}

	//Push current_vertex onto visibility polygon 
	vertices_.push_back( current_vertex );
	chop_spikes_at_back(observer, epsilon);

	while( !q2.empty() ){
	  e = q2.top();

	  if(PRINTING_DEBUG_DATA){
	    std::cout << std::endl
		      << "Examine edge at top of q2." << std::endl
		      << "1st point of e [x  y  bearing  range] = ["
		      << (e->first).x() << "  " 
		      << (e->first).y() << "  "
		      << (e->first).bearing() << "  "
		      << (e->first).range() << "]" << std::endl
		      << "2nd point of e [x  y  bearing  range] = [" 
		      << (e->second).x() << "  " 
		      << (e->second).y() << "  "
		      << (e->second).bearing() << "  "
		      << (e->second).range() << "]" << std::endl;
	  }

	  //If the current_vertex bearing has not passed, in the
	  //lex. order sense, the bearing of the second point of the
	  //edge at the front of q2.
	  if(  ( current_vertex.bearing().get() 
		 <= e->second.bearing().get() )
	       //For robustness.
	       and distance( Ray(observer, current_vertex.bearing()),
			     e->second ) >= epsilon
	       /* was
	       and std::min( distance(Ray(observer, current_vertex.bearing()),
				      e->second), 
			     distance(Ray(observer, e->second.bearing()),
				      current_vertex) 
			     ) >= epsilon
	       */
	       ){
 	    //Find intersection point k of ray (through
	    //current_vertex) with edge e.
	    xing = intersection( Ray(observer, current_vertex.bearing()), 
				 Line_Segment(e->first,
					      e->second),
				 epsilon );
	    	    
	    //assert( xing.size() > 0 );
	    
	    if( xing.size() > 0 ){
	      k = Polar_Point( observer , xing.first() );
	    }
	    else{ //Error contingency.
	      k = current_vertex;
	      e = current_vertex.incident_edge;
	    }

	    if(PRINTING_DEBUG_DATA){
	      std::cout << std::endl
			<< "\E[32m" 
			<< "Add a type 1 k-point to visibility polygon." 
			<< "\x1b[0m" << std::endl
			<< std::endl
			<< "Set active_edge to edge at top of q2." 
			<< std::endl;
	    }

	    //Push k onto the visibility polygon.
	    vertices_.push_back(k);
	    chop_spikes_at_back(observer, epsilon);
	    active_edge = e;
	    break;
	  }

	  if(PRINTING_DEBUG_DATA){
	    std::cout << std::endl
		      << "Pop edge off top of q2." << std::endl;
	  }
	  
	  q2.pop();
	}
      } //Close Type 1.

      //If current_vertex is the _first_vertex_ of its edge.
      if( current_vertex.is_first ){
	//Find intersection point k of ray (through current_vertex)
	//with active_edge.
	xing = intersection( Ray(observer, current_vertex.bearing()), 
			     Line_Segment(active_edge->first,
					  active_edge->second),
			     epsilon );
	if(  xing.size() == 0 
	     or ( distance(active_edge->first, observer) <= epsilon 
		  and active_edge->second.bearing() 
		  <= current_vertex.bearing() )
	     or active_edge->second < current_vertex  ){
	  k_range = INFINITY;
	}
	else{
	  k = Polar_Point( observer , xing.first() );
	  k_range = k.range();
	}

	//Incident edge of current_vertex.
	e = current_vertex.incident_edge;
	
	if(PRINTING_DEBUG_DATA){
	  std::cout << std::endl
		    << "               k_range = " 
		    << k_range 
		    << " (range of active edge along "
		    <<   "bearing of current vertex)" << std::endl
		    << "current_vertex.range() = " 
		    << current_vertex.range() << std::endl;
	}
	
	//Insert e into q2 as long as it doesn't contain the
	//observer.
	if( distance(observer, e->first) > epsilon
	    and distance(observer, e->second) > epsilon ){
	 
	  if(PRINTING_DEBUG_DATA){
	    std::cout << std::endl
		      << "Push current_vertex's edge onto q2."
		      << std::endl;
	  }
	  
	  q2.push(e);
	}

	//TYPE 2: current_vertex is (1) a first vertex of some edge
	//other than active_edge, and (2) that edge should not become
	//the next active_edge.  This happens, e.g., if that edge is
	//(rangewise) in back along the current bearing.
	if( k_range < current_vertex.range() ){

	  if(PRINTING_DEBUG_DATA){
	    std::cout << std::endl
		      << "\E[36m" << "TYPE 2:" << "\x1b[0m"
		      << " current_vertex is" << std::endl
		      << "(1) a first vertex of some edge "
	                 "other than active_edge, and" << std::endl
		      << "(2) that edge should not become "
		      << "the next active_edge."
		      << std::endl;
	 
	  }

	} //Close Type 2.

	//TYPE 3: current_vertex is (1) the first vertex of some edge
	//other than active_edge, and (2) that edge should become the
	//next active_edge.  This happens, e.g., if that edge is
	//(rangewise) in front along the current bearing.
	if(  k_range >= current_vertex.range() 
	     ){
	  
	  if(PRINTING_DEBUG_DATA){
	    std::cout << std::endl
		      << "\E[36m" << "TYPE 3:" << "\x1b[0m"
		      << " current_vertex is" << std::endl
		      << "(1) the first vertex of some edge "
	                 "other than active edge, and" << std::endl
		      << "(2) that edge should become "
		      << "the next active_edge."
		      << std::endl;
	  }

	  //Push k onto the visibility polygon unless effectively
	  //contiguous with current_vertex.
	  if( xing.size() > 0
	      //and k == k
	      and k_range != INFINITY
	      and distance(k, current_vertex) > epsilon 
	      and distance(active_edge->first, observer) > epsilon 
	      ){
	   
	    if(PRINTING_DEBUG_DATA){
	      std::cout << std::endl
			<< "\E[32m" 
			<< "Add type 3 k-point to visibility polygon." 
			<< "\x1b[0m" << std::endl;
	    }

	    //Push k-point onto the visibility polygon.
	    vertices_.push_back(k);
	    chop_spikes_at_back(observer, epsilon);
	  }

	  //Push current_vertex onto the visibility polygon.
	  vertices_.push_back(current_vertex);
	  chop_spikes_at_back(observer, epsilon);
	  //Set active_edge to edge of current_vertex.
	  active_edge = e;

	  if(PRINTING_DEBUG_DATA){
	    std::cout << std::endl
		      << "\E[32m" << "Add current_vertex to visibility polygon." 
		      << "\x1b[0m" << std::endl
		      << std::endl
		      << "Set active_edge to edge of current_vertex." 
		      << std::endl;
	  }

	} //Close Type 3.
      }
      
      if(PRINTING_DEBUG_DATA){
	std::cout << std::endl
		  << "visibility polygon vertices so far are \n"
		  << Polygon(vertices_) << std::endl
		  << std::endl;
      }
    }                            //
                                 //
    //-------END MAIN LOOP-------//
  
    //The Visibility_Polygon should have a minimal representation
    chop_spikes_at_wrap_around( observer , epsilon );
    eliminate_redundant_vertices( epsilon );
    chop_spikes( observer, epsilon );
    enforce_standard_form();
    
    if(PRINTING_DEBUG_DATA){
      std::cout << std::endl
		<< "Final visibility polygon vertices are \n"
		<< Polygon(vertices_) << std::endl
		<< std::endl;      
    }

  }
  Visibility_Polygon::Visibility_Polygon(const Point& observer,
					 const Polygon& polygon_temp,
					 double epsilon)
  {
    *this = Visibility_Polygon( observer, Environment(polygon_temp), epsilon );
  }


  //Visibility_Graph


  Visibility_Graph::Visibility_Graph( const Visibility_Graph& vg2 )
  {
    n_ = vg2.n_;
    vertex_counts_ = vg2.vertex_counts_;

    //Allocate adjacency matrix
    adjacency_matrix_ = new bool*[n_];
    adjacency_matrix_[0] = new bool[n_*n_];
    for(unsigned i=1; i<n_; i++)
      adjacency_matrix_[i] = adjacency_matrix_[i-1] + n_;

    //copy each entry
    for(unsigned i=0; i<n_; i++){
    for(unsigned j=0; j<n_; j++){
      adjacency_matrix_[i][j] 
	= vg2.adjacency_matrix_[i][j];
    }}
  }


  Visibility_Graph::Visibility_Graph(const Environment& environment,
				     double epsilon)
  {
    n_ = environment.n();

    //fill vertex_counts_
    vertex_counts_.reserve( environment.h() );
    for(unsigned i=0; i<environment.h(); i++)
      vertex_counts_.push_back( environment[i].n() );

    //allocate a contiguous chunk of memory for adjacency_matrix_
    adjacency_matrix_ = new bool*[n_];
    adjacency_matrix_[0] = new bool[n_*n_];
    for(unsigned i=1; i<n_; i++)
      adjacency_matrix_[i] = adjacency_matrix_[i-1] + n_;
    
    // fill adjacency matrix by checking for inclusion in the
    // visibility polygons
    Polygon polygon_temp;
    for(unsigned k1=0; k1<n_; k1++){
      polygon_temp = Visibility_Polygon( environment(k1),
					 environment,
					 epsilon );
      for(unsigned k2=0; k2<n_; k2++){
	if( k1 == k2 )
	  adjacency_matrix_[ k1 ][ k1 ] = true;
	else
	  adjacency_matrix_[ k1 ][ k2 ] =
	    adjacency_matrix_[ k2 ][ k1 ] =    
	    environment(k2).in( polygon_temp , epsilon ); 
      }
    }
  }


  Visibility_Graph::Visibility_Graph(const std::vector<Point> points,
				     const Environment& environment,
				     double epsilon)
  {
    n_ = points.size();

    //fill vertex_counts_
    vertex_counts_.push_back( n_ );

    //allocate a contiguous chunk of memory for adjacency_matrix_
    adjacency_matrix_ = new bool*[n_];
    adjacency_matrix_[0] = new bool[n_*n_];
    for(unsigned i=1; i<n_; i++)
      adjacency_matrix_[i] = adjacency_matrix_[i-1] + n_;
    
    // fill adjacency matrix by checking for inclusion in the
    // visibility polygons
    Polygon polygon_temp;
    for(unsigned k1=0; k1<n_; k1++){
      polygon_temp = Visibility_Polygon( points[k1],
					 environment,
					 epsilon );
      for(unsigned k2=0; k2<n_; k2++){
	if( k1 == k2 )
	  adjacency_matrix_[ k1 ][ k1 ] = true;
	else
	  adjacency_matrix_[ k1 ][ k2 ] =
	    adjacency_matrix_[ k2 ][ k1 ] =    
	    points[k2].in( polygon_temp , epsilon ); 
      }
    }
  }

  
  Visibility_Graph::Visibility_Graph(const Guards& guards,
				     const Environment& environment, 
				     double epsilon)
  {
    *this = Visibility_Graph( guards.positions_,
			      environment, 
			      epsilon );
  }
  
  
  bool Visibility_Graph::operator () (unsigned i1,
				      unsigned j1,
				      unsigned i2,
				      unsigned j2) const 
  {
    return adjacency_matrix_[ two_to_one(i1,j1) ][ two_to_one(i2,j2) ];
  }
  bool Visibility_Graph::operator () (unsigned k1,
				      unsigned k2) const 
  {
    return adjacency_matrix_[ k1 ][ k2 ];
  }
  bool& Visibility_Graph::operator () (unsigned i1,
				       unsigned j1,
				       unsigned i2,
				       unsigned j2)
  {
    return adjacency_matrix_[ two_to_one(i1,j1) ][ two_to_one(i2,j2) ];
  }
  bool& Visibility_Graph::operator () (unsigned k1,
				       unsigned k2)
  {
    return adjacency_matrix_[ k1 ][ k2 ];
  }


  Visibility_Graph& Visibility_Graph::operator = 
  (const Visibility_Graph& visibility_graph_temp)
  {
    if( this == &visibility_graph_temp )
      return *this;
    
    n_ = visibility_graph_temp.n_;
    vertex_counts_ = visibility_graph_temp.vertex_counts_;

    //resize adjacency_matrix_
    if( adjacency_matrix_ != NULL ){
      delete [] adjacency_matrix_[0];
      delete [] adjacency_matrix_; 
    }
    adjacency_matrix_ = new bool*[n_];
    adjacency_matrix_[0] = new bool[n_*n_];
    for(unsigned i=1; i<n_; i++)
      adjacency_matrix_[i] = adjacency_matrix_[i-1] + n_;

    //copy each entry
    for(unsigned i=0; i<n_; i++){
    for(unsigned j=0; j<n_; j++){
      adjacency_matrix_[i][j] 
	= visibility_graph_temp.adjacency_matrix_[i][j];
    }}
    
    return *this;
  }
  
  
  unsigned Visibility_Graph::two_to_one(unsigned i,
					unsigned j) const
  {
    unsigned k=0;

    for(unsigned counter=0; counter<i; counter++)
      k += vertex_counts_[counter];
    k += j;

    return k;
  }


  Visibility_Graph::~Visibility_Graph()    
  { 
    if( adjacency_matrix_ != NULL ){
      delete [] adjacency_matrix_[0];
      delete [] adjacency_matrix_; 
    }
  }


  std::ostream& operator << (std::ostream& outs,
			     const Visibility_Graph& visibility_graph)
  {
    for(unsigned k1=0; k1<visibility_graph.n(); k1++){
      for(unsigned k2=0; k2<visibility_graph.n(); k2++){
	outs << visibility_graph( k1, k2 );
	if( k2 < visibility_graph.n()-1 )
	  outs << "  ";
	else
	  outs << std::endl;
      }
    }

    return outs;
  }

}
