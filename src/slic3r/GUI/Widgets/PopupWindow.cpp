#include "PopupWindow.hpp"

static wxWindow *GetTopParent(wxWindow *pWindow)
{
    wxWindow *pWin = pWindow;
    while (pWin->GetParent()) {
        pWin = pWin->GetParent();
        if (auto top = dynamic_cast<wxNonOwnedWindow*>(pWin))
            return top;
    }
    return pWin;
}

bool PopupWindow::Create(wxWindow *parent, int style)
{
    if (!wxPopupTransientWindow::Create(parent, style))
        return false;
#ifdef __WXGTK__
    GetTopParent(parent)->Bind(wxEVT_ACTIVATE, &PopupWindow::topWindowActiavate, this);
#endif
    return true;
}

PopupWindow::~PopupWindow()
{
#ifdef __WXGTK__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActiavate, this);
#endif
}

#ifdef __WXGTK__
void PopupWindow::topWindowActiavate(wxActivateEvent &event)
{
    event.Skip();
    if (!event.GetActive() && IsShown()) DismissAndNotify();
}
#endif
