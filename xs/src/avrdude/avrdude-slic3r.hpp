#ifndef slic3r_avrdude_slic3r_hpp_
#define slic3r_avrdude_slic3r_hpp_

#include <vector>
#include <string>
#include <ostream>
#include <functional>

namespace Slic3r {

namespace AvrDude {
	typedef std::function<void(const char * /* msg */, unsigned /* size */)> MessageFn;

	int main(std::vector<std::string> args, std::string sys_config, MessageFn message_fn);
	int main(std::vector<std::string> args, std::string sys_config, std::ostream &os);
}

}

#endif
