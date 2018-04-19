#include "avrdude-slic3r.hpp"

extern "C" {
#include "ac_cfg.h"
#include "avrdude.h"
}

namespace Slic3r {

namespace AvrDude {

static void avrdude_message_handler_ostream(const char *msg, unsigned size, void *user_p)
{
	(void)size;
	std::ostream &os = *reinterpret_cast<std::ostream*>(user_p);
	os << msg;
}

int main(std::vector<std::string> args, std::string sys_config, std::ostream &stderr)
{
	std::vector<char *> c_args {{ const_cast<char*>(PACKAGE_NAME) }};
	for (const auto &arg : args) {
		c_args.push_back(const_cast<char*>(arg.data()));
	}

	::avrdude_message_handler_set(avrdude_message_handler_ostream, reinterpret_cast<void*>(&stderr));
	const auto res = ::avrdude_main(static_cast<int>(c_args.size()), c_args.data(), sys_config.c_str());
	::avrdude_message_handler_set(nullptr, nullptr);
	return res;
}

}

}
