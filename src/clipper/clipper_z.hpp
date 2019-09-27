// Hackish wrapper around the ClipperLib library to compile the Clipper library with the Z support.

#ifndef clipper_z_hpp
#ifdef clipper_hpp
#error "You should include the clipper_z.hpp first"
#endif

#define clipper_z_hpp

// Enable the Z coordinate support.
#define use_xyz

#include "clipper.hpp"

#undef clipper_hpp
#undef use_xyz

#endif // clipper_z_hpp
