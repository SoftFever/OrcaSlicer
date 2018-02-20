#ifndef slic3r_Bonjour_hpp_
#define slic3r_Bonjour_hpp_

#include <memory>
#include <string>
#include <functional>


namespace Slic3r {


/// Bonjour lookup
class Bonjour : public std::enable_shared_from_this<Bonjour> {
private:
	struct priv;
public:
	typedef std::shared_ptr<Bonjour> Ptr;
	typedef std::function<void(std::string /* IP */, std::string /* host */, std::string /* service_name */)> ReplyFn;
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
