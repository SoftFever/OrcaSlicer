#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>

#include <iostream>
#include <string>

#include "TCPConsole.hpp"

using boost::asio::steady_timer;
using boost::asio::ip::tcp;

namespace Slic3r {
namespace Utils {

void TCPConsole::transmit_next_command()
{
    if (m_cmd_queue.empty()) {
        m_io_context.stop();
        return;
    }

    std::string cmd = m_cmd_queue.front();
    m_cmd_queue.pop_front();

    BOOST_LOG_TRIVIAL(debug) << boost::format("TCPConsole: transmitting '%3%' to %1%:%2%")
        % m_host_name
        % m_port_name
        % cmd;

    m_send_buffer = cmd + m_newline;

    set_deadline_in(m_write_timeout);
    boost::asio::async_write(
        m_socket,
        boost::asio::buffer(m_send_buffer),
        boost::bind(&TCPConsole::handle_write, this, _1, _2)
    );
}

void TCPConsole::wait_next_line()
{
    set_deadline_in(m_read_timeout);
    boost::asio::async_read_until(
        m_socket,
        m_recv_buffer,
        m_newline,
        boost::bind(&TCPConsole::handle_read, this, _1, _2)
    );
}

// TODO: Use std::optional here
std::string TCPConsole::extract_next_line()
{
    char linebuf[1024];
    std::istream is(&m_recv_buffer);
    is.getline(linebuf, sizeof(linebuf));
    return is.good() ? linebuf : std::string{};
}

void TCPConsole::handle_read(
    const boost::system::error_code& ec,
    std::size_t bytes_transferred)
{
    m_error_code = ec;

    if (ec) {
        BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Can't read from %1%:%2%: %3%")
            % m_host_name
            % m_port_name
            % ec.message();

        m_io_context.stop();
    }
    else {
        std::string line = extract_next_line();
        boost::trim(line);

        BOOST_LOG_TRIVIAL(debug) << boost::format("TCPConsole: received '%3%' from %1%:%2%")
            % m_host_name
            % m_port_name
            % line;

        boost::to_lower(line);

        if (line == m_done_string)
            transmit_next_command();
        else
            wait_next_line();
    }
}

void TCPConsole::handle_write(
    const boost::system::error_code& ec,
    std::size_t)
{
    m_error_code = ec;
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Can't write to %1%:%2%: %3%")
            % m_host_name
            % m_port_name
            % ec.message();

        m_io_context.stop();
    }
    else {
        wait_next_line();
    }
}

void TCPConsole::handle_connect(const boost::system::error_code& ec)
{
    m_error_code = ec;

    if (ec) {
        BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Can't connect to %1%:%2%: %3%")
            % m_host_name
            % m_port_name
            % ec.message();

        m_io_context.stop();
    }
    else {
        m_is_connected = true;
        BOOST_LOG_TRIVIAL(info) << boost::format("TCPConsole: connected to %1%:%2%")
            % m_host_name
            % m_port_name;

        transmit_next_command();
    }
}

void TCPConsole::set_deadline_in(std::chrono::steady_clock::duration d)
{
    m_deadline = std::chrono::steady_clock::now() + d;
}
bool TCPConsole::is_deadline_over() const
{
    return m_deadline < std::chrono::steady_clock::now();
}

bool TCPConsole::run_queue()
{
    try {
        // TODO: Add more resets and initializations after previous run (reset() method?..)
        set_deadline_in(m_connect_timeout);
        m_is_connected = false;
        m_io_context.restart();

        auto endpoints = m_resolver.resolve(m_host_name, m_port_name);

        m_socket.async_connect(endpoints->endpoint(),
            boost::bind(&TCPConsole::handle_connect, this, _1)
        );

        // Loop until we get any reasonable result. Negative result is also result.
        // TODO: Rewrite to more graceful way using deadlime_timer
        bool timeout = false;
        while (!(timeout = is_deadline_over()) && !m_io_context.stopped()) {
            if (m_error_code) {
                m_io_context.stop();
            }
            m_io_context.run_for(boost::asio::chrono::milliseconds(100));
        }

        // Override error message if timeout is set
        if (timeout)
            m_error_code = make_error_code(boost::asio::error::timed_out);

        // Socket is not closed automatically by boost
        m_socket.close();

        if (m_error_code) {
            // We expect that message is logged in handler
            return false;
        }

        // It's expected to have empty queue after successful exchange
        if (!m_cmd_queue.empty()) {
            BOOST_LOG_TRIVIAL(error) << "TCPConsole: command queue is not empty after end of exchange";
            return false;
        }
    }
    catch (std::exception& e)
    {
        BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Exception while talking with %1%:%2%: %3%")
            % m_host_name
            % m_port_name
            % e.what();

        return false;
    }

    return true;
}

} // namespace Utils
} // namespace Slic3r
