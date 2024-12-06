// clamp.cpp
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

#include "clamp.h"

using namespace noise::module;

Clamp::Clamp ():
  Module (GetSourceModuleCount ()),
  m_lowerBound (DEFAULT_CLAMP_LOWER_BOUND),
  m_upperBound (DEFAULT_CLAMP_UPPER_BOUND)
{
}

double Clamp::GetValue (double x, double y, double z) const
{
  assert (m_pSourceModule[0] != NULL);

  double value = m_pSourceModule[0]->GetValue (x, y, z);
  if (value < m_lowerBound) {
    return m_lowerBound;
  } else if (value > m_upperBound) {
    return m_upperBound;
  } else {
    return value;
  }
}

void Clamp::SetBounds (double lowerBound, double upperBound)
{
  assert (lowerBound < upperBound);

  m_lowerBound = lowerBound;
  m_upperBound = upperBound;
}
