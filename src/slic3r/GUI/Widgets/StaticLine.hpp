#ifndef slic3r_GUI_StaticLine_hpp_
#define slic3r_GUI_StaticLine_hpp_

#include "wx/window.h"

class StaticLine : public wxWindow
{
public:
    StaticLine(wxWindow* parent, bool vertical = false, const wxString& label = {});

public:
    void SetLabel(const wxString& label) override;

    void SetLineColour(wxColour color);
    
private:
    wxPen pen;
    bool vertical;

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_StaticLine_hpp_
