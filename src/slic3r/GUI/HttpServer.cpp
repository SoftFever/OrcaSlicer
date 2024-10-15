#include "HttpServer.hpp"
#include <boost/log/trivial.hpp>
#include "GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

namespace Slic3r {
namespace GUI {

static std::string parse_params(std::string url, std::string key)
{
    size_t start = url.find(key);
    if (start < 0) return "";
    size_t eq = url.find('=', start);
    if (eq < 0) return "";
    std::string key_str = url.substr(start, eq - start);
    if (key_str != key)
        return "";
    start += key.size() + 1;
    size_t end = url.find('&', start);
    if (end < 0)
        return "";
    std::string result = url.substr(start, end - start);
    return result;
}

std::string http_headers::get_response()
{
    BOOST_LOG_TRIVIAL(info) << "thirdparty_login: get_response";
    std::stringstream ssOut;
    std::string url_str = Http::url_decode(url);
    if (boost::contains(url_str, "access_token")) {
        std::string sHTML = "<html><body><p>redirect to url </p></body></html>";
        std::string redirect_url = parse_params(url_str, "redirect_url");
        std::string access_token = parse_params(url_str, "access_token");
        std::string refresh_token = parse_params(url_str, "refresh_token");
        std::string expires_in_str = parse_params(url_str, "expires_in");
        std::string refresh_expires_in_str = parse_params(url_str, "refresh_expires_in");
        NetworkAgent* agent = wxGetApp().getAgent();

        unsigned int http_code;
        std::string http_body;
        int result = agent->get_my_profile(access_token, &http_code, &http_body);
        if (result == 0) {
            std::string user_id;
            std::string user_name;
            std::string user_account;
            std::string user_avatar;
            try {
                json user_j = json::parse(http_body);
                if (user_j.contains("uidStr"))
                    user_id = user_j["uidStr"].get<std::string>();
                if (user_j.contains("name"))
                    user_name = user_j["name"].get<std::string>();
                if (user_j.contains("avatar"))
                    user_avatar = user_j["avatar"].get<std::string>();
                if (user_j.contains("account"))
                    user_account = user_j["account"].get<std::string>();
            } catch (...) {
                ;
            }
            json j;
            j["data"]["refresh_token"] = refresh_token;
            j["data"]["token"] = access_token;
            j["data"]["expires_in"] = expires_in_str;
            j["data"]["refresh_expires_in"] = refresh_expires_in_str;
            j["data"]["user"]["uid"] = user_id;
            j["data"]["user"]["name"] = user_name;
            j["data"]["user"]["account"] = user_account;
            j["data"]["user"]["avatar"] = user_avatar;
            agent->change_user(j.dump());
            if (agent->is_user_login()) {
                wxGetApp().request_user_login(1);
            }
            GUI::wxGetApp().CallAfter([this] {
                wxGetApp().ShowUserLogin(false);
            });
            std::string location_str = (boost::format("Location: %1%?result=success") % redirect_url).str();
            ssOut << "HTTP/1.1 302 Found" << std::endl;
            ssOut << location_str << std::endl;
            ssOut << "content-type: text/html" << std::endl;
            ssOut << "content-length: " << sHTML.length() << std::endl;
            ssOut << std::endl;
            ssOut << sHTML;
        } else {
            std::string error_str = "get_user_profile_error_" + std::to_string(result);
            std::string location_str = (boost::format("Location: %1%?result=fail&error=%2%") % redirect_url % error_str).str();
            ssOut << "HTTP/1.1 302 Found" << std::endl;
            ssOut << location_str << std::endl;
            ssOut << "content-type: text/html" << std::endl;
            ssOut << "content-length: " << sHTML.length() << std::endl;
            ssOut << std::endl;
            ssOut << sHTML;
        }
    } else {
        std::string sHTML = "<html><body><h1>404 Not Found</h1><p>There's nothing here.</p></body></html>";
        ssOut << "HTTP/1.1 404 Not Found" << std::endl;
        ssOut << "content-type: text/html" << std::endl;
        ssOut << "content-length: " << sHTML.length() << std::endl;
        ssOut << std::endl;
        ssOut << sHTML;
    }
    return ssOut.str();
}


void accept_and_run(boost::asio::ip::tcp::acceptor& acceptor, boost::asio::io_service& io_service)
{
    std::shared_ptr<session> sesh = std::make_shared<session>(io_service);
    acceptor.async_accept(sesh->socket,
        [sesh, &acceptor, &io_service](const boost::beast::error_code& accept_error)
        {
            accept_and_run(acceptor, io_service);
            if (!accept_error)
            {
                session::interact(sesh);
            }
        });
}

HttpServer::HttpServer()
{
    ;
}

void HttpServer::start()
{
    BOOST_LOG_TRIVIAL(info) << "start_http_service...";
    start_http_server = true;
    m_http_server_thread = Slic3r::create_thread(
        [this] {
            boost::asio::io_service io_service;
            boost::asio::ip::tcp::endpoint endpoint{ boost::asio::ip::tcp::v4(), LOCALHOST_PORT};
            boost::asio::ip::tcp::acceptor acceptor { io_service, endpoint};
            acceptor.listen();
            accept_and_run(acceptor, io_service);
            while (start_http_server) {
                io_service.run();
            }
        });
}

void HttpServer::stop()
{
    start_http_server = false;
    if (m_http_server_thread.joinable())
        m_http_server_thread.join();
}

} // GUI
} //Slic3r
