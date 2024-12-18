// voronoi.h
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

#ifndef NOISE_MODULE_VORONOI_H
#define NOISE_MODULE_VORONOI_H

#include "modulebase.h"

namespace noise
{

  namespace module
  {

    /// @addtogroup libnoise
    /// @{

    /// @addtogroup modules
    /// @{

    /// @addtogroup generatormodules
    /// @{

    /// Default displacement to apply to each cell for the
    /// noise::module::Voronoi noise module.
    const double DEFAULT_VORONOI_DISPLACEMENT = 1.0;

    /// Default frequency of the seed points for the noise::module::Voronoi
    /// noise module.
    const double DEFAULT_VORONOI_FREQUENCY = 1.0;

    /// Default seed of the noise function for the noise::module::Voronoi
    /// noise module.
    const int DEFAULT_VORONOI_SEED = 0;

    /// Noise module that outputs Voronoi cells.
    ///
    /// @image html modulevoronoi.png
    ///
    /// In mathematics, a <i>Voronoi cell</i> is a region containing all the
    /// points that are closer to a specific <i>seed point</i> than to any
    /// other seed point.  These cells mesh with one another, producing
    /// polygon-like formations.
    ///
    /// By default, this noise module randomly places a seed point within
    /// each unit cube.  By modifying the <i>frequency</i> of the seed points,
    /// an application can change the distance between seed points.  The
    /// higher the frequency, the closer together this noise module places
    /// the seed points, which reduces the size of the cells.  To specify the
    /// frequency of the cells, call the SetFrequency() method.
    ///
    /// This noise module assigns each Voronoi cell with a random constant
    /// value from a coherent-noise function.  The <i>displacement value</i>
    /// controls the range of random values to assign to each cell.  The
    /// range of random values is +/- the displacement value.  Call the
    /// SetDisplacement() method to specify the displacement value.
    ///
    /// To modify the random positions of the seed points, call the SetSeed()
    /// method.
    ///
    /// This noise module can optionally add the distance from the nearest
    /// seed to the output value.  To enable this feature, call the
    /// EnableDistance() method.  This causes the points in the Voronoi cells
    /// to increase in value the further away that point is from the nearest
    /// seed point.
    ///
    /// Voronoi cells are often used to generate cracked-mud terrain
    /// formations or crystal-like textures
    ///
    /// This noise module requires no source modules.
    class Voronoi: public Module
    {

      public:

        /// Constructor.
        ///
        /// The default displacement value is set to
        /// noise::module::DEFAULT_VORONOI_DISPLACEMENT.
        ///
        /// The default frequency is set to
        /// noise::module::DEFAULT_VORONOI_FREQUENCY.
        ///
        /// The default seed value is set to
        /// noise::module::DEFAULT_VORONOI_SEED.
        Voronoi ();

        /// Enables or disables applying the distance from the nearest seed
        /// point to the output value.
        ///
        /// @param enable Specifies whether to apply the distance to the
        /// output value or not.
        ///
        /// Applying the distance from the nearest seed point to the output
        /// value causes the points in the Voronoi cells to increase in value
        /// the further away that point is from the nearest seed point.
        /// Setting this value to @a true (and setting the displacement to a
        /// near-zero value) causes this noise module to generate cracked mud
        /// formations.
        void EnableDistance (bool enable = true)
        {
          m_enableDistance = enable;
        }

        /// Returns the displacement value of the Voronoi cells.
        ///
        /// @returns The displacement value of the Voronoi cells.
        ///
        /// This noise module assigns each Voronoi cell with a random constant
        /// value from a coherent-noise function.  The <i>displacement
        /// value</i> controls the range of random values to assign to each
        /// cell.  The range of random values is +/- the displacement value.
        double GetDisplacement () const
        {
          return m_displacement;
        }

        /// Returns the frequency of the seed points.
        ///
        /// @returns The frequency of the seed points.
        ///
        /// The frequency determines the size of the Voronoi cells and the
        /// distance between these cells.
        double GetFrequency () const
        {
          return m_frequency;
        }

        virtual int GetSourceModuleCount () const
        {
          return 0;
        }

        /// Returns the seed value used by the Voronoi cells
        ///
        /// @returns The seed value.
        ///
        /// The positions of the seed values are calculated by a
        /// coherent-noise function.  By modifying the seed value, the output
        /// of that function changes.
        int GetSeed () const
        {
          return m_seed;
        }

        /// Determines if the distance from the nearest seed point is applied
        /// to the output value.
        ///
        /// @returns
        /// - @a true if the distance is applied to the output value.
        /// - @a false if not.
        ///
        /// Applying the distance from the nearest seed point to the output
        /// value causes the points in the Voronoi cells to increase in value
        /// the further away that point is from the nearest seed point.
        bool IsDistanceEnabled () const
        {
          return m_enableDistance;
        }

        virtual double GetValue (double x, double y, double z) const;

        /// Sets the displacement value of the Voronoi cells.
        ///
        /// @param displacement The displacement value of the Voronoi cells.
        ///
        /// This noise module assigns each Voronoi cell with a random constant
        /// value from a coherent-noise function.  The <i>displacement
        /// value</i> controls the range of random values to assign to each
        /// cell.  The range of random values is +/- the displacement value.
        void SetDisplacement (double displacement)
        {
          m_displacement = displacement;
        }

        /// Sets the frequency of the seed points.
        ///
        /// @param frequency The frequency of the seed points.
        ///
        /// The frequency determines the size of the Voronoi cells and the
        /// distance between these cells.
        void SetFrequency (double frequency)
        {
          m_frequency = frequency;
        }

        /// Sets the seed value used by the Voronoi cells
        ///
        /// @param seed The seed value.
        ///
        /// The positions of the seed values are calculated by a
        /// coherent-noise function.  By modifying the seed value, the output
        /// of that function changes.
        void SetSeed (int seed)
        {
          m_seed = seed;
        }

      protected:

        /// Scale of the random displacement to apply to each Voronoi cell.
        double m_displacement;

        /// Determines if the distance from the nearest seed point is applied to
        /// the output value.
        bool m_enableDistance;

        /// Frequency of the seed points.
        double m_frequency;

        /// Seed value used by the coherent-noise function to determine the
        /// positions of the seed points.
        int m_seed;

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
