#include <exception>
#include <sstream>
#include <iostream>

#include "Zipper.hpp"
#include "miniz/miniz_zip.h"
#include <boost/filesystem/path.hpp>

namespace Slic3r {

class Zipper::Impl {
public:
    mz_zip_archive arch;
};

Zipper::Zipper(const std::string &zipfname, e_compression compression)
{
    m_impl.reset(new Impl());

    memset(&m_impl->arch, 0, sizeof(m_impl->arch));

    // Initialize the archive data
    if(!mz_zip_writer_init_file(&m_impl->arch, zipfname.c_str(), 0))
        throw std::runtime_error("Cannot open zip archive!");

    m_compression = compression;
    m_zipname = zipfname;
}

Zipper::~Zipper()
{
    finish_entry();
    mz_zip_writer_finalize_archive(&m_impl->arch);
    mz_zip_writer_end(&m_impl->arch);
}

void Zipper::add_entry(const std::string &name)
{
    finish_entry(); // finish previous business
    m_entry = name;
}

void Zipper::finish_entry()
{
    if(!m_data.empty() > 0 && !m_entry.empty()) {
        mz_uint compression = MZ_NO_COMPRESSION;

        switch (m_compression) {
        case NO_COMPRESSION: compression = MZ_NO_COMPRESSION; break;
        case FAST_COMPRESSION: compression = MZ_BEST_SPEED; break;
        case TIGHT_COMPRESSION: compression = MZ_BEST_COMPRESSION; break;
        }

        mz_zip_writer_add_mem(&m_impl->arch, m_entry.c_str(),
                              m_data.c_str(),
                              m_data.size(),
                              compression);
    }

    m_data.clear();
    m_entry.clear();
}

std::string Zipper::get_name() const {
    return boost::filesystem::path(m_zipname).stem().string();
}

}
