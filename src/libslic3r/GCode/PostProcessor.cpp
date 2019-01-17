#include "PostProcessor.hpp"

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#ifdef WIN32

// The standard Windows includes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// https://blogs.msdn.microsoft.com/twistylittlepassagesallalike/2011/04/23/everyone-quotes-command-line-arguments-the-wrong-way/
// This routine appends the given argument to a command line such that CommandLineToArgvW will return the argument string unchanged.
// Arguments in a command line should be separated by spaces; this function does not add these spaces.
// Argument    - Supplies the argument to encode.
// CommandLine - Supplies the command line to which we append the encoded argument string.
static void quote_argv_winapi(const std::wstring &argument, std::wstring &commmand_line_out)
{
	// Don't quote unless we actually need to do so --- hopefully avoid problems if programs won't parse quotes properly.
	if (argument.empty() == false && argument.find_first_of(L" \t\n\v\"") == argument.npos)
		commmand_line_out.append(argument);
	else {
		commmand_line_out.push_back(L'"');
		for (auto it = argument.begin(); ; ++ it) {
			unsigned number_backslashes = 0;
			while (it != argument.end() && *it == L'\\') {
				++ it;
				++ number_backslashes;
			}
			if (it == argument.end()) {
				// Escape all backslashes, but let the terminating double quotation mark we add below be interpreted as a metacharacter.
				commmand_line_out.append(number_backslashes * 2, L'\\');
				break;
			} else if (*it == L'"') {
				// Escape all backslashes and the following double quotation mark.
				commmand_line_out.append(number_backslashes * 2 + 1, L'\\');
				commmand_line_out.push_back(*it);
			} else {
				// Backslashes aren't special here.
				commmand_line_out.append(number_backslashes, L'\\');
				commmand_line_out.push_back(*it);
			}
		}
		commmand_line_out.push_back(L'"');
	}
}

static DWORD execute_process_winapi(const std::wstring &command_line)
{
    // Extract the current environment to be passed to the child process.
	std::wstring envstr;
	{
		wchar_t *env = GetEnvironmentStrings();
		assert(env != nullptr);
		const wchar_t* var = env;
		size_t totallen = 0;
		size_t len;
		while ((len = wcslen(var)) > 0) {
			totallen += len + 1;
			var += len + 1;
		}
		envstr = std::wstring(env, totallen);
		FreeEnvironmentStrings(env);
	}

	STARTUPINFOW startup_info;
	memset(&startup_info, 0, sizeof(startup_info));
	startup_info.cb			 = sizeof(STARTUPINFO);
#if 0
	startup_info.dwFlags	 = STARTF_USESHOWWINDOW;
	startup_info.wShowWindow = SW_HIDE;
#endif
	PROCESS_INFORMATION process_info;
	if (! ::CreateProcessW(
            nullptr /* lpApplicationName */, (LPWSTR)command_line.c_str(), nullptr /* lpProcessAttributes */, nullptr /* lpThreadAttributes */, false /* bInheritHandles */,
			CREATE_UNICODE_ENVIRONMENT /* | CREATE_NEW_CONSOLE */ /* dwCreationFlags */, (LPVOID)envstr.c_str(), nullptr /* lpCurrentDirectory */, &startup_info, &process_info))
		throw std::runtime_error(std::string("Failed starting the script ") + boost::nowide::narrow(command_line) + ", Win32 error: " + std::to_string(int(::GetLastError())));
	::WaitForSingleObject(process_info.hProcess, INFINITE);
	ULONG rc = 0;
	::GetExitCodeProcess(process_info.hProcess, &rc);
	::CloseHandle(process_info.hThread);
	::CloseHandle(process_info.hProcess);
	return rc;
}

// Run the script. If it is a perl script, run it through the bundled perl interpreter.
// If it is a batch file, run it through the cmd.exe.
// Otherwise run it directly.
static int run_script_win32(const std::string &script, const std::string &gcode)
{
    // Unpack the argument list provided by the user.
    int     nArgs;
    LPWSTR *szArglist = CommandLineToArgvW(boost::nowide::widen(script).c_str(), &nArgs);
    if (szArglist == nullptr || nArgs <= 0) {
        // CommandLineToArgvW failed. Maybe the command line escapment is invalid?
		throw std::runtime_error(std::string("Post processing script ") + script + " on file " + gcode + " failed. CommandLineToArgvW() refused to parse the command line path.");
    }

    std::wstring command_line;
    std::wstring command = szArglist[0];
	if (! boost::filesystem::exists(boost::filesystem::path(command)))
		throw std::runtime_error(std::string("The configured post-processing script does not exist: ") + boost::nowide::narrow(command));
    if (boost::iends_with(command, L".pl")) {
        // This is a perl script. Run it through the perl interpreter.
        // The current process may be slic3r.exe or slic3r-console.exe.
        // Find the path of the process:
        wchar_t wpath_exe[_MAX_PATH + 1];
        ::GetModuleFileNameW(nullptr, wpath_exe, _MAX_PATH);
        boost::filesystem::path path_exe(wpath_exe);
        boost::filesystem::path path_perl = path_exe.parent_path() / "perl" / "perl.exe";
        if (! boost::filesystem::exists(path_perl)) {
			LocalFree(szArglist);
			throw std::runtime_error(std::string("Perl interpreter ") + path_perl.string() + " does not exist.");
        }
        // Replace it with the current perl interpreter.
        quote_argv_winapi(boost::nowide::widen(path_perl.string()), command_line);
        command_line += L" ";
    } else if (boost::iends_with(command, ".bat")) {
        // Run a batch file through the command line interpreter.
        command_line = L"cmd.exe /C ";
    }

    for (int i = 0; i < nArgs; ++ i) {
        quote_argv_winapi(szArglist[i], command_line);
        command_line += L" ";
    }
    LocalFree(szArglist);
	quote_argv_winapi(boost::nowide::widen(gcode), command_line);
    return (int)execute_process_winapi(command_line);
}

#else
    #include <sys/stat.h> //for getting filesystem UID/GID
    #include <unistd.h> //for getting current UID/GID
#endif

namespace Slic3r {

void run_post_process_scripts(const std::string &path, const PrintConfig &config)
{
    if (config.post_process.values.empty())
        return;

    config.setenv_();
    auto gcode_file = boost::filesystem::path(path);
    if (! boost::filesystem::exists(gcode_file))
        throw std::runtime_error(std::string("Post-processor can't find exported gcode file"));

    for (const std::string &scripts : config.post_process.values) {
		std::vector<std::string> lines;
		boost::split(lines, scripts, boost::is_any_of("\r\n"));
        for (std::string script : lines) {
            // Ignore empty post processing script lines.
            boost::trim(script);
            if (script.empty())
                continue;
            BOOST_LOG_TRIVIAL(info) << "Executing script " << script << " on file " << path;
#ifdef WIN32
            int result = run_script_win32(script, gcode_file.string());
#else
            //FIXME testing existence of a script is risky, as the script line may contain the script and some additional command line parameters.
            // We would have to process the script line into parameters before testing for the existence of the command, the command may be looked up
            // in the PATH etc.
            if (! boost::filesystem::exists(boost::filesystem::path(script)))
                throw std::runtime_error(std::string("The configured post-processing script does not exist: ") + script);
            struct stat info;
            if (stat(script.c_str(), &info))
                throw std::runtime_error(std::string("Cannot read information for post-processing script: ") + script);
            boost::filesystem::perms script_perms = boost::filesystem::status(script).permissions();
            //if UID matches, check UID perm. else if GID matches, check GID perm. Otherwise check other perm.
            if (!(script_perms & ((info.st_uid == geteuid()) ? boost::filesystem::perms::owner_exe
                               : ((info.st_gid == getegid()) ? boost::filesystem::perms::group_exe
                                                             : boost::filesystem::perms::others_exe))))
                throw std::runtime_error(std::string("The configured post-processing script is not executable: check permissions. ") + script);
    		int result = boost::process::system(script, gcode_file);
    		if (result < 0)
    			BOOST_LOG_TRIVIAL(error) << "Script " << script << " on file " << path << " failed. Negative error code returned.";
#endif
        }
    }
}

} // namespace Slic3r
