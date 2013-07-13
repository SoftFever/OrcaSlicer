#ifndef _myinit_h_
#define _myinit_h_

#include <vector>

namespace Slic3r {}
using namespace Slic3r;

#define av_store_point_xy(AV, X, Y)              \
  av_store(AV, 0, newSViv(X));                   \
  av_store(AV, 1, newSViv(Y))

#endif
