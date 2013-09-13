#ifndef _myinit_h_
#define _myinit_h_

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <ostream>
#include <iostream>

#ifdef SLIC3RXS
extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef do_open
#undef do_close
}
#endif

#define EPSILON 1e-4

namespace Slic3r {}
using namespace Slic3r;

/* Implementation of CONFESS("foo"): */
#define CONFESS(...) confess_at(__FILE__, __LINE__, __func__, __VA_ARGS__)
void confess_at(const char *file, int line, const char *func, const char *pat, ...);
/* End implementation of CONFESS("foo"): */

#endif
