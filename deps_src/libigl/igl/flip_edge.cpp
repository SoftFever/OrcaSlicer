// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2016 Qingan Zhou <qnzhou@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.

#include "flip_edge.h"

template <
  typename DerivedF,
  typename DerivedE,
  typename DeriveduE,
  typename DerivedEMAP,
  typename uE2EType>
IGL_INLINE void igl::flip_edge(
  Eigen::PlainObjectBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedE> & E,
  Eigen::PlainObjectBase<DeriveduE> & uE,
  Eigen::PlainObjectBase<DerivedEMAP> & EMAP,
  std::vector<std::vector<uE2EType> > & uE2E,
  const size_t uei)
{
  typedef typename DerivedF::Scalar Index;
  const size_t num_faces = F.rows();
  assert(F.cols() == 3);
  //          v1                 v1
  //          /|\                / \
  //         / | \              /f1 \
  //     v3 /f2|f1\ v4  =>  v3 /_____\ v4
  //        \  |  /            \ f2  /
  //         \ | /              \   /
  //          \|/                \ /
  //          v2                 v2
  auto& half_edges = uE2E[uei];
  if (half_edges.size() != 2) {
    throw "Cannot flip non-manifold or boundary edge";
  }

  const size_t f1 = half_edges[0] % num_faces;
  const size_t f2 = half_edges[1] % num_faces;
  const size_t c1 = half_edges[0] / num_faces;
  const size_t c2 = half_edges[1] / num_faces;
  assert(c1 < 3);
  assert(c2 < 3);

  assert(f1 != f2);
  const size_t v1 = F(f1, (c1+1)%3);
  const size_t v2 = F(f1, (c1+2)%3);
  const size_t v4 = F(f1, c1);
  const size_t v3 = F(f2, c2);
  assert(F(f2, (c2+2)%3) == v1);
  assert(F(f2, (c2+1)%3) == v2);

  const size_t e_12 = half_edges[0];
  const size_t e_24 = f1 + ((c1 + 1) % 3) * num_faces;
  const size_t e_41 = f1 + ((c1 + 2) % 3) * num_faces;
  const size_t e_21 = half_edges[1];
  const size_t e_13 = f2 + ((c2 + 1) % 3) * num_faces;
  const size_t e_32 = f2 + ((c2 + 2) % 3) * num_faces;
  assert(E(e_12, 0) == v1);
  assert(E(e_12, 1) == v2);
  assert(E(e_24, 0) == v2);
  assert(E(e_24, 1) == v4);
  assert(E(e_41, 0) == v4);
  assert(E(e_41, 1) == v1);
  assert(E(e_21, 0) == v2);
  assert(E(e_21, 1) == v1);
  assert(E(e_13, 0) == v1);
  assert(E(e_13, 1) == v3);
  assert(E(e_32, 0) == v3);
  assert(E(e_32, 1) == v2);

  const size_t ue_24 = EMAP(e_24);
  const size_t ue_41 = EMAP(e_41);
  const size_t ue_13 = EMAP(e_13);
  const size_t ue_32 = EMAP(e_32);

  F(f1, 0) = v1;
  F(f1, 1) = v3;
  F(f1, 2) = v4;
  F(f2, 0) = v2;
  F(f2, 1) = v4;
  F(f2, 2) = v3;

  uE(uei, 0) = v3;
  uE(uei, 1) = v4;

  const size_t new_e_34 = f1;
  const size_t new_e_41 = f1 + num_faces;
  const size_t new_e_13 = f1 + num_faces*2;
  const size_t new_e_43 = f2;
  const size_t new_e_32 = f2 + num_faces;
  const size_t new_e_24 = f2 + num_faces*2;

  E(new_e_34, 0) = v3;
  E(new_e_34, 1) = v4;
  E(new_e_41, 0) = v4;
  E(new_e_41, 1) = v1;
  E(new_e_13, 0) = v1;
  E(new_e_13, 1) = v3;
  E(new_e_43, 0) = v4;
  E(new_e_43, 1) = v3;
  E(new_e_32, 0) = v3;
  E(new_e_32, 1) = v2;
  E(new_e_24, 0) = v2;
  E(new_e_24, 1) = v4;

  EMAP(new_e_34) = uei;
  EMAP(new_e_43) = uei;
  EMAP(new_e_41) = ue_41;
  EMAP(new_e_13) = ue_13;
  EMAP(new_e_32) = ue_32;
  EMAP(new_e_24) = ue_24;

  auto replace = [](std::vector<Index>& array, Index old_v, Index new_v) {
    std::replace(array.begin(), array.end(), old_v, new_v);
  };
  replace(uE2E[uei], e_12, new_e_34);
  replace(uE2E[uei], e_21, new_e_43);
  replace(uE2E[ue_13], e_13, new_e_13);
  replace(uE2E[ue_32], e_32, new_e_32);
  replace(uE2E[ue_24], e_24, new_e_24);
  replace(uE2E[ue_41], e_41, new_e_41);

#ifndef NDEBUG
  auto sanity_check = [&](size_t ue) {
    const auto& adj_faces = uE2E[ue];
    if (adj_faces.size() == 2) {
      const size_t first_f  = adj_faces[0] % num_faces;
      const size_t first_c  = adj_faces[0] / num_faces;
      const size_t second_f = adj_faces[1] % num_faces;
      const size_t second_c = adj_faces[1] / num_faces;
      const size_t vertex_0 = F(first_f, (first_c+1) % 3);
      const size_t vertex_1 = F(first_f, (first_c+2) % 3);
      assert(vertex_0 == F(second_f, (second_c+2) % 3));
      assert(vertex_1 == F(second_f, (second_c+1) % 3));
    }
  };

  sanity_check(uei);
  sanity_check(ue_13);
  sanity_check(ue_32);
  sanity_check(ue_24);
  sanity_check(ue_41);
#endif
}


#ifdef IGL_STATIC_LIBRARY
template void igl::flip_edge<Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, int>(Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, std::vector<std::vector<int, std::allocator<int> >, std::allocator<std::vector<int, std::allocator<int> > > >&, unsigned long);
#endif