#ifndef slic3r_GUI_Utils_FixModelByWin10_hpp_
#define slic3r_GUI_Utils_FixModelByWin10_hpp_

#include <string>

#ifdef HAS_WIN10SDK

extern bool is_windows10();
extern bool fix_model_by_win10_sdk(const std::string &path_src, const std::string &path_dst);

#else /* HAS_WIN10SDK */

inline bool is_windows10() { return false; }
inline bool fix_model_by_win10_sdk(const std::string &, const std::string &) { return false; }

#endif /* HAS_WIN10SDK *

#endif /* slic3r_GUI_Utils_FixModelByWin10_hpp_ */
