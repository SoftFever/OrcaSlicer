
#ifndef _WX_STATTEXT2_H_
#define _WX_STATTEXT2_H_

#include "wx/stattext.h"

class WXDLLIMPEXP_CORE wxStaticText2 : public wxPanel
{
public:
    wxString m_msg;
    wxStaticText2();
    wxStaticText2(wxWindow *parent,
                 wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxSize(200,200));

    void paintEvent(wxPaintEvent &evt);

    void SetLabel(wxString msg){m_msg = msg;};

};
#endif