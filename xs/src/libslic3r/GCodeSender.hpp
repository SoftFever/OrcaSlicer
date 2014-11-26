#ifndef slic3r_GCodeSender_hpp_
#define slic3r_GCodeSender_hpp_
#ifdef BOOST_LIBS

#include <myinit.h>
#include <string>
#include <vector>
#include <boost/asio.hpp>

namespace Slic3r {

namespace asio = boost::asio;

class GCodeSender {
    public:
    GCodeSender(std::string devname, unsigned int baud_rate);
    void send(const std::vector<std::string> &lines);
    void send(const std::string &s);
    
    
    private:
    asio::io_service io;
    asio::serial_port serial;
    std::vector<std::string> lines;
    
    void send_line(const std::string &line);
    void read_line(std::string* line);
};

}

#endif
#endif
