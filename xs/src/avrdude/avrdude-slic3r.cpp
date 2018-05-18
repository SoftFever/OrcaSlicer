#include "avrdude-slic3r.hpp"

extern "C" {
#include "ac_cfg.h"
#include "avrdude.h"
}


namespace Slic3r {

// Used by our custom code in avrdude to receive messages that avrdude normally outputs on stdout (see avrdude_message())
static void avrdude_message_handler_closure(const char *msg, unsigned size, void *user_p)
{
	auto *message_fn = reinterpret_cast<AvrDude::MessageFn*>(user_p);
	(*message_fn)(msg, size);
}

// Used by our custom code in avrdude to report progress in the GUI
static void avrdude_progress_handler_closure(const char *task, unsigned progress, void *user_p)
{
	auto *progress_fn = reinterpret_cast<AvrDude::ProgressFn*>(user_p);
	(*progress_fn)(task, progress);
}


AvrDude::AvrDude() {}
AvrDude::~AvrDude() {}

AvrDude& AvrDude::sys_config(std::string sys_config)
{
	m_sys_config = std::move(sys_config);
	return *this;
}

AvrDude& AvrDude::on_message(MessageFn fn)
{
	m_message_fn = std::move(fn);
	return *this;
}

AvrDude& AvrDude::on_progress(MessageFn fn)
{
	m_progress_fn = std::move(fn);
	return *this;
}

int AvrDude::run(std::vector<std::string> args)
{
	std::vector<char *> c_args {{ const_cast<char*>(PACKAGE_NAME) }};
	for (const auto &arg : args) {
		c_args.push_back(const_cast<char*>(arg.data()));
	}

	if (m_message_fn) {
		::avrdude_message_handler_set(avrdude_message_handler_closure, reinterpret_cast<void*>(&m_message_fn));
	} else {
		::avrdude_message_handler_set(nullptr, nullptr);
	}
	
	if (m_progress_fn) {
		::avrdude_progress_handler_set(avrdude_progress_handler_closure, reinterpret_cast<void*>(&m_progress_fn));
	} else {
		::avrdude_progress_handler_set(nullptr, nullptr);
	}
	
	const auto res = ::avrdude_main(static_cast<int>(c_args.size()), c_args.data(), m_sys_config.c_str());
	
	::avrdude_message_handler_set(nullptr, nullptr);
	::avrdude_progress_handler_set(nullptr, nullptr);
	return res;
}


}
