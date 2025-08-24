// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2015 Alec Jacobson
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.
#include "piecewise_constant_winding_number.h"
#include "../../piecewise_constant_winding_number.h"
#include "remesh_self_intersections.h"
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <algorithm>

template < typename DerivedV, typename DerivedF>
IGL_INLINE bool igl::copyleft::cgal::piecewise_constant_winding_number(
  const Eigen::PlainObjectBase<DerivedV> & V,
  const Eigen::PlainObjectBase<DerivedF> & F)
{
  Eigen::Matrix<CGAL::Epeck::FT,Eigen::Dynamic,3> VV;
  Eigen::Matrix<typename DerivedF::Scalar,Eigen::Dynamic,3> FF;
  Eigen::Matrix<typename DerivedF::Index,Eigen::Dynamic,2> IF;
  Eigen::Matrix<typename DerivedF::Index,Eigen::Dynamic,1> J;
  Eigen::Matrix<typename DerivedV::Index,Eigen::Dynamic,1> UIM,IM;
  // resolve intersections
  remesh_self_intersections(V,F,{false,false,true},VV,FF,IF,J,IM);
  return igl::piecewise_constant_winding_number(FF);
}
