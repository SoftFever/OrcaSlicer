// mathconsts.h
//
// Copyright (C) 2003, 2004 Jason Bevins
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or (at
// your option) any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
// License (COPYING.txt) for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this library; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// The developer's email is jlbezigvins@gmzigail.com (for great email, take
// off every 'zig'.)
//

#ifndef NOISE_MATHCONSTS_H
#define NOISE_MATHCONSTS_H

// For whatever reason, I can't find the basic math consts in the MSVC version
// of math.h.

namespace noise
{

  /// @addtogroup libnoise
  /// @{

  /// Pi.
  const double PI = 3.1415926535897932385;

  /// Square root of 2.
  const double SQRT_2 = 1.4142135623730950488;

  /// Square root of 3.
  const double SQRT_3 = 1.7320508075688772935;

  /// Converts an angle from degrees to radians.
  const double DEG_TO_RAD = PI / 180.0;

  /// Converts an angle from radians to degrees.
  const double RAD_TO_DEG = 1.0 / DEG_TO_RAD;

  /// @}

}

#endif
