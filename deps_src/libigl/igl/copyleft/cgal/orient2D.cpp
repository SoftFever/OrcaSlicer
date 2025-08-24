// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Qingan Zhou <qnzhou@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.

#include "orient2D.h"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>

template<typename Scalar>
IGL_INLINE short igl::copyleft::cgal::orient2D(
    const Scalar pa[2],
    const Scalar pb[2],
    const Scalar pc[2])
{
  typedef CGAL::Exact_predicates_exact_constructions_kernel Epeck;
  typedef CGAL::Exact_predicates_inexact_constructions_kernel Epick;
  typedef typename std::conditional<std::is_same<Scalar, Epeck::FT>::value,
          Epeck, Epick>::type Kernel;

  switch(CGAL::orientation(
        typename Kernel::Point_2(pa[0], pa[1]),
        typename Kernel::Point_2(pb[0], pb[1]),
        typename Kernel::Point_2(pc[0], pc[1]))) {
    case CGAL::LEFT_TURN:
      return 1;
    case CGAL::RIGHT_TURN:
      return -1;
    case CGAL::COLLINEAR:
      return 0;
    default:
      throw "Invalid orientation";
  }
}
