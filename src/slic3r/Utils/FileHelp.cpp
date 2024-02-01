#include "FileHelp.hpp"
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <regex>
namespace Slic3r {
    namespace Utils {

bool is_file_too_large(std::string file_path, bool &try_ok)
{
    try {
        uintmax_t fileSizeBytes = boost::filesystem::file_size(file_path);
        double    fileSizeMB    = static_cast<double>(fileSizeBytes) / 1024 / 1024;
        try_ok                  = true;
        if (fileSizeMB > STL_SVG_MAX_FILE_SIZE_MB) { return true; }
    } catch (boost::filesystem::filesystem_error &e) {
        try_ok = false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " error message: " << e.what();
    }
    return false;
}

void slash_to_back_slash(std::string &file_path) {
    std::regex regex("\\\\");
    file_path = std::regex_replace(file_path, regex, "/");
}

}} // namespace Slic3r::Utils
