// scalepoint.cpp
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

#include "scalepoint.h"

using namespace noise::module;

ScalePoint::ScalePoint ():
  Module (GetSourceModuleCount ()),
  m_xScale (DEFAULT_SCALE_POINT_X),
  m_yScale (DEFAULT_SCALE_POINT_Y),
  m_zScale (DEFAULT_SCALE_POINT_Z)
{
}

double ScalePoint::GetValue (double x, double y, double z) const
{
  assert (m_pSourceModule[0] != NULL);

  return m_pSourceModule[0]->GetValue (x * m_xScale, y * m_yScale,
    z * m_zScale);
}
