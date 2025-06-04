#ifndef slic3r_GUI_LabeledStaticBox_hpp_
#define slic3r_GUI_LabeledStaticBox_hpp_

#include "libslic3r/Utils.hpp"
#include <wx/settings.h>
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/Widgets/StateHandler.hpp"

#include <wx/statbox.h>

#include <wx/dcclient.h>
#include <wx/dcgraph.h>
#include <wx/dc.h>
#include <wx/pen.h>

#include <wx/window.h>


class LabeledStaticBox : public wxStaticBox
{
public:
    LabeledStaticBox();

    LabeledStaticBox(
        wxWindow*       parent,
        const wxString& label   = wxEmptyString,
        const wxPoint&  pos     = wxDefaultPosition,
        const wxSize&   size    = wxDefaultSize,
        long            style   = 0
    );

    bool Create(
        wxWindow*       parent,
        const wxString& label   = wxEmptyString,
        const wxPoint&  pos     = wxDefaultPosition,
        const wxSize&   size    = wxDefaultSize,
        long            style   = 0
    );

    void SetCornerRadius(int radius);

    void SetBorderWidth(int width);

    void SetBorderColor(StateColor const &color);

    void SetFont(wxFont set_font);

    bool Enable(bool enable);

private:
    void paintEvent(wxPaintEvent& evt);

protected:
    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   background_color;
    int          border_width;
    int          radius;
    wxFont       font;

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_LabeledStaticBox_hpp_