// libslic3r references NanoSVG without carrying its header-only implementation. This probe links
// libslic3r without the GUI BitmapCache.cpp that supplies the implementation in the application.
#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
