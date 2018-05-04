#include "avrdude-slic3r.hpp"

extern "C" {
#include "ac_cfg.h"
#include "avrdude.h"
}


namespace Slic3r {

namespace AvrDude {


// Used by our custom code in avrdude to receive messages that avrdude normally outputs on stdout (see avrdude_message())
static void avrdude_message_handler_closure(const char *msg, unsigned size, void *user_p)
{
	auto *message_fn = reinterpret_cast<MessageFn*>(user_p);
	(*message_fn)(msg, size);
}

int main(std::vector<std::string> args, std::string sys_config, MessageFn message_fn)
{
	std::vector<char *> c_args {{ const_cast<char*>(PACKAGE_NAME) }};
	for (const auto &arg : args) {
		c_args.push_back(const_cast<char*>(arg.data()));
	}

	::avrdude_message_handler_set(avrdude_message_handler_closure, reinterpret_cast<void*>(&message_fn));
	const auto res = ::avrdude_main(static_cast<int>(c_args.size()), c_args.data(), sys_config.c_str());
	::avrdude_message_handler_set(nullptr, nullptr);
	return res;
}

int main(std::vector<std::string> args, std::string sys_config, std::ostream &os)
{
	return main(std::move(args), std::move(sys_config), std::move([&os](const char *msg, unsigned /* size */) {
		os << msg;
	}));
}


}

}
