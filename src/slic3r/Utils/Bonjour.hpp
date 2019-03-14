#ifndef slic3r_Bonjour_hpp_
#define slic3r_Bonjour_hpp_

#include <cstdint>
#include <memory>
#include <string>
#include <set>
#include <unordered_map>
#include <functional>
#include <boost/asio/ip/address.hpp>


namespace Slic3r {


struct BonjourReply
{
	typedef std::unordered_map<std::string, std::string> TxtData;

	boost::asio::ip::address ip;
	uint16_t port;
	std::string service_name;
	std::string hostname;
	std::string full_address;

	TxtData txt_data;

	BonjourReply() = delete;
	BonjourReply(boost::asio::ip::address ip,
		uint16_t port,
		std::string service_name,
		std::string hostname,
		TxtData txt_data);

	std::string path() const;

	bool operator==(const BonjourReply &other) const;
	bool operator<(const BonjourReply &other) const;
};

std::ostream& operator<<(std::ostream &, const BonjourReply &);


/// Bonjour lookup performer
class Bonjour : public std::enable_shared_from_this<Bonjour> {
private:
	struct priv;
public:
	typedef std::shared_ptr<Bonjour> Ptr;
	typedef std::function<void(BonjourReply &&)> ReplyFn;
	typedef std::function<void()> CompleteFn;
	typedef std::set<std::string> TxtKeys;

	Bonjour(std::string service);
	Bonjour(Bonjour &&other);
	~Bonjour();

	// Set requested service protocol, "tcp" by default
	Bonjour& set_protocol(std::string protocol);
	// Set which TXT key-values should be collected
	// Note that "path" is always collected
	Bonjour& set_txt_keys(TxtKeys txt_keys);
	Bonjour& set_timeout(unsigned timeout);
	Bonjour& set_retries(unsigned retries);
	// ^ Note: By default there is 1 retry (meaning 1 broadcast is sent).
	// Timeout is per one retry, ie. total time spent listening = retries * timeout.
	// If retries > 1, then care needs to be taken as more than one reply from the same service may be received.

	Bonjour& on_reply(ReplyFn fn);
	Bonjour& on_complete(CompleteFn fn);

	Ptr lookup();
private:
	std::unique_ptr<priv> p;
};


}

#endif
