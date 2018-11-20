#ifndef slic3r_GCode_PostProcessor_hpp_
#define slic3r_GCode_PostProcessor_hpp_

#include <string>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

namespace Slic3r {

extern void run_post_process_scripts(const std::string &path, const PrintConfig &config);

} // namespace Slic3r

#endif /* slic3r_GCode_PostProcessor_hpp_ */
