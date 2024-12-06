// noise.h
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

#ifndef NOISE_H
#define NOISE_H

/// @mainpage libnoise
///
/// @section intro Introduction
///
/// libnoise is a portable C++ library that is used to generate <i>coherent
/// noise</i>, a type of smoothly-changing noise. libnoise can generate Perlin
/// noise, ridged multifractal noise, and other types of coherent noise.
///
/// Coherent noise is often used by graphics programmers to generate
/// natural-looking textures, planetary terrain, and other things. It can
/// also be used to move critters in a realistic way.
///
/// libnoise is known to compile using the following compilers on the
/// following platforms:
/// - Microsoft Visual C++ 5.0 under Microsoft Windows 2000 Service Pack 4
/// - gcc 3.3.4 under Gentoo Linux 10.0 (x86)
///
/// It is not known if libnoise will compile on 64-bit platforms, although
/// there is a good change that it will.
///
/// @section noise Noise Modules
///
/// In libnoise, coherent-noise generators are encapsulated in classes called
/// <i>noise modules</i>. There are many different types of noise modules.
/// Some noise modules can combine or modify the outputs of other noise
/// modules in various ways; you can join these modules together to generate
/// very complex coherent noise.
///
/// A noise module receives a 3-dimensional input value from the application,
/// computes the noise value given that input value, and returns the resulting
/// value back to the application.
///
/// If the application passes the same input value to a noise module, the
/// noise module returns the same output value.
///
/// All noise modules are derived from the noise::module::Module abstract
/// base class.
///
/// @section contact Contact
///
/// Contact jas for questions about libnoise.  The spam-resistant email
/// address is jlbezigvins@gmzigail.com (For great email, take off every
/// <a href=http://www.planettribes.com/allyourbase/story.shtml>zig</a>.)

#include "module/module.h"
#include "model/model.h"
#include "misc.h"

#endif
