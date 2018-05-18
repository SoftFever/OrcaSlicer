#include "avrdude-slic3r.hpp"

#include <thread>

extern "C" {
#include "ac_cfg.h"
#include "avrdude.h"
}


namespace Slic3r {


// C callbacks

// Used by our custom code in avrdude to receive messages that avrdude normally outputs on stdout (see avrdude_message())
static void avrdude_message_handler_closure(const char *msg, unsigned size, void *user_p)
{
	auto *message_fn = reinterpret_cast<AvrDude::MessageFn*>(user_p);
	(*message_fn)(msg, size);
}

// Used by our custom code in avrdude to report progress in the GUI
static bool avrdude_progress_handler_closure(const char *task, unsigned progress, void *user_p)
{
	auto *progress_fn = reinterpret_cast<AvrDude::ProgressFn*>(user_p);
	return (*progress_fn)(task, progress);
}


// Private

struct AvrDude::priv
{
	std::string sys_config;
	std::vector<std::string> args;
	MessageFn message_fn;
	ProgressFn progress_fn;
	CompleteFn complete_fn;

	std::thread avrdude_thread;

	int run();
};

int AvrDude::priv::run() {
	std::vector<char*> c_args {{ const_cast<char*>(PACKAGE_NAME) }};
	for (const auto &arg : args) {
		c_args.push_back(const_cast<char*>(arg.data()));
	}

	if (message_fn) {
		::avrdude_message_handler_set(avrdude_message_handler_closure, reinterpret_cast<void*>(&message_fn));
	} else {
		::avrdude_message_handler_set(nullptr, nullptr);
	}

	if (progress_fn) {
		::avrdude_progress_handler_set(avrdude_progress_handler_closure, reinterpret_cast<void*>(&progress_fn));
	} else {
		::avrdude_progress_handler_set(nullptr, nullptr);
	}

	const auto res = ::avrdude_main(static_cast<int>(c_args.size()), c_args.data(), sys_config.c_str());

	::avrdude_message_handler_set(nullptr, nullptr);
	::avrdude_progress_handler_set(nullptr, nullptr);
	return res;
}


// Public

AvrDude::AvrDude() : p(new priv()) {}

AvrDude::AvrDude(AvrDude &&other) : p(std::move(other.p)) {}

AvrDude::~AvrDude()
{
	if (p && p->avrdude_thread.joinable()) {
		p->avrdude_thread.detach();
	}
}

AvrDude& AvrDude::sys_config(std::string sys_config)
{
	if (p) { p->sys_config = std::move(sys_config); }
	return *this;
}

AvrDude& AvrDude::args(std::vector<std::string> args)
{
	if (p) { p->args = std::move(args); }
	return *this;
}

AvrDude& AvrDude::on_message(MessageFn fn)
{
	if (p) { p->message_fn = std::move(fn); }
	return *this;
}

AvrDude& AvrDude::on_progress(ProgressFn fn)
{
	if (p) { p->progress_fn = std::move(fn); }
	return *this;
}

AvrDude& AvrDude::on_complete(CompleteFn fn)
{
	if (p) { p->complete_fn = std::move(fn); }
	return *this;
}

int AvrDude::run_sync()
{
	return p->run();
}

AvrDude::Ptr AvrDude::run()
{
	auto self = std::make_shared<AvrDude>(std::move(*this));

	if (self->p) {
		auto avrdude_thread = std::thread([self]() {
				auto res = self->p->run();
				if (self->p->complete_fn) {
					self->p->complete_fn(res);
				}
			});
		self->p->avrdude_thread = std::move(avrdude_thread);
	}

	return self;
}

void AvrDude::join()
{
	if (p && p->avrdude_thread.joinable()) {
		p->avrdude_thread.join();
	}
}


}
