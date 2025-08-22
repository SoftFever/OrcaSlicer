// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_IS_BORDER_VERTEX_H
#define IGL_IS_BORDER_VERTEX_H
#include "igl_inline.h"

#include <Eigen/Core>
#include <vector>

namespace igl 
{
  // Determine vertices on open boundary of a (manifold) mesh with triangle
  // faces F
  //
  // Inputs:
  //   V  #V by dim list of vertex positions 
  //   F  #F by 3 list of triangle indices
  // Returns #V vector of bools revealing whether vertices are on boundary
  //
  // Known Bugs: - does not depend on V
  //             - assumes mesh is edge manifold
  // 
  template <typename DerivedV, typename DerivedF>
  IGL_INLINE std::vector<bool> is_border_vertex(
   const Eigen::PlainObjectBase<DerivedV> &V,
   const Eigen::PlainObjectBase<DerivedF> &F);
}

#ifndef IGL_STATIC_LIBRARY
#  include "is_border_vertex.cpp"
#endif

#endif
