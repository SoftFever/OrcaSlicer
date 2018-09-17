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

namespace Slic3r {

void run_post_process_scripts(const std::string &path, const PrintConfig &config)
{
    if (config.post_process.values.empty())
        return;
    config.setenv_();
    for (std::string script: config.post_process.values) {
        // Ignore empty post processing script lines.
        boost::trim(script);
        if (script.empty())
            continue;
        BOOST_LOG_TRIVIAL(info) << "Executing script " << script << " on file " << path;
        if (! boost::filesystem::exists(boost::filesystem::path(path)))
            throw std::runtime_exception(std::string("The configured post-processing script does not exist: ") + path);
#ifndef WIN32
        file_status fs = boost::filesystem::status(path);
        //FIXME test if executible by the effective UID / GID.
        // throw std::runtime_exception(std::string("The configured post-processing script is not executable: check permissions. ") + path));
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
            result = boost::process::system((path_exe.parent_path() / "perl5.24.0.exe").string(), script, output_file);
        } else
#else
        result = boost::process::system(script, output_file);
#endif
        if (result < 0)
            BOOST_LOG_TRIVIAL(error) << "Script " << script << " on file " << path << " failed. Negative error code returned.";
    }
}

} // namespace Slic3r

#endif
