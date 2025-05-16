#pragma once

#include <string>
#include "../GUI/Widgets/ProgressDialog.hpp"

class ProgressDialog;

namespace Slic3r {

class Model;
class ModelObject;
class Print;

extern bool is_repair_available();
char const * repair_not_available_reason();
// returt false, if fixing was canceled
extern bool fix_model(ModelObject &model_object, int volume_idx,GUI::ProgressDialog &progress_dlg, const wxString &msg_header, std::string &fix_result);

} // namespace Slic3r
