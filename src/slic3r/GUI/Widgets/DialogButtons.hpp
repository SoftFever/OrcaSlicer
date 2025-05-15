#ifndef slic3r_GUI_DialogButtons_hpp_
#define slic3r_GUI_DialogButtons_hpp_

#include "wx/wx.h"
#include "wx/sizer.h"
#include "set"

#include "Button.hpp"
#include "Label.hpp"

namespace Slic3r { namespace GUI {

class DialogButtons {
public:

    DialogButtons(wxWindow* parent, std::vector<wxString> non_translated_labels, const wxString& focused_btn_label = "");

    wxBoxSizer* GetSizer() const { return m_sizer; }

    Button* GetButtonFromID(wxStandardID id);
    Button* GetButtonFromLabel(wxString label);

    Button* GetOK();
    Button* GetYES();
    Button* GetAPPLY();
    Button* GetCONFIRM();
    Button* GetNO();
    Button* GetCANCEL();
    Button* GetBACK();
    Button* GetFORWARD();

    void SetFocus(wxString label);

    void Refresh();

    void AddTo(wxBoxSizer* sizer);

    ~DialogButtons();

private:
    wxBoxSizer*          m_sizer;
    wxWindow*            m_parent;
    std::vector<Button*> m_buttons;
    wxString             m_focus;

    void on_dpi_changed(wxDPIChangedEvent& event);

    void on_keydown(wxKeyEvent& event);
};

}} // namespace Slic3r::GUI
#endif // !slic3r_GUI_DialogButtons_hpp_