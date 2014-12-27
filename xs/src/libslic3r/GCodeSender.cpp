#ifdef BOOST_LIBS
#include "GCodeSender.hpp"
#include <iostream>
#include <istream>
#include <string>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/lexical_cast.hpp>

namespace Slic3r {

namespace asio = boost::asio;

GCodeSender::GCodeSender(std::string devname, unsigned int baud_rate)
    : io(), serial(io), can_send(false), sent(0), error(false), connected(false)
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
    this->open = true;
    
    // this gives some work to the io_service before it is started
    // (post() runs the supplied function in its thread)
    this->io.post(boost::bind(&GCodeSender::do_read, this));
    
    // start reading in the background thread
    boost::thread t(boost::bind(&asio::io_service::run, &this->io));
    this->background_thread.swap(t);
}

void
GCodeSender::disconnect()
{
    if (!this->open) return;

    this->open = false;
    this->connected = false;
    this->io.post(boost::bind(&GCodeSender::do_close, this));
    this->background_thread.join();
    this->io.reset();
    if (this->error_status()) {
        throw(boost::system::system_error(boost::system::error_code(),
            "Error while closing the device"));
    }
}

bool
GCodeSender::is_connected() const
{
    return this->connected;
}

size_t
GCodeSender::queue_size() const
{
    boost::lock_guard<boost::mutex> l(this->queue_mutex);
    return this->queue.size();
}

void
GCodeSender::do_close()
{
    boost::system::error_code ec;
    this->serial.cancel(ec);
    if (ec) this->set_error_status(true);
    this->serial.close(ec);
    if (ec) this->set_error_status(true);
}

void
GCodeSender::set_error_status(bool e)
{
    boost::lock_guard<boost::mutex> l(this->error_mutex);
    this->error = e;
}

bool
GCodeSender::error_status() const
{
    boost::lock_guard<boost::mutex> l(this->error_mutex);
    return this->error;
}

void
GCodeSender::do_read()
{
    // read one line
    asio::async_read_until(
        this->serial,
        this->read_buffer,
        '\n',
        boost::bind(
            &GCodeSender::on_read,
            this,
            asio::placeholders::error,
            asio::placeholders::bytes_transferred
        )
    );
}

void
GCodeSender::on_read(const boost::system::error_code& error,
    size_t bytes_transferred)
{
    if (error) {
        // error can be true even because the serial port was closed.
        // In this case it is not a real error, so ignore.
        if (this->open) {
            this->do_close();
            this->set_error_status(true);
        }
        return;
    }
    
    // copy the read buffer into string
    std::string line((std::istreambuf_iterator<char>(&this->read_buffer)), 
        std::istreambuf_iterator<char>());
    
    // parse incoming line
    if (!this->connected
        && (boost::starts_with(line, "start")
         || boost::starts_with(line, "Grbl "))) {
        this->connected = true;
        this->can_send = true;
        this->send();
    } else if (boost::starts_with(line, "ok")) {
        {
            boost::lock_guard<boost::mutex> l(this->queue_mutex);
            this->queue.pop();
        }
        this->can_send = true;
        this->send();
    } else if (boost::istarts_with(line, "resend")
            || boost::istarts_with(line, "rs")) {
        // extract the first number from line
        using boost::lexical_cast;
        using boost::bad_lexical_cast;
        boost::algorithm::trim_left_if(line, !boost::algorithm::is_digit());
        size_t toresend = lexical_cast<size_t>(line.substr(0, line.find_first_not_of("0123456789")));
        if (toresend == this->sent) {
            this->sent--;
            this->can_send = true;
            this->send();
        } else {
            printf("Cannot resend %lu (last was %lu)\n", toresend, this->sent);
        }
    }
    
    this->do_read();
}

void
GCodeSender::send(const std::vector<std::string> &lines)
{
    // append lines to queue
    {
        boost::lock_guard<boost::mutex> l(this->queue_mutex);
        for (std::vector<std::string>::const_iterator line = lines.begin(); line != lines.end(); ++line)
            this->queue.push(*line);
    }
    this->send();
}

void
GCodeSender::send(const std::string &line)
{
    // append line to queue
    {
        boost::lock_guard<boost::mutex> l(this->queue_mutex);
        this->queue.push(line);
    }
    this->send();
}

void
GCodeSender::send()
{
    // printer is not connected or we're still waiting for the previous ack
    if (!this->can_send) return;
    
    boost::lock_guard<boost::mutex> l(this->queue_mutex);
    if (this->queue.empty()) return;
    
    // get line and strip any comment
    std::string line = this->queue.front();
    if (size_t comment_pos = line.find_first_of(';') != std::string::npos)
        line.erase(comment_pos, std::string::npos);
    boost::algorithm::trim(line);
    
    // calculate checksum
    int cs = 0;
    for (std::string::const_iterator it = line.begin(); it != line.end(); ++it)
       cs = cs ^ *it;
    
    sent++;
    asio::streambuf b;
    std::ostream os(&b);
    os << "N" << sent << " " << line
       << "*" << cs << "\n";
    asio::write(this->serial, b);
    this->can_send = false;
}

#ifdef SLIC3RXS
REGISTER_CLASS(GCodeSender, "GCode::Sender");
#endif

}
#endif
