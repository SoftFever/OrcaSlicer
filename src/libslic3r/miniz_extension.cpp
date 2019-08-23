#include "miniz_extension.hpp"

#if defined(_MSC_VER) || defined(__MINGW64__)
#include "boost/nowide/cstdio.hpp"
#endif

namespace Slic3r {

namespace {
bool open_zip(mz_zip_archive *zip, const char *fname, bool isread)
{
    if (!zip) return false;
    const char *mode = isread ? "rb" : "wb";

    FILE *f = nullptr;
#if defined(_MSC_VER) || defined(__MINGW64__)
    f = boost::nowide::fopen(fname, mode);
#elif defined(__GNUC__) && defined(_LARGEFILE64_SOURCE)
    f = fopen64(fname, mode);
#else
    f = fopen(fname, mode);
#endif

    if (!f) {
        zip->m_last_error = MZ_ZIP_FILE_OPEN_FAILED;
        return false;
    }

    return isread ? mz_zip_reader_init_cfile(zip, f, 0, 0)
                  : mz_zip_writer_init_cfile(zip, f, 0);
}

bool close_zip(mz_zip_archive *zip, bool isread)
{
    bool ret = false;
    if (zip) {
        FILE *f = mz_zip_get_cfile(zip);
        ret     = bool(isread ? mz_zip_reader_end(zip)
                          : mz_zip_writer_end(zip));
        if (f) fclose(f);
    }
    return ret;
}
}

bool open_zip_reader(mz_zip_archive *zip, const std::string &fname)
{
    return open_zip(zip, fname.c_str(), true);
}

bool open_zip_writer(mz_zip_archive *zip, const std::string &fname)
{
    return open_zip(zip, fname.c_str(), false);
}

bool close_zip_reader(mz_zip_archive *zip) { return close_zip(zip, true); }
bool close_zip_writer(mz_zip_archive *zip) { return close_zip(zip, false); }

}
