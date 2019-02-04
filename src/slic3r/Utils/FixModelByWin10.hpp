#ifndef slic3r_GUI_Utils_FixModelByWin10_hpp_
#define slic3r_GUI_Utils_FixModelByWin10_hpp_

#include <string>

namespace Slic3r {

class Model;
class ModelObject;
class Print;

#ifdef HAS_WIN10SDK

extern bool is_windows10();
extern void fix_model_by_win10_sdk_gui(ModelObject &model_object, int volume_idx);

#else /* HAS_WIN10SDK */

inline bool is_windows10() { return false; }
inline void fix_model_by_win10_sdk_gui(ModelObject &, int) {}

#endif /* HAS_WIN10SDK */

} // namespace Slic3r

#endif /* slic3r_GUI_Utils_FixModelByWin10_hpp_ */
