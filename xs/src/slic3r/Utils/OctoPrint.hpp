#ifndef slic3r_OctoPrint_hpp_
#define slic3r_OctoPrint_hpp_

#include <string>

#include "Http.hpp"

namespace Slic3r {


class DynamicPrintConfig;
class Http;

class OctoPrint
{
public:
    OctoPrint(DynamicPrintConfig *config);

    std::string test() const;
    // XXX: style
    void send_gcode(int windowId, int completeEvt, int errorEvt, const std::string &filename, bool print = false) const;
private:
    std::string host;
    std::string apikey;
    std::string cafile;

    void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;
    static std::string format_error(std::string error, unsigned status);
};


}

#endif
