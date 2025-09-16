// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MATRIX_TO_LIST_H
#define IGL_MATRIX_TO_LIST_H
#include "igl_inline.h"
#include <vector>
#include <Eigen/Dense>

namespace igl
{
  // Convert a matrix to a list (std::vector) of row vectors of the same size
  //
  // Template: 
  //   Mat  Matrix type, must implement:
  //     .resize(m,n)
  //     .row(i) = Row
  //   T  type that can be safely cast to type in Mat via '='
  // Inputs:
  //   M  an m by n matrix
  // Outputs:
  //   V  a m-long list of vectors of size n
  //
  // See also: list_to_matrix
  template <typename DerivedM>
  IGL_INLINE void matrix_to_list(
    const Eigen::DenseBase<DerivedM> & M, 
    std::vector<std::vector<typename DerivedM::Scalar > > & V);
  // Convert a matrix to a list (std::vector) of elements in column-major
  // ordering.
  //
  // Inputs:
  //    M  an m by n matrix
  // Outputs:
  //    V  an m*n list of elements
  template <typename DerivedM>
  IGL_INLINE void matrix_to_list(
    const Eigen::DenseBase<DerivedM> & M, 
    std::vector<typename DerivedM::Scalar > & V);
  // Return wrapper
  template <typename DerivedM>
  IGL_INLINE std::vector<typename DerivedM::Scalar > matrix_to_list(
      const Eigen::DenseBase<DerivedM> & M);
}

#ifndef IGL_STATIC_LIBRARY
#  include "matrix_to_list.cpp"
#endif

#endif

