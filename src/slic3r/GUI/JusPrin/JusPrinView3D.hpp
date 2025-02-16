#ifndef slic3r_GUI_JusPrinView3D_hpp_
#define slic3r_GUI_JusPrinView3D_hpp_

#include "../../GUI/GUI_Preview.hpp"
#include "JusPrinChatPanel.hpp"

// Forward declarations
class wxStaticBitmap;
class wxWindow;
class wxSizeEvent;

namespace Slic3r {
namespace GUI {


class JustPrinButton : public wxPanel
{
public:
    JustPrinButton(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);

private:
    bool                 m_isHovered;
    wxAnimationCtrlBase* m_animationCtrl{nullptr};
};


class JusPrinView3D : public View3D {
public:
    JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
    virtual ~JusPrinView3D();

protected:
    void OnSize(wxSizeEvent& evt);
    void OnCanvasMouseDown(SimpleEvent& evt);

private:
    JusPrinChatPanel* m_chat_panel{nullptr};
    JustPrinButton*   m_overlay_btn{nullptr};

    void init_overlay();
};

} // namespace GUI
} // namespace Slic3r

#endif