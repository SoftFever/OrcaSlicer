#ifndef MINIZ_EXTENSION_HPP
#define MINIZ_EXTENSION_HPP

#include <string>
#include <miniz.h>

namespace Slic3r {

bool open_zip_reader(mz_zip_archive *zip, const std::string &fname_utf8);
bool open_zip_writer(mz_zip_archive *zip, const std::string &fname_utf8);
bool close_zip_reader(mz_zip_archive *zip);
bool close_zip_writer(mz_zip_archive *zip);

}

#endif // MINIZ_EXTENSION_HPP
