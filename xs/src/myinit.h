#ifndef _myinit_h_
#define _myinit_h_

// undef some macros set by Perl which cause compilation errors on Win32
#undef read
#undef seekdir
#undef bind
#undef send
#undef connect
#undef wait
#undef accept
#undef close
#undef open
#undef write
#undef socket
#undef listen
#undef shutdown
#undef ioctl
#undef getpeername
#undef rect
#undef setsockopt
#undef getsockopt
#undef getsockname
#undef gethostname
#undef select
#undef socketpair
#undef recvfrom
#undef sendto

// these need to be included early for Win32 (listing it in Build.PL is not enough)
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
