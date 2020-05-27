#ifndef SLAIMPORT_HPP
#define SLAIMPORT_HPP

#include <functional>

#include <libslic3r/Point.hpp>
#include <libslic3r/PrintConfig.hpp>

namespace Slic3r {

class TriangleMesh;
class DynamicPrintConfig;

void import_sla_archive(const std::string &zipfname, DynamicPrintConfig &out);

void import_sla_archive(
    const std::string &      zipfname,
    Vec2i                    windowsize,
    TriangleMesh &           out,
    DynamicPrintConfig &     profile,
    std::function<bool(int)> progr = [](int) { return true; });

inline void import_sla_archive(
    const std::string &      zipfname,
    Vec2i                    windowsize,
    TriangleMesh &           out,
    std::function<bool(int)> progr = [](int) { return true; })
{
    DynamicPrintConfig profile;
    import_sla_archive(zipfname, windowsize, out, profile, progr);
}

}

#endif // SLAIMPORT_HPP
