#ifndef slic3r_GUI_ConfirmHintDialog_hpp_
#define slic3r_GUI_ConfirmHintDialog_hpp_

#include "GUI_Utils.hpp"
#include <wx/statbmp.h>
#include "Widgets/Button.hpp"
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_CONFIRM_HINT, wxCommandEvent);

class ConfirmHintDialog : public DPIDialog
{
private:
    wxStaticText* m_staticText_hint;
    Button* m_button_confirm;
    Button* m_button_close;
    wxStaticBitmap* m_bitmap_home;
    ScalableBitmap  m_home_bmp;
    wxString firm_up_hint = "";

    void OnPaint(wxPaintEvent& event);
    void render(wxDC& dc);
    void on_button_confirm(wxCommandEvent& event);
    void on_button_close(wxCommandEvent& event);
    void on_dpi_changed(const wxRect& suggested_rect) override;

public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };

    ConfirmHintDialog(wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        enum ButtonStyle btn_style = CONFIRM_AND_CANCEL,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION);

    const wxColour text_color = wxColour(107, 107, 107);

    void SetHint(const wxString &hint);

    ~ConfirmHintDialog();
};
}} // namespace Slic3r::GUI

#endif