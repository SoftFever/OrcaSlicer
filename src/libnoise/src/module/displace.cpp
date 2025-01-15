// displace.cpp
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

#include "displace.h"

using namespace noise::module;

Displace::Displace ():
  Module (GetSourceModuleCount ())
{
}

double Displace::GetValue (double x, double y, double z) const
{
  assert (m_pSourceModule[0] != NULL);
  assert (m_pSourceModule[1] != NULL);
  assert (m_pSourceModule[2] != NULL);
  assert (m_pSourceModule[3] != NULL);

  // Get the output values from the three displacement modules.  Add each
  // value to the corresponding coordinate in the input value.
  double xDisplace = x + (m_pSourceModule[1]->GetValue (x, y, z));
  double yDisplace = y + (m_pSourceModule[2]->GetValue (x, y, z));
  double zDisplace = z + (m_pSourceModule[3]->GetValue (x, y, z));

  // Retrieve the output value using the offsetted input value instead of
  // the original input value.
  return m_pSourceModule[0]->GetValue (xDisplace, yDisplace, zDisplace);
}
