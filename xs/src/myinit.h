#ifndef _myinit_h_
#define _myinit_h_

// this needs to be included early for MSVC (listing it in Build.PL is not enough)
#include <ostream>
#include <iostream>

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
#undef do_open
#undef do_close
}

#define EPSILON 1e-4

namespace Slic3r {}
using namespace Slic3r;

/* Implementation of CONFESS("foo"): */
#define CONFESS(...) \
     confess_at(__FILE__, __LINE__, __func__, __VA_ARGS__)

void
do_confess(SV *error_sv)
{
     dSP;
     ENTER;
     SAVETMPS;
     PUSHMARK(SP);
     XPUSHs( sv_2mortal(error_sv) );
     PUTBACK;
     call_pv("Carp::confess", G_DISCARD);
     FREETMPS;
     LEAVE;
}

void
confess_at(const char *file, int line, const char *func,
            const char *pat, ...)
{
     va_list args;
     SV *error_sv = newSVpvf("Error in function %s at %s:%d: ", func,
         file, line);

     va_start(args, pat);
     sv_vcatpvf(error_sv, pat, &args);
     va_end(args);

     sv_catpvn(error_sv, "\n\t", 2);

     do_confess(error_sv);
}
/* End implementation of CONFESS("foo") */

#endif
