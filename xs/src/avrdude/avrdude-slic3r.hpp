#ifndef slic3r_avrdude_slic3r_hpp_
#define slic3r_avrdude_slic3r_hpp_

#include <vector>
#include <string>
#include <ostream>
#include <functional>

namespace Slic3r {

class AvrDude
{
public:
	typedef std::function<void(const char * /* msg */, unsigned /* size */)> MessageFn;
	typedef std::function<void(const char * /* task */, unsigned /* progress */)> ProgressFn;

	AvrDude();
	AvrDude(AvrDude &&) = delete;
	AvrDude(const AvrDude &) = delete;
	AvrDude &operator=(AvrDude &&) = delete;
	AvrDude &operator=(const AvrDude &) = delete;
	~AvrDude();

	// Set location of avrdude's main configuration file
	AvrDude& sys_config(std::string sys_config);
	// Set message output callback
	AvrDude& on_message(MessageFn fn);
	// Set progress report callback
	// Progress is reported per each task (reading / writing), progress is reported in percents.
	AvrDude& on_progress(MessageFn fn);
	
	int run(std::vector<std::string> args);
private:
	std::string m_sys_config;
	MessageFn   m_message_fn;
	ProgressFn  m_progress_fn;
};


}

#endif
