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

        TCPConsole::TCPConsole() : resolver_(io_context_), socket_(io_context_)
        {
            set_defaults();
        }

        TCPConsole::TCPConsole(const std::string& host_name, const std::string& port_name) :
            resolver_(io_context_), socket_(io_context_)
        {
            set_defaults();
            set_remote(host_name, port_name);
        }

        void TCPConsole::transmit_next_command()
        {
            if (cmd_queue_.empty()) {
                io_context_.stop();
                return;
            }

            std::string cmd = cmd_queue_.front();
            cmd_queue_.pop_front();

            BOOST_LOG_TRIVIAL(debug) << boost::format("TCPConsole: transmitting '%3%' to %1%:%2%")
                % host_name_
                % port_name_
                % cmd;


            send_buffer_ = cmd + newline_;

            set_deadline_in(write_timeout_);
            boost::asio::async_write(
                socket_,
                boost::asio::buffer(send_buffer_),
                boost::bind(&TCPConsole::handle_write, this, _1, _2)
            );
        }

        void TCPConsole::wait_next_line()
        {
            set_deadline_in(read_timeout_);
            boost::asio::async_read_until(
                socket_,
                recv_buffer_,
                newline_,
                boost::bind(&TCPConsole::handle_read, this, _1, _2)
            );
        }

        // TODO: Use std::optional here
        std::string TCPConsole::extract_next_line()
        {
            char linebuf[1024];

            std::istream is(&recv_buffer_);
            is.getline(linebuf, sizeof(linebuf));
            if (is.good()) {
                return linebuf;
            }

            return "";
        }

        void TCPConsole::handle_read(
            const boost::system::error_code& ec,
            std::size_t bytes_transferred)
        {
            error_code_ = ec;

            if (ec) {
                BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Can't read from %1%:%2%: %3%")
                    % host_name_
                    % port_name_
                    % ec.message();

                io_context_.stop();
            }
            else {
                std::string line = extract_next_line();
                boost::trim(line);

                BOOST_LOG_TRIVIAL(debug) << boost::format("TCPConsole: received '%3%' from %1%:%2%")
                    % host_name_
                    % port_name_
                    % line;

                boost::to_lower(line);

                if (line == done_string_) {
                    transmit_next_command();
                }
                else {
                    wait_next_line();
                }
            }
        }

        void TCPConsole::handle_write(
            const boost::system::error_code& ec,
            std::size_t)
        {
            error_code_ = ec;
            if (ec) {
                BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Can't write to %1%:%2%: %3%")
                    % host_name_
                    % port_name_
                    % ec.message();

                io_context_.stop();
            }
            else {
                wait_next_line();
            }
        }

        void TCPConsole::handle_connect(const boost::system::error_code& ec)
        {
            error_code_ = ec;

            if (ec) {
                BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Can't connect to %1%:%2%: %3%")
                    % host_name_
                    % port_name_
                    % ec.message();

                io_context_.stop();
            }
            else {
                is_connected_ = true;
                BOOST_LOG_TRIVIAL(info) << boost::format("TCPConsole: connected to %1%:%2%")
                    % host_name_
                    % port_name_;


                transmit_next_command();
            }
        }

        void TCPConsole::set_deadline_in(std::chrono::steady_clock::duration d)
        {
            deadline_ = std::chrono::steady_clock::now() + d;
        }
        bool TCPConsole::is_deadline_over()
        {
            return deadline_ < std::chrono::steady_clock::now();
        }

        bool TCPConsole::run_queue()
        {
            auto now = std::chrono::steady_clock::now();
            try {
                // TODO: Add more resets and initializations after previous run (reset() method?..)
                set_deadline_in(connect_timeout_);
                is_connected_ = false;
                io_context_.restart();

                auto endpoints = resolver_.resolve(host_name_, port_name_);

                socket_.async_connect(endpoints->endpoint(),
                    boost::bind(&TCPConsole::handle_connect, this, _1)
                );

                // Loop until we get any reasonable result. Negative result is also result.
                // TODO: Rewrite to more graceful way using deadlime_timer
                bool timeout = false;
                while (!(timeout = is_deadline_over()) && !io_context_.stopped()) {
                    if (error_code_) {
                        io_context_.stop();
                    }
                    io_context_.run_for(boost::asio::chrono::milliseconds(100));
                }

                // Override error message if timeout is set
                if (timeout) {
                    error_code_ = make_error_code(boost::asio::error::timed_out);
                }

                // Socket is not closed automatically by boost
                socket_.close();

                if (error_code_) {
                    // We expect that message is logged in handler
                    return false;
                }

                // It's expected to have empty queue after successful exchange
                if (!cmd_queue_.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "TCPConsole: command queue is not empty after end of exchange";
                    return false;
                }
            }
            catch (std::exception& e)
            {
                BOOST_LOG_TRIVIAL(error) << boost::format("TCPConsole: Exception while talking with %1%:%2%: %3%")
                    % host_name_
                    % port_name_
                    % e.what();

                return false;
            }

            return true;
        }


    }
}
