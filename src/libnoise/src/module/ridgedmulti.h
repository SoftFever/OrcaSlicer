// ridgedmulti.h
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

#ifndef NOISE_MODULE_RIDGEDMULTI_H
#define NOISE_MODULE_RIDGEDMULTI_H

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

    /// Default frequency for the noise::module::RidgedMulti noise module.
    const double DEFAULT_RIDGED_FREQUENCY = 1.0;

    /// Default lacunarity for the noise::module::RidgedMulti noise module.
    const double DEFAULT_RIDGED_LACUNARITY = 2.0;

    /// Default number of octaves for the noise::module::RidgedMulti noise
    /// module.
    const int DEFAULT_RIDGED_OCTAVE_COUNT = 6;

    /// Default noise quality for the noise::module::RidgedMulti noise
    /// module.
    const noise::NoiseQuality DEFAULT_RIDGED_QUALITY = QUALITY_STD;

    /// Default noise seed for the noise::module::RidgedMulti noise module.
    const int DEFAULT_RIDGED_SEED = 0;

    /// Maximum number of octaves for the noise::module::RidgedMulti noise
    /// module.
    const int RIDGED_MAX_OCTAVE = 30;

    /// Noise module that outputs 3-dimensional ridged-multifractal noise.
    ///
    /// @image html moduleridgedmulti.png
    ///
    /// This noise module, heavily based on the Perlin-noise module, generates
    /// ridged-multifractal noise.  Ridged-multifractal noise is generated in
    /// much of the same way as Perlin noise, except the output of each octave
    /// is modified by an absolute-value function.  Modifying the octave
    /// values in this way produces ridge-like formations.
    ///
    /// Ridged-multifractal noise does not use a persistence value.  This is
    /// because the persistence values of the octaves are based on the values
    /// generated from from previous octaves, creating a feedback loop (or
    /// that's what it looks like after reading the code.)
    ///
    /// This noise module outputs ridged-multifractal-noise values that
    /// usually range from -1.0 to +1.0, but there are no guarantees that all
    /// output values will exist within that range.
    ///
    /// @note For ridged-multifractal noise generated with only one octave,
    /// the output value ranges from -1.0 to 0.0.
    ///
    /// Ridged-multifractal noise is often used to generate craggy mountainous
    /// terrain or marble-like textures.
    ///
    /// This noise module does not require any source modules.
    ///
    /// <b>Octaves</b>
    ///
    /// The number of octaves control the <i>amount of detail</i> of the
    /// ridged-multifractal noise.  Adding more octaves increases the detail
    /// of the ridged-multifractal noise, but with the drawback of increasing
    /// the calculation time.
    ///
    /// An application may specify the number of octaves that generate
    /// ridged-multifractal noise by calling the SetOctaveCount() method.
    ///
    /// <b>Frequency</b>
    ///
    /// An application may specify the frequency of the first octave by
    /// calling the SetFrequency() method.
    ///
    /// <b>Lacunarity</b>
    ///
    /// The lacunarity specifies the frequency multipler between successive
    /// octaves.
    ///
    /// The effect of modifying the lacunarity is subtle; you may need to play
    /// with the lacunarity value to determine the effects.  For best results,
    /// set the lacunarity to a number between 1.5 and 3.5.
    ///
    /// <b>References &amp; Acknowledgments</b>
    ///
    /// <a href=http://www.texturingandmodeling.com/Musgrave.html>F.
    /// Kenton "Doc Mojo" Musgrave's texturing page</a> - This page contains
    /// links to source code that generates ridged-multfractal noise, among
    /// other types of noise.  The source file <a
    /// href=http://www.texturingandmodeling.com/CODE/MUSGRAVE/CLOUD/fractal.c>
    /// fractal.c</a> contains the code I used in my ridged-multifractal class
    /// (see the @a RidgedMultifractal() function.)  This code was written by F.
    /// Kenton Musgrave, the person who created
    /// <a href=http://www.pandromeda.com/>MojoWorld</a>.  He is also one of
    /// the authors in <i>Texturing and Modeling: A Procedural Approach</i>
    /// (Morgan Kaufmann, 2002. ISBN 1-55860-848-6.)
    class RidgedMulti: public Module
    {

      public:

        /// Constructor.
        ///
        /// The default number of octaves is set to
        /// noise::module::DEFAULT_RIDGED_OCTAVE_COUNT.
        ///
        /// The default frequency is set to
        /// noise::module::DEFAULT_RIDGED_FREQUENCY.
        ///
        /// The default lacunarity is set to
        /// noise::module::DEFAULT_RIDGED_LACUNARITY.
        ///
        /// The default seed value is set to
        /// noise::module::DEFAULT_RIDGED_SEED.
        RidgedMulti ();

        /// Returns the frequency of the first octave.
        ///
        /// @returns The frequency of the first octave.
        double GetFrequency () const
        {
          return m_frequency;
        }

        /// Returns the lacunarity of the ridged-multifractal noise.
        ///
        /// @returns The lacunarity of the ridged-multifractal noise.
        /// 
        /// The lacunarity is the frequency multiplier between successive
        /// octaves.
        double GetLacunarity () const
        {
          return m_lacunarity;
        }

        /// Returns the quality of the ridged-multifractal noise.
        ///
        /// @returns The quality of the ridged-multifractal noise.
        ///
        /// See noise::NoiseQuality for definitions of the various
        /// coherent-noise qualities.
        noise::NoiseQuality GetNoiseQuality () const
        {
          return m_noiseQuality;
        }

        /// Returns the number of octaves that generate the
        /// ridged-multifractal noise.
        ///
        /// @returns The number of octaves that generate the
        /// ridged-multifractal noise.
        ///
        /// The number of octaves controls the amount of detail in the
        /// ridged-multifractal noise.
        int GetOctaveCount () const
        {
          return m_octaveCount;
        }

        /// Returns the seed value used by the ridged-multifractal-noise
        /// function.
        ///
        /// @returns The seed value.
        int GetSeed () const
        {
          return m_seed;
        }

        virtual int GetSourceModuleCount () const
        {
          return 0;
        }

        virtual double GetValue (double x, double y, double z) const;

        /// Sets the frequency of the first octave.
        ///
        /// @param frequency The frequency of the first octave.
        void SetFrequency (double frequency)
        {
          m_frequency = frequency;
        }

        /// Sets the lacunarity of the ridged-multifractal noise.
        ///
        /// @param lacunarity The lacunarity of the ridged-multifractal noise.
        /// 
        /// The lacunarity is the frequency multiplier between successive
        /// octaves.
        ///
        /// For best results, set the lacunarity to a number between 1.5 and
        /// 3.5.
        void SetLacunarity (double lacunarity)
        {
          m_lacunarity = lacunarity;
          CalcSpectralWeights ();
        }

        /// Sets the quality of the ridged-multifractal noise.
        ///
        /// @param noiseQuality The quality of the ridged-multifractal noise.
        ///
        /// See noise::NoiseQuality for definitions of the various
        /// coherent-noise qualities.
        void SetNoiseQuality (noise::NoiseQuality noiseQuality)
        {
          m_noiseQuality = noiseQuality;
        }

        /// Sets the number of octaves that generate the ridged-multifractal
        /// noise.
        ///
        /// @param octaveCount The number of octaves that generate the
        /// ridged-multifractal noise.
        ///
        /// @pre The number of octaves ranges from 1 to
        /// noise::module::RIDGED_MAX_OCTAVE.
        ///
        /// @throw noise::ExceptionInvalidParam An invalid parameter was
        /// specified; see the preconditions for more information.
        ///
        /// The number of octaves controls the amount of detail in the
        /// ridged-multifractal noise.
        ///
        /// The larger the number of octaves, the more time required to
        /// calculate the ridged-multifractal-noise value.
        void SetOctaveCount (int octaveCount)
        {
          if (octaveCount > RIDGED_MAX_OCTAVE) {
            throw noise::ExceptionInvalidParam ();
          }
          m_octaveCount = octaveCount;
        }

        /// Sets the seed value used by the ridged-multifractal-noise
        /// function.
        ///
        /// @param seed The seed value.
        void SetSeed (int seed)
        {
          m_seed = seed;
        }

      protected:

        /// Calculates the spectral weights for each octave.
        ///
        /// This method is called when the lacunarity changes.
        void CalcSpectralWeights ();

        /// Frequency of the first octave.
        double m_frequency;

        /// Frequency multiplier between successive octaves.
        double m_lacunarity;

        /// Quality of the ridged-multifractal noise.
        noise::NoiseQuality m_noiseQuality;

        /// Total number of octaves that generate the ridged-multifractal
        /// noise.
        int m_octaveCount;

        /// Contains the spectral weights for each octave.
        double m_pSpectralWeights[RIDGED_MAX_OCTAVE];

        /// Seed value used by the ridged-multfractal-noise function.
        int m_seed;

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
