// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_POINT_SIMPLEX_SQUARED_DISTANCE_H
#define IGL_POINT_SIMPLEX_SQUARED_DISTANCE_H
#include "igl_inline.h"
#include <Eigen/Core>
namespace igl
{
  // Determine squared distance from a point to linear simplex. 
  //
  // Inputs:
  //   p  d-long query point
  //   V  #V by d list of vertices
  //   Ele  #Ele by ss<=d+1 list of simplex indices into V
  //   i  index into Ele of simplex
  // Outputs:
  //   sqr_d  squared distance of Ele(i) to p
  //   c  closest point on Ele(i) 
  //
  template <
    int DIM,
    typename Derivedp,
    typename DerivedV,
    typename DerivedEle,
    typename Derivedsqr_d,
    typename Derivedc>
  IGL_INLINE void point_simplex_squared_distance(
    const Eigen::MatrixBase<Derivedp> & p,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedEle> & Ele,
    const typename DerivedEle::Index i,
    Derivedsqr_d & sqr_d,
    Eigen::MatrixBase<Derivedc> & c);
  // Determine squared distance from a point to linear simplex.
  // Also return barycentric coordinate of closest point. 
  //
  // Inputs:
  //   p  d-long query point
  //   V  #V by d list of vertices
  //   Ele  #Ele by ss<=d+1 list of simplex indices into V
  //   i  index into Ele of simplex
  // Outputs:
  //   sqr_d  squared distance of Ele(i) to p
  //   c  closest point on Ele(i) 
  //   b  barycentric coordinates of closest point on Ele(i) 
  //
  template <
    int DIM,
    typename Derivedp,
    typename DerivedV,
    typename DerivedEle,
    typename Derivedsqr_d,
    typename Derivedc,
    typename Derivedb>
  IGL_INLINE void point_simplex_squared_distance(
    const Eigen::MatrixBase<Derivedp> & p,
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedEle> & Ele,
    const typename DerivedEle::Index i,
    Derivedsqr_d & sqr_d,
    Eigen::MatrixBase<Derivedc> & c,
    Eigen::PlainObjectBase<Derivedb> & b);
}
#ifndef IGL_STATIC_LIBRARY
#  include "point_simplex_squared_distance.cpp"
#endif
#endif
