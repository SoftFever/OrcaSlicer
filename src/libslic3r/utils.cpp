#include "Utils.hpp"
#include "I18N.hpp"

#include <locale>
#include <ctime>
#include <cstdarg>
#include <stdio.h>

#ifdef WIN32
	#include <windows.h>
	#include <psapi.h>
#else
	#include <unistd.h>
	#include <sys/types.h>
	#include <sys/param.h>
	#ifdef BSD
		#include <sys/sysctl.h>
	#endif
#endif

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
#include <boost/nowide/cstdio.hpp>

#include <tbb/task_scheduler_init.h>

#if defined(__linux) || defined(__GNUC__ )
#include <strings.h>
#endif /* __linux */

#ifdef _MSC_VER 
    #define strcasecmp _stricmp
#endif

namespace Slic3r {

static boost::log::trivial::severity_level logSeverity = boost::log::trivial::error;

static boost::log::trivial::severity_level level_to_boost(unsigned level)
{
    switch (level) {
    // Report fatal errors only.
    case 0: return boost::log::trivial::fatal;
    // Report fatal errors and errors.
    case 1: return boost::log::trivial::error;
    // Report fatal errors, errors and warnings.
    case 2: return boost::log::trivial::warning;
    // Report all errors, warnings and infos.
    case 3: return boost::log::trivial::info;
    // Report all errors, warnings, infos and debugging.
    case 4: return boost::log::trivial::debug;
    // Report everyting including fine level tracing information.
    default: return boost::log::trivial::trace;
    }
}

void set_logging_level(unsigned int level)
{
    logSeverity = level_to_boost(level);

    boost::log::core::get()->set_filter
    (
        boost::log::trivial::severity >= logSeverity
    );
}

unsigned get_logging_level()
{
    switch (logSeverity) {
    case boost::log::trivial::fatal : return 0;
    case boost::log::trivial::error : return 1;
    case boost::log::trivial::warning : return 2;
    case boost::log::trivial::info : return 3;
    case boost::log::trivial::debug : return 4;
    case boost::log::trivial::trace : return 5;
    default: return 1;
    }
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
    boost::log::trivial::severity_level severity = level_to_boost(level);

    BOOST_LOG_STREAM_WITH_PARAMS(::boost::log::trivial::logger::get(),\
        (::boost::log::keywords::severity = severity)) << message;
}

void disable_multi_threading()
{
    // Disable parallelization so the Shiny profiler works
    static tbb::task_scheduler_init *tbb_init = nullptr;
    if (tbb_init == nullptr)
        tbb_init = new tbb::task_scheduler_init(1);
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
    auto file = (boost::filesystem::path(g_var_dir) / file_name).make_preferred();
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

// Translate function callback, to call wxWidgets translate function to convert non-localized UTF8 string to a localized one.
Slic3r::I18N::translate_fn_type Slic3r::I18N::translate_fn = nullptr;

static std::string g_data_dir;

void set_data_dir(const std::string &dir)
{
    g_data_dir = dir;
}

const std::string& data_dir()
{
    return g_data_dir;
}


// borrowed from LVVM lib/Support/Windows/Path.inc
int rename_file(const std::string &from, const std::string &to)
{
    int ec = 0;

#ifdef _WIN32

	// Convert to utf-16.
    std::wstring wide_from = boost::nowide::widen(from);
    std::wstring wide_to   = boost::nowide::widen(to);

    // Retry while we see recoverable errors.
    // System scanners (eg. indexer) might open the source file when it is written
    // and closed.
    bool TryReplace = true;

    // This loop may take more than 2000 x 1ms to finish.
    for (int i = 0; i < 2000; ++ i) {
        if (i > 0)
            // Sleep 1ms
            ::Sleep(1);
        if (TryReplace) {
            // Try ReplaceFile first, as it is able to associate a new data stream
            // with the destination even if the destination file is currently open.
            if (::ReplaceFileW(wide_to.data(), wide_from.data(), NULL, 0, NULL, NULL))
                return 0;
            DWORD ReplaceError = ::GetLastError();
            ec = -1; // ReplaceError
            // If ReplaceFileW returned ERROR_UNABLE_TO_MOVE_REPLACEMENT or
            // ERROR_UNABLE_TO_MOVE_REPLACEMENT_2, retry but only use MoveFileExW().
            if (ReplaceError == ERROR_UNABLE_TO_MOVE_REPLACEMENT ||
                ReplaceError == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
                TryReplace = false;
                continue;
            }
            // If ReplaceFileW returned ERROR_UNABLE_TO_REMOVE_REPLACED, retry
            // using ReplaceFileW().
            if (ReplaceError == ERROR_UNABLE_TO_REMOVE_REPLACED)
                continue;
            // We get ERROR_FILE_NOT_FOUND if the destination file is missing.
            // MoveFileEx can handle this case.
            if (ReplaceError != ERROR_ACCESS_DENIED && ReplaceError != ERROR_FILE_NOT_FOUND && ReplaceError != ERROR_SHARING_VIOLATION)
                break;
        }
        if (::MoveFileExW(wide_from.c_str(), wide_to.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
            return 0;
        DWORD MoveError = ::GetLastError();
        ec = -1; // MoveError
        if (MoveError != ERROR_ACCESS_DENIED && MoveError != ERROR_SHARING_VIOLATION)
            break;
    }

#else

	boost::nowide::remove(to.c_str());
	ec = boost::nowide::rename(from.c_str(), to.c_str());

#endif

    return ec;
}

int copy_file(const std::string &from, const std::string &to)
{
    const boost::filesystem::path source(from);
    const boost::filesystem::path target(to);
    static const auto perms = boost::filesystem::owner_read | boost::filesystem::owner_write | boost::filesystem::group_read | boost::filesystem::others_read;   // aka 644

    // Make sure the file has correct permission both before and after we copy over it.
    try {
        if (boost::filesystem::exists(target))
            boost::filesystem::permissions(target, perms);
        boost::filesystem::copy_file(source, target, boost::filesystem::copy_option::overwrite_if_exists);
        boost::filesystem::permissions(target, perms);
    } catch (std::exception & /* ex */) {
        return -1;
    }
    return 0;
}

// Ignore system and hidden files, which may be created by the DropBox synchronisation process.
// https://github.com/prusa3d/PrusaSlicer/issues/1298
bool is_plain_file(const boost::filesystem::directory_entry &dir_entry)
{
    if (! boost::filesystem::is_regular_file(dir_entry.status()))
        return false;
#ifdef _MSC_VER
    DWORD attributes = GetFileAttributesW(boost::nowide::widen(dir_entry.path().string()).c_str());
    return (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0;
#else
    return true;
#endif
}

bool is_ini_file(const boost::filesystem::directory_entry &dir_entry)
{
    return is_plain_file(dir_entry) && strcasecmp(dir_entry.path().extension().string().c_str(), ".ini") == 0;
}

bool is_idx_file(const boost::filesystem::directory_entry &dir_entry)
{
	return is_plain_file(dir_entry) && strcasecmp(dir_entry.path().extension().string().c_str(), ".idx") == 0;
}

} // namespace Slic3r

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


std::string string_printf(const char *format, ...)
{
    va_list args1;
    va_start(args1, format);
    va_list args2;
    va_copy(args2, args1);

    size_t needed_size = ::vsnprintf(nullptr, 0, format, args1) + 1;
    va_end(args1);

    std::string res(needed_size, '\0');
    ::vsnprintf(&res.front(), res.size(), format, args2);
    va_end(args2);

    return res;
}


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

unsigned get_current_pid()
{
#ifdef WIN32
    return GetCurrentProcessId();
#else
    return ::getpid();
#endif
}

std::string xml_escape(std::string text)
{
    std::string::size_type pos = 0;
    for (;;)
    {
        pos = text.find_first_of("\"\'&<>", pos);
        if (pos == std::string::npos)
            break;

        std::string replacement;
        switch (text[pos])
        {
        case '\"': replacement = "&quot;"; break;
        case '\'': replacement = "&apos;"; break;
        case '&':  replacement = "&amp;";  break;
        case '<':  replacement = "&lt;";   break;
        case '>':  replacement = "&gt;";   break;
        default: break;
        }

        text.replace(pos, 1, replacement);
        pos += replacement.size();
    }

    return text;
}

std::string format_memsize_MB(size_t n) 
{
    std::string out;
    size_t n2 = 0;
    size_t scale = 1;
    // Round to MB
    n +=  500000;
    n /= 1000000;
    while (n >= 1000) {
        n2 = n2 + scale * (n % 1000);
        n /= 1000;
        scale *= 1000;
    }
    char buf[8];
    sprintf(buf, "%d", (int)n);
    out = buf;
    while (scale != 1) {
        scale /= 1000;
        n = n2 / scale;
        n2 = n2  % scale;
        sprintf(buf, ",%03d", (int)n);
        out += buf;
    }
    return out + "MB";
}

#ifdef WIN32

#ifndef PROCESS_MEMORY_COUNTERS_EX
    // MingW32 doesn't have this struct in psapi.h
    typedef struct _PROCESS_MEMORY_COUNTERS_EX {
      DWORD  cb;
      DWORD  PageFaultCount;
      SIZE_T PeakWorkingSetSize;
      SIZE_T WorkingSetSize;
      SIZE_T QuotaPeakPagedPoolUsage;
      SIZE_T QuotaPagedPoolUsage;
      SIZE_T QuotaPeakNonPagedPoolUsage;
      SIZE_T QuotaNonPagedPoolUsage;
      SIZE_T PagefileUsage;
      SIZE_T PeakPagefileUsage;
      SIZE_T PrivateUsage;
    } PROCESS_MEMORY_COUNTERS_EX, *PPROCESS_MEMORY_COUNTERS_EX;
#endif /* PROCESS_MEMORY_COUNTERS_EX */

std::string log_memory_info()
{
    std::string out;
    if (logSeverity <= boost::log::trivial::info) {
        HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ::GetCurrentProcessId());
        if (hProcess != nullptr) {
            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
				out = " WorkingSet: " + format_memsize_MB(pmc.WorkingSetSize) + " PrivateBytes: " + format_memsize_MB(pmc.PrivateUsage) + " Pagefile(peak): " + format_memsize_MB(pmc.PagefileUsage) + "(" + format_memsize_MB(pmc.PeakPagefileUsage) + ")";
            CloseHandle(hProcess);
        }
    }
    return out;
}

#else
std::string log_memory_info()
{
    return std::string();
}
#endif

// Returns the size of physical memory (RAM) in bytes.
// http://nadeausoftware.com/articles/2012/09/c_c_tip_how_get_physical_memory_size_system
size_t total_physical_memory()
{
#if defined(_WIN32) && (defined(__CYGWIN__) || defined(__CYGWIN32__))
	// Cygwin under Windows. ------------------------------------
	// New 64-bit MEMORYSTATUSEX isn't available.  Use old 32.bit
	MEMORYSTATUS status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatus( &status );
	return (size_t)status.dwTotalPhys;
#elif defined(_WIN32)
	// Windows. -------------------------------------------------
	// Use new 64-bit MEMORYSTATUSEX, not old 32-bit MEMORYSTATUS
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx( &status );
	return (size_t)status.ullTotalPhys;
#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
	// UNIX variants. -------------------------------------------
	// Prefer sysctl() over sysconf() except sysctl() HW_REALMEM and HW_PHYSMEM

#if defined(CTL_HW) && (defined(HW_MEMSIZE) || defined(HW_PHYSMEM64))
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_MEMSIZE)
	mib[1] = HW_MEMSIZE;            // OSX. ---------------------
#elif defined(HW_PHYSMEM64)
	mib[1] = HW_PHYSMEM64;          // NetBSD, OpenBSD. ---------
#endif
	int64_t size = 0;               // 64-bit
	size_t len = sizeof( size );
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	return 0L;			// Failed?

#elif defined(_SC_AIX_REALMEM)
	// AIX. -----------------------------------------------------
	return (size_t)sysconf( _SC_AIX_REALMEM ) * (size_t)1024L;

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGESIZE)
	// FreeBSD, Linux, OpenBSD, and Solaris. --------------------
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGESIZE );

#elif defined(_SC_PHYS_PAGES) && defined(_SC_PAGE_SIZE)
	// Legacy. --------------------------------------------------
	return (size_t)sysconf( _SC_PHYS_PAGES ) *
		(size_t)sysconf( _SC_PAGE_SIZE );

#elif defined(CTL_HW) && (defined(HW_PHYSMEM) || defined(HW_REALMEM))
	// DragonFly BSD, FreeBSD, NetBSD, OpenBSD, and OSX. --------
	int mib[2];
	mib[0] = CTL_HW;
#if defined(HW_REALMEM)
	mib[1] = HW_REALMEM;		// FreeBSD. -----------------
#elif defined(HW_PYSMEM)
	mib[1] = HW_PHYSMEM;		// Others. ------------------
#endif
	unsigned int size = 0;		// 32-bit
	size_t len = sizeof( size );
	if ( sysctl( mib, 2, &size, &len, NULL, 0 ) == 0 )
		return (size_t)size;
	return 0L;			// Failed?
#endif // sysctl and sysconf variants

#else
	return 0L;			// Unknown OS.
#endif
}

}; // namespace Slic3r
