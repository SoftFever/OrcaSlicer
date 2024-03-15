#ifndef slic3r_Http_App_hpp_
#define slic3r_Http_App_hpp_

#include <iostream>
#include <mutex>
#include <stack>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <string>
#include <memory>

using namespace boost::system;
using namespace boost::asio;

#define LOCALHOST_PORT      13618
#define LOCALHOST_URL       "http://localhost:"

namespace Slic3r {
namespace GUI {

class http_headers
{
    std::string method;
    std::string url;
    std::string version;

    std::map<std::string, std::string> headers;

public:

    std::string get_response();

    int content_length()
    {
        auto request = headers.find("content-length");
        if (request != headers.end())
        {
            std::stringstream ssLength(request->second);
            int content_length;
            ssLength >> content_length;
            return content_length;
        }
        return 0;
    }

    void on_read_header(std::string line)
    {
        //std::cout << "header: " << line << std::endl;

        std::stringstream ssHeader(line);
        std::string headerName;
        std::getline(ssHeader, headerName, ':');

        std::string value;
        std::getline(ssHeader, value);
        headers[headerName] = value;
    }

    void on_read_request_line(std::string line)
    {
        std::stringstream ssRequestLine(line);
        ssRequestLine >> method;
        ssRequestLine >> url;
        ssRequestLine >> version;

        std::cout << "request for resource: " << url << std::endl;
    }
};

class session
{
    boost::asio::streambuf buff;
    http_headers headers;

    static void read_body(std::shared_ptr<session> pThis)
    {
        int nbuffer = 1000;
        std::shared_ptr<std::vector<char>> bufptr = std::make_shared<std::vector<char>>(nbuffer);
        boost::asio::async_read(pThis->socket, boost::asio::buffer(*bufptr, nbuffer), [pThis](const boost::beast::error_code& e, std::size_t s)
            {
            });
    }

    static void read_next_line(std::shared_ptr<session> pThis)
    {
        boost::asio::async_read_until(pThis->socket, pThis->buff, '\r', [pThis](const boost::beast::error_code& e, std::size_t s)
            {
                std::string line, ignore;
                std::istream stream{ &pThis->buff };
                std::getline(stream, line, '\r');
                std::getline(stream, ignore, '\n');
                pThis->headers.on_read_header(line);

                if (line.length() == 0)
                {
                    if (pThis->headers.content_length() == 0)
                    {
                        std::shared_ptr<std::string> str = std::make_shared<std::string>(pThis->headers.get_response());
                        boost::asio::async_write(pThis->socket, boost::asio::buffer(str->c_str(), str->length()), [pThis, str](const boost::beast::error_code& e, std::size_t s)
                            {
                                std::cout << "done" << std::endl;
                            });
                    }
                    else
                    {
                        pThis->read_body(pThis);
                    }
                }
                else
                {
                    pThis->read_next_line(pThis);
                }
            });
    }

    static void read_first_line(std::shared_ptr<session> pThis)
    {
        boost::asio::async_read_until(pThis->socket, pThis->buff, '\r', [pThis](const boost::beast::error_code& e, std::size_t s)
            {
                std::string line, ignore;
                std::istream stream{ &pThis->buff };
                std::getline(stream, line, '\r');
                std::getline(stream, ignore, '\n');
                pThis->headers.on_read_request_line(line);
                pThis->read_next_line(pThis);
            });
    }

public:
    boost::asio::ip::tcp::socket socket;

    session(io_service& io_service)
        :socket(io_service)
    {
    }

    static void interact(std::shared_ptr<session> pThis)
    {
        read_first_line(pThis);
    }
};

class HttpServer {
public:
    HttpServer();

    boost::thread    m_http_server_thread;
    bool             start_http_server = false;

    bool            is_started() { return start_http_server; }
    void            start();
    void            stop();
};

}
};

#endif
