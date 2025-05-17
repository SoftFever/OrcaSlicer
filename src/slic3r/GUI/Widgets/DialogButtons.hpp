#ifndef slic3r_GUI_DialogButtons_hpp_
#define slic3r_GUI_DialogButtons_hpp_

#include "wx/wx.h"
#include "wx/sizer.h"
#include "map"
#include "set"

#include "Button.hpp"
#include "Label.hpp"

#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r { namespace GUI {

class DialogButtons  : public wxWindow{
public:

    DialogButtons(wxWindow* parent, std::vector<wxString> non_translated_labels, const wxString& primary_btn_label = "");

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

    void SetPrimaryButton(wxString label);

    void Refresh();

    void AddTo(wxBoxSizer* sizer);

    //virtual ~DialogButtons();
    ~DialogButtons();

private:
    wxWindow*            m_parent;
    wxBoxSizer*          m_sizer;
    std::vector<Button*> m_buttons;
    wxString             m_primary;
    const std::map<wxString, wxStandardID> m_standardIDs = {
        // missing ones Transfer / Update / Create
        {"ok",      wxID_OK},
        {"yes",     wxID_YES},
        {"apply",   wxID_APPLY},
        {"confirm", wxID_APPLY}, // no id for confirm, reusing wxID_APPLY
        {"no",      wxID_NO},
        {"cancel",  wxID_CANCEL},
        {"open",    wxID_OPEN},
        {"add",     wxID_ADD},
        {"remove",  wxID_REMOVE},
        {"delete",  wxID_DELETE},
        {"refresh", wxID_REFRESH},
        {"retry",   wxID_RETRY},
        {"copy",    wxID_COPY},
        {"save",    wxID_SAVE},
        {"save as", wxID_SAVEAS},
        {"back",    wxID_BACKWARD},
        {"next",    wxID_FORWARD},
        {"help",    wxID_HELP},
        {"abort",   wxID_ABORT},
        {"ignore",  wxID_IGNORE},
        {"stop",    wxID_STOP}
    };

    int  FromDIP(int d);

    void on_dpi_changed(wxDPIChangedEvent& event);

    void on_keydown(wxKeyEvent& event);
};

}} // namespace Slic3r::GUI
#endif // !slic3r_GUI_DialogButtons_hpp_