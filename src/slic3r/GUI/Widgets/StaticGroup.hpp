#ifndef slic3r_GUI_StaticGroup_hpp_
#define slic3r_GUI_StaticGroup_hpp_

#include "../wxExtensions.hpp"

#include <wx/statbox.h>

class StaticGroup : public wxStaticBox
{
public:
    StaticGroup(wxWindow *parent, wxWindowID id, const wxString &label);

public:
    void ShowBadge(bool show);
    void SetBorderColor(const wxColour &color);
private:
#ifdef __WXMSW__
    void OnPaint(wxPaintEvent &evt);
    void PaintForeground(wxDC &dc, const struct tagRECT &rc) override;
#endif

private:
#ifdef __WXMSW__
    ScalableBitmap badge;
#endif
#ifdef __WXOSX__
    ScalableButton * badge { nullptr };
#endif
    wxColour       borderColor_;
};

#endif // !slic3r_GUI_StaticGroup_hpp_
