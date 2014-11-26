#ifdef BOOST_LIBS
#include "GCodeSender.hpp"
#include <iostream>

namespace Slic3r {

namespace asio = boost::asio;

GCodeSender::GCodeSender(std::string devname, unsigned int baud_rate)
    : io(), serial(io)
{
    this->serial.open(devname);
    this->serial.set_option(asio::serial_port_base::baud_rate(baud_rate));
    this->serial.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::odd));
    this->serial.set_option(asio::serial_port_base::character_size(asio::serial_port_base::character_size(8)));
    this->serial.set_option(asio::serial_port_base::flow_control(asio::serial_port_base::flow_control::none));
    this->serial.set_option(asio::serial_port_base::stop_bits(asio::serial_port_base::stop_bits::one));
    this->serial.close();
    this->serial.open(devname);
    this->serial.set_option(asio::serial_port_base::parity(asio::serial_port_base::parity::none));
    
    std::string greeting;
    this->read_line(&greeting);
}

void
GCodeSender::send(const std::vector<std::string> &lines)
{
    this->lines = lines;
}

void
GCodeSender::send(const std::string &s)
{
    {
        std::stringstream ss(s);
        std::string line;
        while (std::getline(ss, line, '\n'))
            this->lines.push_back(line);
    }
    
    for (std::vector<std::string>::const_iterator line = this->lines.begin(); line != this->lines.end(); ++line) {
        this->send_line(*line);
    }
}

void
GCodeSender::send_line(const std::string &line)
{
    asio::streambuf b;
    std::ostream os(&b);
    os << line << "\n";
    asio::write(this->serial, b);
}

void
GCodeSender::read_line(std::string* line)
{
    for (;;) {
        char c;
        asio::read(this->serial, asio::buffer(&c, 1));
        switch (c) {
            case '\r':
                break;
            case '\n':
                return;
            default:
                *line += c;
        }
    }
}

#ifdef SLIC3RXS
REGISTER_CLASS(GCodeSender, "GCode::Sender");
#endif

}
#endif
