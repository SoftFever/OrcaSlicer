// Hackish wrapper around the ClipperLib library to compile the Clipper library with the Z support.

// Enable the Z coordinate support.
#define use_xyz

// and let it compile
#include "clipper.cpp"
