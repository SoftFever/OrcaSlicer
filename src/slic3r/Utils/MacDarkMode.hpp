#ifndef slic3r_MacDarkMode_hpp_
#define slic3r_MacDarkMode_hpp_

namespace Slic3r {
namespace GUI {

#if __APPLE__
extern bool mac_dark_mode();
extern double mac_max_scaling_factor();
#endif


} // namespace GUI
} // namespace Slic3r

#endif // MacDarkMode_h
