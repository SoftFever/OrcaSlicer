#include <locale>
#include <ctime>

#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>

#include <boost/locale.hpp>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/integration/filesystem.hpp>
#include <boost/nowide/convert.hpp>

namespace Slic3r {

static boost::log::trivial::severity_level logSeverity = boost::log::trivial::error;

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

// Force set_logging_level(<=error) after loading of the DLL.
// Switch boost::filesystem to utf8.
static struct RunOnInit {
    RunOnInit() { 
        boost::nowide::nowide_filesystem();
        set_logging_level(1);
    }
} g_RunOnInit;

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

static std::string g_var_dir;

void set_var_dir(const std::string &dir)
{
    g_var_dir = dir;
}

const std::string& var_dir()
{
    return g_var_dir;
}

std::string var(const std::string &file_name)
{
    auto file = boost::filesystem::canonical(boost::filesystem::path(g_var_dir) / file_name).make_preferred();
    return file.string();
}

static std::string g_resources_dir;

void set_resources_dir(const std::string &dir)
{
    g_resources_dir = dir;
}

const std::string& resources_dir()
{
    return g_resources_dir;
}

static std::string g_local_dir;

void set_local_dir(const std::string &dir)
{
    g_local_dir = dir;
}

const std::string& localization_dir()
{
	return g_local_dir;
}

static std::string g_data_dir;

void set_data_dir(const std::string &dir)
{
    g_data_dir = dir;
}

const std::string& data_dir()
{
    return g_data_dir;
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

#ifdef WIN32
    #ifndef NOMINMAX
    # define NOMINMAX
    #endif
    #include <windows.h>
#endif /* WIN32 */

namespace Slic3r {

// Encode an UTF-8 string to the local code page.
std::string encode_path(const char *src)
{    
#ifdef WIN32
    // Convert the source utf8 encoded string to a wide string.
    std::wstring wstr_src = boost::nowide::widen(src);
    if (wstr_src.length() == 0)
        return std::string();
    // Convert a wide string to a local code page.
    int size_needed = ::WideCharToMultiByte(0, 0, wstr_src.data(), (int)wstr_src.size(), nullptr, 0, nullptr, nullptr);
    std::string str_dst(size_needed, 0);
    ::WideCharToMultiByte(0, 0, wstr_src.data(), (int)wstr_src.size(), const_cast<char*>(str_dst.data()), size_needed, nullptr, nullptr);
    return str_dst;
#else /* WIN32 */
    return src;
#endif /* WIN32 */
}

// Encode an 8-bit string from a local code page to UTF-8.
std::string decode_path(const char *src)
{  
#ifdef WIN32
    int len = int(strlen(src));
    if (len == 0)
        return std::string();
    // Convert the string encoded using the local code page to a wide string.
    int size_needed = ::MultiByteToWideChar(0, 0, src, len, nullptr, 0);
    std::wstring wstr_dst(size_needed, 0);
    ::MultiByteToWideChar(0, 0, src, len, const_cast<wchar_t*>(wstr_dst.data()), size_needed);
    // Convert a wide string to utf8.
    return boost::nowide::narrow(wstr_dst.c_str());
#else /* WIN32 */
    return src;
#endif /* WIN32 */
}

std::string normalize_utf8_nfc(const char *src)
{
    static std::locale locale_utf8(boost::locale::generator().generate(""));
    return boost::locale::normalize(src, boost::locale::norm_nfc, locale_utf8);
}

namespace PerlUtils {
    // Get a file name including the extension.
    std::string path_to_filename(const char *src)       { return boost::filesystem::path(src).filename().string(); }
    // Get a file name without the extension.
    std::string path_to_stem(const char *src)           { return boost::filesystem::path(src).stem().string(); }
    // Get just the extension.
    std::string path_to_extension(const char *src)      { return boost::filesystem::path(src).extension().string(); }
    // Get a directory without the trailing slash.
    std::string path_to_parent_path(const char *src)    { return boost::filesystem::path(src).parent_path().string(); }
};

std::string timestamp_str()
{
    const auto now = boost::posix_time::second_clock::local_time();
    char buf[2048];
    sprintf(buf, "on %04d-%02d-%02d at %02d:%02d:%02d",
        // Local date in an ANSII format.
        int(now.date().year()), int(now.date().month()), int(now.date().day()),
        int(now.time_of_day().hours()), int(now.time_of_day().minutes()), int(now.time_of_day().seconds()));
    return buf;
}

}; // namespace Slic3r
