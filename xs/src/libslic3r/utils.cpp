#ifdef SLIC3R_HAS_BROKEN_CROAK

// Some Strawberry Perl builds (mainly the latest 64bit builds) have a broken mechanism
// for emiting Perl exception after handling a C++ exception. Perl interpreter
// simply hangs. Better to show a message box in that case and stop the application.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <Windows.h>
#endif

void confess_at(const char *file, int line, const char *func, const char *format, ...)
{
    char dest[1024*8];
    va_list argptr;
    va_start(argptr, format);
    vsprintf(dest, format, argptr);
    va_end(argptr);

    char filelinefunc[1024*8];
    sprintf(filelinefunc, "\r\nin function: %s\r\nfile: %s\r\nline: %d\r\n", func, file, line);
    strcat(dest, filelinefunc);
    strcat(dest, "\r\n Closing the application.\r\n");
    #ifdef WIN32
    ::MessageBoxA(NULL, dest, "Slic3r Prusa Edition", MB_OK | MB_ICONERROR);
    #endif

    // Give up.
    printf(dest);
    exit(-1);
}

#else

#include <xsinit.h>

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

#endif
