#ifndef slic3r_SendSystemInfoDialog_hpp_
#define slic3r_SendSystemInfoDialog_hpp_

#include "GUI_Utils.hpp"

#include <string>

namespace Slic3r {

namespace GUI {

class SendSystemInfoDialog : public DPIDialog
{
    enum {
        MIN_WIDTH = 60,
        MIN_HEIGHT = 40
    };

public:
    SendSystemInfoDialog(wxWindow* parent);

private:
    const std::string m_system_info_json;
    wxButton* m_btn_send;
    wxButton* m_btn_dont_send;
    wxButton* m_btn_ask_later;

    void on_dpi_changed(const wxRect&) override;
};


void show_send_system_info_dialog_if_needed();


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_SendSystemInfoDialog_hpp_
