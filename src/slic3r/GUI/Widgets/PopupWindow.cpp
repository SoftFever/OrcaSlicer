#include "PopupWindow.hpp"
#include <wx/popupwin.h>
#include <wx/event.h>

#ifdef __WXGTK__
#include <gtk/gtk.h>
#include "slic3r/GUI/LinuxDisplayBackend.hpp"
#endif

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
#ifdef __WXOSX__
    if (style & wxPU_CONTAINS_CONTROLS)
    for (auto evt : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP, wxEVT_LEFT_DCLICK, wxEVT_MOTION, wxEVT_MOUSEWHEEL})
        Bind(evt, &PopupWindow::OnMouseEvent2, this);
#endif
    return true;
}

PopupWindow::~PopupWindow()
{
#ifdef __WXGTK__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActiavate, this);
#endif
#ifdef __WXMSW__
    GetTopParent(this)->Unbind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Unbind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Unbind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
#endif
}

#ifdef __WXOSX__

static wxEvtHandler * HitTest(wxWindow * parent, wxMouseEvent &evt)
{
    auto pt = evt.GetPosition();
    const wxWindowList &children = parent->GetChildren();
    for (auto w : children) {
        wxRect rc { w->GetPosition(), w->GetSize() };
        if (rc.Contains(pt)) {
            evt.SetPosition(pt - rc.GetTopLeft());
            if (auto child = HitTest(w, evt))
                return child;
            return w;
        }
    }
    return nullptr;
}

void PopupWindow::OnMouseEvent2(wxMouseEvent &evt)
{
    auto child = ::HitTest(this, evt);
    if (evt.GetEventType() == wxEVT_MOTION) {
        auto h = child ? child : this;
        if (hovered != h) {
            wxMouseEvent leave(wxEVT_LEAVE_WINDOW);
            leave.SetEventObject(hovered);
            leave.SetId(static_cast<wxWindow*>(hovered)->GetId());
            hovered->ProcessEventLocally(leave);
            hovered = h;
            wxMouseEvent enter(wxEVT_ENTER_WINDOW);
            enter.SetEventObject(hovered);
            enter.SetId(static_cast<wxWindow*>(hovered)->GetId());
            hovered->ProcessEventLocally(enter);
        }
    }
    if (child) {
        child->ProcessEventLocally(evt);
    } else {
        evt.Skip();
    }
}

#endif

#ifdef __WXGTK__
bool PopupWindow::Show(bool show)
{
    // On Wayland a shown GTK tooltip is itself an xdg_popup, and GDK maps a new popup only as a child of
    // the topmost one ("Tried to map a popup with a non-top most parent"). GTK hides the tooltip on a
    // click or key press only after the widgets have handled it, so a popup opened from that click would
    // never appear. GtkMenu hides the tooltip first for the same reason, through a private call; hiding the
    // tooltip window is the public equivalent, and GtkTooltip then treats it as closed and shows it again
    // on a later hover.
    if (show && !IsShown() && Slic3r::GUI::is_running_on_wayland()) {
        GList *toplevels = gtk_window_list_toplevels();
        for (GList *l = toplevels; l; l = l->next) {
            GtkWidget *win = GTK_WIDGET(l->data);
            if (gtk_window_get_type_hint(GTK_WINDOW(win)) == GDK_WINDOW_TYPE_HINT_TOOLTIP && gtk_widget_get_visible(win))
                gtk_widget_hide(win);
        }
        g_list_free(toplevels);
    }
    return wxPopupTransientWindow::Show(show);
}

void PopupWindow::topWindowActiavate(wxActivateEvent &event)
{
    event.Skip();
    if (!event.GetActive() && IsShown() && ShouldDismissOnTopWindowDeactivate())
        DismissAndNotify();
}
#endif

#ifdef __WXMSW__
void PopupWindow::BindUnfocusEvent()
{
    GetTopParent(this)->Bind(wxEVT_ACTIVATE, &PopupWindow::topWindowActivate, this);
    GetTopParent(this)->Bind(wxEVT_ICONIZE, &PopupWindow::topWindowIconize, this);
    GetTopParent(this)->Bind(wxEVT_SHOW, &PopupWindow::topWindowShow, this);
}

void PopupWindow::topWindowActivate(wxActivateEvent &event)
{
    if (!event.GetActive())
        Dismiss();
}

void PopupWindow::topWindowIconize(wxIconizeEvent &event)
{
    event.Skip();
    if (event.IsIconized())
        Dismiss();
}

void PopupWindow::topWindowShow(wxShowEvent &event)
{
    event.Skip();
    if (!event.IsShown())
        Dismiss();
}
#endif
