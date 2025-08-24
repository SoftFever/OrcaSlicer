// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2013 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "writeDMAT.h"
#include "list_to_matrix.h"
#include <Eigen/Core>

#include <cstdio>

template <typename DerivedW>
IGL_INLINE bool igl::writeDMAT(
  const std::string file_name, 
  const Eigen::MatrixBase<DerivedW> & W,
  const bool ascii)
{
  FILE * fp = fopen(file_name.c_str(),"wb");
  if(fp == NULL)
  {
    fprintf(stderr,"IOError: writeDMAT() could not open %s...",file_name.c_str());
    return false; 
  }
  if(ascii)
  {
    // first line contains number of rows and number of columns
    fprintf(fp,"%d %d\n",(int)W.cols(),(int)W.rows());
    // Loop over columns slowly
    for(int j = 0;j < W.cols();j++)
    {
      // loop over rows (down columns) quickly
      for(int i = 0;i < W.rows();i++)
      {
        fprintf(fp,"%0.17lg\n",(double)W(i,j));
      }
    }
  }else
  {
    // write header for ascii
    fprintf(fp,"0 0\n");
    // first line contains number of rows and number of columns
    fprintf(fp,"%d %d\n",(int)W.cols(),(int)W.rows());
    // reader assumes the binary part is double precision
    Eigen::MatrixXd Wd = W.template cast<double>();
    fwrite(Wd.data(),sizeof(double),Wd.size(),fp);
    //// Loop over columns slowly
    //for(int j = 0;j < W.cols();j++)
    //{
    //  // loop over rows (down columns) quickly
    //  for(int i = 0;i < W.rows();i++)
    //  {
    //    double d = (double)W(i,j);
    //    fwrite(&d,sizeof(double),1,fp);
    //  }
    //}
  }
  fclose(fp);
  return true;
}

template <typename Scalar>
IGL_INLINE bool igl::writeDMAT(
  const std::string file_name, 
  const std::vector<std::vector<Scalar> > & W,
  const bool ascii)
{
  Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic> mW;
  list_to_matrix(W,mW);
  return igl::writeDMAT(file_name,mW,ascii);
}

template <typename Scalar>
IGL_INLINE bool igl::writeDMAT(
  const std::string file_name, 
  const std::vector<Scalar > & W,
  const bool ascii)
{
  Eigen::Matrix<Scalar,Eigen::Dynamic,1> mW;
  list_to_matrix(W,mW);
  return igl::writeDMAT(file_name,mW,ascii);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template bool igl::writeDMAT<Eigen::Matrix<double, -1, -1, 0, -1, -1> >(std::basic_string<char, std::char_traits<char>, std::allocator<char> >, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, bool);
template bool igl::writeDMAT<Eigen::Matrix<double, 1, 3, 1, 1, 3> >(std::string, Eigen::MatrixBase<Eigen::Matrix<double, 1, 3, 1, 1, 3> > const&, bool);
template bool igl::writeDMAT<Eigen::Matrix<float, 1, 3, 1, 1, 3> >(std::string, Eigen::MatrixBase<Eigen::Matrix<float, 1, 3, 1, 1, 3> > const&, bool);
template bool igl::writeDMAT<Eigen::Matrix<int, -1, -1, 0, -1, -1> >(std::basic_string<char, std::char_traits<char>, std::allocator<char> >, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, bool);
template bool igl::writeDMAT<Eigen::Matrix<int, -1, 2, 0, -1, 2> >(std::basic_string<char, std::char_traits<char>, std::allocator<char> >, Eigen::MatrixBase<Eigen::Matrix<int, -1, 2, 0, -1, 2> > const&, bool);
#endif
