#include "avrdude-slic3r.hpp"

#include <deque>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <new>
#include <exception>

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
static void avrdude_progress_handler_closure(const char *task, unsigned progress, void *user_p)
{
	auto *progress_fn = reinterpret_cast<AvrDude::ProgressFn*>(user_p);
	(*progress_fn)(task, progress);
}

static void avrdude_oom_handler(const char *context, void *user_p)
{
	throw std::bad_alloc();
}


// Private

struct AvrDude::priv
{
	std::string sys_config;
	std::deque<std::vector<std::string>> args;
	bool cancelled = false;
	int exit_code = 0;
	size_t current_args_set = 0;
	RunFn run_fn;
	MessageFn message_fn;
	ProgressFn progress_fn;
	CompleteFn complete_fn;

	std::thread avrdude_thread;

	priv(std::string &&sys_config) : sys_config(sys_config) {}

	void set_handlers();
	void unset_handlers();
	int run_one(const std::vector<std::string> &args);
	int run();

	struct HandlerGuard
	{
		priv &p;

		HandlerGuard(priv &p) : p(p) { p.set_handlers(); }
		~HandlerGuard() { p.unset_handlers(); }
	};
};

void AvrDude::priv::set_handlers()
{
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

	::avrdude_oom_handler_set(avrdude_oom_handler, nullptr);
}

void AvrDude::priv::unset_handlers()
{
	::avrdude_message_handler_set(nullptr, nullptr);
	::avrdude_progress_handler_set(nullptr, nullptr);
	::avrdude_oom_handler_set(nullptr, nullptr);
}


int AvrDude::priv::run_one(const std::vector<std::string> &args) {
	std::vector<char*> c_args {{ const_cast<char*>(PACKAGE_NAME) }};
	for (const auto &arg : args) {
		c_args.push_back(const_cast<char*>(arg.data()));
	}

	HandlerGuard guard(*this);

	const auto res = ::avrdude_main(static_cast<int>(c_args.size()), c_args.data(), sys_config.c_str());

	return res;
}

int AvrDude::priv::run() {
	for (; args.size() > 0; current_args_set++) {
		int res = run_one(args.front());
		args.pop_front();
		if (res != 0) {
			return res;
		}
	}

	return 0;
}


// Public

AvrDude::AvrDude(std::string sys_config) : p(new priv(std::move(sys_config))) {}

AvrDude::AvrDude(AvrDude &&other) : p(std::move(other.p)) {}

AvrDude::~AvrDude()
{
	if (p && p->avrdude_thread.joinable()) {
		p->avrdude_thread.detach();
	}
}

AvrDude& AvrDude::push_args(std::vector<std::string> args)
{
	if (p) { p->args.push_back(std::move(args)); }
	return *this;
}

AvrDude& AvrDude::on_run(RunFn fn)
{
	if (p) { p->run_fn = std::move(fn); }
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
	return p ? p->run() : -1;
}

AvrDude::Ptr AvrDude::run()
{
	auto self = std::make_shared<AvrDude>(std::move(*this));

	if (self->p) {
		auto avrdude_thread = std::thread([self]() {
			try {
				if (self->p->run_fn) {
					self->p->run_fn(self);
				}

				if (! self->p->cancelled) {
					self->p->exit_code = self->p->run();
				}

				if (self->p->complete_fn) {
					self->p->complete_fn();
				}
			} catch (const std::exception &ex) {
				self->p->exit_code = EXIT_EXCEPTION;

				static const char *msg = "An exception was thrown in the background thread:\n";

				const char *what = ex.what();
				auto &message_fn = self->p->message_fn;
				if (message_fn) {
					message_fn(msg, sizeof(msg));
					message_fn(what, std::strlen(what));
					message_fn("\n", 1);
				}

				if (self->p->complete_fn) {
					self->p->complete_fn();
				}
			} catch (...) {
				self->p->exit_code = EXIT_EXCEPTION;

				static const char *msg = "An unkown exception was thrown in the background thread.\n";

				if (self->p->message_fn) {
					self->p->message_fn(msg, sizeof(msg));
				}

				if (self->p->complete_fn) {
					self->p->complete_fn();
				}
			}
		});

		self->p->avrdude_thread = std::move(avrdude_thread);
	}

	return self;
}

void AvrDude::cancel()
{
	if (p) {
		p->cancelled = true;
		::avrdude_cancel();
	}
}

void AvrDude::join()
{
	if (p && p->avrdude_thread.joinable()) {
		p->avrdude_thread.join();
	}
}

bool AvrDude::cancelled()
{
	return p ? p->cancelled : false;
}

int AvrDude::exit_code()
{
	return p ? p->exit_code : 0;
}

size_t AvrDude::last_args_set()
{
	return p ? p->current_args_set : 0;
}


}
