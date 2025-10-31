// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef FACET_COMPONENTS_H
#define FACET_COMPONENTS_H
#include "igl_inline.h"
#include <Eigen/Core>
#include <vector>
namespace igl
{
  // Compute connected components of facets based on edge-edge adjacency.
  //
  // Inputs:
  //   F  #F by 3 list of triangle indices
  // Outputs:
  //   C  #F list of connected component ids
  template <typename DerivedF, typename DerivedC>
  IGL_INLINE void facet_components(
    const Eigen::PlainObjectBase<DerivedF> & F,
    Eigen::PlainObjectBase<DerivedC> & C);
  // Inputs:
  //   TT  #TT by 3 list of list of adjacency triangles (see
  //   triangle_triangle_adjacency.h)
  // Outputs:
  //   C  #F list of connected component ids
  //   counts #C list of number of facets in each components
  template <
    typename TTIndex, 
    typename DerivedC,
    typename Derivedcounts>
  IGL_INLINE void facet_components(
    const std::vector<std::vector<std::vector<TTIndex > > > & TT,
    Eigen::PlainObjectBase<DerivedC> & C,
    Eigen::PlainObjectBase<Derivedcounts> & counts);
}
#ifndef IGL_STATIC_LIBRARY
#  include "facet_components.cpp"
#endif
#endif
