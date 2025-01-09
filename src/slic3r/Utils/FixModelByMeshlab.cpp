#ifndef HAS_WIN10SDK

#include "FixModel.hpp"

#include "libslic3r/Model.hpp"
#include "../GUI/MsgDialog.hpp"

namespace Slic3r {

bool is_repair_available()
{
    return false;
}

// returt FALSE, if fixing was canceled
// fix_result is empty, if fixing finished successfully
// fix_result containes a message if fixing failed
bool fix_model(ModelObject &model_object, int volume_idx, GUI::ProgressDialog& progress_dialog, const wxString& msg_header, std::string& fix_result)
{
    return false;
}

} // namespace Slic3r

#endif // ifndef HAS_WIN10SDK
