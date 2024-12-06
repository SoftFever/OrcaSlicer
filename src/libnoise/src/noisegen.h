// noisegen.h
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

#ifndef NOISE_NOISEGEN_H
#define NOISE_NOISEGEN_H

#include <math.h>
#include "basictypes.h"

namespace noise
{

  /// @addtogroup libnoise
  /// @{

  /// Enumerates the noise quality.
  enum NoiseQuality
  {

    /// Generates coherent noise quickly.  When a coherent-noise function with
    /// this quality setting is used to generate a bump-map image, there are
    /// noticeable "creasing" artifacts in the resulting image.  This is
    /// because the derivative of that function is discontinuous at integer
    /// boundaries.
    QUALITY_FAST = 0,

    /// Generates standard-quality coherent noise.  When a coherent-noise
    /// function with this quality setting is used to generate a bump-map
    /// image, there are some minor "creasing" artifacts in the resulting
    /// image.  This is because the second derivative of that function is
    /// discontinuous at integer boundaries.
    QUALITY_STD = 1,

    /// Generates the best-quality coherent noise.  When a coherent-noise
    /// function with this quality setting is used to generate a bump-map
    /// image, there are no "creasing" artifacts in the resulting image.  This
    /// is because the first and second derivatives of that function are
    /// continuous at integer boundaries.
    QUALITY_BEST = 2

  };

  /// Generates a gradient-coherent-noise value from the coordinates of a
  /// three-dimensional input value.
  ///
  /// @param x The @a x coordinate of the input value.
  /// @param y The @a y coordinate of the input value.
  /// @param z The @a z coordinate of the input value.
  /// @param seed The random number seed.
  /// @param noiseQuality The quality of the coherent-noise.
  ///
  /// @returns The generated gradient-coherent-noise value.
  ///
  /// The return value ranges from -1.0 to +1.0.
  ///
  /// For an explanation of the difference between <i>gradient</i> noise and
  /// <i>value</i> noise, see the comments for the GradientNoise3D() function.
  double GradientCoherentNoise3D (double x, double y, double z, int seed = 0,
    NoiseQuality noiseQuality = QUALITY_STD);

  /// Generates a gradient-noise value from the coordinates of a
  /// three-dimensional input value and the integer coordinates of a
  /// nearby three-dimensional value.
  ///
  /// @param fx The floating-point @a x coordinate of the input value.
  /// @param fy The floating-point @a y coordinate of the input value.
  /// @param fz The floating-point @a z coordinate of the input value.
  /// @param ix The integer @a x coordinate of a nearby value.
  /// @param iy The integer @a y coordinate of a nearby value.
  /// @param iz The integer @a z coordinate of a nearby value.
  /// @param seed The random number seed.
  ///
  /// @returns The generated gradient-noise value.
  ///
  /// @pre The difference between @a fx and @a ix must be less than or equal
  /// to one.
  ///
  /// @pre The difference between @a fy and @a iy must be less than or equal
  /// to one.
  ///
  /// @pre The difference between @a fz and @a iz must be less than or equal
  /// to one.
  ///
  /// A <i>gradient</i>-noise function generates better-quality noise than a
  /// <i>value</i>-noise function.  Most noise modules use gradient noise for
  /// this reason, although it takes much longer to calculate.
  ///
  /// The return value ranges from -1.0 to +1.0.
  ///
  /// This function generates a gradient-noise value by performing the
  /// following steps:
  /// - It first calculates a random normalized vector based on the
  ///   nearby integer value passed to this function.
  /// - It then calculates a new value by adding this vector to the
  ///   nearby integer value passed to this function.
  /// - It then calculates the dot product of the above-generated value
  ///   and the floating-point input value passed to this function.
  ///
  /// A noise function differs from a random-number generator because it
  /// always returns the same output value if the same input value is passed
  /// to it.
  double GradientNoise3D (double fx, double fy, double fz, int ix, int iy,
    int iz, int seed = 0);

  /// Generates an integer-noise value from the coordinates of a
  /// three-dimensional input value.
  ///
  /// @param x The integer @a x coordinate of the input value.
  /// @param y The integer @a y coordinate of the input value.
  /// @param z The integer @a z coordinate of the input value.
  /// @param seed A random number seed.
  ///
  /// @returns The generated integer-noise value.
  ///
  /// The return value ranges from 0 to 2147483647.
  ///
  /// A noise function differs from a random-number generator because it
  /// always returns the same output value if the same input value is passed
  /// to it.
  int IntValueNoise3D (int x, int y, int z, int seed = 0);

  /// Modifies a floating-point value so that it can be stored in a
  /// noise::int32 variable.
  ///
  /// @param n A floating-point number.
  ///
  /// @returns The modified floating-point number.
  ///
  /// This function does not modify @a n.
  ///
  /// In libnoise, the noise-generating algorithms are all integer-based;
  /// they use variables of type noise::int32.  Before calling a noise
  /// function, pass the @a x, @a y, and @a z coordinates to this function to
  /// ensure that these coordinates can be cast to a noise::int32 value.
  ///
  /// Although you could do a straight cast from double to noise::int32, the
  /// resulting value may differ between platforms.  By using this function,
  /// you ensure that the resulting value is identical between platforms.
  inline double MakeInt32Range (double n)
  {
    if (n >= 1073741824.0) {
      return (2.0 * fmod (n, 1073741824.0)) - 1073741824.0;
    } else if (n <= -1073741824.0) {
      return (2.0 * fmod (n, 1073741824.0)) + 1073741824.0;
    } else {
      return n;
    }
  }

  /// Generates a value-coherent-noise value from the coordinates of a
  /// three-dimensional input value.
  ///
  /// @param x The @a x coordinate of the input value.
  /// @param y The @a y coordinate of the input value.
  /// @param z The @a z coordinate of the input value.
  /// @param seed The random number seed.
  /// @param noiseQuality The quality of the coherent-noise.
  ///
  /// @returns The generated value-coherent-noise value.
  ///
  /// The return value ranges from -1.0 to +1.0.
  ///
  /// For an explanation of the difference between <i>gradient</i> noise and
  /// <i>value</i> noise, see the comments for the GradientNoise3D() function.
  double ValueCoherentNoise3D (double x, double y, double z, int seed = 0,
    NoiseQuality noiseQuality = QUALITY_STD);

  /// Generates a value-noise value from the coordinates of a
  /// three-dimensional input value.
  ///
  /// @param x The @a x coordinate of the input value.
  /// @param y The @a y coordinate of the input value.
  /// @param z The @a z coordinate of the input value.
  /// @param seed A random number seed.
  ///
  /// @returns The generated value-noise value.
  ///
  /// The return value ranges from -1.0 to +1.0.
  ///
  /// A noise function differs from a random-number generator because it
  /// always returns the same output value if the same input value is passed
  /// to it.
  double ValueNoise3D (int x, int y, int z, int seed = 0);

  /// @}

}

#endif
