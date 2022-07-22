#ifndef slic3r_UpdateErrorMessage_hpp_
#define slic3r_UpdateErrorMessage_hpp_

#include "GUI_App.hpp"
#include "GUI.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/StepCtrl.hpp"
#include "BitmapCache.hpp"
#include "slic3r/Utils/Http.hpp"
#include "libslic3r/Thread.hpp"

namespace Slic3r {
namespace GUI {


std::string show_error_message(int error_code);


wxDECLARE_EVENT(EVT_UPDATE_ERROR_MESSAGE, wxCommandEvent);
}
}


#endif