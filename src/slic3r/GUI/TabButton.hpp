#ifndef slic3r_GUI_TabButton_hpp_
#define slic3r_GUI_TabButton_hpp_

#include "wxExtensions.hpp"
#include "Widgets/StaticBox.hpp"

class TabButton : public StaticBox
{
    wxSize   textSize;
    wxSize   minSize;
    wxSize   paddingSize;
    ScalableBitmap icon;

    StateColor   text_color;
    StateColor   border_color;
    bool pressedDown = false;

public:
    TabButton();

    TabButton(wxWindow *parent, wxString text, ScalableBitmap &icon, long style = 0, int iconSize = 0);

    bool Create(wxWindow *parent, wxString text, ScalableBitmap &icon, long style = 0, int iconSize = 0);

    void SetLabel(const wxString& label) override;

    void SetMinSize(const wxSize& size) override;
    
    void SetPaddingSize(const wxSize& size);

    const wxSize& GetPaddingSize();
    
    void SetTextColor(StateColor const &color);

    void SetBorderColor(StateColor const &color);

    void SetBGColor(StateColor const &color);

    void SetBitmap(ScalableBitmap &bitmap);

    bool Enable(bool enable = true);

    void Rescale();

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent& event);

    void sendButtonEvent();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_Button_hpp_
