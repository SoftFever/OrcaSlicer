// modulebase.cpp
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

#include "modulebase.h"

using namespace noise::module;

Module::Module (int sourceModuleCount)
{
  m_pSourceModule = NULL;

  // Create an array of pointers to all source modules required by this
  // noise module.  Set these pointers to NULL.
  if (sourceModuleCount > 0) {
    m_pSourceModule = new const Module*[sourceModuleCount];
    for (int i = 0; i < sourceModuleCount; i++) {
      m_pSourceModule[i] = NULL;
    }
  } else {
    m_pSourceModule = NULL;
  }
}

Module::~Module ()
{
  delete[] m_pSourceModule;
}
