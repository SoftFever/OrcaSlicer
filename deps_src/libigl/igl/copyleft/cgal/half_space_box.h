#ifndef IGL_COPYLEFT_CGAL_HALF_SPACE_BOX_H
#define IGL_COPYLEFT_CGAL_HALF_SPACE_BOX_H
#include "../../igl_inline.h"
#include <Eigen/Core>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Plane_3.h>

namespace igl
{
  namespace copyleft
  {
    namespace cgal
    {
      // Construct a mesh of box (BV,BF) so that it contains the intersection of
      // the half-space under the plane (P) and the bounding box of V, and does not
      // contain any of the half-space above (P).
      //
      // Inputs:
      //   P  plane so that normal points away from half-space
      //   V  #V by 3 list of vertex positions
      // Outputs:
      //   BV  #BV by 3 list of box vertex positions
      //   BF  #BF b3 list of box triangle indices into BV
      template <typename DerivedV>
      IGL_INLINE void half_space_box(
        const CGAL::Plane_3<CGAL::Epeck> & P,
        const Eigen::MatrixBase<DerivedV> & V,
        Eigen::Matrix<CGAL::Epeck::FT,8,3> & BV,
        Eigen::Matrix<int,12,3> & BF);
      // Inputs:
      //   p  3d point on plane
      //   n  3d vector of normal of plane pointing away from inside
      //   V  #V by 3 list of vertex positions
      // Outputs:
      //   BV  #BV by 3 list of box vertex positions
      //   BF  #BF b3 list of box triangle indices into BV
      template <typename Derivedp, typename Derivedn, typename DerivedV>
      IGL_INLINE void half_space_box(
        const Eigen::MatrixBase<Derivedp> & p,
        const Eigen::MatrixBase<Derivedn> & n,
        const Eigen::MatrixBase<DerivedV> & V,
        Eigen::Matrix<CGAL::Epeck::FT,8,3> & BV,
        Eigen::Matrix<int,12,3> & BF);
      // Inputs:
      //   equ  plane equation: a*x+b*y+c*z + d = 0
      //   V  #V by 3 list of vertex positions
      // Outputs:
      //   BV  #BV by 3 list of box vertex positions
      //   BF  #BF b3 list of box triangle indices into BV
      template <typename Derivedequ, typename DerivedV>
      IGL_INLINE void half_space_box(
        const Eigen::MatrixBase<Derivedequ> & equ,
        const Eigen::MatrixBase<DerivedV> & V,
        Eigen::Matrix<CGAL::Epeck::FT,8,3> & BV,
        Eigen::Matrix<int,12,3> & BF);
    }
  }
}

#ifndef IGL_STATIC_LIBRARY
#  include "half_space_box.cpp"
#endif
#endif
