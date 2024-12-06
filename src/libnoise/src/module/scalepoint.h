// scalepoint.h
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

#ifndef NOISE_MODULE_SCALEPOINT_H
#define NOISE_MODULE_SCALEPOINT_H

#include "modulebase.h"

namespace noise
{

  namespace module
  {

    /// @addtogroup libnoise
    /// @{

    /// @addtogroup modules
    /// @{

    /// @addtogroup transformermodules
    /// @{

    /// Default scaling factor applied to the @a x coordinate for the
    /// noise::module::ScalePoint noise module.
    const double DEFAULT_SCALE_POINT_X = 1.0;

    /// Default scaling factor applied to the @a y coordinate for the
    /// noise::module::ScalePoint noise module.
    const double DEFAULT_SCALE_POINT_Y = 1.0;

    /// Default scaling factor applied to the @a z coordinate for the
    /// noise::module::ScalePoint noise module.
    const double DEFAULT_SCALE_POINT_Z = 1.0;

    /// Noise module that scales the coordinates of the input value before
    /// returning the output value from a source module.
    ///
    /// @image html modulescalepoint.png
    ///
    /// The GetValue() method multiplies the ( @a x, @a y, @a z ) coordinates
    /// of the input value with a scaling factor before returning the output
    /// value from the source module.  To set the scaling factor, call the
    /// SetScale() method.  To set the scaling factor to apply to the
    /// individual @a x, @a y, or @a z coordinates, call the SetXScale(),
    /// SetYScale() or SetZScale() methods, respectively.
    ///
    /// This noise module requires one source module.
    class ScalePoint: public Module
    {

      public:

        /// Constructor.
        ///
        /// The default scaling factor applied to the @a x coordinate is set
        /// to noise::module::DEFAULT_SCALE_POINT_X.
        ///
        /// The default scaling factor applied to the @a y coordinate is set
        /// to noise::module::DEFAULT_SCALE_POINT_Y.
        ///
        /// The default scaling factor applied to the @a z coordinate is set
        /// to noise::module::DEFAULT_SCALE_POINT_Z.
        ScalePoint ();

        virtual int GetSourceModuleCount () const
        {
          return 1;
        }

        virtual double GetValue (double x, double y, double z) const;

        /// Returns the scaling factor applied to the @a x coordinate of the
        /// input value.
        ///
        /// @returns The scaling factor applied to the @a x coordinate.
        double GetXScale () const
        {
          return m_xScale;
        }

        /// Returns the scaling factor applied to the @a y coordinate of the
        /// input value.
        ///
        /// @returns The scaling factor applied to the @a y coordinate.
        double GetYScale () const
        {
          return m_yScale;
        }

        /// Returns the scaling factor applied to the @a z coordinate of the
        /// input value.
        ///
        /// @returns The scaling factor applied to the @a z coordinate.
        double GetZScale () const
        {
          return m_zScale;
        }

        /// Sets the scaling factor to apply to the input value.
        ///
        /// @param scale The scaling factor to apply.
        ///
        /// The GetValue() method multiplies the ( @a x, @a y, @a z )
        /// coordinates of the input value with a scaling factor before
        /// returning the output value from the source module.
        void SetScale (double scale)
        {
          m_xScale = scale;
          m_yScale = scale;
          m_zScale = scale;
        }

        /// Sets the scaling factor to apply to the ( @a x, @a y, @a z )
        /// coordinates of the input value.
        ///
        /// @param xScale The scaling factor to apply to the @a x coordinate.
        /// @param yScale The scaling factor to apply to the @a y coordinate.
        /// @param zScale The scaling factor to apply to the @a z coordinate.
        ///
        /// The GetValue() method multiplies the ( @a x, @a y, @a z )
        /// coordinates of the input value with a scaling factor before
        /// returning the output value from the source module.
        void SetScale (double xScale, double yScale, double zScale)
        {
          m_xScale = xScale;
          m_yScale = yScale;
          m_zScale = zScale;
        }

        /// Sets the scaling factor to apply to the @a x coordinate of the
        /// input value.
        ///
        /// @param xScale The scaling factor to apply to the @a x coordinate.
        ///
        /// The GetValue() method multiplies the ( @a x, @a y, @a z )
        /// coordinates of the input value with a scaling factor before
        /// returning the output value from the source module.
        void SetXScale (double xScale)
        {
          m_xScale = xScale;
        }

        /// Sets the scaling factor to apply to the @a y coordinate of the
        /// input value.
        ///
        /// @param yScale The scaling factor to apply to the @a y coordinate.
        ///
        /// The GetValue() method multiplies the ( @a x, @a y, @a z )
        /// coordinates of the input value with a scaling factor before
        /// returning the output value from the source module.
        void SetYScale (double yScale)
        {
          m_yScale = yScale;
        }

        /// Sets the scaling factor to apply to the @a z coordinate of the
        /// input value.
        ///
        /// @param zScale The scaling factor to apply to the @a z coordinate.
        ///
        /// The GetValue() method multiplies the ( @a x, @a y, @a z )
        /// coordinates of the input value with a scaling factor before
        /// returning the output value from the source module.
        void SetZScale (double zScale)
        {
          m_zScale = zScale;
        }

      protected:

        /// Scaling factor applied to the @a x coordinate of the input value.
        double m_xScale;

        /// Scaling factor applied to the @a y coordinate of the input value.
        double m_yScale;

        /// Scaling factor applied to the @a z coordinate of the input value.
        double m_zScale;

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
