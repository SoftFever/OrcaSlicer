// perlin.h
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

#ifndef NOISE_MODULE_PERLIN_H
#define NOISE_MODULE_PERLIN_H

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

    /// Default frequency for the noise::module::Perlin noise module.
    const double DEFAULT_PERLIN_FREQUENCY = 1.0;

    /// Default lacunarity for the noise::module::Perlin noise module.
    const double DEFAULT_PERLIN_LACUNARITY = 2.0;

    /// Default number of octaves for the noise::module::Perlin noise module.
    const int DEFAULT_PERLIN_OCTAVE_COUNT = 6;

    /// Default persistence value for the noise::module::Perlin noise module.
    const double DEFAULT_PERLIN_PERSISTENCE = 0.5;

    /// Default noise quality for the noise::module::Perlin noise module.
    const noise::NoiseQuality DEFAULT_PERLIN_QUALITY = QUALITY_STD;

    /// Default noise seed for the noise::module::Perlin noise module.
    const int DEFAULT_PERLIN_SEED = 0;

    /// Maximum number of octaves for the noise::module::Perlin noise module.
    const int PERLIN_MAX_OCTAVE = 30;

    /// Noise module that outputs 3-dimensional Perlin noise.
    ///
    /// @image html moduleperlin.png
    ///
    /// Perlin noise is the sum of several coherent-noise functions of
    /// ever-increasing frequencies and ever-decreasing amplitudes.
    ///
    /// An important property of Perlin noise is that a small change in the
    /// input value will produce a small change in the output value, while a
    /// large change in the input value will produce a random change in the
    /// output value.
    ///
    /// This noise module outputs Perlin-noise values that usually range from
    /// -1.0 to +1.0, but there are no guarantees that all output values will
    /// exist within that range.
    ///
    /// For a better description of Perlin noise, see the links in the
    /// <i>References and Acknowledgments</i> section.
    ///
    /// This noise module does not require any source modules.
    ///
    /// <b>Octaves</b>
    ///
    /// The number of octaves control the <i>amount of detail</i> of the
    /// Perlin noise.  Adding more octaves increases the detail of the Perlin
    /// noise, but with the drawback of increasing the calculation time.
    ///
    /// An octave is one of the coherent-noise functions in a series of
    /// coherent-noise functions that are added together to form Perlin
    /// noise.
    ///
    /// An application may specify the frequency of the first octave by
    /// calling the SetFrequency() method.
    ///
    /// An application may specify the number of octaves that generate Perlin
    /// noise by calling the SetOctaveCount() method.
    ///
    /// These coherent-noise functions are called octaves because each octave
    /// has, by default, double the frequency of the previous octave.  Musical
    /// tones have this property as well; a musical C tone that is one octave
    /// higher than the previous C tone has double its frequency.
    ///
    /// <b>Frequency</b>
    ///
    /// An application may specify the frequency of the first octave by
    /// calling the SetFrequency() method.
    ///
    /// <b>Persistence</b>
    ///
    /// The persistence value controls the <i>roughness</i> of the Perlin
    /// noise.  Larger values produce rougher noise.
    ///
    /// The persistence value determines how quickly the amplitudes diminish
    /// for successive octaves.  The amplitude of the first octave is 1.0.
    /// The amplitude of each subsequent octave is equal to the product of the
    /// previous octave's amplitude and the persistence value.  So a
    /// persistence value of 0.5 sets the amplitude of the first octave to
    /// 1.0; the second, 0.5; the third, 0.25; etc.
    ///
    /// An application may specify the persistence value by calling the
    /// SetPersistence() method.
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
    /// <b>References &amp; acknowledgments</b>
    ///
    /// <a href=http://www.noisemachine.com/talk1/>The Noise Machine</a> -
    /// From the master, Ken Perlin himself.  This page contains a
    /// presentation that describes Perlin noise and some of its variants.
    /// He won an Oscar for creating the Perlin noise algorithm!
    ///
    /// <a
    /// href=http://freespace.virgin.net/hugo.elias/models/m_perlin.htm>
    /// Perlin Noise</a> - Hugo Elias's webpage contains a very good
    /// description of Perlin noise and describes its many applications.  This
    /// page gave me the inspiration to create libnoise in the first place.
    /// Now that I know how to generate Perlin noise, I will never again use
    /// cheesy subdivision algorithms to create terrain (unless I absolutely
    /// need the speed.)
    ///
    /// <a
    /// href=http://www.robo-murito.net/code/perlin-noise-math-faq.html>The
    /// Perlin noise math FAQ</a> - A good page that describes Perlin noise in
    /// plain English with only a minor amount of math.  During development of
    /// libnoise, I noticed that my coherent-noise function generated terrain
    /// with some "regularity" to the terrain features.  This page describes a
    /// better coherent-noise function called <i>gradient noise</i>.  This
    /// version of noise::module::Perlin uses gradient coherent noise to
    /// generate Perlin noise.
    class Perlin: public Module
    {

      public:

        /// Constructor.
        ///
        /// The default frequency is set to
        /// noise::module::DEFAULT_PERLIN_FREQUENCY.
        ///
        /// The default lacunarity is set to
        /// noise::module::DEFAULT_PERLIN_LACUNARITY.
        ///
        /// The default number of octaves is set to
        /// noise::module::DEFAULT_PERLIN_OCTAVE_COUNT.
        ///
        /// The default persistence value is set to
        /// noise::module::DEFAULT_PERLIN_PERSISTENCE.
        ///
        /// The default seed value is set to
        /// noise::module::DEFAULT_PERLIN_SEED.
        Perlin ();

        /// Returns the frequency of the first octave.
        ///
        /// @returns The frequency of the first octave.
        double GetFrequency () const
        {
          return m_frequency;
        }

        /// Returns the lacunarity of the Perlin noise.
        ///
        /// @returns The lacunarity of the Perlin noise.
        /// 
        /// The lacunarity is the frequency multiplier between successive
        /// octaves.
        double GetLacunarity () const
        {
          return m_lacunarity;
        }

        /// Returns the quality of the Perlin noise.
        ///
        /// @returns The quality of the Perlin noise.
        ///
        /// See noise::NoiseQuality for definitions of the various
        /// coherent-noise qualities.
        noise::NoiseQuality GetNoiseQuality () const
        {
          return m_noiseQuality;
        }

        /// Returns the number of octaves that generate the Perlin noise.
        ///
        /// @returns The number of octaves that generate the Perlin noise.
        ///
        /// The number of octaves controls the amount of detail in the Perlin
        /// noise.
        int GetOctaveCount () const
        {
          return m_octaveCount;
        }

        /// Returns the persistence value of the Perlin noise.
        ///
        /// @returns The persistence value of the Perlin noise.
        ///
        /// The persistence value controls the roughness of the Perlin noise.
        double GetPersistence () const
        {
          return m_persistence;
        }

        /// Returns the seed value used by the Perlin-noise function.
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

        /// Sets the lacunarity of the Perlin noise.
        ///
        /// @param lacunarity The lacunarity of the Perlin noise.
        /// 
        /// The lacunarity is the frequency multiplier between successive
        /// octaves.
        ///
        /// For best results, set the lacunarity to a number between 1.5 and
        /// 3.5.
        void SetLacunarity (double lacunarity)
        {
          m_lacunarity = lacunarity;
        }

        /// Sets the quality of the Perlin noise.
        ///
        /// @param noiseQuality The quality of the Perlin noise.
        ///
        /// See noise::NoiseQuality for definitions of the various
        /// coherent-noise qualities.
        void SetNoiseQuality (noise::NoiseQuality noiseQuality)
        {
          m_noiseQuality = noiseQuality;
        }

        /// Sets the number of octaves that generate the Perlin noise.
        ///
        /// @param octaveCount The number of octaves that generate the Perlin
        /// noise.
        ///
        /// @pre The number of octaves ranges from 1 to
        /// noise::module::PERLIN_MAX_OCTAVE.
        ///
        /// @throw noise::ExceptionInvalidParam An invalid parameter was
        /// specified; see the preconditions for more information.
        ///
        /// The number of octaves controls the amount of detail in the Perlin
        /// noise.
        ///
        /// The larger the number of octaves, the more time required to
        /// calculate the Perlin-noise value.
        void SetOctaveCount (int octaveCount)
        {
          if (octaveCount < 1 || octaveCount > PERLIN_MAX_OCTAVE) {
            throw noise::ExceptionInvalidParam ();
          }
          m_octaveCount = octaveCount;
        }

        /// Sets the persistence value of the Perlin noise.
        ///
        /// @param persistence The persistence value of the Perlin noise.
        ///
        /// The persistence value controls the roughness of the Perlin noise.
        ///
        /// For best results, set the persistence to a number between 0.0 and
        /// 1.0.
        void SetPersistence (double persistence)
        {
          m_persistence = persistence;
        }

        /// Sets the seed value used by the Perlin-noise function.
        ///
        /// @param seed The seed value.
        void SetSeed (int seed)
        {
          m_seed = seed;
        }

      protected:

        /// Frequency of the first octave.
        double m_frequency;

        /// Frequency multiplier between successive octaves.
        double m_lacunarity;

        /// Quality of the Perlin noise.
        noise::NoiseQuality m_noiseQuality;

        /// Total number of octaves that generate the Perlin noise.
        int m_octaveCount;

        /// Persistence of the Perlin noise.
        double m_persistence;

        /// Seed value used by the Perlin-noise function.
        int m_seed;

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
