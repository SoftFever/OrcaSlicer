#ifndef slic3r_Bonjour_hpp_
#define slic3r_Bonjour_hpp_

#include <memory>
#include <string>
#include <functional>
// #include <ostream>
#include <boost/asio/ip/address.hpp>


namespace Slic3r {


// TODO: reply data structure
struct BonjourReply
{
	boost::asio::ip::address ip;
	std::string service_name;
	std::string hostname;
	std::string path;
	std::string version;

	BonjourReply(boost::asio::ip::address ip, std::string service_name, std::string hostname);
};

std::ostream& operator<<(std::ostream &, const BonjourReply &);


/// Bonjour lookup performer
class Bonjour : public std::enable_shared_from_this<Bonjour> {
private:
	struct priv;
public:
	typedef std::shared_ptr<Bonjour> Ptr;
	typedef std::function<void(BonjourReply &&reply)> ReplyFn;
	typedef std::function<void()> CompleteFn;

	Bonjour(std::string service, std::string protocol = "tcp");
	Bonjour(Bonjour &&other);
	~Bonjour();

	Bonjour& set_timeout(unsigned timeout);
	Bonjour& on_reply(ReplyFn fn);
	Bonjour& on_complete(CompleteFn fn);

	Ptr lookup();

	static void pokus();    // XXX: remove
private:
	std::unique_ptr<priv> p;
};


}

#endif
