#include "libslic3r/libslic3r.h"
// nanosvg's core implementation is instantiated in libslic3r/NSVGUtils.cpp; only the
// rasterizer is left to the program.
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"
