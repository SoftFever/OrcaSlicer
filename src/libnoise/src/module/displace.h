// displace.h
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

#ifndef NOISE_MODULE_DISPLACE_H
#define NOISE_MODULE_DISPLACE_H

#include "modulebase.h"

namespace noise
{

  namespace module
  {

    /// @addtogroup libnoise
    /// @{

    /// @addtogroup modules
    /// @{

    /// @defgroup transformermodules Transformer Modules
    /// @addtogroup transformermodules
    /// @{

    /// Noise module that uses three source modules to displace each
    /// coordinate of the input value before returning the output value from
    /// a source module.
    ///
    /// @image html moduledisplace.png
    ///
    /// Unlike most other noise modules, the index value assigned to a source
    /// module determines its role in the displacement operation:
    /// - Source module 0 (left in the diagram) outputs a value.
    /// - Source module 1 (lower left in the diagram) specifies the offset to
    ///   apply to the @a x coordinate of the input value.
    /// - Source module 2 (lower center in the diagram) specifies the
    ///   offset to apply to the @a y coordinate of the input value.
    /// - Source module 3 (lower right in the diagram) specifies the offset
    ///   to apply to the @a z coordinate of the input value.
    ///
    /// The GetValue() method modifies the ( @a x, @a y, @a z ) coordinates of
    /// the input value using the output values from the three displacement
    /// modules before retrieving the output value from the source module.
    ///
    /// The noise::module::Turbulence noise module is a special case of the
    /// displacement module; internally, there are three Perlin-noise modules
    /// that perform the displacement operation.
    ///
    /// This noise module requires four source modules.
    class Displace: public Module
    {

      public:

      /// Constructor.
      Displace ();

      virtual int GetSourceModuleCount () const
      {
        return 4;
      }

      virtual double GetValue (double x, double y, double z) const;

      /// Returns the @a x displacement module.
      ///
      /// @returns A reference to the @a x displacement module.
      ///
      /// @pre This displacement module has been added to this noise module
      /// via a call to SetSourceModule() or SetXDisplaceModule().
      ///
      /// @throw noise::ExceptionNoModule See the preconditions for more
      /// information.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from this displacement module to the @a x coordinate of the
      /// input value before returning the output value from the source
      /// module.
      const Module& GetXDisplaceModule () const
      {
        if (m_pSourceModule == NULL || m_pSourceModule[1] == NULL) {
          throw noise::ExceptionNoModule ();
        }
        return *(m_pSourceModule[1]);
      }

      /// Returns the @a y displacement module.
      ///
      /// @returns A reference to the @a y displacement module.
      ///
      /// @pre This displacement module has been added to this noise module
      /// via a call to SetSourceModule() or SetYDisplaceModule().
      ///
      /// @throw noise::ExceptionNoModule See the preconditions for more
      /// information.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from this displacement module to the @a y coordinate of the
      /// input value before returning the output value from the source
      /// module.
      const Module& GetYDisplaceModule () const
      {
        if (m_pSourceModule == NULL || m_pSourceModule[2] == NULL) {
          throw noise::ExceptionNoModule ();
        }
        return *(m_pSourceModule[2]);
      }

      /// Returns the @a z displacement module.
      ///
      /// @returns A reference to the @a z displacement module.
      ///
      /// @pre This displacement module has been added to this noise module
      /// via a call to SetSourceModule() or SetZDisplaceModule().
      ///
      /// @throw noise::ExceptionNoModule See the preconditions for more
      /// information.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from this displacement module to the @a z coordinate of the
      /// input value before returning the output value from the source
      /// module.
      const Module& GetZDisplaceModule () const
      {
        if (m_pSourceModule == NULL || m_pSourceModule[3] == NULL) {
          throw noise::ExceptionNoModule ();
        }
        return *(m_pSourceModule[3]);
      }

      /// Sets the @a x, @a y, and @a z displacement modules.
      ///
      /// @param xDisplaceModule Displacement module that displaces the @a x
      /// coordinate of the input value.
      /// @param yDisplaceModule Displacement module that displaces the @a y
      /// coordinate of the input value.
      /// @param zDisplaceModule Displacement module that displaces the @a z
      /// coordinate of the input value.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from each of the displacement modules to the corresponding
      /// coordinates of the input value before returning the output value
      /// from the source module.
      ///
      /// This method assigns an index value of 1 to the @a x displacement
      /// module, an index value of 2 to the @a y displacement module, and an
      /// index value of 3 to the @a z displacement module.
      ///
      /// These displacement modules must exist throughout the lifetime of
      /// this noise module unless another displacement module replaces it.
      void SetDisplaceModules (const Module& xDisplaceModule,
        const Module& yDisplaceModule, const Module& zDisplaceModule)
      {
        SetXDisplaceModule (xDisplaceModule);
        SetYDisplaceModule (yDisplaceModule);
        SetZDisplaceModule (zDisplaceModule);
      }

      /// Sets the @a x displacement module.
      ///
      /// @param xDisplaceModule Displacement module that displaces the @a x
      /// coordinate.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from this displacement module to the @a x coordinate of the
      /// input value before returning the output value from the source
      /// module.
      ///
      /// This method assigns an index value of 1 to the @a x displacement
      /// module.  Passing this displacement module to this method produces
      /// the same results as passing this displacement module to the
      /// SetSourceModule() method while assigning it an index value of 1.
      ///
      /// This displacement module must exist throughout the lifetime of this
      /// noise module unless another displacement module replaces it.
      void SetXDisplaceModule (const Module& xDisplaceModule)
      {
        assert (m_pSourceModule != NULL);
        m_pSourceModule[1] = &xDisplaceModule;
      }

      /// Sets the @a y displacement module.
      ///
      /// @param yDisplaceModule Displacement module that displaces the @a y
      /// coordinate.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from this displacement module to the @a y coordinate of the
      /// input value before returning the output value from the source
      /// module.
      ///
      /// This method assigns an index value of 2 to the @a y displacement
      /// module.  Passing this displacement module to this method produces
      /// the same results as passing this displacement module to the
      /// SetSourceModule() method while assigning it an index value of 2.
      ///
      /// This displacement module must exist throughout the lifetime of this
      /// noise module unless another displacement module replaces it.
      void SetYDisplaceModule (const Module& yDisplaceModule)
      {
        assert (m_pSourceModule != NULL);
        m_pSourceModule[2] = &yDisplaceModule;
      }

      /// Sets the @a z displacement module.
      ///
      /// @param zDisplaceModule Displacement module that displaces the @a z
      /// coordinate.
      ///
      /// The GetValue() method displaces the input value by adding the output
      /// value from this displacement module to the @a z coordinate of the
      /// input value before returning the output value from the source
      /// module.
      ///
      /// This method assigns an index value of 3 to the @a z displacement
      /// module.  Passing this displacement module to this method produces
      /// the same results as passing this displacement module to the
      /// SetSourceModule() method while assigning it an index value of 3.
      ///
      /// This displacement module must exist throughout the lifetime of this
      /// noise module unless another displacement module replaces it.
      void SetZDisplaceModule (const Module& zDisplaceModule)
      {
        assert (m_pSourceModule != NULL);
        m_pSourceModule[3] = &zDisplaceModule;
      }

    };

    /// @}

    /// @}

    /// @}

  }

}

#endif
