#ifndef CANVAS_HPP
#define CANVAS_HPP

#include <memory>

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/glcanvas.h>
#include <wx/msgdlg.h>

#include "GLScene.hpp"

namespace Slic3r { namespace GL {

class Canvas: public wxGLCanvas, public Slic3r::GL::Display
{
    std::unique_ptr<wxGLContext> m_context;
public:
    
    void set_active(long w, long h) override
    {
        SetCurrent(*m_context);
        Slic3r::GL::Display::set_active(w, h);
    }
    
    void swap_buffers() override { SwapBuffers(); }
        
    template<class...Args>
    Canvas(Args &&...args): wxGLCanvas(std::forward<Args>(args)...)
    {
        auto ctx = new wxGLContext(this);
        if (!ctx || !ctx->IsOK()) {
            wxMessageBox("Could not create OpenGL context.", "Error",
                         wxOK | wxICON_ERROR);
            return;
        }
        
        m_context.reset(ctx);
        
        Bind(
            wxEVT_MOUSEWHEEL,
            [this](wxMouseEvent &evt) {
                on_scroll(evt.GetWheelRotation(), evt.GetWheelDelta(),
                          evt.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL ?
                              Slic3r::GL::MouseInput::waVertical :
                              Slic3r::GL::MouseInput::waHorizontal);
            },
            GetId());
        
        Bind(
            wxEVT_MOTION,
            [this](wxMouseEvent &evt) {
                on_moved_to(evt.GetPosition().x, evt.GetPosition().y);
            },
            GetId());
        
        Bind(
            wxEVT_RIGHT_DOWN,
            [this](wxMouseEvent & /*evt*/) { on_right_click_down(); },
            GetId());
        
        Bind(
            wxEVT_RIGHT_UP,
            [this](wxMouseEvent & /*evt*/) { on_right_click_up(); },
            GetId());
        
        Bind(
            wxEVT_LEFT_DOWN,
            [this](wxMouseEvent & /*evt*/) { on_left_click_down(); },
            GetId());
        
        Bind(
            wxEVT_LEFT_UP,
            [this](wxMouseEvent & /*evt*/) { on_left_click_up(); },
            GetId());
        
        Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
            // This is required even though dc is not used otherwise.
            wxPaintDC dc(this);
            
            // Set the OpenGL viewport according to the client size of this
            // canvas. This is done here rather than in a wxSizeEvent handler
            // because our OpenGL rendering context (and thus viewport setting) is
            // used with multiple canvases: If we updated the viewport in the
            // wxSizeEvent handler, changing the size of one canvas causes a
            // viewport setting that is wrong when next another canvas is
            // repainted.
            const wxSize ClientSize = GetClientSize();
            repaint(ClientSize.x, ClientSize.y);
        }, GetId());
    }
};

}} // namespace Slic3r::GL

#endif // CANVAS_HPP
