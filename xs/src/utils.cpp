#include <myinit.h>

void
confess_at(const char *file, int line, const char *func,
            const char *pat, ...)
{
    #ifdef SLIC3RXS
     va_list args;
     SV *error_sv = newSVpvf("Error in function %s at %s:%d: ", func,
         file, line);

     va_start(args, pat);
     sv_vcatpvf(error_sv, pat, &args);
     va_end(args);

     sv_catpvn(error_sv, "\n\t", 2);

     dSP;
     ENTER;
     SAVETMPS;
     PUSHMARK(SP);
     XPUSHs( sv_2mortal(error_sv) );
     PUTBACK;
     call_pv("Carp::confess", G_DISCARD);
     FREETMPS;
     LEAVE;
    #endif
}
