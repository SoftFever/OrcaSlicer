// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2016 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "combine.h"
#include <cassert>

template <
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedV,
  typename DerivedF,
  typename DerivedVsizes,
  typename DerivedFsizes>
IGL_INLINE void igl::combine(
  const std::vector<DerivedVV> & VV,
  const std::vector<DerivedFF> & FF,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedVsizes> & Vsizes,
  Eigen::PlainObjectBase<DerivedFsizes> & Fsizes)
{
  assert(VV.size() == FF.size() &&
    "Lists of verex lists and face lists should be same size");
  Vsizes.resize(VV.size());
  Fsizes.resize(FF.size());
  // Dimension of vertex positions
  const int dim = VV.size() > 0 ? VV[0].cols() : 0;
  // Simplex/element size
  const int ss = FF.size() > 0 ? FF[0].cols() : 0;
  int n = 0;
  int m = 0;
  for(int i = 0;i<VV.size();i++)
  {
    const auto & Vi = VV[i];
    const auto & Fi = FF[i];
    Vsizes(i) = Vi.rows();
    n+=Vi.rows();
    assert((Vi.size()==0 || dim == Vi.cols()) && "All vertex lists should have same #columns");
    Fsizes(i) = Fi.rows();
    m+=Fi.rows();
    assert((Fi.size()==0 || ss == Fi.cols()) && "All face lists should have same #columns");
  }
  V.resize(n,dim);
  F.resize(m,ss);
  {
    int kv = 0;
    int kf = 0;
    for(int i = 0;i<VV.size();i++)
    {
      const auto & Vi = VV[i];
      const int ni = Vi.rows();
      const auto & Fi = FF[i];
      const int mi = Fi.rows();
      if(Fi.size() >0)
      {
        F.block(kf,0,mi,ss) = Fi.array()+kv;
      }
      kf+=mi;
      if(Vi.size() >0)
      {
        V.block(kv,0,ni,dim) = Vi;
      }
      kv+=ni;
    }
    assert(kv == V.rows());
    assert(kf == F.rows());
  }
}

template <
  typename DerivedVV,
  typename DerivedFF,
  typename DerivedV,
  typename DerivedF>
IGL_INLINE void igl::combine(
  const std::vector<DerivedVV> & VV,
  const std::vector<DerivedFF> & FF,
  Eigen::PlainObjectBase<DerivedV> & V,
  Eigen::PlainObjectBase<DerivedF> & F)
{
  Eigen::VectorXi Vsizes,Fsizes;
  return igl::combine(VV,FF,V,F,Vsizes,Fsizes);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::combine<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<unsigned long, -1, 1, 0, -1, 1>, Eigen::Matrix<unsigned long, -1, 1, 0, -1, 1> >(std::vector<Eigen::Matrix<double, -1, -1, 0, -1, -1>, std::allocator<Eigen::Matrix<double, -1, -1, 0, -1, -1> > > const&, std::vector<Eigen::Matrix<int, -1, -1, 0, -1, -1>, std::allocator<Eigen::Matrix<int, -1, -1, 0, -1, -1> > > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<unsigned long, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<unsigned long, -1, 1, 0, -1, 1> >&);
template void igl::combine<Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3>, Eigen::Matrix<double, -1, 3, 1, -1, 3>, Eigen::Matrix<int, -1, 3, 1, -1, 3> >(std::vector<Eigen::Matrix<double, -1, 3, 1, -1, 3>, std::allocator<Eigen::Matrix<double, -1, 3, 1, -1, 3> > > const&, std::vector<Eigen::Matrix<int, -1, 3, 1, -1, 3>, std::allocator<Eigen::Matrix<int, -1, 3, 1, -1, 3> > > const&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, 3, 1, -1, 3> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 3, 1, -1, 3> >&);
#ifdef WIN32
template void igl::combine<Eigen::Matrix<double,-1,-1,0,-1,-1>, Eigen::Matrix<int,-1,-1,0,-1,-1>,Eigen::Matrix<double,-1,-1,0,-1,-1>,Eigen::Matrix<int,-1,-1,0,-1,-1>,Eigen::Matrix<unsigned __int64,-1,1,0,-1,1>,Eigen::Matrix<unsigned __int64,-1,1,0,-1,1> >(class std::vector<Eigen::Matrix<double,-1,-1,0,-1,-1>,class std::allocator<Eigen::Matrix<double,-1,-1,0,-1,-1> > > const &,class std::vector<Eigen::Matrix<int,-1,-1,0,-1,-1>,class std::allocator<Eigen::Matrix<int,-1,-1,0,-1,-1> > > const &,Eigen::PlainObjectBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > &,Eigen::PlainObjectBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > &,Eigen::PlainObjectBase<Eigen::Matrix<unsigned __int64,-1,1,0,-1,1> > &,Eigen::PlainObjectBase<Eigen::Matrix<unsigned __int64,-1,1,0,-1,1> > &);
template void igl::combine<Eigen::Matrix<double,-1,-1,0,-1,-1>, Eigen::Matrix<int,-1,-1,0,-1,-1>,Eigen::Matrix<double,-1,-1,0,-1,-1>,Eigen::Matrix<int,-1,-1,0,-1,-1>,Eigen::Matrix<unsigned __int64,-1,1,0,-1,1>,Eigen::Matrix<unsigned __int64,-1,1,0,-1,1> >(class std::vector<Eigen::Matrix<double,-1,-1,0,-1,-1>,class std::allocator<Eigen::Matrix<double,-1,-1,0,-1,-1> > > const &,class std::vector<Eigen::Matrix<int,-1,-1,0,-1,-1>,class std::allocator<Eigen::Matrix<int,-1,-1,0,-1,-1> > > const &,Eigen::PlainObjectBase<Eigen::Matrix<double,-1,-1,0,-1,-1> > &,Eigen::PlainObjectBase<Eigen::Matrix<int,-1,-1,0,-1,-1> > &,Eigen::PlainObjectBase<Eigen::Matrix<unsigned __int64,-1,1,0,-1,1> > &,Eigen::PlainObjectBase<Eigen::Matrix<unsigned __int64,-1,1,0,-1,1> > &);
#endif
#endif
