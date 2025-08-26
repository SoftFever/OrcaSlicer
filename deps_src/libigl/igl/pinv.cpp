#include "pinv.h"
#include <limits>
#include <cmath>

template <typename DerivedA, typename DerivedX>
void igl::pinv(
  const Eigen::MatrixBase<DerivedA> & A,
  typename DerivedA::Scalar tol,
  Eigen::PlainObjectBase<DerivedX> & X)
{
  Eigen::JacobiSVD<DerivedA> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV );
  typedef typename DerivedA::Scalar Scalar;
  const Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic> & U = svd.matrixU();
  const Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic> & V = svd.matrixV();
  const Eigen::Matrix<Scalar,Eigen::Dynamic,1> & S = svd.singularValues();
  if(tol < 0)
  {
    const Scalar smax = S.array().abs().maxCoeff();
    tol = 
      (Scalar)(std::max(A.rows(),A.cols())) *
      (smax-std::nextafter(smax,std::numeric_limits<Scalar>::epsilon()));
  }
  const int rank = (S.array()>0).count();
  X = (V.leftCols(rank).array().rowwise() * 
      (1.0/S.head(rank).array()).transpose()).matrix()*
    U.leftCols(rank).transpose();
}

template <typename DerivedA, typename DerivedX>
void igl::pinv(
  const Eigen::MatrixBase<DerivedA> & A,
  Eigen::PlainObjectBase<DerivedX> & X)
{
  return pinv(A,-1,X);
}
