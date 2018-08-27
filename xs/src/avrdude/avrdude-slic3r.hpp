#ifndef slic3r_avrdude_slic3r_hpp_
#define slic3r_avrdude_slic3r_hpp_

#include <memory>
#include <vector>
#include <string>
#include <functional>

namespace Slic3r {

class AvrDude
{
public:
	enum {
		EXIT_SUCCEESS   = 0,
		EXIT_EXCEPTION  = -1000,
	};

	typedef std::shared_ptr<AvrDude> Ptr;
	typedef std::function<void(Ptr /* avrdude */)> RunFn;
	typedef std::function<void(const char * /* msg */, unsigned /* size */)> MessageFn;
	typedef std::function<void(const char * /* task */, unsigned /* progress */)> ProgressFn;
	typedef std::function<void()> CompleteFn;

	// Main c-tor, sys_config is the location of avrdude's main configuration file
	AvrDude(std::string sys_config);
	AvrDude(AvrDude &&);
	AvrDude(const AvrDude &) = delete;
	AvrDude &operator=(AvrDude &&) = delete;
	AvrDude &operator=(const AvrDude &) = delete;
	~AvrDude();

	// Push a set of avrdude cli arguments
	// Each set makes one avrdude invocation - use this method multiple times to push
	// more than one avrdude invocations.
	AvrDude& push_args(std::vector<std::string> args);

	// Set a callback to be called just after run() before avrdude is ran
	// This can be used to perform any needed setup tasks from the background thread,
	// and, optionally, to cancel by writing true to the `cancel` argument.
	// This has no effect when using run_sync().
	AvrDude& on_run(RunFn fn);

	// Set message output callback
	AvrDude& on_message(MessageFn fn);

	// Set progress report callback
	// Progress is reported per each task (reading / writing) in percents.
	AvrDude& on_progress(ProgressFn fn);

	// Called when the last avrdude invocation finishes with the exit status of zero,
	// or earlier, if one of the invocations return a non-zero status.
	// The second argument contains the sequential id of the last avrdude invocation argument set.
	// This has no effect when using run_sync().
	AvrDude& on_complete(CompleteFn fn);

	// Perform AvrDude invocation(s) synchronously on the current thread
	int run_sync();

	// Perform AvrDude invocation(s) on a background thread.
	// Current instance is moved into a shared_ptr which is returned (and also passed in on_run, if any).
	Ptr run();

	// Cancel current operation
	void cancel();

	// If there is a background thread and it is joinable, join() it,
	// that is, wait for it to finish.
	void join();

	bool cancelled();          // Whether avrdude run was cancelled
	int exit_code();           // The exit code of the last invocation
	size_t last_args_set();    // Index of the last argument set that was processsed
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}

#endif
