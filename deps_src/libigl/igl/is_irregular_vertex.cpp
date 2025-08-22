// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "is_irregular_vertex.h"
#include <vector>

#include "is_border_vertex.h"

template <typename DerivedV, typename DerivedF>
IGL_INLINE std::vector<bool> igl::is_irregular_vertex(const Eigen::PlainObjectBase<DerivedV> &V, const Eigen::PlainObjectBase<DerivedF> &F)
{
  Eigen::VectorXi count = Eigen::VectorXi::Zero(F.maxCoeff());

  for(unsigned i=0; i<F.rows();++i)
  {
    for(unsigned j=0; j<F.cols();++j)
    {
      if (F(i,j) < F(i,(j+1)%F.cols())) // avoid duplicate edges
      {
        count(F(i,j  )) += 1;
        count(F(i,(j+1)%F.cols())) += 1;
      }
    }
  }

  std::vector<bool> border = is_border_vertex(V,F);

  std::vector<bool> res(count.size());

  for (unsigned i=0; i<res.size(); ++i)
    res[i] = !border[i] && count[i] != (F.cols() == 3 ? 6 : 4 );

  return res;
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template std::vector<bool, std::allocator<bool> > igl::is_irregular_vertex<Eigen::Matrix<double, -1, 3, 0, -1, 3>, Eigen::Matrix<int, -1, 3, 0, -1, 3> >(Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 0, -1, 3> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 0, -1, 3> > const&);
template std::vector<bool, std::allocator<bool> > igl::is_irregular_vertex<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&);
#endif
