// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#ifndef IGL_MATLAB_FORMAT_H
#define IGL_MATLAB_FORMAT_H

#include "igl_inline.h"

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <string>

namespace igl
{
  // This is a routine to print a matrix using format suitable for pasting into
  // the matlab IDE
  //
  // Templates:
  //   DerivedM  e.g. derived from MatrixXd
  // Input:
  //   input  some matrix to be formatted
  //   name  name of matrix
  // Returns  Formatted matrix
  //
  // Example:
  // // M := [1 2 3;4 5 6];
  // cout<<matlab_format(M)<<endl;
  // // Prints:
  // // [
  // //   1 2 3
  // //   4 5 6
  // // ];
  // cout<<matlab_format(M,"M")<<endl;
  // // Prints:
  // // M = [
  // //   1 2 3
  // //   4 5 6
  // // ];
  template <typename DerivedM>
  IGL_INLINE const Eigen::WithFormat< DerivedM > matlab_format(
    const Eigen::DenseBase<DerivedM> & M,
    const std::string name = "");
  // Same but for sparse matrices. Print IJV format into an auxiliary variable
  // and then print a call to sparse which will construct the sparse matrix
  // Example:
  // // S := [0 2 3;4 5 0];
  // cout<<matlab_format(S,"S")<<endl;
  // // Prints:
  // // SIJV = [
  // //   2 1 4
  // //   1 2 2
  // //   2 2 5
  // //   1 3 3
  // // ];
  // // S = sparse(SIJV(:,1),SIJV(:,2),SIJV(:,3));
  //
  template <typename DerivedS>
  IGL_INLINE const std::string matlab_format(
    const Eigen::SparseMatrix<DerivedS> & S,
    const std::string name = "");
  IGL_INLINE const std::string matlab_format(
    const double v,
    const std::string name = "");
  IGL_INLINE const std::string matlab_format(
    const float v,
    const std::string name = "");
  // Return just IOFormat 
  //
  // Example:
  // // M := [1 2 3;4 5 6];
  // cout<<M.format(matlab_format())<<endl;
  // // Prints:
  // // [
  // //   1 2 3
  // //   4 5 6
  // // ];
  IGL_INLINE Eigen::IOFormat matlab_format();
}

#ifndef IGL_STATIC_LIBRARY
#  include "matlab_format.cpp"
#endif

#endif

