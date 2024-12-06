// blend.h
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

#ifndef NOISE_MODULE_BLEND_H
#define NOISE_MODULE_BLEND_H

#include "modulebase.h"

namespace noise
{

  namespace module
  {

    /// @addtogroup libnoise
    /// @{

    /// @addtogroup modules
    /// @{

    /// @defgroup selectormodules Selector Modules
    /// @addtogroup selectormodules
    /// @{

    /// Noise module that outputs a weighted blend of the output values from
    /// two source modules given the output value supplied by a control module.
    ///
    /// @image html moduleblend.png
    ///
    /// Unlike most other noise modules, the index value assigned to a source
    /// module determines its role in the blending operation:
    /// - Source module 0 (upper left in the diagram) outputs one of the
    ///   values to blend.
    /// - Source module 1 (lower left in the diagram) outputs one of the
    ///   values to blend.
    /// - Source module 2 (bottom of the diagram) is known as the <i>control
    ///   module</i>.  The control module determines the weight of the
    ///   blending operation.  Negative values weigh the blend towards the
    ///   output value from the source module with an index value of 0.
    ///   Positive values weigh the blend towards the output value from the
    ///   source module with an index value of 1.
    ///
    /// An application can pass the control module to the SetControlModule()
    /// method instead of the SetSourceModule() method.  This may make the
    /// application code easier to read.
    ///
    /// This noise module uses linear interpolation to perform the blending
    /// operation.
    ///
    /// This noise module requires three source modules.
    class Blend: public Module
    {

      public:

        /// Constructor.
        Blend ();

        /// Returns the control module.
        ///
        /// @returns A reference to the control module.
        ///
        /// @pre A control module has been added to this noise module via a
        /// call to SetSourceModule() or SetControlModule().
        ///
        /// @throw noise::ExceptionNoModule See the preconditions for more
        /// information.
        ///
        /// The control module determines the weight of the blending
        /// operation.  Negative values weigh the blend towards the output
        /// value from the source module with an index value of 0.  Positive
        /// values weigh the blend towards the output value from the source
        /// module with an index value of 1.
        const Module& GetControlModule () const
        {
          if (m_pSourceModule == NULL || m_pSourceModule[2] == NULL) {
            throw noise::ExceptionNoModule ();
          }
          return *(m_pSourceModule[2]);
        }

        virtual int GetSourceModuleCount () const
        {
          return 3;
        }

	      virtual double GetValue (double x, double y, double z) const;

        /// Sets the control module.
        ///
        /// @param controlModule The control module.
        ///
        /// The control module determines the weight of the blending
        /// operation.  Negative values weigh the blend towards the output
        /// value from the source module with an index value of 0.  Positive
        /// values weigh the blend towards the output value from the source
        /// module with an index value of 1.
        ///
        /// This method assigns the control module an index value of 2.
        /// Passing the control module to this method produces the same
        /// results as passing the control module to the SetSourceModule()
        /// method while assigning that noise module an index value of 2.
        ///
        /// This control module must exist throughout the lifetime of this
        /// noise module unless another control module replaces that control
        /// module.
        void SetControlModule (const Module& controlModule)
        {
          assert (m_pSourceModule != NULL);
          m_pSourceModule[2] = &controlModule;
        }

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
