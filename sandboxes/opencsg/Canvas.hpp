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
    }
};

}} // namespace Slic3r::GL

#endif // CANVAS_HPP
