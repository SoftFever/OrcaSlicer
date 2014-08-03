#ifndef _myinit_h_
#define _myinit_h_

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#undef read
#undef seekdir
#include <ostream>
#include <iostream>
#include <sstream>

#ifdef SLIC3RXS
extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef do_open
#undef do_close
}
#include "perlglue.hpp"
#endif

#include "libslic3r/libslic3r.h"

#endif
