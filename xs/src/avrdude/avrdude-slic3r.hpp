#ifndef slic3r_avrdude_slic3r_hpp_
#define slic3r_avrdude_slic3r_hpp_

#include <vector>
#include <string>
#include <ostream>

namespace Slic3r {

namespace AvrDude {
	int main(std::vector<std::string> args, std::string sys_config, std::ostream &os);
}

}

#endif
