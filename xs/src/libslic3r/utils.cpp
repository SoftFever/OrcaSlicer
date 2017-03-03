#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

namespace Slic3r {

static boost::log::trivial::severity_level logSeverity = boost::log::trivial::fatal;

void set_logging_level(unsigned int level)
{
    switch (level) {
    // Report fatal errors only.
    case 0: logSeverity = boost::log::trivial::fatal; break;
    // Report fatal errors and errors.
    case 1: logSeverity = boost::log::trivial::error; break;
    // Report fatal errors, errors and warnings.
    case 2: logSeverity = boost::log::trivial::warning; break;
    // Report all errors, warnings and infos.
    case 3: logSeverity = boost::log::trivial::info; break;
    // Report all errors, warnings, infos and debugging.
    case 4: logSeverity = boost::log::trivial::debug; break;
    // Report everyting including fine level tracing information.
    default: logSeverity = boost::log::trivial::trace; break;
    }

    boost::log::core::get()->set_filter
    (
        boost::log::trivial::severity >= logSeverity
    );
}

void trace(unsigned int level, const char *message)
{
    boost::log::trivial::severity_level severity = boost::log::trivial::trace;
    switch (level) {
    // Report fatal errors only.
    case 0: severity = boost::log::trivial::fatal; break;
    // Report fatal errors and errors.
    case 1: severity = boost::log::trivial::error; break;
    // Report fatal errors, errors and warnings.
    case 2: severity = boost::log::trivial::warning; break;
    // Report all errors, warnings and infos.
    case 3: severity = boost::log::trivial::info; break;
    // Report all errors, warnings, infos and debugging.
    case 4: severity = boost::log::trivial::debug; break;
    // Report everyting including fine level tracing information.
    default: severity = boost::log::trivial::trace; break;
    }

    BOOST_LOG_STREAM_WITH_PARAMS(::boost::log::trivial::logger::get(),\
        (::boost::log::keywords::severity = severity)) << message;
}

} // namespace Slic3r

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
