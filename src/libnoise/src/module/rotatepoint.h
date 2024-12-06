// rotatepoint.h
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

#ifndef NOISE_MODULE_ROTATEPOINT_H
#define NOISE_MODULE_ROTATEPOINT_H

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

    /// Default @a x rotation angle for the noise::module::RotatePoint noise
    /// module.
    const double DEFAULT_ROTATE_X = 0.0;

    /// Default @a y rotation angle for the noise::module::RotatePoint noise
    /// module.
    const double DEFAULT_ROTATE_Y = 0.0;

    /// Default @a z rotation angle for the noise::module::RotatePoint noise
    /// module.
    const double DEFAULT_ROTATE_Z = 0.0;

    /// Noise module that rotates the input value around the origin before
    /// returning the output value from a source module.
    ///
    /// @image html modulerotatepoint.png
    ///
    /// The GetValue() method rotates the coordinates of the input value
    /// around the origin before returning the output value from the source
    /// module.  To set the rotation angles, call the SetAngles() method.  To
    /// set the rotation angle around the individual @a x, @a y, or @a z axes,
    /// call the SetXAngle(), SetYAngle() or SetZAngle() methods,
    /// respectively.
    ///
    /// The coordinate system of the input value is assumed to be
    /// "left-handed" (@a x increases to the right, @a y increases upward,
    /// and @a z increases inward.)
    ///
    /// This noise module requires one source module.
    class RotatePoint: public Module
    {

      public:

        /// Constructor.
        ///
        /// The default rotation angle around the @a x axis, in degrees, is
        /// set to noise::module::DEFAULT_ROTATE_X.
        ///
        /// The default rotation angle around the @a y axis, in degrees, is
        /// set to noise::module::DEFAULT_ROTATE_Y.
        ///
        /// The default rotation angle around the @a z axis, in degrees, is
        /// set to noise::module::DEFAULT_ROTATE_Z.
        RotatePoint ();

        virtual int GetSourceModuleCount () const
        {
          return 1;
        }

        virtual double GetValue (double x, double y, double z) const;

        /// Returns the rotation angle around the @a x axis to apply to the
        /// input value.
        ///
        /// @returns The rotation angle around the @a x axis, in degrees.
        double GetXAngle () const
        {
          return m_xAngle;
        }

        /// Returns the rotation angle around the @a y axis to apply to the
        /// input value.
        ///
        /// @returns The rotation angle around the @a y axis, in degrees.
        double GetYAngle () const
        {
          return m_yAngle;
        }

        /// Returns the rotation angle around the @a z axis to apply to the
        /// input value.
        ///
        /// @returns The rotation angle around the @a z axis, in degrees.
        double GetZAngle () const
        {
          return m_zAngle;
        }

        /// Sets the rotation angles around all three axes to apply to the
        /// input value.
        ///
        /// @param xAngle The rotation angle around the @a x axis, in degrees.
        /// @param yAngle The rotation angle around the @a y axis, in degrees.
        /// @param zAngle The rotation angle around the @a z axis, in degrees.
        ///
        /// The GetValue() method rotates the coordinates of the input value
        /// around the origin before returning the output value from the
        /// source module.
        void SetAngles (double xAngle, double yAngle, double zAngle);

        /// Sets the rotation angle around the @a x axis to apply to the input
        /// value.
        ///
        /// @param xAngle The rotation angle around the @a x axis, in degrees.
        ///
        /// The GetValue() method rotates the coordinates of the input value
        /// around the origin before returning the output value from the
        /// source module.
        void SetXAngle (double xAngle)
        {
          SetAngles (xAngle, m_yAngle, m_zAngle);
        }

        /// Sets the rotation angle around the @a y axis to apply to the input
        /// value.
        ///
        /// @param yAngle The rotation angle around the @a y axis, in degrees.
        ///
        /// The GetValue() method rotates the coordinates of the input value
        /// around the origin before returning the output value from the
        /// source module.
        void SetYAngle (double yAngle)
        {
          SetAngles (m_xAngle, yAngle, m_zAngle);
        }

        /// Sets the rotation angle around the @a z axis to apply to the input
        /// value.
        ///
        /// @param zAngle The rotation angle around the @a z axis, in degrees.
        ///
        /// The GetValue() method rotates the coordinates of the input value
        /// around the origin before returning the output value from the
        /// source module.
        void SetZAngle (double zAngle)
        {
          SetAngles (m_xAngle, m_yAngle, zAngle);
        }

      protected:

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_x1Matrix;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_x2Matrix;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_x3Matrix;

        /// @a x rotation angle applied to the input value, in degrees.
        double m_xAngle;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_y1Matrix;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_y2Matrix;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_y3Matrix;

        /// @a y rotation angle applied to the input value, in degrees.
        double m_yAngle;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_z1Matrix;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_z2Matrix;

        /// An entry within the 3x3 rotation matrix used for rotating the
        /// input value.
        double m_z3Matrix;

        /// @a z rotation angle applied to the input value, in degrees.
        double m_zAngle;

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
