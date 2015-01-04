#ifndef slic3r_GCodeSender_hpp_
#define slic3r_GCodeSender_hpp_
#ifdef BOOST_LIBS

#include <myinit.h>
#include <queue>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

namespace Slic3r {

namespace asio = boost::asio;

class GCodeSender : private boost::noncopyable {
    public:
    GCodeSender();
    ~GCodeSender();
    bool connect(std::string devname, unsigned int baud_rate);
    void send(const std::vector<std::string> &lines, bool priority = false);
    void send(const std::string &s, bool priority = false);
    void disconnect();
    bool error_status() const;
    bool is_connected() const;
    size_t queue_size() const;
    void pause_queue();
    void resume_queue();
    
    private:
    asio::io_service io;
    asio::serial_port serial;
    boost::thread background_thread;
    boost::asio::streambuf read_buffer;
    bool open;      // whether the serial socket is connected
    bool connected; // whether the printer is online
    bool error;
    mutable boost::mutex error_mutex;
    
    // this mutex guards queue, priqueue, can_send, queue_paused, sent, last_sent
    mutable boost::mutex queue_mutex;
    std::queue<std::string> queue, priqueue;
    bool can_send;
    bool queue_paused;
    size_t sent;
    std::string last_sent;
    
    void set_baud_rate(unsigned int baud_rate);
    void set_error_status(bool e);
    void do_send(const std::string &line);
    void do_close();
    void do_read();
    void on_read(const boost::system::error_code& error, size_t bytes_transferred);
    void send();
};

}

#endif
#endif
