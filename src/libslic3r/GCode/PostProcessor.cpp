#include "PostProcessor.hpp"

#ifdef WIN32

namespace Slic3r {

//FIXME Ignore until we include boost::process
void run_post_process_scripts(const std::string &path, const PrintConfig &config)
{
}

} // namespace Slic3r

#else

#include <boost/process/system.hpp>
#ifndef WIN32
    #include <sys/stat.h> //for getting filesystem UID/GID
    #include <unistd.h> //for getting current UID/GID
#endif

namespace Slic3r {

void run_post_process_scripts(const std::string &path, const PrintConfig &config)
{
    if (config.post_process.values.empty())
        return;
    //config.setenv_();
    auto gcode_file = boost::filesystem::path(path);
    if (!boost::filesystem::exists(gcode_file))
        throw std::runtime_error(std::string("Post-processor can't find exported gcode file"));

    for (std::string script: config.post_process.values) {
        // Ignore empty post processing script lines.
        boost::trim(script);
        if (script.empty())
            continue;
        BOOST_LOG_TRIVIAL(info) << "Executing script " << script << " on file " << path;
        if (! boost::filesystem::exists(boost::filesystem::path(script)))
            throw std::runtime_error(std::string("The configured post-processing script does not exist: ") + script);
#ifndef WIN32
        struct stat info;
        if (stat(script.c_str(), &info))
            throw std::runtime_error(std::string("Cannot read information for post-processing script: ") + script);
        boost::filesystem::perms script_perms = boost::filesystem::status(script).permissions();
        //if UID matches, check UID perm. else if GID matches, check GID perm. Otherwise check other perm.
        if (!(script_perms & ((info.st_uid == geteuid()) ? boost::filesystem::perms::owner_exe
                           : ((info.st_gid == getegid()) ? boost::filesystem::perms::group_exe
                                                         : boost::filesystem::perms::others_exe))))
            throw std::runtime_error(std::string("The configured post-processing script is not executable: check permissions. ") + script);
#endif
        int result = 0;
#ifdef WIN32
        if (boost::iends_with(file, ".gcode")) {
            // The current process may be slic3r.exe or slic3r-console.exe.
            // Find the path of the process:
            wchar_t wpath_exe[_MAX_PATH + 1];
            ::GetModuleFileNameW(nullptr, wpath_exe, _MAX_PATH);
            boost::filesystem::path path_exe(wpath_exe);
            // Replace it with the current perl interpreter.
            result = boost::process::system((path_exe.parent_path() / "perl5.24.0.exe").string(), script, gcode_file);
        } else
#else
        result = boost::process::system(script, gcode_file);
#endif
        if (result < 0)
            BOOST_LOG_TRIVIAL(error) << "Script " << script << " on file " << path << " failed. Negative error code returned.";
    }
}

} // namespace Slic3r

#endif
