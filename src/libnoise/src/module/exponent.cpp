// exponent.cpp
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

#include "exponent.h"

using namespace noise::module;

Exponent::Exponent ():
  Module (GetSourceModuleCount ()),
  m_exponent (DEFAULT_EXPONENT)
{
}

double Exponent::GetValue (double x, double y, double z) const
{
  assert (m_pSourceModule[0] != NULL);

  double value = m_pSourceModule[0]->GetValue (x, y, z);
  return (pow (fabs ((value + 1.0) / 2.0), m_exponent) * 2.0 - 1.0);
}
