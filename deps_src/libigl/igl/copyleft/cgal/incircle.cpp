// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Qingan Zhou <qnzhou@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.

#include "incircle.h"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

template<typename Scalar>
IGL_INLINE short igl::copyleft::cgal::incircle(
    const Scalar pa[2],
    const Scalar pb[2],
    const Scalar pc[2],
    const Scalar pd[2])
{
  typedef CGAL::Exact_predicates_exact_constructions_kernel Epeck;
  typedef CGAL::Exact_predicates_inexact_constructions_kernel Epick;
  typedef typename std::conditional<std::is_same<Scalar, Epeck::FT>::value,
          Epeck, Epick>::type Kernel;

  switch(CGAL::side_of_oriented_circle(
        typename Kernel::Point_2(pa[0], pa[1]),
        typename Kernel::Point_2(pb[0], pb[1]),
        typename Kernel::Point_2(pc[0], pc[1]),
        typename Kernel::Point_2(pd[0], pd[1]))) {
    case CGAL::ON_POSITIVE_SIDE:
      return 1;
    case CGAL::ON_NEGATIVE_SIDE:
      return -1;
    case CGAL::ON_ORIENTED_BOUNDARY:
      return 0;
    default:
      throw "Invalid incircle result";
  }
}
