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
	typedef std::shared_ptr<AvrDude> Ptr;
	typedef std::function<void()> RunFn;
	typedef std::function<void(const char * /* msg */, unsigned /* size */)> MessageFn;
	typedef std::function<void(const char * /* task */, unsigned /* progress */)> ProgressFn;
	typedef std::function<void(int /* exit status */, size_t /* args_id */)> CompleteFn;

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
	// This can be used to perform any needed setup tasks from the background thread.
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

	int run_sync();
	Ptr run();

	void cancel();
	void join();
private:
	struct priv;
	std::unique_ptr<priv> p;
};


}

#endif
