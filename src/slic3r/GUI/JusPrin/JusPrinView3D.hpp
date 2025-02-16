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
    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
    void AddJoin(std::function<void(wxMouseEvent&)> do_some) { m_do = do_some; }

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);



private:
    bool                 m_isHovered{false};
    wxAnimationCtrlBase* m_animationCtrl{nullptr};
    std::function<void(wxMouseEvent&)> m_do{nullptr};
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